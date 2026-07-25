#include "SubtitleStyle.h"

#include "SubtitleText.h"

#include <QtCore/QHash>
#include <QtCore/QPair>
#include <QtCore/QStringView>

#include <cmath>
#include <utility>

namespace {

// WCAG relative luminance, and the contrast ratio built from it. A plain
// brightness difference is not good enough here: it calls pure red on a nearly
// black row unreadable, when it is one of the most legible things on it.
double relativeLuminance(const QColor &colour)
{
    auto channel = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF())
           + 0.0722 * channel(colour.blueF());
}

// WCAG's threshold for body text, which is what a row of dialogue is. The hue
// and saturation survive the adjustment, so speakers stay distinguishable even
// when their lightness has to move a long way.
constexpr double kMinimumContrast = 4.5;

void appendEscaped(QString &out, QChar c)
{
    switch (c.unicode()) {
    case u'<':
        out += QLatin1String("&lt;");
        break;
    case u'>':
        out += QLatin1String("&gt;");
        break;
    case u'&':
        out += QLatin1String("&amp;");
        break;
    case u'"':
        out += QLatin1String("&quot;");
        break;
    default:
        out += c;
    }
}

// The state a run of text is drawn in. Compared between runs so tags are only
// opened and closed when something actually changes.
struct Style
{
    bool italic = false;
    bool bold = false;
    bool underline = false;
    QColor colour;  // invalid means "whatever the row's own colour is"

    bool operator==(const Style &o) const
    {
        return italic == o.italic && bold == o.bold && underline == o.underline
               && colour == o.colour;
    }
    bool operator!=(const Style &o) const { return !(*this == o); }
};

void openTags(QString &out, const Style &style)
{
    if (style.colour.isValid()) {
        out += QLatin1String("<font color=\"");
        out += style.colour.name(QColor::HexRgb);
        out += QLatin1String("\">");
    }
    if (style.bold)
        out += QLatin1String("<b>");
    if (style.italic)
        out += QLatin1String("<i>");
    if (style.underline)
        out += QLatin1String("<u>");
}

void closeTags(QString &out, const Style &style)
{
    if (style.underline)
        out += QLatin1String("</u>");
    if (style.italic)
        out += QLatin1String("</i>");
    if (style.bold)
        out += QLatin1String("</b>");
    if (style.colour.isValid())
        out += QLatin1String("</font>");
}

// One {\...} override block. Only the tags that change how a line *reads* are
// handled: position, rotation, karaoke timing and the rest say where and when to
// draw something over the video, which a list of lines has no use for.
void applyOverrides(QStringView block, const Style &base, Style &style,
                    int &drawingScale, const QColor &background)
{
    for (qsizetype i = 0; i < block.size(); ++i) {
        if (block.at(i) != u'\\')
            continue;

        const QStringView rest = block.mid(i + 1);
        auto number = [&rest](qsizetype offset, int fallback) {
            qsizetype end = offset;
            while (end < rest.size() && rest.at(end).isDigit())
                ++end;
            if (end == offset)
                return fallback;
            return rest.mid(offset, end - offset).toInt();
        };

        if (rest.startsWith(u'r')) {
            // \r resets to the line's style, and \rName switches to another
            // named style we do not have. Both mean "stop whatever I said".
            style = base;
        } else if (rest.startsWith(QLatin1String("i")) && rest.size() > 1
                   && rest.at(1).isDigit()) {
            style.italic = number(1, 0) != 0;
        } else if (rest.startsWith(QLatin1String("b")) && rest.size() > 1
                   && rest.at(1).isDigit()) {
            // \b1 is bold, \b0 is not, and \b400..\b900 are weights -- anything
            // at or above 500 reads as bold here.
            const int weight = number(1, 0);
            style.bold = weight == 1 || weight >= 500;
        } else if (rest.startsWith(QLatin1String("u")) && rest.size() > 1
                   && rest.at(1).isDigit()) {
            style.underline = number(1, 0) != 0;
        } else if (rest.startsWith(QLatin1String("p")) && rest.size() > 1
                   && rest.at(1).isDigit()) {
            // \p1 and up start a vector drawing: what follows is coordinates,
            // not words. \p0 ends it.
            drawingScale = number(1, 0);
        } else if (rest.startsWith(QLatin1String("c&H"))
                   || rest.startsWith(QLatin1String("1c&H"))) {
            const qsizetype amp = rest.indexOf(u'&', 1);
            qsizetype end = rest.indexOf(u'&', amp + 1);
            // libass tolerates a missing closing &, so the rest of the block is
            // the value in that case rather than the tag being dropped.
            if (end < 0)
                end = rest.size() - 1;
            if (end >= amp) {
                const QColor parsed =
                    SubtitleStyle::parseAssColour(rest.mid(amp, end - amp + 1));
                if (parsed.isValid())
                    style.colour = SubtitleStyle::readableOn(parsed, background);
            }
        }
    }
}

}  // namespace

