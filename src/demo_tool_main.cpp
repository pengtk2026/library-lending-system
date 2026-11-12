/**
 * @file demo_tool_main.cpp
 * @brief 演示数据生成工具的控制台入口（构建目标 DemoDataTool.exe）。
 *
 * @par 设计说明
 * - 定位：与 GUI 程序并列的可执行目标，不链接 FLTK，只链接 lls_core 静态库；
 *   由 generate_demo.bat 调用，也可在命令行直接运行。
 * - 职责：准备 data/ 目录 → 打开数据库 → 幂等建表 → 调用 DemoDataGenerator 重建
 *   1000/100/500 数据 → 生成一致性校验报告 → 同时输出到控制台与
 *   data/demo_data_report.txt → 以退出码反映结果。
 * - 退出码约定：0 = 生成且全部校验通过；1 = 生成过程抛异常；2 = 生成完成但一致性校验有 FAIL。
 * - 编码处理：Windows 控制台默认代码页为 GBK，直接输出 UTF-8 中文会乱码；
 *   故在 main 开头调用 SetConsoleOutputCP(CP_UTF8)，与批处理中的 `chcp 65001` 配合。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "database.h"    // Database / DbInitializer：连接与建表
#include "demo_data.h"   // DemoDataGenerator / DemoDataReport：生成与校验
#include "domain.h"      // 领域层类型与日期工具（报告中的 today()）

#include <filesystem>    // std::filesystem::create_directories：准备 data/
#include <fstream>       // std::ofstream：写出报告文件
#include <iostream>      // std::cout / std::cerr：控制台输出
#include <string>        // std::string：报告文本

#if defined(_WIN32)
#include <windows.h>     // SetConsoleOutputCP / CP_UTF8：控制台 UTF-8 支持
#endif

/*
 * @brief 控制台工具入口。
 * @return 0 成功；1 生成异常；2 一致性校验未通过。
 * @note 主要算法：
 *       1) 设置 UTF-8 控制台代码页；
 *       2) 组装 Database 并建表（幂等，可在已有大演示库上重复执行）；
 *       3) generate() 在单事务内清空并重建四类数据；
 *       4) detail() + verify() 拼成完整报告，落盘并回显；
 *       5) 通过检查报告中是否含 "FAIL" 决定退出码，便于批处理判断是否成功。
 *       所有异常在此统一捕获，错误信息同时写入报告文件，方便无人值守执行时排查。
 */
int main() {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);                             ///< 让控制台正确显示 UTF-8 中文
#endif
    try {
        using namespace lls;
        std::filesystem::create_directories("data");         ///< 确保数据目录存在
        Database database("data/library.db");                ///< 栈对象，析构自动关闭连接
        DbInitializer::createSchema(database);               ///< 幂等建表/索引/视图

        const DemoDataReport report =
            DemoDataGenerator::generate(database, 1000, 100, 500);  ///< 目标规模：书/生/借阅
        const std::string text =
            report.detail() + DemoDataGenerator::verify(database);    ///< 规模 + 校验报告

        std::ofstream output("data/demo_data_report.txt",     ///< 报告文件（覆盖写）
                             std::ios::trunc);
        if (output) output << text;                          ///< 落盘供 generate_demo.bat 显示

        std::cout << text << std::endl;                      ///< 同时回显到控制台
        return text.find("FAIL") == std::string::npos ? 0 : 2;  ///< 含 FAIL 则返回 2
    } catch (const std::exception& error) {
        const std::string message = std::string("生成失败：") + error.what();  ///< 错误说明
        std::cerr << message << std::endl;                   ///< 控制台错误输出
        std::ofstream("data/demo_data_report.txt", std::ios::trunc)
            << message << std::endl;                         ///< 同时落盘，便于排查
        return 1;
    }
}
