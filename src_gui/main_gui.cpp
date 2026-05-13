#include <pybind11/embed.h>
#include <QApplication>
#include "MainWindow.h"
#include <csignal>
#include <QStyleFactory>

using namespace grip;

int main(int argc, char **argv)
{
    const std::string cfg_path = (argc > 1) ? argv[1] : "configs/config.yaml";
    AppSettings cfg = load_app_settings(cfg_path);

    pybind11::scoped_interpreter guard{};
    pybind11::gil_scoped_release release;

    QApplication app(argc, argv);

    QApplication::setStyle(QStyleFactory::create("Fusion"));

    // Optional: set a fixed font to reduce layout differences
    QFont f("Segoe UI");          // or "Noto Sans"
    f.setPointSize(10);
    app.setFont(f);

    MainWindow window(cfg);
    window.show();

    return app.exec();

    // std::signal(SIGINT, +[](int){});
#if defined(__linux__)
    pause();
#else
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(1));
#endif

    // ctrl->stop_all();
    return 0;
}
