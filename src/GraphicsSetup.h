// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#pragma once

#include <QtCore/QString>

// Picks a GL driver before Qt creates its first context, so the player is not
// silently stuck on a software rasterizer when the machine has a usable GPU.
//
// This exists because WSLg is not honest about it: Qt's EGL path fails with
// "failed to create dri2 screen" and Mesa quietly lands on llvmpipe, where
// 10-bit video renders wrong and a large framebuffer renders corrupt (CLAUDE.md
// traps 9 and 10). Setting GALLIUM_DRIVER=d3d12 gets real hardware GL through
// the host GPU, and that had to be typed by hand on every run.
//
// It is deliberately *not* a general "try every driver" mechanism. On native
// Linux Mesa already picks iris/radeonsi/nouveau correctly and interfering could
// only make that worse; on Windows there is no Mesa in the picture at all. WSL
// is the one environment that needs a nudge, so WSL is the only case handled.
namespace GraphicsSetup {

// Result of choosing, for logging.
struct Choice
{
    QString driver;   // what was forced, empty if nothing was
    QString reason;   // why, in a form worth putting in the log
};

// Runs before QGuiApplication. May set GALLIUM_DRIVER in this process's
// environment. Honours an existing GALLIUM_DRIVER and SUBIXA_NO_GPU=1.
Choice configure(int argc, char *argv[]);

// True when argv asks for probe mode -- the child process spawned by configure()
// to find out whether the hardware driver actually works. It creates a context,
// prints GL_RENDERER, and exits.
bool isProbeRequest(int argc, char *argv[]);

// Body of probe mode. Returns the process exit code.
int runProbe();

}  // namespace GraphicsSetup