namespace SubtitleStyle {

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor parseAssColour(QStringView field)
{
    // &HBBGGRR& or &HAABBGGRR&, and libass tolerates a missing trailing &.
    if (field.startsWith(u'&'))
        field = field.mid(1);
    if (field.startsWith(u'H') || field.startsWith(u'h'))
        field = field.mid(1);
    while (field.endsWith(u'&'))
        field.chop(1);
    if (field.isEmpty() || field.size() > 8)
        return {};

    bool ok = false;
    const uint value = field.toUInt(&ok, 16);
    if (!ok)
        return {};

    // Blue first. Alpha, when present, is transparency rather than opacity, and
    // is dropped: a half-transparent cue over video is still a cue in a list.
    return QColor(int(value & 0xFF), int((value >> 8) & 0xFF),
                  int((value >> 16) & 0xFF));
}

QColor readableOn(const QColor &colour, const QColor &background)
{
    if (!colour.isValid() || !background.isValid())
        return colour;

    // Memoised, because this is on the hottest path in the browser and it is
    // not cheap: the walk below runs up to 50 steps, each computing two relative
    // luminances, each of those three std::pow -- so up to 300 pow() calls for
    // one colour tag. Styling is derived per *visible row* and Text queries the
    // role more than once per row when it wraps, so a karaoke or multi-speaker
    // ASS line with a dozen colour tags was paying that repeatedly while
    // scrolling.
    //
    // The domain is tiny and that is the whole point: a track uses a handful of
    // speaker colours against one row background, so the table saturates within
    // the first screenful and never grows again. thread_local rather than a
    // shared cache with a mutex -- the contention would cost more than the work.
    static thread_local QHash<QPair<QRgb, QRgb>, QColor> memo;
    const QPair<QRgb, QRgb> key(colour.rgb(), background.rgb());
    const auto cached = memo.constFind(key);
    if (cached != memo.constEnd())
        return cached.value();

    const QColor answer = readableOnUncached(colour, background);
    // A cue can only carry so many distinct colours before something has gone
    // wrong with the file rather than with us; drop the table rather than let a
    // pathological track grow it without bound.
    if (memo.size() > 4096)
        memo.clear();
    memo.insert(key, answer);
    return answer;
}

QColor readableOnUncached(const QColor &colour, const QColor &background)
{
    if (SubtitleStyle::contrastRatio(colour, background) >= kMinimumContrast)
        return colour;

    // Keep the hue and the saturation -- they are what says "this speaker" --
    // and walk the lightness away from the background until the two can be told
    // apart. White dialogue on a light theme ends up dark grey rather than
    // invisible, and a yellow speaker stays recognisably yellow.
    const QColor hsl = colour.toHsl();
    const qreal hue = hsl.hslHueF();
    const qreal saturation = hue < 0 ? 0 : hsl.hslSaturationF();
    const bool darken = relativeLuminance(background) > 0.18;

    qreal lightness = hsl.lightnessF();
    for (int step = 0; step < 50; ++step) {
        lightness = darken ? lightness - 0.02 : lightness + 0.02;
        if (lightness <= 0.0 || lightness >= 1.0)
            break;
        QColor candidate;
        candidate.setHslF(hue < 0 ? 0 : hue, saturation, lightness, 1.0);
        if (SubtitleStyle::contrastRatio(candidate, background) >= kMinimumContrast)
            return candidate.toRgb();
    }

    // Nothing in that direction worked -- a mid-grey background can leave no
    // room. Black or white, whichever the background is further from.
    return relativeLuminance(background) > 0.18 ? QColor(Qt::black) : QColor(Qt::white);
}

QString toStyledText(const QString &assPayload, const QColor &background)
{
    QString out;
    out.reserve(assPayload.size() + 16);

    const Style base;
    Style style;
    Style open;  // what is currently emitted
    bool anyOpen = false;
    int drawingScale = 0;

    auto sync = [&] {
        if (anyOpen && open == style)
            return;
        if (anyOpen)
            closeTags(out, open);
        openTags(out, style);
        open = style;
        anyOpen = true;
    };

    for (qsizetype i = 0; i < assPayload.size(); ++i) {
        const QChar c = assPayload.at(i);

        if (c == u'{') {
            const qsizetype end = assPayload.indexOf(u'}', i + 1);
            if (end < 0) {
                // Unclosed brace: the rest is not markup we understand, so it is
                // dropped rather than shown as literal tags.
                break;
            }
            applyOverrides(QStringView(assPayload).mid(i + 1, end - i - 1), base,
                           style, drawingScale, background);
            i = end;
            continue;
        }

        // Inside a vector drawing the "text" is a path: m 0 0 l 100 0 and so on.
        if (drawingScale > 0)
            continue;

        // A real newline: the extractor joins a cue's rects with one, and
        // StyledText would treat it as ordinary whitespace.
        if (c == u'\n') {
            sync();
            out += QLatin1String("<br>");
            continue;
        }

        if (c == u'\\' && i + 1 < assPayload.size()) {
            const QChar n = assPayload.at(i + 1);
            if (n == u'N' || n == u'n') {
                sync();
                out += QLatin1String("<br>");
                ++i;
                continue;
            }
            if (n == u'h') {
                sync();
                out += QLatin1String("&nbsp;");
                ++i;
                continue;
            }
        }

        // Entities are decoded here, on the text runs only, and then escaped
        // again on the way out -- so "&amp;" arrives, becomes "&", and is
        // emitted as "&amp;", which StyledText renders as "&". That is exactly
        // what the flattened `text` role holds, which is what search matches.
        //
        // Doing it any earlier would corrupt the payload: an "&amp;lt;" in
        // dialogue would decode to "<" and then be read as the start of a tag.
        if (c == u'&') {
            QString decoded;
            const qsizetype used = SubtitleText::decodeEntityAt(
                QStringView(assPayload), i, &decoded);
            if (used > 0) {
                sync();
                for (const QChar d : std::as_const(decoded))
                    appendEscaped(out, d);
                i += used - 1;
                continue;
            }
        }

        sync();
        appendEscaped(out, c);
    }

    if (anyOpen)
        closeTags(out, open);

    // Leading and trailing breaks are noise in a list; the cue's own text is
    // what the row is for.
    while (out.startsWith(QLatin1String("<br>")))
        out = out.mid(4);
    while (out.endsWith(QLatin1String("<br>")))
        out.chop(4);
    return out.trimmed();
}

}  // namespace SubtitleStyle
