// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Akash Jose

#include "GraphicsSetup.h"

#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QSettings>
#include <QtCore/QStringList>
#include <QtGui/QGuiApplication>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>

#include <cstdio>

namespace {

constexpr auto kProbeFlag = "--gl-probe";
constexpr auto kDriver = "d3d12";

// Bumped when the probe's meaning changes, so a cached answer from an older
// build is not trusted.
constexpr int kProbeVersion = 1;

bool isSoftwareRenderer(const QString &renderer)
{
    const QString lower = renderer.toLower();
    return lower.contains(QLatin1String("llvmpipe"))
           || lower.contains(QLatin1String("softpipe"))
           || lower.contains(QLatin1String("swrast"))
           || lower.contains(QLatin1String("software"));
}

bool runningUnderWsl()
{
    QFile osrelease(QStringLiteral("/proc/sys/kernel/osrelease"));
    if (!osrelease.open(QIODevice::ReadOnly))
        return false;
    return osrelease.readAll().toLower().contains("microsoft");
}

// The pieces D3D12 passthrough needs. All three ship with WSL and a current
// Mesa, but a machine missing any of them would fail to create a context at all
// -- and a forced GALLIUM_DRIVER does not fall back, it just fails.
bool d3d12PartsPresent()
{
    return QFile::exists(QStringLiteral("/dev/dxg"))
           && QFile::exists(QStringLiteral("/usr/lib/wsl/lib/libd3d12.so"))
           && QFile::exists(
               QStringLiteral("/usr/lib/x86_64-linux-gnu/dri/d3d12_dri.so"));
}

QString cacheKey()
{
    // Keyed loosely: a kernel upgrade can change whether passthrough works, and
    // re-probing once after one is cheap.
    QFile osrelease(QStringLiteral("/proc/sys/kernel/osrelease"));
    QString kernel;
    if (osrelease.open(QIODevice::ReadOnly))
        kernel = QString::fromLatin1(osrelease.readAll()).trimmed();
    return QStringLiteral("graphics/probe-%1-%2").arg(kProbeVersion).arg(kernel);
}

}  // namespace

namespace GraphicsSetup {

bool isProbeRequest(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], kProbeFlag) == 0)
            return true;
    }
    return false;
}

int runProbe()
{
    // Deliberately minimal: create a context on an offscreen surface, read
    // GL_RENDERER, print it, leave. If the forced driver cannot load, this is
    // where it fails -- in a throwaway process rather than in the player.
    QOffscreenSurface surface;
    surface.create();
    if (!surface.isValid())
        return 2;

    QOpenGLContext context;
    if (!context.create() || !context.makeCurrent(&surface))
        return 3;

    const auto *name = reinterpret_cast<const char *>(
        context.functions()->glGetString(GL_RENDERER));
    const QString renderer = QString::fromUtf8(name ? name : "");
    context.doneCurrent();

    if (renderer.isEmpty())
        return 4;

    std::fprintf(stdout, "%s\n", renderer.toUtf8().constData());
    std::fflush(stdout);

    // Non-zero for a software rasterizer: forcing the driver bought nothing.
    return isSoftwareRenderer(renderer) ? 1 : 0;
}

Choice configure(int argc, char *argv[], QSettings &settings)
{
    Q_UNUSED(argc);

    if (qEnvironmentVariableIsSet("SUBIXA_NO_GPU"))
        return {QString(), QStringLiteral("SUBIXA_NO_GPU set, leaving the driver alone")};

    if (qEnvironmentVariableIsSet("GALLIUM_DRIVER")) {
        return {QString::fromLocal8Bit(qgetenv("GALLIUM_DRIVER")),
                QStringLiteral("GALLIUM_DRIVER already set, honouring it")};
    }

    if (!runningUnderWsl()) {
        // Native Linux picks the right driver by itself, and Windows has no
        // Mesa to point at. Nothing to do, which is the correct answer.
        return {QString(), QStringLiteral("not WSL, leaving driver selection to Mesa")};
    }

    if (!d3d12PartsPresent()) {
        return {QString(),
                QStringLiteral("WSL without D3D12 passthrough, staying on software")};
    }

    // Ask the cache before spending a process on it.
    const QString key = cacheKey();
    const QVariant cached = settings.value(key);
    if (cached.isValid()) {
        if (!cached.toBool())
            return {QString(), QStringLiteral("probe previously failed, staying on software")};
        qputenv("GALLIUM_DRIVER", kDriver);
        return {QString::fromLatin1(kDriver), QStringLiteral("hardware GL (remembered)")};
    }

    // Probe in a child so a driver that cannot load takes the child down and not
    // the player. Qt has not been initialised yet in this process, so the child
    // gets a clean environment of its own.
    QProcess probe;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GALLIUM_DRIVER"), QString::fromLatin1(kDriver));
    // The probe needs no window; offscreen keeps it from touching the compositor.
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    probe.setProcessEnvironment(env);
    probe.start(QString::fromLocal8Bit(argv[0]),
                {QString::fromLatin1(kProbeFlag)});

    if (!probe.waitForFinished(8000)) {
        probe.kill();
        probe.waitForFinished(1000);
        settings.setValue(key, false);
        return {QString(), QStringLiteral("hardware probe timed out, staying on software")};
    }

    const QString renderer = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        settings.setValue(key, false);
        return {QString(),
                QStringLiteral("hardware probe failed (%1), staying on software")
                    .arg(renderer.isEmpty() ? QStringLiteral("no renderer") : renderer)};
    }

    settings.setValue(key, true);
    qputenv("GALLIUM_DRIVER", kDriver);
    return {QString::fromLatin1(kDriver),
            QStringLiteral("hardware GL confirmed by probe: %1").arg(renderer)};
}

}  // namespace GraphicsSetup
