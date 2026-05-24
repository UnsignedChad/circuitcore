#include <QApplication>
#include <QSurfaceFormat>
#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "MainWindow.h"

int main(int argc, char** argv) {
    CLI::App cli{"sikit — open-source Power Integrity analysis for KiCad PCBs"};
    cli.allow_extras();  // Don't trip on Qt's --platform, --style, etc.

    std::string pcb_path;
    cli.add_option("--open,pcb", pcb_path,
                   "KiCad .kicad_pcb file to open on startup")
        ->check(CLI::ExistingFile);

    try {
        cli.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return cli.exit(e);
    }

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("sikit");
    QApplication::setApplicationVersion("0.0.1");

    spdlog::info("sikit starting");

    MainWindow w;
    w.show();
    if (!pcb_path.empty()) {
        w.loadKicadPcb(QString::fromStdString(pcb_path));
    }
    return app.exec();
}
