/**
 * @file daos.h
 * @brief 数据访问对象（DAO）声明与两个只读展示结构体。
 *
 * @section design 设计说明
 * - 所属层次：数据访问层，是**唯一允许出现 SQL**的层；实现见 `src/daos.cpp`。
 *   界面层与业务层只通过这里声明的接口取数，不接触 sqlite3 C API。
 * - 继承与复用：`Dao<T>` 是模板基类，统一 insert / update / removeById / findById / listAll 契约，
 *   并提供 `requirePersisted()` 公共守卫；四个派生 DAO 各自实现具体 SQL 与行映射。
 * - 列序契约：每个 DAO 的查询都以「固定列清单 + WHERE/ORDER BY」拼 SQL，
 *   再由 `mapXxx(Statement&)` 按列下标还原实体；修改 SELECT 列表必须同步修改映射函数。
 * - 展示结构：`LoanDetail`（借阅明细 = 领域对象 + JOIN 出的学生/图书/书架字段）与
 *   `ShelfStat`（书架 + 藏书数/在馆数）专为界面设计，避免界面层发起 N+1 查询。
 * - 异常约定：findById 无结果抛 NotFoundException；底层 SQL 错误抛 DbException；
 *   DAO 不吞异常，由 Service / UI 统一处理。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#pragma once

#include "database.h"  // Database / Statement：全部 SQL 执行的基础设施
#include "domain.h"    // 实体、枚举与异常类型

#include <string>      // std::string：关键字、编号、展示文本
#include <vector>      // std::vector：查询结果容器

namespace lls {

/**
 * @brief DAO 模板基类：统一 CRUD 契约 + 可复用守卫。
 * @tparam T 该 DAO 负责的领域实体类型（Book / Student / Bookshelf / BorrowRecord）。
 * @note 采用「模板 + 虚函数」混合：公共前置校验在基类，具体 SQL 由派生类重写。
 */
template <typename T>
class Dao {
protected:
    Database& db_;    ///< 共享的数据库连接引用（由构造函数注入）

    /**
     * @brief 更新前的主键守卫。
     * @param id 待检查的主键。
     * @exception ValidationException id <= 0 时抛出，防止全表更新。
     */
    static void requirePersisted(int id) {
        if (id <= 0) throw ValidationException("记录 ID 无效");
    }

public:
    /** @brief 构造。 @param db 已打开的连接；DAO 不接管其生命周期。 */
    explicit Dao(Database& db) : db_(db) {}
    /** @brief 虚析构，保证通过基类指针正确析构派生 DAO。 */
    virtual ~Dao() = default;

    /** @brief 插入实体。 @param entity 待插入对象。 @return 新记录自增主键。 */
    virtual int insert(const T& entity) = 0;
    /** @brief 全字段更新实体。 @param entity 含有效 id 的对象。 */
    virtual void update(const T& entity) = 0;
    /** @brief 按主键删除。 @param id 主键。 @return 是否真的删除了记录。 */
    virtual bool removeById(int id) = 0;
    /** @brief 按主键查询。 @param id 主键。 @return 实体。 @exception NotFoundException 不存在时。 */
    virtual T findById(int id) const = 0;
    /** @brief 列出全部记录。 @return 按主键升序的实体容器。 */
    virtual std::vector<T> listAll() const = 0;
};

/**
 * @brief 借阅明细展示结构：领域对象 + JOIN 出的展示字段。
 * @note 数据源为视图 `v_loan_detail`，界面一次取齐所有列，无需再查学生/图书表。
 */
struct LoanDetail {
    BorrowRecord record;      ///< 借阅单据本体（含 ID、日期、状态）
    std::string studentNo;    ///< 学号（JOIN students）
    std::string studentName;  ///< 学生姓名（JOIN students）
    std::string isbn;         ///< ISBN（JOIN books）
    std::string bookTitle;    ///< 图书题名（JOIN books）
    std::string shelfCode;    ///< 书架编号（LEFT JOIN bookshelves）

