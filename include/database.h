/**
 * @file database.h
 * @brief 数据访问层基础设施声明：SQLite 连接、预编译语句、事务与模式初始化。
 *
 * @section design 设计说明
 * - 所属层次：数据访问层的基础设施，向下封装 sqlite3 C API，向上为四个 DAO 提供类型；
 *   实现见 `src/database.cpp`。本头文件**不包含 sqlite3.h**，只用不透明结构体前向声明，
 *   避免把 696 KB 的 SQLite 头文件传播给 UI/Service 层，缩短编译时间并保持封装。
 * - RAII 资源管理（核心设计）：
 *   1) `Database` 构造/析构管理 `sqlite3*` 连接，禁止拷贝，独占所有权；
 *   2) `Statement` 构造/析构管理 `sqlite3_stmt*`，支持移动语义，
 *      使 `Database::prepare()` 可以按值返回语句对象；
 *   3) `Transaction` 构造即 `BEGIN IMMEDIATE`，未显式 `commit()` 时析构自动 `ROLLBACK`，
 *      保证异常栈展开时「借阅记录 + 图书状态」一起回滚。
 * - SQL 安全：所有带参 SQL 通过 `bindInt/bindText/bindNull` 绑定占位符，杜绝拼接注入；
 *   `bindText` 使用 SQLITE_TRANSIENT 让 SQLite 自行复制字符串。
 * - 连接级配置：打开后固定执行 `foreign_keys=ON`、`busy_timeout=5000`、
 *   `journal_mode=WAL`，由 `DbInitializer::createSchema()` 保证表结构幂等存在。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#pragma once

#include <string>   // std::string：SQL 文本、路径与错误信息

struct sqlite3;       ///< SQLite 连接句柄（不透明类型，定义藏在 sqlite3.c）
struct sqlite3_stmt;  ///< SQLite 预编译语句句柄（真实类型为 Vdbe）

namespace lls {

class Database;       ///< 前置声明：Transaction 需要持有其引用

/**
 * @brief 预编译语句的 RAII 封装，支持移动、禁止拷贝。
 * @note 设计要点：保存 SQL 原文到 `sql_`，任何一步失败时异常信息都附带完整 SQL，
 *       便于课程演示中直接定位问题；`reset()` 使批量插入可以复用同一编译结果。
 */
class Statement {
    sqlite3_stmt* stmt_ = nullptr;  ///< SQLite 语句句柄，析构时 finalize
    std::string sql_;               ///< 原始 SQL 文本，用于错误信息与诊断

public:
    /**
     * @brief 预编译一条 SQL。
     * @param db 已打开的连接句柄。
     * @param sql SQL 文本，可含 ? 占位符。
     * @exception DbException 数据库未打开或 SQL 编译失败。
     */
    Statement(sqlite3* db, const std::string& sql);

    /** @brief 析构：sqlite3_finalize 释放语句句柄。 */
    ~Statement();

    /** @brief 禁止拷贝（独占句柄所有权）。 */
    Statement(const Statement&) = delete;
    /** @brief 禁止拷贝赋值。 */
    Statement& operator=(const Statement&) = delete;

    /**
     * @brief 移动构造：接管源对象句柄并把源置空。
     * @param other 源语句对象。
     */
    Statement(Statement&& other) noexcept;
    /**
     * @brief 移动赋值：先释放自身资源再接管源资源。
     * @param other 源语句对象。
     * @return 自身引用。
     */
    Statement& operator=(Statement&& other) noexcept;

    /**
     * @brief 绑定整型参数。
     * @param index 占位符下标，从 1 开始（SQLite 约定）。
     * @param value 整数值。
     * @return 自身引用，支持链式 bind。
     */
    Statement& bindInt(int index, int value);
    /**
     * @brief 绑定 UTF-8 文本参数。
     * @param index 占位符下标，从 1 开始。
     * @param value 字符串（中文按 UTF-8 传入，SQLite 会复制）。
     * @return 自身引用。
     */
    Statement& bindText(int index, const std::string& value);
    /**
     * @brief 绑定 SQL NULL（用于「尚未归还」的 return_date 列）。
     * @param index 占位符下标，从 1 开始。
     * @return 自身引用。
     */
    Statement& bindNull(int index);

    /**
     * @brief 推进一步。
     * @return true 表示取到一行（SQLITE_ROW）；false 表示执行完毕（SQLITE_DONE）。
     * @exception DbException 约束冲突、锁超时等其它状态码。
     */
    bool step();
    /** @brief 执行无结果集语句，循环 step 直到 DONE。 */
    void execute();

    /**
     * @brief 读取当前行整型列。
     * @param column 列下标，从 0 开始（与绑定的 1 起始不同）。
     * @return 列值；SQL NULL 被 SQLite 转为 0。
     */
    int columnInt(int column) const;
    /**
     * @brief 读取当前行文本列。
     * @param column 列下标，从 0 开始。
     * @return UTF-8 字符串；SQL NULL 返回空串。
     */
    std::string columnText(int column) const;
    /**
     * @brief 判断当前行列是否为 SQL NULL。
     * @param column 列下标，从 0 开始。
     * @return true 表示 NULL；DAO 据此决定是否读取可空日期。
     */
    bool columnIsNull(int column) const;

