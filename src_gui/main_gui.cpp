#include <pybind11/embed.h>
#include <QApplication>
#include <QMessageBox>
#include <QStyleFactory>
#include "MainWindow.h"
#include <csignal>
#include <exception>
#include <string>

using namespace grip;

int main(int argc, char **argv)
{
    const std::string cfg_path = (argc > 1) ? argv[1] : "configs/config.yaml";

    pybind11::scoped_interpreter guard{};
    pybind11::gil_scoped_release release;

    QApplication app(argc, argv);

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Optional: set a fixed font to reduce layout differences
    QFont f("Segoe UI");          // or "Noto Sans"
    f.setPointSize(10);
    app.setFont(f);

    // Load the configuration only once a QApplication exists, so that a
    // broken or missing file can be reported in a dialog instead of
    // terminating the process with an unhandled exception.
    AppSettings cfg;
    try
    {
        cfg = load_app_settings(cfg_path);
    }
    catch (const std::exception &e)
    {
        QMessageBox::critical(
            nullptr, QObject::tr("Configuration error"),
            QObject::tr("Could not load the application configuration:\n\n%1\n\n"
                        "Path: %2\n\n"
                        "Run the application from the project root, or pass the path "
                        "to a valid configuration file as the first argument.")
                .arg(QString::fromUtf8(e.what()), QString::fromStdString(cfg_path)));
        return 1;
    }

    MainWindow window(cfg);
    window.show();

    return app.exec();
}