    /** @brief 取记录主键。 @return record.id()。 */
    int recordId() const noexcept { return record.id(); }
    /** @brief 取状态中文文本。 @return 如「借出中」「逾期」。 */
    const char* statusText() const noexcept { return record.statusText(); }
};

/**
 * @brief 书架统计展示结构。
 * @note 数据源为视图 `v_shelf_stat`（LEFT JOIN + GROUP BY 已在 SQL 层完成聚合）。
 */
struct ShelfStat {
    Bookshelf shelf;              ///< 书架本体（含编号、位置、容量）
    int bookCount = 0;            ///< 当前藏书总数
    int availableCount = 0;       ///< 其中在馆（status=0）册数
};

/**
 * @brief books 表的数据访问对象。
 * @note 附加方法服务于检索、借书下拉、库存状态更新与删除预检。
 */
class BookDao : public Dao<Book> {
public:
    /** @brief 继承基类构造函数（注入 Database&）。 */
    using Dao<Book>::Dao;

    /** @brief 插入图书。 @return 新主键。 */
    int insert(const Book& book) override;
    /** @brief 全字段更新图书。 */
    void update(const Book& book) override;
    /** @brief 删除图书。 @return 是否命中。 */
    bool removeById(int id) override;
    /** @brief 按 ID 查图书。 @return Book 实体。 */
    Book findById(int id) const override;
    /** @brief 列出全部图书。 @return 按 ID 升序的容器。 */
    std::vector<Book> listAll() const override;

    /**
     * @brief 按题名 / 作者 / ISBN 模糊检索。
     * @param keyword 关键字（内部包成 %kw% 绑定三次）。
     * @return 命中图书列表。
     */
    std::vector<Book> search(const std::string& keyword) const;
    /** @brief 仅列出在馆（status=0）图书，供借书下拉框使用。 */
    std::vector<Book> availableList() const;
    /**
     * @brief 仅更新图书状态。
     * @param id 图书主键。
     * @param status 目标状态。
     * @note 必须与借阅记录的插入/更新处于同一事务。
     */
    void updateStatus(int id, BookStatus status);
    /** @brief 统计某书架藏书数（删除预检）。 @return 册数。 */
    int countByShelf(int shelfId) const;
};

/**
 * @brief students 表的数据访问对象。
 * @note `existsNo` 用「排除自身 ID」的方式让新增与编辑共用同一唯一性检查。
 */
class StudentDao : public Dao<Student> {
public:
    /** @brief 继承基类构造函数。 */
    using Dao<Student>::Dao;

    /** @brief 插入学生。 @return 新主键。 */
    int insert(const Student& student) override;
    /** @brief 全字段更新学生。 */
    void update(const Student& student) override;
    /** @brief 删除学生。 @return 是否命中。 */
    bool removeById(int id) override;
    /** @brief 按 ID 查学生。 @return Student 实体。 */
    Student findById(int id) const override;
    /** @brief 列出全部学生。 @return 按 ID 升序的容器。 */
    std::vector<Student> listAll() const override;

    /**
     * @brief 按学号精确查询。
     * @param no 学号。
     * @param out 输出参数，命中时回填实体。
     * @return 是否命中（查不到属正常分支，不抛异常）。
     */
    bool findByStudentNo(const std::string& no, Student& out) const;
    /**
     * @brief 判断学号是否被其它记录占用。
     * @param no 学号。
     * @param excludeId 编辑时排除的自身 ID（新增传 0）。
     * @return true 表示重复。
     */
    bool existsNo(const std::string& no, int excludeId) const;
    /**
     * @brief 统计学生当前未归还册数（status IN (0,2)）。
     * @param studentId 学生主键。
     * @return 未归还记录数，供业务层判断可借上限。
     */
    int countActiveLoans(int studentId) const;
};

