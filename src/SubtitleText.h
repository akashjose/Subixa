#pragma once

#include <QtCore/QString>
#include <QtCore/QStringView>

// Text handling shared by the two things that turn a decoder payload into
// something a reader sees: the extractor's flattened `text`, and
// SubtitleStyle's markup.
//
// It lives here because those two had drifted. ffmpeg's SRT and WebVTT decoders
// convert `<i>` into ASS override tags but leave character entities alone, so
// "Fish &amp; chips" arrives literal. The extractor decoded them; the styling
// path did not, and then escaped the surviving ampersand -- so with styling on
// (the default) the browser showed "Fish &amp; chips" while search matched
// "Fish & chips". A browser whose displayed text and searched text disagree is
// the one bug this panel must not have.
namespace SubtitleText {

// Decodes the character entity beginning at `pos`, which must be '&'. Returns
// how many source characters it consumed, or 0 when what is there is not an
// entity at all -- a bare ampersand, which is common in dialogue and must
// survive unchanged.
//
// Exposed per-position, rather than only as the whole-string form below,
// because the styling path cannot decode the payload up front: it has to walk
// it anyway to tell override blocks and vector drawings from words, and only
// the words are entity-bearing.
qsizetype decodeEntityAt(QStringView in, qsizetype pos, QString *out);

// The whole-string form, for a payload that is already known to be all text.
QString decodeEntities(const QString &in);

}  // namespace SubtitleText
