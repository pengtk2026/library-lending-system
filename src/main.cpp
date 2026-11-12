/**
 * @file main.cpp
 * @brief 程序入口：GUI 启动流程、无界面演示数据生成模式、全局异常兜底与 Win32 入口包装。
 *
 * @par 设计说明
 * - 所属层次：应用层（Application 组装根），负责把四层对象按依赖顺序组装起来：
 *   Database（栈对象，RAII 管理连接）→ LibraryService（引用注入）→ MainWindow（引用注入）
 *   → Fl::run() 进入事件循环。窗口关闭后按构造逆序析构，连接自动释放。
 * - 两种运行模式：
 *   1) 无参数：启动 GUI 桌面程序；
 *   2) `--generate-demo`：不创建窗口，重建 1000/100/500 演示数据并写报告文件，
 *      供 generate_demo.bat 调用；该模式与 DemoDataTool.exe 功能等价。
 * - 中文字体算法：界面层所有文本为 UTF-8，但 FLTK 默认字体不含完整中文字形；
 *   因此在任何窗口创建之前，把 FL_HELVETICA 的四个变体统一映射到「Microsoft YaHei」，
 *   使标题、按钮、列表与数据库中文内容都能正确渲染。
 * - 异常边界：main 捕获 std::exception 与未知异常，用 fl_alert 弹出可读提示后返回非零，
 *   避免异常穿透到 Windows 入口导致无提示的进程崩溃。
 * - Windows 子系统：CMake 以 WIN32 目标生成无控制台窗口的 GUI 程序，
 *   链接器入口为 WinMain；此处提供 WinMain 包装，转调 C 标准入口 main(__argc, __argv)，
 *   使同一份代码既能用标准 main 调试，也能作为纯 GUI 程序发布。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "database.h"    // Database / DbInitializer：数据库连接与建表
#include "demo_data.h"   // DemoDataGenerator / DemoDataReport：无界面演示数据生成
#include "service.h"     // LibraryService：业务门面
#include "ui.h"          // MainWindow 与 FLTK 头文件

#include <FL/fl_ask.H>   // fl_alert：启动失败的图形化提示

#include <exception>     // std::exception：全局异常捕获
#include <filesystem>    // std::filesystem::create_directories：准备 data/ 目录
#include <fstream>       // std::ofstream：写出演示数据报告
#include <string>        // std::string：命令行参数比较

namespace {  // 匿名命名空间：仅本文件使用的启动函数

/*
 * @brief 组装对象并运行 GUI 事件循环。
 * @param argc, argv 标准命令行参数，传递给 Fl_Window::show()。
 * @return Fl::run() 的退出码（所有窗口关闭后返回 0）。
 * @note 主要算法（依赖组装顺序）：
 *       1) 设置外观 scheme 与中文字体（必须在控件创建之前）；
 *       2) 创建 data/ 目录，打开 SQLite 数据库；
 *       3) 构造 Service 并 initialize()（幂等建表 + 空库播种 + 逾期刷新）；
 *       4) 构造 MainWindow（构造函数内完成首屏刷新）；
 *       5) show() 后进入 Fl::run() 事件循环，直到用户关闭窗口。
 */
int runApplication(int argc, char** argv) {
    using namespace lls;

    Fl::scheme("gtk+");                                      ///< 统一控件外观风格
    Fl::set_font(FL_HELVETICA, "Microsoft YaHei");          ///< 常规字体 → 微软雅黑（含中文字形）
    Fl::set_font(FL_HELVETICA_BOLD, "Microsoft YaHei");     ///< 粗体变体
    Fl::set_font(FL_HELVETICA_ITALIC, "Microsoft YaHei");   ///< 斜体变体
    Fl::set_font(FL_HELVETICA_BOLD_ITALIC, "Microsoft YaHei");  ///< 粗斜体变体

    std::filesystem::create_directories("data");             ///< 确保数据库目录存在
    Database database("data/library.db");                    ///< 栈对象：析构自动关闭连接
    LibraryService service(database);                        ///< 同一连接注入四个 DAO
    service.initialize();                                    ///< 建表 / 播种 / 逾期刷新

    MainWindow window(1024, 680,                            ///< 主窗口尺寸
                      "简易图书借阅系统 · C++17 / FLTK / SQLite",  ///< 窗口标题（UTF-8）
                      service);                              ///< 注入业务门面
    window.show(argc, argv);                                 ///< 显示窗口
    return Fl::run();                                        ///< 进入事件循环
}

}  // namespace 匿名命名空间结束

/*
 * @brief C/C++ 标准入口。
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @return 0 成功；1 启动或生成失败。
 * @note 算法：先线性扫描参数，命中 `--generate-demo` 则走无界面分支并立即返回；
 *       否则进入 GUI 分支。GUI 分支外层 try/catch 是最后一道异常防线。
 */
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {                         ///< 从 1 开始跳过程序名
        if (std::string(argv[i]) == "--generate-demo") {     ///< 无界面演示数据模式
            try {
                using namespace lls;
                std::filesystem::create_directories("data"); ///< 准备目录
                Database database("data/library.db");        ///< 打开/创建数据库
                DbInitializer::createSchema(database);       ///< 幂等建表
                const DemoDataReport report =
                    DemoDataGenerator::generate(database, 1000, 100, 500);  ///< 单事务重建数据
                std::ofstream("data/demo_data_report.txt", std::ios::trunc)
                    << report.detail()                      ///< 规模统计
                    << DemoDataGenerator::verify(database); ///< 一致性校验结果
                return 0;                                    ///< 成功（报告写入文件）
            } catch (const std::exception& error) {
                std::ofstream("data/demo_data_report.txt", std::ios::trunc)
                    << "生成失败：" << error.what() << std::endl;  ///< 失败原因落盘
                return 1;
            }
        }
    }
    try {
        return runApplication(argc, argv);                   ///< 正常 GUI 启动
    } catch (const std::exception& error) {
        fl_alert("程序启动失败：\n%s", error.what());        ///< 图形化错误提示
        return 1;
    } catch (...) {
        fl_alert("程序启动失败：未知异常");                   ///< 兜底
        return 1;
    }
}

#if defined(_WIN32)
#include <windows.h>                                         ///< WinMain / HINSTANCE / __argc
/*
 * @brief Windows GUI 子系统入口，转调标准 main。
 * @return main() 的退出码。
 * @note CMake 的 WIN32 可执行目标不弹控制台，链接器要求 WinMain 入口；
 *       CRT 在启动时已把命令行解析到 __argc/__argv，故此处可直接转发，
 *       从而让 `--generate-demo` 等参数在 GUI 子系统下同样可用。
 */
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
