#include "PaintedText.h"

#include <QtGui/QFontMetricsF>
#include <QtGui/QPainter>
#include <QtGui/QAbstractTextDocumentLayout>
#include <QtGui/QPalette>
#include <QtGui/QTextDocument>
#include <QtGui/QTextOption>

namespace {

// Text's wrapMode values, which are what callers pass through.
enum QmlWrapMode { NoWrap = 0, WordWrap = 1, WrapAnywhere = 2, Wrap = 3 };
// Text's elide values.
enum QmlElide { ElideNone = 0, ElideLeft = 1, ElideMiddle = 2, ElideRight = 3 };

Qt::TextElideMode toElideMode(int elide)
{
    switch (elide) {
    case ElideLeft:
        return Qt::ElideLeft;
    case ElideMiddle:
        return Qt::ElideMiddle;
    case ElideRight:
        return Qt::ElideRight;
    default:
        return Qt::ElideNone;
    }
}

QTextOption::WrapMode toWrapMode(int wrapMode)
{
    switch (wrapMode) {
    case WordWrap:
        return QTextOption::WordWrap;
    case WrapAnywhere:
        return QTextOption::WrapAnywhere;
    case Wrap:
        return QTextOption::WrapAtWordBoundaryOrAnywhere;
    default:
        return QTextOption::NoWrap;
    }
}

}  // namespace

PaintedText::PaintedText(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    // The whole point is to hand the scene graph a plain texture rather than a
    // glyph material, so the item must have its own painted surface.
    //
    // Image, not FramebufferObject: an FBO per item would mean one GPU
    // allocation for every label on screen, and the subtitle browser alone has
    // two per visible row. These are small, mostly static rectangles of text --
    // painting into a QImage on the CPU and uploading it is cheaper, and it
    // avoids FBO churn as rows are recycled during a scroll.
    setRenderTarget(QQuickPaintedItem::Image);
    setMipmap(false);
    setAntialiasing(true);
    // Nothing here is opaque -- text is glyph coverage over whatever is behind.
    setOpaquePainting(false);
}

PaintedText::~PaintedText() = default;

QTextDocument *PaintedText::document()
{
    if (!m_document) {
        m_document = std::make_unique<QTextDocument>();
        m_document->setDocumentMargin(0);
    }
    return m_document.get();
}

qreal PaintedText::availableWidth() const
{
    return qMax(0.0, width() - m_leftPadding - m_rightPadding);
}

void PaintedText::refresh()
{
    const bool wrapping = m_wrapMode != NoWrap;
    const qreal avail = availableWidth();

    if (m_textFormat == StyledText || m_textFormat == RichText) {
        QTextDocument *doc = document();
        doc->setDefaultFont(m_font);
        QTextOption option;
        option.setWrapMode(toWrapMode(m_wrapMode));
        option.setAlignment(Qt::Alignment(m_hAlign));
        doc->setDefaultTextOption(option);
        doc->setHtml(m_text);
        doc->setTextWidth(wrapping && avail > 0 ? avail : -1);
        m_contentSize = doc->size();
    } else {
        const QFontMetricsF metrics(m_font);
        if (wrapping && avail > 0) {
            const QRectF bounds =
                metrics.boundingRect(QRectF(0, 0, avail, 0),
                                     int(toWrapMode(m_wrapMode) == QTextOption::NoWrap
                                             ? Qt::TextSingleLine
                                             : Qt::TextWordWrap),
                                     m_text);
            m_contentSize = bounds.size();
        } else {
            m_contentSize = QSizeF(metrics.horizontalAdvance(m_text),
                                   metrics.height());
        }
        // Line height is applied as a multiplier the way Text's
        // ProportionalHeight does, so a wrapped cue in the browser keeps the
        // spacing the design asks for.
        if (m_lineHeight != 1.0 && m_contentSize.height() > 0)
            m_contentSize.setHeight(m_contentSize.height() * m_lineHeight);
    }

    // A line cap is a height cap: the paint clips, so anything past the limit
    // is simply not drawn. Text would elide the last line instead, which is
    // nicer and not worth a line-breaking pass here for the one caller that
    // uses it -- the seek bar's two-line cue preview.
    if (m_maximumLineCount > 0) {
        const qreal cap = QFontMetricsF(m_font).lineSpacing() * m_maximumLineCount
                          * (m_lineHeight > 0 ? m_lineHeight : 1.0);
        m_contentSize.setHeight(qMin(m_contentSize.height(), cap));
    }

    setImplicitWidth(m_contentSize.width() + m_leftPadding + m_rightPadding);
    setImplicitHeight(m_contentSize.height());
    emit contentSizeChanged();
    update();
}

