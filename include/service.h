/**
 * @file service.h
 * @brief 业务逻辑层门面声明：统计结构、业务规则校验、事务编排与组合查询接口。
 *
 * @par 设计说明
 * - 所属层次：业务逻辑层（Service），是界面层唯一允许调用的下层门面；
 *   实现见 `src/service.cpp`。界面层不得直接访问 DAO，更不能出现 SQL。
 * - 依赖注入：`LibraryService` 构造时接收 `Database&`，并把同一连接注入四个 DAO，
 *   从而保证事务（BEGIN/COMMIT）对所有 DAO 生效；Service 不接管连接生命周期。
 * - 职责分工：领域对象负责不变式与状态迁移规则，DAO 负责取数写库，
 *   本层负责跨对象流程编排、事务边界与面向用户的中文业务文案。
 * - 事务边界（最关键）：`borrowBook()` 与 `returnBook()` 必须在同一事务内
 *   同时更新 borrow_records 与 books.status；异常时由 RAII Transaction 自动回滚。
 * - 异常约定：本层抛 ValidationException / NotFoundException / BusinessException，
 *   UI 回调统一捕获并转成弹窗与状态栏提示，异常绝不穿透 FLTK 回调。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#pragma once

#include "daos.h"    // 四个 DAO 与 LoanDetail / ShelfStat / LoanFilter

#include <string>    // std::string：统计条文本与检索关键字
#include <vector>    // std::vector：列表返回值

namespace lls {

/**
 * @brief 主窗口顶部统计条的 6 项计数。
 * @note 由 `LibraryService::statistics()` 用一条 SQL 的 6 个标量子查询一次取回。
 */
struct Statistics {
    int totalBooks = 0;       ///< 藏书总册数
    int availableBooks = 0;   ///< 在馆（status=0）册数
    int borrowedBooks = 0;    ///< 借出（status=1）册数
    int overdueLoans = 0;     ///< 逾期未还记录数
    int totalStudents = 0;    ///< 学生总数
    int totalShelves = 0;     ///< 书架总数

    /**
     * @brief 格式化为统计条文本。
     * @return 形如「藏书 1000 册 在馆 800 借出 200 逾期 40 学生 100 书架 20」。
     */
    std::string summary() const;
};

/**
 * @brief 业务门面：界面层与数据层之间的唯一通道。
 * @note 所有写操作遵循「先校验、后事务、再刷新」；所有删除都先做业务预检，
 *       把数据库外键 RESTRICT 错误转换为可读的中文提示。
 */
class LibraryService {
    Database& db_;                    ///< 共享连接引用：事务与统计查询使用
    BookDao bookDao_;                 ///< 图书 DAO
    StudentDao studentDao_;           ///< 学生 DAO
    BookshelfDao shelfDao_;           ///< 书架 DAO
    BorrowRecordDao loanDao_;         ///< 借阅记录 DAO

    /**
     * @brief 通用实体校验入口（多态落点）。
     * @param entity 任意实体基类引用。
     * @exception ValidationException 实体 validate() 返回非空错误文案。
     * @note 通过 `const Entity&` 动态调用派生类的 validate()，体现运行时多态。
     */
    static void checkEntity(const Entity& entity);

public:
    /**
     * @brief 构造并注入数据库连接。
     * @param db 已打开的连接；Service 不接管其所有权。
     */
    explicit LibraryService(Database& db);

    /**
     * @brief 初始化：幂等建表 → 空库播种 → 批量刷新逾期。
     * @note 顺序不可颠倒：先建表才能查询，最后刷新以正确标记演示数据中的逾期记录。
     */
    void initialize();

    // ---------------- 图书业务 ----------------

