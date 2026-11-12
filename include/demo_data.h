/**
 * @file demo_data.h
 * @brief 可复现的大批量演示数据生成器声明与统计报告结构。
 *
 * @section design 设计说明
 * - 定位：系统内置功能，不是一次性外部脚本；实现见 `src/demo_data.cpp`，
 *   由 `DemoDataTool.exe` 与 `LibraryApp.exe --generate-demo` 两个入口调用。
 * - 目标规模：20 书架 / 1000 图书 / 100 学生 / 500 借阅记录
 *   （当前未还 200，其中逾期 40；历史已归还 300）。
 * - 可复现性：全部数据由确定性公式生成（不使用随机数），同一天多次运行结果一致。
 * - 原子性：四张业务表在**一个事务**内清空并按外键依赖顺序重建，失败自动回滚。
 * - 一致性：`verify()` 用 4 条反例计数 SQL 加 `PRAGMA foreign_key_check`
 *   校验库存状态、可借上限、逾期日期与外键完整性。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#pragma once

#include "database.h"  // Database：生成与校验都基于同一连接

#include <string>      // std::string：报告文本

namespace lls {

/**
 * @brief 演示数据生成后的规模统计。
 * @note 「当前未还」= borrowed，「历史已归还」= loans - borrowed，由生成算法保证恒等。
 */
struct DemoDataReport {
    int shelves = 0;      ///< 书架数（当前固定 20）
    int students = 0;     ///< 学生数（目标 100）
    int books = 0;        ///< 图书总数（目标 1000）
    int loans = 0;        ///< 借阅记录总数（目标 500）
    int available = 0;    ///< 在馆册数（目标 800）
    int borrowed = 0;     ///< 借出册数（目标 200）
    int overdue = 0;      ///< 逾期记录数（目标 40）

    /**
     * @brief 生成单行规模摘要，供控制台输出。
     * @return 如「书架 20 / 学生 100 / 图书 1000（在馆 800，借出 200）…」。
     */
    std::string summary() const;
    /**
     * @brief 生成多行详细报告，写入 `data/demo_data_report.txt`。
     * @return 含生成时间、各类数量与未还/已还拆分的文本。
     */
    std::string detail() const;
};

/**
 * @brief 演示数据生成器（全静态工具类）。
 * @note 主要算法：先规划 200 个互不重复的「当前借出」图书 ID 并置位图；
 *       再用「轮转 + 可借上限检查」分配学生；历史记录只从剩余图书中选取，
 *       避免与部分唯一索引 `uq_active_loan` 冲突；批量插入复用 Statement::reset()。
 */
class DemoDataGenerator {
public:
    /**
     * @brief 在单个事务内重建全部业务数据。
     * @param db 已打开的连接（内部先幂等建表）。
     * @param books 目标图书数（当前调用传 1000）。
     * @param students 目标学生数（当前调用传 100）。
     * @param loans 目标借阅记录数（当前调用传 500）。
     * @return 生成后的统计报告。
     * @exception ValidationException 规模参数过小。
     * @exception BusinessException 没有可用于历史借阅的图书。
     */
    static DemoDataReport generate(Database& db, int books, int students, int loans);

    /**
     * @brief 生成一致性校验报告。
     * @param db 已生成数据的连接。
     * @return 可读的 PASS/FAIL 文本（5 项检查）。
     * @note 工具通过检查返回文本中是否含 "FAIL" 决定退出码 0 或 2。
     */
    static std::string verify(Database& db);
};

}  // namespace lls