    /** @brief 复位语句（sqlite3_reset + clear_bindings），供批量插入复用。 */
    void reset();

    /** @brief 取底层句柄。 @return sqlite3_stmt*，仅供诊断使用。 */
    sqlite3_stmt* raw() const noexcept { return stmt_; }
    /** @brief 取原始 SQL 文本。 @return SQL 常量引用。 */
    const std::string& sql() const noexcept { return sql_; }
};

/**
 * @brief RAII 事务：构造开启、显式提交、异常自动回滚。
 * @note 算法：构造执行 `BEGIN IMMEDIATE`（提前取写锁）；
 *       `commit()` 执行 COMMIT 并置 `committed_=true`；析构时若仍为 false 则 ROLLBACK，
 *       且吞掉回滚异常以遵守「析构不抛异常」原则。
 */
class Transaction {
    Database& db_;                ///< 数据库连接引用（不持有所有权）
    bool committed_ = false;      ///< 是否已提交，决定析构是否回滚

public:
    /** @brief 开启事务。 @param db 已打开的连接。 */
    explicit Transaction(Database& db);
    /** @brief 析构：未提交则自动 ROLLBACK。 */
    ~Transaction();

    /** @brief 禁止拷贝（事务不可复制）。 */
    Transaction(const Transaction&) = delete;
    /** @brief 禁止拷贝赋值。 */
    Transaction& operator=(const Transaction&) = delete;

    /** @brief 提交事务，之后析构不再回滚。 */
    void commit();
};

/**
 * @brief SQLite 连接封装；独占句柄、禁止拷贝、析构自动关闭。
 * @note 所有 DAO 共享同一个 Database 引用，因此事务对全部 DAO 生效。
 */
class Database {
    sqlite3* db_ = nullptr;       ///< 连接句柄；nullptr 表示未打开
    std::string path_;            ///< 数据库文件路径，供诊断

public:
    /** @brief 默认构造：不打开连接，需随后调用 open()。 */
    Database() = default;
    /**
     * @brief 构造并打开指定数据库。
     * @param path 文件路径，如 "data/library.db"。
     */
    explicit Database(const std::string& path);
    /** @brief 析构：关闭连接释放文件句柄与 WAL 资源。 */
    ~Database();

    /** @brief 禁止拷贝（独占连接）。 */
    Database(const Database&) = delete;
    /** @brief 禁止拷贝赋值。 */
    Database& operator=(const Database&) = delete;

    /**
     * @brief 打开或创建数据库并完成连接级配置。
     * @param path 文件路径；父目录不存在时自动创建。
     * @exception DbException 打开失败。
     * @note 依次执行 PRAGMA foreign_keys / busy_timeout / journal_mode。
     */
    void open(const std::string& path);
    /** @brief 幂等关闭连接并置空句柄。 */
    void close() noexcept;
    /** @brief 判断连接是否有效。 @return true 表示已打开。 */
    bool isOpen() const noexcept { return db_ != nullptr; }
    /** @brief 取底层句柄。 @return sqlite3*，供 Statement / Transaction 使用。 */
    sqlite3* handle() const noexcept { return db_; }
    /** @brief 取数据库路径。 @return 路径常量引用。 */
    const std::string& path() const noexcept { return path_; }

    /**
     * @brief 执行无参数 SQL（可含分号分隔的多条语句，如 DDL 脚本）。
     * @param sql SQL 文本。
     * @exception DbException 执行失败，附带 SQLite 错误与原始 SQL。
     */
    void exec(const std::string& sql) const;
    /**
     * @brief 预编译参数化 SQL。
     * @param sql 含 ? 占位符的 SQL。
     * @return 可移动的 Statement 对象。
     */
    Statement prepare(const std::string& sql) const;

    /** @brief 最近一次 INSERT 的自增主键。 @return rowid；未打开返回 0。 */
    long long lastInsertId() const noexcept;
    /** @brief 最近一条 SQL 影响的行数。 @return 行数；DAO 用它判断删除是否命中。 */
    int changes() const noexcept;
    /** @brief 最近一次错误文本。 @return sqlite3_errmsg 或「数据库未打开」。 */
    std::string lastError() const;
};

/**
 * @brief 数据库模式初始化器（全静态工具类）。
 * @note 所有 DDL 使用 IF NOT EXISTS，重复调用不破坏数据；
 *       `seedDemoData()` 仅在空库时插入 3 书架 / 8 图书 / 4 学生 / 2 借阅的基础演示数据。
 */
class DbInitializer {
public:
    /** @brief 幂等创建 4 张表、7 个索引与 2 个视图。 @param db 已打开的连接。 */
    static void createSchema(Database& db);
    /** @brief 空库时写入基础演示数据（单事务）。 @param db 已打开的连接。 */
    static void seedDemoData(Database& db);
};

}  // namespace lls
