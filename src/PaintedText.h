// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>

#include <memory>

class QTextDocument;

// A drop-in replacement for `Text` that paints with QPainter instead of the
// scene graph's glyph materials.
//
// It exists for one reason, and it is somebody else's bug. On Mesa's D3D12
// driver under WSLg, Qt Quick's text materials render in the wrong colour:
// #aab2c2 arrives as pure green, #e8eaf0 as yellow, and an 11px #7e93b5
// timestamp loses so much luminance it reads as black and vanishes. Everything
// else Qt draws is pixel-exact in the same frame -- rectangles, RGB images,
// single-channel greyscale images, and QtQuick.Shapes geometry -- and it
// reproduces in Qt's own `qml` binary with no application code, on Mesa 25.2.8
// and 26.1.5 alike. Text painted through QPainter into an ordinary texture is
// measured correct to the byte on the same driver, which is what this is.
//
// It is deliberately *not* the default. Native `Text` is faster (one shared
// glyph atlas rather than a texture per item), sharper, and correct everywhere
// the driver is. `AppText.qml` chooses between the two at runtime, so a healthy
// machine pays nothing and this disappears the day Mesa is fixed.
//
// The API mirrors the parts of `Text` the app actually uses. It is not a
// complete reimplementation and should not grow into one.
class PaintedText : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    // Mirrors Text.TextFormat for the two values used here: PlainText and
    // StyledText. StyledText goes through QTextDocument, which understands the
    // same <b>/<i>/<u>/<font color>/<br> subset SubtitleStyle emits.
    Q_PROPERTY(int textFormat READ textFormat WRITE setTextFormat NOTIFY textFormatChanged)
    Q_PROPERTY(int wrapMode READ wrapMode WRITE setWrapMode NOTIFY wrapModeChanged)
    Q_PROPERTY(int elide READ elide WRITE setElide NOTIFY elideChanged)
    Q_PROPERTY(int horizontalAlignment READ horizontalAlignment
                   WRITE setHorizontalAlignment NOTIFY horizontalAlignmentChanged)
    Q_PROPERTY(int verticalAlignment READ verticalAlignment
                   WRITE setVerticalAlignment NOTIFY verticalAlignmentChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged)
    // 0 means unlimited, matching Text's default.
    Q_PROPERTY(int maximumLineCount READ maximumLineCount WRITE setMaximumLineCount
                   NOTIFY maximumLineCountChanged)
    Q_PROPERTY(qreal leftPadding READ leftPadding WRITE setLeftPadding NOTIFY paddingChanged)
    Q_PROPERTY(qreal rightPadding READ rightPadding WRITE setRightPadding NOTIFY paddingChanged)
    // Read by layouts exactly as Text's are.
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)

public:
    // Values match Text's so a caller can pass Text.PlainText / Text.StyledText
    // straight through and read the same way as the native item.
    enum TextFormat { PlainText = 0, RichText = 1, StyledText = 4 };
    Q_ENUM(TextFormat)

    explicit PaintedText(QQuickItem *parent = nullptr);
    ~PaintedText() override;

    void paint(QPainter *painter) override;

    QString text() const { return m_text; }
    void setText(const QString &text);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    int textFormat() const { return m_textFormat; }
    void setTextFormat(int format);
    int wrapMode() const { return m_wrapMode; }
    void setWrapMode(int mode);
    int elide() const { return m_elide; }
    void setElide(int elide);
    int horizontalAlignment() const { return m_hAlign; }
    void setHorizontalAlignment(int alignment);
    int verticalAlignment() const { return m_vAlign; }
    void setVerticalAlignment(int alignment);
    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal lineHeight);
    int maximumLineCount() const { return m_maximumLineCount; }
    void setMaximumLineCount(int count);
    qreal leftPadding() const { return m_leftPadding; }
    void setLeftPadding(qreal padding);
    qreal rightPadding() const { return m_rightPadding; }
    void setRightPadding(qreal padding);

    qreal contentWidth() const { return m_contentSize.width(); }
    qreal contentHeight() const { return m_contentSize.height(); }

signals:
    void textChanged();
    void colorChanged();
    void fontChanged();
    void textFormatChanged();
    void wrapModeChanged();
    void elideChanged();
    void horizontalAlignmentChanged();
    void verticalAlignmentChanged();
    void lineHeightChanged();
    void paddingChanged();
    void maximumLineCountChanged();
    void contentSizeChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    // Recomputes the implicit size and asks for a repaint. Everything that can
    // change the drawn result funnels through here, so there is one place that
    // decides what a layout is told.
    void refresh();
    qreal availableWidth() const;
    // The document is built lazily and only for StyledText: a plain row of
    // dialogue is the common case by a wide margin and QPainter draws it
    // directly, without a QTextDocument per visible row.
    QTextDocument *document();

    QString m_text;
    QColor m_color = Qt::white;
    QFont m_font;
    int m_textFormat = PlainText;
    int m_wrapMode = 0;      // Text.NoWrap
    int m_elide = 0;         // Text.ElideNone
    int m_hAlign = Qt::AlignLeft;
    int m_vAlign = Qt::AlignTop;
    qreal m_lineHeight = 1.0;
    int m_maximumLineCount = 0;
    qreal m_leftPadding = 0.0;
    qreal m_rightPadding = 0.0;
    QSizeF m_contentSize;
    std::unique_ptr<QTextDocument> m_document;
};
