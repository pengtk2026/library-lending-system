// main.cpp -- application entry point and global exception boundary
#include "database.h"
#include "service.h"
#include "ui.h"

#include <FL/fl_ask.H>

#include <exception>
#include <filesystem>
#include <string>

namespace {

int runApplication(int argc, char** argv) {
    using namespace lls;

    Fl::scheme("gtk+");
    // Use a font with CJK glyphs so all Chinese labels/database text render.
    Fl::set_font(FL_HELVETICA, "Microsoft YaHei");
    Fl::set_font(FL_HELVETICA_BOLD, "Microsoft YaHei");
    Fl::set_font(FL_HELVETICA_ITALIC, "Microsoft YaHei");
    Fl::set_font(FL_HELVETICA_BOLD_ITALIC, "Microsoft YaHei");

    std::filesystem::create_directories("data");
    Database database("data/library.db");
    LibraryService service(database);
    service.initialize();

    MainWindow window(1024, 680, "简易图书借阅系统 · C++17 / FLTK / SQLite", service);
    window.show(argc, argv);
    return Fl::run();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return runApplication(argc, argv);
    } catch (const std::exception& error) {
        fl_alert("程序启动失败：\n%s", error.what());
        return 1;
    } catch (...) {
        fl_alert("程序启动失败：未知异常");
        return 1;
    }
}

#if defined(_WIN32)
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
