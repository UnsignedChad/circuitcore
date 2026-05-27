// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// circuitcore_studio entry point.
//
// Single mode: launch the Qt GUI. An optional positional argument is
// opened as a .kicad_pcb on startup. The headless analysis paths live
// in the per-tool binaries (sikit, pdnkit, emikit) -- studio is GUI-only
// by design, since the value here is the unified workflow.

#include <QApplication>
#include <QString>
#include <QSurfaceFormat>

#include "StudioWindow.h"

int main(int argc, char** argv) {
    // Even though the skeleton's Board tab is QPainter-only today, the
    // shared OpenGL canvas lands in a follow-up PR. Request a 3.3 core
    // profile up-front so the swap-in is seamless.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setOrganizationName("circuitcore");
    QApplication::setOrganizationName("circuitcore");
    QApplication::setApplicationName("circuitcore-studio");
    QApplication::setApplicationVersion("0.0.1");

    circuitcore::studio::StudioWindow w;
    w.show();
    if (argc > 1) {
        w.loadKicadPcb(QString::fromUtf8(argv[1]));
    }
    return app.exec();
}
