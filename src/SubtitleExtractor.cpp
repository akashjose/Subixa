#include "SubtitleExtractor.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QStringView>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/mathematics.h>
}

namespace {

// Sidecar extensions worth opening. Deliberately text-only: .sup (PGS) and
// .idx/.sub (VOBSUB) are bitmap formats with nothing to browse.
const char *const kSidecarExtensions[] = {"srt", "ass", "ssa", "vtt"};

// How long a cue with no usable duration is assumed to last.
constexpr qint64 kFallbackCueMs = 3000;

QString dictValue(AVDictionary *meta, const char *key)
{
    if (!meta)
        return {};
    AVDictionaryEntry *e = av_dict_get(meta, key, nullptr, 0);
    return e ? QString::fromUtf8(e->value).trimmed() : QString();
}

SubtitleKind kindFor(AVCodecID id)
{
    // ffmpeg's own codec table is the authority on text vs bitmap, so this stays
    // correct as new subtitle codecs appear.
    const AVCodecDescriptor *desc = avcodec_descriptor_get(id);
    if (!desc)
        return SubtitleKind::Unknown;
    if (desc->props & AV_CODEC_PROP_TEXT_SUB)
        return SubtitleKind::Text;
    if (desc->props & AV_CODEC_PROP_BITMAP_SUB)
        return SubtitleKind::Bitmap;
    return SubtitleKind::Unknown;
}

// Every text subtitle decoder in ffmpeg normalises its output to ASS, so SRT,
// ASS and mov_text all arrive here in the same shape:
//
//   ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text
//
// i.e. the text is everything past the 8th comma. ffmpeg before 4.0 emitted a
// full "Dialogue: ..." line instead (9 fields ahead of the text); that form is
// handled too rather than silently eating the first words of every cue.
QString assDialogueText(const QString &ass)
{
    QStringView v(ass);
    int fieldsBeforeText = 8;
    if (v.startsWith(QLatin1String("Dialogue:"))) {
        v = v.mid(9);
        fieldsBeforeText = 9;
    }

    qsizetype pos = 0;
    for (int i = 0; i < fieldsBeforeText; ++i) {
        const qsizetype comma = v.indexOf(QLatin1Char(','), pos);
        if (comma < 0)
            return ass;  // not the layout we expect -- better whole than truncated
        pos = comma + 1;
    }
    return v.mid(pos).toString();
}

// Reduce an ASS payload to displayable text: drop {\...} override blocks, turn
// the two line-break escapes into real newlines, and make \h a hard space.
QString stripAssTags(const QString &in)
{
    QString out;
    out.reserve(in.size());

    int depth = 0;
    for (qsizetype i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        if (c == u'{') {
            ++depth;
            continue;
        }
        if (c == u'}') {
            if (depth > 0)
                --depth;
            continue;
        }
        if (depth > 0)
            continue;

        if (c == u'\\' && i + 1 < in.size()) {
            const QChar n = in.at(i + 1);
            // \N is a hard break, \n a soft one. In a browsable list both should
            // read as a break.
            if (n == u'N' || n == u'n') {
                out.append(u'\n');
                ++i;
                continue;
            }
            if (n == u'h') {
                out.append(QChar(0x00A0));
                ++i;
                continue;
            }
        }
        out.append(c);
    }
    return out.trimmed();
}

// SRT and WebVTT carry HTML-ish markup. ffmpeg's decoders turn the tags into ASS
// overrides (which stripAssTags then drops) but leave character entities alone,
// so "Fish &amp; chips" would otherwise show -- and be searched -- literally.
QString decodeEntities(const QString &in)
{
    if (!in.contains(u'&'))
        return in;

    static const struct {
        QLatin1String name;
        QChar value;
    } kNamed[] = {
        {QLatin1String("amp"), u'&'},   {QLatin1String("lt"), u'<'},
        {QLatin1String("gt"), u'>'},    {QLatin1String("quot"), u'"'},
        {QLatin1String("apos"), u'\''}, {QLatin1String("nbsp"), QChar(0x00A0)},
    };
    // Longest entity handled is "&#x10FFFF;"; anything longer is not one.
    constexpr int kMaxEntityLen = 10;

    QString out;
    out.reserve(in.size());

    for (qsizetype i = 0; i < in.size(); ++i) {
        if (in.at(i) != u'&') {
            out.append(in.at(i));
            continue;
        }

        const qsizetype end = in.indexOf(u';', i + 1);
        if (end < 0 || end - i > kMaxEntityLen) {
            out.append(in.at(i));
            continue;
        }

        const QString body = in.mid(i + 1, end - i - 1);
        bool handled = false;

        if (body.startsWith(u'#')) {
            const bool hex = body.size() > 1
                && (body.at(1) == u'x' || body.at(1) == u'X');
            bool ok = false;
            const char32_t code = body.mid(hex ? 2 : 1).toUInt(&ok, hex ? 16 : 10);
            if (ok && code > 0 && code <= 0x10FFFF) {
                out.append(QString::fromUcs4(&code, 1));
                handled = true;
            }
        } else {
            for (const auto &entity : kNamed) {
                if (body == entity.name) {
                    out.append(entity.value);
                    handled = true;
                    break;
                }
            }
        }

        if (handled)
            i = end;
        else
            out.append(in.at(i));  // not an entity: a bare ampersand
    }
    return out;
}

// A sidecar named "movie.en.srt" or "movie.forced.eng.srt" carries its language
// in the filename; it has none of the container metadata embedded tracks have.
QString languageFromSidecarName(const QString &videoBase, const QFileInfo &sidecar)
{
    const QString stem = sidecar.completeBaseName();
    if (stem.size() <= videoBase.size() + 1 || !stem.startsWith(videoBase))
        return {};

    const QStringList tags =
        stem.mid(videoBase.size() + 1).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &tag : tags) {
        if ((tag.size() == 2 || tag.size() == 3)
            && std::all_of(tag.cbegin(), tag.cend(),
                           [](QChar ch) { return ch.isLetter(); })) {
            return tag.toLower();
        }
    }
    return {};
}

