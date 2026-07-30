// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtGui/QColor>

#include <QtCore/QString>
#include <QtCore/QStringView>

#include "SubtitleTypes.h"

// Turning a decoder's ASS payload into markup the browser can show.
//
// The extractor already keeps `rawText` per cue for exactly this, and until now
// nothing read it: the list showed the flattened text, so a line spoken in
// italics, or a second speaker's line in a different colour, looked the same as
// everything else. Subtitlers use both to say something, and a browser built for
// reading subtitles should not be the one place that throws it away.
//
// The output is Qt's StyledText subset -- <b>, <i>, <u>, <font color>, <br> --
// rather than full rich text: it is what a QML Text can render cheaply, and
// styling done per visible row must stay cheap on a 200k-cue track.
namespace SubtitleStyle {

// `background` is the colour the row will be drawn on. Subtitle colours are
// chosen to sit over a picture, so a light theme would otherwise render white
// dialogue invisible -- see readableOn().
//
// `base` is the cue's [V4+ Styles] row, which is where most professionally
// authored ASS says everything: a track styled entirely through named styles
// carries no override tags at all, and read without its table it comes out plain
// in the browser while libass draws it italic and coloured over the picture --
// the two halves of the reader's screen disagreeing on the one thing this
// product is for. Override tags are applied *on top* of it, and \r drops back to
// it, which is the precedence ASS itself has.
QString toStyledText(const QString &assPayload, const QColor &background,
                     const AssStyle &base = AssStyle());

// The [V4+ Styles] section of an ASS header, as libavcodec publishes it in
// `AVCodecContext::subtitle_header`, parsed into a name -> style map.
//
// The section's own `Format:` line declares which column is which and files do
// differ, so it is read rather than assumed -- guessing the canonical order would
// silently read Bold out of ScaleX on the files that reorder, and produce
// plausible nonsense instead of an error.
AssStyleTable parseStyleTable(const QString &header);

// A colour close enough to `colour` to still read as that speaker's colour, but
// far enough from `background` to be legible on it. Returns `colour` unchanged
// when it already contrasts.
//
// Memoised: it is computed per visible row rather than stored per cue, and the
// walk it performs is expensive enough that a track with many speaker colours
// felt it while scrolling. The uncached form is exposed so a test can assert
// the guarantee itself rather than whatever happens to be in the table.
QColor readableOn(const QColor &colour, const QColor &background);
QColor readableOnUncached(const QColor &colour, const QColor &background);

// WCAG contrast ratio, 1.0 (identical) to 21.0 (black on white). Exposed
// because it is the promise readableOn() makes -- 4.5:1, the body-text
// threshold -- and a test that checked a lightness number instead would be
// asserting the implementation rather than the guarantee.
double contrastRatio(const QColor &a, const QColor &b);

// "&HBBGGRR&" -- ASS stores colours the other way round from everyone else, and
// with an alpha byte that may or may not be there. Invalid input gives an
// invalid QColor rather than black, so a malformed tag is ignored instead of
// silently painting a line.
QColor parseAssColour(QStringView field);

}  // namespace SubtitleStyle