    /**
     * @brief 新增图书（校验 → 书架外键预检 → 补入库日 → 插入）。
     * @param book 待新增实体。
     * @return 新记录主键。
     */
    int addBook(const Book& book);
    /**
     * @brief 更新图书（校验 → 确认原记录与新书架存在 → 更新）。
     * @param book 含有效 id 的实体。
     */
    void updateBook(const Book& book);
    /**
     * @brief 删除图书；存在未归还记录时拒绝。
     * @param id 图书主键。
     * @exception BusinessException 被借阅记录引用时抛出。
     */
    void removeBook(int id);
    /**
     * @brief 列出图书，可按关键字检索。
     * @param keyword 题名/作者/ISBN 关键字，空串表示全部。
     * @return 图书列表。
     * @note 空关键字直接走 listAll()，避免 `% %` 形式的全表 LIKE。
     */
    std::vector<Book> listBooks(const std::string& keyword = "");
    /** @brief 列出全部在馆图书，供借书页下拉框使用。 */
    std::vector<Book> availableBooks();

    // ---------------- 学生业务 ----------------

    /**
     * @brief 新增学生（校验 → 学号唯一性预检 → 插入）。
     * @param student 待新增实体。
     * @return 新主键。
     */
    int addStudent(const Student& student);
    /**
     * @brief 更新学生；唯一性检查排除自身 ID。
     * @param student 含有效 id 的实体。
     */
    void updateStudent(const Student& student);
    /**
     * @brief 删除学生；尚有未归还图书时拒绝。
     * @param id 学生主键。
     */
    void removeStudent(int id);
    /** @brief 列出全部学生。 */
    std::vector<Student> listStudents();
    /**
     * @brief 查询某学生当前已借册数（界面「已借册数」列数据源）。
     * @param studentId 学生主键。
     * @return 未归还记录数。
     */
    int activeLoanCount(int studentId);

    // ---------------- 书架业务 ----------------

    /**
     * @brief 新增书架（校验 → 编号唯一性预检 → 插入）。
     * @param shelf 待新增实体。
     * @return 新主键。
     */
    int addShelf(const Bookshelf& shelf);
    /**
     * @brief 更新书架；唯一性检查排除自身 ID。
     * @param shelf 含有效 id 的实体。
     */
    void updateShelf(const Bookshelf& shelf);
    /**
     * @brief 删除书架；书架上仍有藏书时拒绝。
     * @param id 书架主键。
     */
    void removeShelf(int id);
    /** @brief 列出全部书架，供图书对话框的下拉选项使用。 */
    std::vector<Bookshelf> listShelves();
    /** @brief 读取书架统计（藏书数/在馆数），供书架管理页展示。 */
    std::vector<ShelfStat> shelfStats();

    // ---------------- 借阅业务（核心） ----------------

    /**
     * @brief 办理借书。
     * @param studentId 学生主键。
     * @param bookId 图书主键。
     * @return 新借阅记录主键。
     * @note 主要算法（校验链 + 事务）：
     *       ① 学生须启用；② 无逾期未还（直接比 due_date）；③ 当前借阅数未达上限；
     *       ④ 图书在馆；⑤ 构造单据（借期 30 天）并多态校验；
     *       ⑥ 事务内「插入借阅记录 + 更新 books.status=1」→ 提交；异常自动回滚。
     */
    int borrowBook(int studentId, int bookId);
    /**
     * @brief 办理还书。
     * @param recordId 借阅记录主键。
     * @note 事务内「更新记录为已归还 + 恢复图书在馆」；拒绝重复归还，异常自动回滚。
     */
    void returnBook(int recordId);
    /**
     * @brief 按筛选条件查询借阅明细。
     * @param filter 全部 / 当前借阅 / 逾期。
     * @return LoanDetail 列表（JOIN 已包含展示字段）。
     */
    std::vector<LoanDetail> listLoans(LoanFilter filter);

    // ---------------- 统计与维护 ----------------

    /**
     * @brief 汇总统计条 6 项数字。
     * @return Statistics 结构。
     * @note 一条 SQL 的 6 个标量子查询一次取回；逾期数按 due_date 实时计算。
     */
    Statistics statistics();
    /**
     * @brief 批量把到期未还记录标记为逾期。
     * @return 本次被标记行数，供状态栏提示；DAO 的 UPDATE 幂等可重复执行。
     */
    int refreshOverdue();
};

}  // namespace lls