QStringList findSidecars(const QString &mediaPath)
{
    const QFileInfo info(mediaPath);
    const QDir dir = info.absoluteDir();
    const QString base = info.completeBaseName();
    if (base.isEmpty() || !dir.exists())
        return {};

    QStringList patterns;
    for (const char *ext : kSidecarExtensions) {
        patterns << QStringLiteral("%1.%2").arg(base, QLatin1String(ext));
        patterns << QStringLiteral("%1.*.%2").arg(base, QLatin1String(ext));
    }

    QStringList found;
    const QFileInfoList entries =
        dir.entryInfoList(patterns, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString path = entry.absoluteFilePath();
        if (path != info.absoluteFilePath() && !found.contains(path))
            found << path;
    }
    return found;
}

// RAII for the format handle, so the many early-outs below cannot leak it.
struct FormatContextGuard
{
    AVFormatContext *ctx = nullptr;
    ~FormatContextGuard()
    {
        if (ctx)
            avformat_close_input(&ctx);
    }
};

struct CodecContextGuard
{
    AVCodecContext *ctx = nullptr;
    ~CodecContextGuard()
    {
        if (ctx)
            avcodec_free_context(&ctx);
    }
};

QString avError(int rc)
{
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(rc, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

} // namespace

SubtitleExtractor::SubtitleExtractor(QObject *parent) : QObject(parent)
{
    // ffmpeg logs straight to stderr and is chatty about codec details we do not
    // care about; keep it to genuine errors.
    av_log_set_level(AV_LOG_ERROR);
}

void SubtitleExtractor::extract(const QString &mediaPath, int requestId)
{
    if (cancelled(requestId))
        return;

    SubtitleTrackList tracks;
    QString error;

    if (!readContainer(mediaPath, /*videoBase=*/QString(), tracks, requestId, &error)) {
        if (!cancelled(requestId))
            emit failed(requestId, error);
        return;
    }

    const QString videoBase = QFileInfo(mediaPath).completeBaseName();
    for (const QString &sidecar : findSidecars(mediaPath)) {
        if (cancelled(requestId))
            return;
        // A broken sidecar must not sink the embedded tracks, so a failure here
        // becomes a listed-but-empty track instead of propagating.
        QString sidecarError;
        if (!readContainer(sidecar, videoBase, tracks, requestId, &sidecarError)) {
            SubtitleTrack broken;
            broken.sidecar = true;
            broken.sourcePath = sidecar;
            broken.title = QFileInfo(sidecar).fileName();
            broken.note = sidecarError;
            tracks.append(broken);
        }
    }

    if (cancelled(requestId))
        return;

    for (int i = 0; i < tracks.size(); ++i)
        tracks[i].id = i;

    emit finished(requestId, tracks);
}

// `videoBase` non-empty means `path` is a sidecar file: it is the base name of
// the video the sidecar sits next to, used to pull the language tag out of the
// filename.
bool SubtitleExtractor::readContainer(const QString &path, const QString &videoBase,
                                      SubtitleTrackList &out, int requestId,
                                      QString *error)
{
    const bool sidecar = !videoBase.isEmpty();
    const QString shortName = QFileInfo(path).fileName();
    const QByteArray encodedPath = QFile::encodeName(path);

    FormatContextGuard fmt;
    int rc = avformat_open_input(&fmt.ctx, encodedPath.constData(), nullptr, nullptr);
    if (rc < 0) {
        *error = QStringLiteral("cannot open %1: %2").arg(shortName, avError(rc));
        return false;
    }

    rc = avformat_find_stream_info(fmt.ctx, nullptr);
    if (rc < 0) {
        *error = QStringLiteral("no stream info in %1: %2").arg(shortName, avError(rc));
        return false;
    }

    // stream index -> index into `out`, and stream index -> its open decoder.
    QHash<int, int> trackForStream;
    QHash<int, AVCodecContext *> decoderForStream;
    auto freeDecoders = [&decoderForStream] {
        for (AVCodecContext *ctx : std::as_const(decoderForStream))
            avcodec_free_context(&ctx);
        decoderForStream.clear();
    };

    for (unsigned i = 0; i < fmt.ctx->nb_streams; ++i) {
        AVStream *st = fmt.ctx->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            // Video and audio packets are pure overhead for this pass.
            st->discard = AVDISCARD_ALL;
            continue;
        }

        SubtitleTrack track;
        track.streamIndex = static_cast<int>(i);
        track.sourcePath = path;
        track.sidecar = sidecar;
        track.kind = kindFor(st->codecpar->codec_id);
        if (const AVCodecDescriptor *d = avcodec_descriptor_get(st->codecpar->codec_id))
            track.codecName = QString::fromUtf8(d->name);
        track.language = dictValue(st->metadata, "language");
        track.title = dictValue(st->metadata, "title");

        if (sidecar) {
            if (track.title.isEmpty())
                track.title = shortName;
            if (track.language.isEmpty())
                track.language = languageFromSidecarName(videoBase, QFileInfo(path));
        }

        if (track.kind == SubtitleKind::Bitmap) {
            // PGS, VOBSUB and friends are images. There is no text to list, and
            // producing any would mean OCR -- explicitly out of scope.
            track.note = QStringLiteral("bitmap format: no text without OCR");
            st->discard = AVDISCARD_ALL;
            out.append(track);
            continue;
        }

        const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            track.note = QStringLiteral("no decoder for %1").arg(track.codecName);
            st->discard = AVDISCARD_ALL;
            out.append(track);
            continue;
        }

        CodecContextGuard dctx;
        dctx.ctx = avcodec_alloc_context3(dec);
        if (!dctx.ctx || avcodec_parameters_to_context(dctx.ctx, st->codecpar) < 0) {
            track.note = QStringLiteral("decoder setup failed");
            st->discard = AVDISCARD_ALL;
            out.append(track);
            continue;
        }
        // Some subtitle decoders rescale against this rather than the packet's
        // own stream, and get their timing wrong without it.
        dctx.ctx->pkt_timebase = st->time_base;

        rc = avcodec_open2(dctx.ctx, dec, nullptr);
        if (rc < 0) {
            track.note = QStringLiteral("cannot open %1 decoder: %2")
                             .arg(track.codecName, avError(rc));
            st->discard = AVDISCARD_ALL;
            out.append(track);
            continue;
        }

        trackForStream.insert(static_cast<int>(i), out.size());
        decoderForStream.insert(static_cast<int>(i), dctx.ctx);
        dctx.ctx = nullptr;  // ownership moved into decoderForStream
        out.append(track);
    }

    if (trackForStream.isEmpty())
        return true;  // nothing decodable here, but that is not a failure

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        freeDecoders();
        *error = QStringLiteral("out of memory");
        return false;
    }

    const AVRational msBase{1, 1000};

    // mpv rebases playback to start at zero (--rebase-start-time, on by default),
    // so a container whose timestamps begin elsewhere -- MPEG-TS routinely starts
    // an hour in -- needs the same shift or every seek from the browser lands in
    // the wrong place. Applied per track after demuxing, not here: see below.
    const qint64 startOffsetMs =
        fmt.ctx->start_time != AV_NOPTS_VALUE
            ? std::max<qint64>(0, av_rescale_q(fmt.ctx->start_time, AV_TIME_BASE_Q,
                                              msBase))
            : 0;

    // Progress is measured in bytes read, not cues found. Subtitle packets are
    // scattered through the container, so the whole file has to be walked
    // whatever the cue count -- which is why a 3 GB film takes ~9 s while a
    // 200k-cue sidecar takes under one.
    const qint64 totalBytes = fmt.ctx->pb ? avio_size(fmt.ctx->pb) : 0;
    int lastPercent = -1;

    int packetsSeen = 0;
    while (av_read_frame(fmt.ctx, pkt) >= 0) {
        // Cancellation is polled rather than checked per packet: the check is
        // cheap, but this loop runs tens of thousands of times on a feature.
        if ((++packetsSeen & 0xFF) == 0 && totalBytes > 0 && fmt.ctx->pb) {
            const qint64 done = avio_tell(fmt.ctx->pb) * 100 / totalBytes;
            const int percent = int(qBound(qint64(0), done, qint64(100)));
            if (percent != lastPercent) {
                lastPercent = percent;
                emit progress(requestId, percent);
            }
        }
        if ((packetsSeen & 0xFF) == 0 && cancelled(requestId)) {
            av_packet_free(&pkt);
            freeDecoders();
            return true;
        }

        AVCodecContext *dec = decoderForStream.value(pkt->stream_index, nullptr);
        if (!dec) {
            av_packet_unref(pkt);
            continue;
        }

        AVSubtitle sub{};
        int got = 0;
        const int ret = avcodec_decode_subtitle2(dec, &sub, &got, pkt);
        if (ret < 0 || !got) {
            if (got)
                avsubtitle_free(&sub);
            av_packet_unref(pkt);
            continue;
        }

        AVStream *st = fmt.ctx->streams[pkt->stream_index];
        const qint64 baseMs = pkt->pts != AV_NOPTS_VALUE
                                  ? av_rescale_q(pkt->pts, st->time_base, msBase)
                                  : 0;
        const qint64 packetMs =
            pkt->duration > 0 ? av_rescale_q(pkt->duration, st->time_base, msBase) : 0;

        QStringList rawParts;
        for (unsigned r = 0; r < sub.num_rects; ++r) {
            const AVSubtitleRect *rect = sub.rects[r];
            if (!rect)
                continue;
            if (rect->ass && *rect->ass)
                rawParts << assDialogueText(QString::fromUtf8(rect->ass));
            else if (rect->text && *rect->text)
                rawParts << QString::fromUtf8(rect->text);
        }

        if (!rawParts.isEmpty()) {
            SubtitleLine line;
            line.rawText = rawParts.join(QLatin1Char('\n'));
            line.text = decodeEntities(stripAssTags(line.rawText));
            line.startMs = baseMs + sub.start_display_time;

            if (sub.end_display_time > sub.start_display_time
                && sub.end_display_time != UINT32_MAX)
                line.endMs = baseMs + sub.end_display_time;
            else if (packetMs > 0)
                line.endMs = baseMs + packetMs;
            else
                line.endMs = line.startMs + kFallbackCueMs;

            // Blank cues are real (timing-only ASS events, karaoke leftovers) and
            // would be dead rows in the browser.
            if (!line.text.isEmpty())
                out[trackForStream.value(pkt->stream_index)].lines.append(line);
        }

        avsubtitle_free(&sub);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    freeDecoders();

    // Containers do not guarantee subtitle packets arrive in presentation order,
    // and both click-to-seek and auto-follow assume a sorted list.
    for (int trackIdx : std::as_const(trackForStream)) {
        QVector<SubtitleLine> &lines = out[trackIdx].lines;
        std::stable_sort(lines.begin(), lines.end(),
                         [](const SubtitleLine &a, const SubtitleLine &b) {
                             return a.startMs < b.startMs;
                         });

        // Rebase only when this track's own timeline actually carries the
        // container's offset. Muxers do produce files where the video starts an
        // hour in but the subtitle stream still starts at zero; subtracting
        // there would flatten every cue onto 00:00:00 rather than fix anything.
        if (startOffsetMs > 0 && !lines.isEmpty()
            && lines.constFirst().startMs >= startOffsetMs) {
            for (SubtitleLine &line : lines) {
                line.startMs -= startOffsetMs;
                line.endMs = std::max(line.startMs, line.endMs - startOffsetMs);
            }
        }

        if (lines.isEmpty() && out[trackIdx].note.isEmpty())
            out[trackIdx].note = QStringLiteral("decoded to no text");
    }

    return true;
}
