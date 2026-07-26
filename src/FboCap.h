// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QSize>

#include <cmath>

// How large a framebuffer to hand mpv, on the software rasterizer only.
//
// Two separate reasons to render smaller than the video pane, both applying only
// when there is no GPU (see CLAUDE.md traps 9 and 10):
//
//   1. Correctness. Above roughly 2.9 MP of FBO, llvmpipe composites a partially
//      rasterised surface -- black, or a fine mesh of unwritten pixels -- even
//      with glFinish() in place. Staying small keeps that unreachable.
//   2. Cost. Software-rendering a 2560 px pane burns ~1000% CPU for a picture no
//      better than rendering at the video's own resolution and letting Qt scale.
//
// The pane's aspect ratio is preserved rather than the video's. mpv letterboxes
// the video inside whatever framebuffer it is given, and Qt stretches that
// framebuffer back across the pane, so changing the aspect here would distort the
// result. Scaling the pane rect uniformly keeps the geometry identical.
namespace FboCap {

// Below the observed boundary rather than just under it. The corruption appeared
// between 2.86 MP (clean) and 2.99 MP (corrupt), and trap 10 records that a
// *trivial* draw at those sizes is fine -- so the failure depends on render load
// as well as area, and a threshold sitting on the measured edge would not hold.
inline constexpr int SafeArea = 2'000'000;

// Scale factor that brings `pane` down to at most `safeArea` pixels, or 1.0.
inline double areaScale(const QSize &pane, int safeArea)
{
    const double area = double(pane.width()) * double(pane.height());
    if (area <= 0.0 || safeArea <= 0 || area <= double(safeArea))
        return 1.0;
    return std::sqrt(double(safeArea) / area);
}

// Scale factor beyond which the video is only being upscaled. mpv fits the video
// into the framebuffer preserving aspect, so what matters is the width the video
// itself occupies in the pane, not the pane's own width -- for a 4:3 video in a
// wide pane those differ a great deal.
inline double nativeScale(const QSize &pane, const QSize &video)
{
    if (video.width() <= 0 || video.height() <= 0 || pane.width() <= 0
        || pane.height() <= 0)
        return 1.0;

    const double videoAspect = double(video.width()) / double(video.height());
    const double fittedWidth =
        std::min(double(pane.width()), double(pane.height()) * videoAspect);
    if (fittedWidth <= 0.0)
        return 1.0;

    // Never scale *up*: a 4K video in a small window must keep the small window.
    return std::min(1.0, double(video.width()) / fittedWidth);
}

// The framebuffer size to render into. `video` may be empty before mpv reports
// the video size, in which case only the area limit applies.
inline QSize cappedSize(const QSize &pane, const QSize &video, int safeArea = SafeArea)
{
    if (pane.width() <= 0 || pane.height() <= 0)
        return pane;

    const double scale = std::min(areaScale(pane, safeArea), nativeScale(pane, video));
    if (scale >= 1.0)
        return pane;

    // Floor, not round: this is a ceiling, and rounding up can cross it. At
    // 2220x1345 rounding gives 1817x1101, which is 2 000 517 px -- over a limit
    // whose whole purpose is not to be exceeded. The lost fraction of a pixel
    // costs nothing; the aspect ratio survives to well under a thousandth.
    return QSize(std::max(1, int(std::floor(pane.width() * scale))),
                 std::max(1, int(std::floor(pane.height() * scale))));
}

}  // namespace FboCap
