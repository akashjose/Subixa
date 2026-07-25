#include "SubtitleText.h"

#include <QtCore/QLatin1String>

namespace {

// Longest entity handled is "&#x10FFFF;"; anything longer is not one, and the
// bound is what stops a stray ampersand from scanning the rest of a cue.
constexpr qsizetype kMaxEntityLen = 10;

struct NamedEntity
{
    QLatin1String name;
    QChar value;
};

// Deliberately short. These are the ones ffmpeg's text decoders leave behind in
// practice; a full HTML5 table would be two thousand entries for no gain in a
// subtitle file.
constexpr NamedEntity kNamed[] = {
    {QLatin1String("amp"), u'&'},   {QLatin1String("lt"), u'<'},
    {QLatin1String("gt"), u'>'},    {QLatin1String("quot"), u'"'},
    {QLatin1String("apos"), u'\''}, {QLatin1String("nbsp"), QChar(0x00A0)},
};

}  // namespace

namespace SubtitleText {

qsizetype decodeEntityAt(QStringView in, qsizetype pos, QString *out)
{
    if (!out || pos < 0 || pos >= in.size() || in.at(pos) != u'&')
        return 0;

    const qsizetype end = in.indexOf(u';', pos + 1);
    if (end < 0 || end - pos > kMaxEntityLen)
        return 0;

    const QStringView body = in.mid(pos + 1, end - pos - 1);
    if (body.isEmpty())
        return 0;

    if (body.startsWith(u'#')) {
        const bool hex =
            body.size() > 1 && (body.at(1) == u'x' || body.at(1) == u'X');
        bool ok = false;
        const char32_t code = body.mid(hex ? 2 : 1).toUInt(&ok, hex ? 16 : 10);
        // Rejecting NUL and anything past the last code point keeps a malformed
        // numeric entity from becoming an invalid QString rather than text.
        if (!ok || code == 0 || code > 0x10FFFF)
            return 0;
        out->append(QString::fromUcs4(&code, 1));
        return end - pos + 1;
    }

    for (const NamedEntity &entity : kNamed) {
        if (body == entity.name) {
            out->append(entity.value);
            return end - pos + 1;
        }
    }
    return 0;
}

QString decodeEntities(const QString &in)
{
    if (!in.contains(u'&'))
        return in;

    QString out;
    out.reserve(in.size());

    for (qsizetype i = 0; i < in.size(); ++i) {
        const qsizetype used = decodeEntityAt(in, i, &out);
        if (used > 0) {
            i += used - 1;
            continue;
        }
        out.append(in.at(i));
    }
    return out;
}

}  // namespace SubtitleText