void PaintedText::paint(QPainter *painter)
{
    if (m_text.isEmpty() || !m_color.isValid())
        return;

    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setFont(m_font);
    painter->setPen(m_color);

    if (m_textFormat == StyledText || m_textFormat == RichText) {
        QTextDocument *doc = document();
        doc->setTextWidth(m_wrapMode != NoWrap ? availableWidth() : -1);
        // The document carries its own colours for anything the markup sets --
        // a speaker's colour, say -- and inherits this one for the rest.
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, m_color);
        context.clip = QRectF(0, 0, availableWidth(), height());
        painter->save();
        painter->translate(m_leftPadding, 0);
        // Vertical alignment is ours to apply: a document always lays out from
        // its own origin.
        if (m_vAlign & Qt::AlignVCenter)
            painter->translate(0, (height() - doc->size().height()) / 2.0);
        else if (m_vAlign & Qt::AlignBottom)
            painter->translate(0, height() - doc->size().height());
        doc->documentLayout()->draw(painter, context);
        painter->restore();
        return;
    }

    QTextOption option;
    option.setWrapMode(toWrapMode(m_wrapMode));
    option.setAlignment(Qt::Alignment(m_hAlign | m_vAlign));

    QString shown = m_text;
    if (m_elide != ElideNone && m_wrapMode == NoWrap && availableWidth() > 0) {
        const QFontMetricsF metrics(m_font);
        shown = metrics.elidedText(m_text, toElideMode(m_elide), availableWidth());
    }

    painter->drawText(QRectF(m_leftPadding, 0, availableWidth(), height()),
                      shown, option);
}

void PaintedText::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    // Width decides wrapping and eliding, so a resize changes the content size
    // as well as the surface.
    if (newGeometry.width() != oldGeometry.width())
        refresh();
}

void PaintedText::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    emit textChanged();
    refresh();
}

void PaintedText::setColor(const QColor &color)
{
    if (m_color == color)
        return;
    m_color = color;
    emit colorChanged();
    update();
}

void PaintedText::setFont(const QFont &font)
{
    if (m_font == font)
        return;
    m_font = font;
    emit fontChanged();
    refresh();
}

void PaintedText::setTextFormat(int format)
{
    if (m_textFormat == format)
        return;
    m_textFormat = format;
    emit textFormatChanged();
    refresh();
}

void PaintedText::setWrapMode(int mode)
{
    if (m_wrapMode == mode)
        return;
    m_wrapMode = mode;
    emit wrapModeChanged();
    refresh();
}

void PaintedText::setElide(int elide)
{
    if (m_elide == elide)
        return;
    m_elide = elide;
    emit elideChanged();
    update();
}

void PaintedText::setHorizontalAlignment(int alignment)
{
    if (m_hAlign == alignment)
        return;
    m_hAlign = alignment;
    emit horizontalAlignmentChanged();
    refresh();
}

void PaintedText::setVerticalAlignment(int alignment)
{
    if (m_vAlign == alignment)
        return;
    m_vAlign = alignment;
    emit verticalAlignmentChanged();
    update();
}

void PaintedText::setLineHeight(qreal lineHeight)
{
    if (qFuzzyCompare(m_lineHeight, lineHeight))
        return;
    m_lineHeight = lineHeight;
    emit lineHeightChanged();
    refresh();
}

void PaintedText::setLeftPadding(qreal padding)
{
    if (qFuzzyCompare(m_leftPadding, padding))
        return;
    m_leftPadding = padding;
    emit paddingChanged();
    refresh();
}

void PaintedText::setRightPadding(qreal padding)
{
    if (qFuzzyCompare(m_rightPadding, padding))
        return;
    m_rightPadding = padding;
    emit paddingChanged();
    refresh();
}

void PaintedText::setMaximumLineCount(int count)
{
    if (m_maximumLineCount == count)
        return;
    m_maximumLineCount = count;
    emit maximumLineCountChanged();
    refresh();
}