/**
 * @brief bookshelves 表的数据访问对象。
 * @note `listWithStats()` 直接查聚合视图，避免逐架 COUNT 的 N+1 查询。
 */
class BookshelfDao : public Dao<Bookshelf> {
public:
    /** @brief 继承基类构造函数。 */
    using Dao<Bookshelf>::Dao;

    /** @brief 插入书架。 @return 新主键。 */
    int insert(const Bookshelf& shelf) override;
    /** @brief 全字段更新书架。 */
    void update(const Bookshelf& shelf) override;
    /** @brief 删除书架。 @return 是否命中。 */
    bool removeById(int id) override;
    /** @brief 按 ID 查书架。 @return Bookshelf 实体。 */
    Bookshelf findById(int id) const override;
    /** @brief 列出全部书架。 @return 按 ID 升序的容器。 */
    std::vector<Bookshelf> listAll() const override;

    /**
     * @brief 判断书架编号是否被其它记录占用。
     * @param code 编号。
     * @param excludeId 编辑时排除的自身 ID（新增传 0）。
     * @return true 表示重复。
     */
    bool existsCode(const std::string& code, int excludeId) const;
    /** @brief 统计书架藏书数（删除预检）。 @return 册数。 */
    int countBooks(int shelfId) const;
    /** @brief 读取书架统计视图。 @return 每个书架的藏书数/在馆数。 */
    std::vector<ShelfStat> listWithStats() const;
};

/**
 * @brief borrow_records 表的数据访问对象。
 * @note 关键算法：`listDetails()` 按 LoanFilter 拼接固定 WHERE 条件；
 *       `markOverdue()` 只更新 status=0 且 due_date < 今天的记录，幂等可重复执行。
 */
class BorrowRecordDao : public Dao<BorrowRecord> {
public:
    /** @brief 继承基类构造函数。 */
    using Dao<BorrowRecord>::Dao;

    /** @brief 插入借阅记录。 @return 新主键。 */
    int insert(const BorrowRecord& record) override;
    /** @brief 全字段更新借阅记录（空归还日写 SQL NULL）。 */
    void update(const BorrowRecord& record) override;
    /** @brief 删除借阅记录。 @return 是否命中。 */
    bool removeById(int id) override;
    /** @brief 按 ID 查借阅记录。 @return BorrowRecord 实体。 */
    BorrowRecord findById(int id) const override;
    /** @brief 列出全部记录。 @return 按借出日与 ID 倒序。 */
    std::vector<BorrowRecord> listAll() const override;

    /**
     * @brief 办理归还：登记归还日并置状态为已归还。
     * @param id 记录主键。
     * @param returnDate 归还日期。
     * @return 是否命中未归还记录（WHERE status<>1 实现幂等保护）。
     */
    bool close(int id, const std::string& returnDate);
    /**
     * @brief 按筛选条件查询借阅明细。
     * @param filter 全部 / 当前借阅 / 逾期。
     * @return LoanDetail 列表，按借出日与记录号倒序。
     */
    std::vector<LoanDetail> listDetails(LoanFilter filter) const;
    /** @brief 无参重载：查询全部借阅明细。 */
    std::vector<LoanDetail> listDetails() const;
    /** @brief 统计学生当前未归还册数。 @return 记录数。 */
    int countActiveByStudent(int studentId) const;
    /**
     * @brief 判断学生是否存在逾期未还记录。
     * @param studentId 学生主键。
     * @param day 判定基准日。
     * @return true 表示存在逾期。
     * @note 直接比较 due_date < day，不依赖批量逾期标记是否执行过。
     */
    bool hasOverdue(int studentId, const std::string& day) const;
    /** @brief 判断某册书是否有未归还记录（删除图书前的引用预检）。 */
    bool activeForBook(int bookId) const;
    /**
     * @brief 批量把到期未还记录标记为逾期。
     * @param day 判定基准日。
     * @return 被更新行数，供状态栏反馈。
     */
    int markOverdue(const std::string& day);
};

}  // namespace lls
