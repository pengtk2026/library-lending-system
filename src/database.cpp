/**
 * @file database.cpp
 * @brief 数据访问层实现：SQLite C API 的 RAII 封装、预编译语句、事务、建表与小规模演示数据。
 *
 * @par 设计说明
 * - 所属层次：数据访问层（DAO 基础设施），向下封装 sqlite3 C API，向上为四个 DAO 提供
 *   Database / Statement / Transaction 三个 RAII 类型，禁止业务层直接接触 sqlite3*。
 * - 对应声明：include/database.h；字段与 DDL 全文另见《详细设计说明书》第 4 章。
 * - 资源管理（核心算法）：
 *   1) Database 构造/析构 → sqlite3_open_v2 / sqlite3_close，独占句柄、禁止拷贝；
 *   2) Statement 构造/析构 → sqlite3_prepare_v2 / sqlite3_finalize，支持移动语义，
 *      因此 Database::prepare() 可以按值返回语句对象；
 *   3) Transaction 构造即 BEGIN IMMEDIATE，显式 commit() 提交，
 *      若因异常栈展开导致析构且尚未提交，则析构函数自动 ROLLBACK（且吞掉异常，
 *      遵守「析构函数不得抛异常」原则）。
 * - SQL 安全：所有带参数的写操作一律使用「预编译 + 参数绑定」，
 *   bindText 采用 SQLITE_TRANSIENT，让 SQLite 自行复制字符串，避免悬垂指针与注入。
 * - 并发与完整性：连接建立后开启外键、设置 busy_timeout、启用 WAL 日志模式；
 *   DDL 中的部分唯一索引 uq_active_loan 从数据库层保证「同一册书同时至多一条未归还记录」。
 * - 幂等性：createSchema() 全部使用 IF NOT EXISTS，重复启动不会破坏既有数据；
 *   seedDemoData() 仅在 bookshelves 为空时插入基础演示数据。
 *
 * @note 大批量演示数据（1000/100/500）由 DemoDataGenerator 负责，不在本文件。
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "database.h"

#include "domain.h"      // 复用异常类型 AppException 家族与日期约定

#include <sqlite3.h>     // SQLite C API：prepare/bind/step/finalize/exec

#include <filesystem>    // std::filesystem：自动创建 data/ 目录
#include <string>        // std::string：SQL 文本与错误信息

namespace lls {

// ==================== Statement（预编译语句 RAII 封装） ====================

/*
 * @brief 预编译一条 SQL 语句。
 * @param db 已打开的 sqlite3 连接句柄。
 * @param sql 待编译的 SQL 文本（可含 ? 占位符）。
 * @exception DbException 数据库未打开或 SQL 语法/表结构错误时抛出。
 * @note 保存 SQL 原文到 sql_ 的目的：后续任何一步失败时，异常信息都能附带完整 SQL，
 *       便于课程演示中直接定位问题；sqlite3_prepare_v2 的 -1 表示让 SQLite 自动计算长度。
 */
Statement::Statement(sqlite3* db, const std::string& sql) : sql_(sql) {
    if (!db) throw DbException("数据库未打开");               ///< 空句柄保护
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1,
                                &stmt_, nullptr);            ///< 编译结果写入 stmt_
    if (rc != SQLITE_OK) {
        std::string message = sqlite3_errmsg(db);            ///< 取 SQLite 的具体错误文本
        stmt_ = nullptr;                                     ///< 失败时句柄无效，置空避免二次 finalize
        throw DbException("SQL 预编译失败：" + message + "\n" + sql);
    }
}

/* @brief 析构：释放语句句柄。finalize 失败无需处理，因为对象即将消亡。 */
Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);                      ///< 释放预编译语句
}

/*
 * @brief 移动构造：接管另一个语句对象的句柄所有权。
 * @param other 源对象，移动后其 stmt_ 置空，析构时不会重复释放。
 * @note 算法：指针转移 + 源置空（move-and-null），是 RAII 类型的标准移动语义实现，
 *       使 Database::prepare() 能按值返回 Statement 而不发生拷贝。
 */
Statement::Statement(Statement&& other) noexcept
    : stmt_(other.stmt_),                                    ///< 接管句柄
      sql_(std::move(other.sql_)) {                         ///< 接管 SQL 文本
    other.stmt_ = nullptr;                                  ///< 源对象放弃所有权
}

/*
 * @brief 移动赋值：先释放自身资源，再接管对方资源。
 * @param other 源对象。
 * @return 自身引用，支持链式赋值。
 */
Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {                                    ///< 防止自赋值导致资源被提前释放
        if (stmt_) sqlite3_finalize(stmt_);                  ///< 释放原有语句
        stmt_ = other.stmt_;                                ///< 接管新句柄
        sql_ = std::move(other.sql_);                        ///< 接管 SQL 文本
        other.stmt_ = nullptr;                               ///< 源对象置空
    }
    return *this;
}

/*
 * @brief 绑定整型参数。
 * @param index 占位符下标，从 1 开始（SQLite 约定）。
 * @param value 待绑定的整数。
 * @return 自身引用，支持 bindInt(...).bindText(...) 链式调用。
 */
Statement& Statement::bindInt(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK)
        throw DbException("参数绑定失败（int）：" + sql_);
    return *this;
}

/*
 * @brief 绑定 UTF-8 文本参数。
 * @param index 占位符下标，从 1 开始。
 * @param value 待绑定字符串（中文按 UTF-8 传入）。
 * @return 自身引用。
 * @note SQLITE_TRANSIENT 让 SQLite 立即复制一份内容，
 *       因此传入局部 std::string 的 c_str() 也不会在后续 step() 时悬垂。
 */
Statement& Statement::bindText(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), -1,
                          SQLITE_TRANSIENT) != SQLITE_OK)
        throw DbException("参数绑定失败（text）：" + sql_);
    return *this;
}

/*
 * @brief 绑定 SQL NULL。
 * @param index 占位符下标，从 1 开始。
 * @return 自身引用。
 * @note 用于「尚未归还」记录的 return_date 列，与空字符串严格区分。
 */
Statement& Statement::bindNull(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK)
        throw DbException("参数绑定失败（null）：" + sql_);
    return *this;
}

/*
 * @brief 推进一步语句执行。
 * @return true 表示取到一行结果（SQLITE_ROW）；false 表示执行完毕（SQLITE_DONE）。
 * @exception DbException 约束冲突、数据库锁、磁盘错误等。
 * @note 算法：把 sqlite3_step 的三态结果映射为「有行 / 结束 / 异常」，
 *       异常时用 sqlite3_db_handle 反查所属连接并附带 errmsg，便于定位是哪条 SQL 出错。
 */
bool Statement::step() {
    int rc = sqlite3_step(stmt_);                            ///< SQLite 状态码
    if (rc == SQLITE_ROW) return true;                       ///< 还有一行可读
    if (rc == SQLITE_DONE) return false;                     ///< 语句执行完毕
    std::string message = sqlite3_errmsg(sqlite3_db_handle(stmt_));  ///< 反查连接取错误文本
    throw DbException("SQL 执行失败：" + message + "\n" + sql_);
}

/* @brief 执行无结果集的语句（INSERT/UPDATE/DELETE/DDL），循环 step 直到 DONE。 */
void Statement::execute() {
    while (step()) {                                         ///< 忽略任何返回行，跑到结束
    }
}

/*
 * @brief 读取当前行的整型列。
 * @param column 列下标，从 0 开始（与绑定的 1 起始不同）。
 * @return 列值；NULL 会被 SQLite 转为 0。
 */
int Statement::columnInt(int column) const {
    return sqlite3_column_int(stmt_, column);
}

/*
 * @brief 读取当前行的文本列。
 * @param column 列下标，从 0 开始。
 * @return UTF-8 字符串；SQL NULL 返回空串。
 */
std::string Statement::columnText(int column) const {
    const unsigned char* value = sqlite3_column_text(stmt_, column);  ///< SQLite 内部 UTF-8 缓冲
    return value ? reinterpret_cast<const char*>(value)
                 : std::string();                           ///< NULL 统一转空串
}

/*
 * @brief 判断当前行的某列是否为 SQL NULL。
 * @param column 列下标，从 0 开始。
 * @return true 表示 NULL。
 * @note DAO 在读取可空列（如 return_date）时先调用本方法，再决定是否 columnText。
 */
bool Statement::columnIsNull(int column) const {
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

/*
 * @brief 复位语句以便复用（批量插入演示数据时显著减少 SQL 编译次数）。
 * @note 算法：sqlite3_reset 回到初始状态，sqlite3_clear_bindings 清空上次绑定，
 *       之后即可重新 bind 并执行；静态库句柄仍归本对象所有。
 */
void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ==================== Transaction（RAII 事务） ====================

/*
 * @brief 开启事务。
 * @param db 数据库连接引用（不持有所有权）。
 * @note 使用 BEGIN IMMEDIATE 而非 BEGIN DEFERRED：提前取得写锁，
 *       避免借书流程「先读后写」之间被其它连接插入，保证库存判断的有效性。
 */
Transaction::Transaction(Database& db) : db_(db) {
    db_.exec("BEGIN IMMEDIATE;");
}

/*
 * @brief 析构：未提交则自动回滚。
 * @note 主要算法（异常安全）：正常路径由 commit() 把 committed_ 置 true；
 *       若中途抛异常，栈展开触发本析构，执行 ROLLBACK 把借阅记录与图书状态一起撤销。
 *       回滚本身可能失败（例如连接已断），故用 try/catch(...) 吞掉异常，
 *       保证析构函数在栈展开期间不会再次抛异常导致 terminate。
 */
Transaction::~Transaction() {
    if (!committed_) {                                       ///< 仅在未显式提交时回滚
        try {
            db_.exec("ROLLBACK;");
        } catch (...) {
            // 析构函数不得抛异常；回滚失败也不影响上层异常的传播。
        }
    }
}

/* @brief 提交事务并标记为已完成，之后析构不再回滚。 */
void Transaction::commit() {
    db_.exec("COMMIT;");
    committed_ = true;
}

// ==================== Database（连接封装） ====================

/* @brief 便捷构造：直接打开指定路径的数据库文件。 */
Database::Database(const std::string& path) {
    open(path);
}

/* @brief 析构：关闭连接，释放 SQLite 持有的文件句柄与 WAL 资源。 */
Database::~Database() {
    close();
}

/*
 * @brief 打开（或创建）SQLite 数据库并完成连接级配置。
 * @param path 数据库文件路径，如 "data/library.db"。
 * @exception DbException 打开失败时抛出。
 * @note 算法：
 *       1) 先 close() 保证重复调用幂等；
 *       2) create_directories 自动建立父目录（用 error_code 版本避免权限问题抛异常）；
 *       3) sqlite3_open_v2 以 READWRITE|CREATE 打开；失败时及时关闭句柄防止泄漏；
 *       4) 依次设置 PRAGMA：foreign_keys=ON（外键生效）、busy_timeout=5000（锁等待）、
 *          journal_mode=WAL（读写并发，演示时 GUI 与数据工具可同时打开）。
 */
void Database::open(const std::string& path) {
    close();                                                  ///< 幂等：先释放旧连接
    std::filesystem::path file(path);                         ///< 路径对象，便于取父目录
    if (file.has_parent_path()) {
        std::error_code ec;                                   ///< 错误码输出，不抛异常
        std::filesystem::create_directories(file.parent_path(), ec);
    }
    sqlite3* handle = nullptr;                                ///< 输出参数：新连接句柄
    int rc = sqlite3_open_v2(path.c_str(), &handle,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                             nullptr);                        ///< 使用默认 VFS
    if (rc != SQLITE_OK) {
        std::string message = handle ? sqlite3_errmsg(handle)
                                     : "内存不足";             ///< 取错误信息
        if (handle) sqlite3_close(handle);                    ///< 失败时也要释放句柄
        throw DbException("无法打开数据库：" + message);
    }
    db_ = handle;                                             ///< 接管连接所有权
    path_ = path;                                             ///< 记录路径供诊断
    exec("PRAGMA foreign_keys = ON;");                        ///< 外键约束必须逐连接开启
    exec("PRAGMA busy_timeout = 5000;");                      ///< 锁等待 5 秒
    exec("PRAGMA journal_mode = WAL;");                       ///< 写前日志，提升并发读性能
}

/* @brief 幂等关闭连接；置空指针保证析构与显式调用不冲突。 */
void Database::close() noexcept {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

/*
 * @brief 执行一条或多条不含参数的 SQL（DDL / PRAGMA / 批量脚本）。
 * @param sql SQL 文本，可包含分号分隔的多条语句。
 * @exception DbException 执行失败时抛出，并附带 SQLite 错误与原始 SQL。
 * @note sqlite3_exec 的 error 指针由 SQLite 用 sqlite3_malloc 分配，必须用 sqlite3_free 释放。
 */
void Database::exec(const std::string& sql) const {
    if (!db_) throw DbException("数据库未打开，无法执行：" + sql);
    char* error = nullptr;                                    ///< SQLite 分配的错误文本
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db_);  ///< 优先用详细错误
        if (error) sqlite3_free(error);                       ///< 释放 SQLite 分配的内存
        throw DbException("SQL 执行失败：" + message + "\n" + sql);
    }
}

/*
 * @brief 预编译参数化 SQL 并返回可移动的 Statement。
 * @param sql 含 ? 占位符的 SQL。
 * @return Statement 对象（移动语义，无拷贝开销）。
 */
Statement Database::prepare(const std::string& sql) const {
    if (!db_) throw DbException("数据库未打开，无法预编译：" + sql);
    return Statement(db_, sql);
}

/* @brief 返回最近一次 INSERT 生成的自增主键（DAO::insert 用它回填实体 ID）。 */
long long Database::lastInsertId() const noexcept {
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

/* @brief 返回最近一条 SQL 影响的行数（用于判断删除/更新是否命中）。 */
int Database::changes() const noexcept {
    return db_ ? sqlite3_changes(db_) : 0;
}

/* @brief 返回连接最近一次错误文本，供日志与诊断使用。 */
std::string Database::lastError() const {
    return db_ ? std::string(sqlite3_errmsg(db_))
               : std::string("数据库未打开");
}

// ==================== DbInitializer（模式与基础演示数据） ====================
namespace {

/*
 * @brief 完整数据库模式脚本（幂等）。
 * @design 4 张业务表：
 *   - bookshelves：书架，code UNIQUE，capacity > 0；
 *   - students：学生，student_no UNIQUE，max_borrow 1..20，active 0/1；
 *   - books：馆藏副本，shelf_id 外键 RESTRICT，status 0..3，publish_year 有 CHECK；
 *   - borrow_records：借阅单据，双外键 RESTRICT，日期先后 CHECK，status 0..2。
 * @design 索引：uq_active_loan 是「部分唯一索引」，只对未归还记录 (status IN (0,2))
 *   约束 book_id 唯一，这是防止同一册书被重复借出的最后一道防线；
 *   其余 6 个普通索引服务于题名/ISBN 检索、状态统计、书架聚合与逾期扫描。
 * @design 视图：v_loan_detail 一次 JOIN 出借阅页所需全部列，避免 N+1 查询；
 *   v_shelf_stat 用 LEFT JOIN + GROUP BY 聚合每架藏书数与在馆数（含 remark 列以保持行完整）。
 * @note 所有语句均带 IF NOT EXISTS，重复执行不会丢数据。
 */
const char* const kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS bookshelves (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    code      TEXT    NOT NULL UNIQUE,
    location  TEXT    NOT NULL,
    category  TEXT    NOT NULL DEFAULT '',
    capacity  INTEGER NOT NULL CHECK (capacity > 0),
    remark    TEXT    NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS students (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    student_no  TEXT    NOT NULL UNIQUE,
    name        TEXT    NOT NULL,
    major       TEXT    NOT NULL DEFAULT '',
    phone       TEXT    NOT NULL DEFAULT '',
    max_borrow  INTEGER NOT NULL DEFAULT 5 CHECK (max_borrow BETWEEN 1 AND 20),
    active      INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0,1)),
    created_at  TEXT    NOT NULL DEFAULT (date('now','localtime'))
);

CREATE TABLE IF NOT EXISTS books (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    isbn          TEXT    NOT NULL,
    title         TEXT    NOT NULL,
    author        TEXT    NOT NULL DEFAULT '',
    publisher     TEXT    NOT NULL DEFAULT '',
    publish_year  INTEGER NOT NULL DEFAULT 0
                  CHECK (publish_year = 0 OR publish_year BETWEEN 1450 AND 2100),
    category      TEXT    NOT NULL DEFAULT '',
    shelf_id      INTEGER NOT NULL REFERENCES bookshelves(id) ON DELETE RESTRICT,
    status        INTEGER NOT NULL DEFAULT 0 CHECK (status IN (0,1,2,3)),
    added_at      TEXT    NOT NULL DEFAULT (date('now','localtime'))
);

CREATE TABLE IF NOT EXISTS borrow_records (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id   INTEGER NOT NULL REFERENCES students(id) ON DELETE RESTRICT,
    book_id      INTEGER NOT NULL REFERENCES books(id) ON DELETE RESTRICT,
    borrow_date  TEXT    NOT NULL,
    due_date     TEXT    NOT NULL,
    return_date  TEXT,
    status       INTEGER NOT NULL DEFAULT 0 CHECK (status IN (0,1,2)),
    remark       TEXT    NOT NULL DEFAULT '',
    CHECK (due_date >= borrow_date),
    CHECK (return_date IS NULL OR return_date >= borrow_date)
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_active_loan
    ON borrow_records(book_id) WHERE status IN (0,2);
CREATE INDEX IF NOT EXISTS idx_books_title    ON books(title);
CREATE INDEX IF NOT EXISTS idx_books_isbn     ON books(isbn);
CREATE INDEX IF NOT EXISTS idx_books_status   ON books(status);
CREATE INDEX IF NOT EXISTS idx_books_shelf    ON books(shelf_id);
CREATE INDEX IF NOT EXISTS idx_loans_student  ON borrow_records(student_id, status);
CREATE INDEX IF NOT EXISTS idx_loans_due      ON borrow_records(due_date);

CREATE VIEW IF NOT EXISTS v_loan_detail AS
SELECT r.id AS record_id, r.student_id, s.student_no, s.name AS student_name,
       r.book_id, b.isbn, b.title, sh.code AS shelf_code,
       r.borrow_date, r.due_date, r.return_date, r.status, r.remark
FROM borrow_records r
JOIN students s ON s.id = r.student_id
JOIN books b ON b.id = r.book_id
LEFT JOIN bookshelves sh ON sh.id = b.shelf_id;

CREATE VIEW IF NOT EXISTS v_shelf_stat AS
SELECT sh.id, sh.code, sh.location, sh.category, sh.capacity, sh.remark,
       COUNT(b.id) AS book_count,
       SUM(CASE WHEN b.status = 0 THEN 1 ELSE 0 END) AS available_count
FROM bookshelves sh
LEFT JOIN books b ON b.shelf_id = sh.id
GROUP BY sh.id, sh.code, sh.location, sh.category, sh.capacity, sh.remark;
)SQL";

/* @brief 基础演示数据：3 个书架。shelf_id 用子查询按 code 取，避免硬编码自增 ID。 */
const char* const kDemoShelves = R"SQL(
INSERT INTO bookshelves(code, location, category, capacity, remark) VALUES
 ('A-01','一层东侧','文学',30,'文学类藏书'),
 ('A-02','二层东侧','计算机',40,'计算机与信息技术'),
 ('B-01','二层西侧','经济管理',30,'经管类藏书');
)SQL";

/* @brief 基础演示数据：4 名学生，其中赵强 active=0 用于演示「停用学生禁借」。 */
const char* const kDemoStudents = R"SQL(
INSERT INTO students(student_no, name, major, phone, max_borrow, active) VALUES
 ('20240001','张明','经济学 2024 级','13800000001',5,1),
 ('20240002','李华','人力资源管理 2024 级','13800000002',5,1),
 ('20240003','王芳','工商管理 2023 级','13800000003',3,1),
 ('20240004','赵强','信息管理 2022 级','13800000004',5,0);
)SQL";

/* @brief 基础演示数据：8 册图书，日期用 SQLite date() 生成，保证与本机时区一致。 */
const char* const kDemoBooks = R"SQL(
INSERT INTO books(isbn,title,author,publisher,publish_year,category,shelf_id,status,added_at)
VALUES
 ('9787111213826','C++ Primer 中文版','Stanley B. Lippman','机械工业出版社',2006,'计算机',
   (SELECT id FROM bookshelves WHERE code='A-02'),0,date('now','localtime')),
 ('9787302147510','数据结构（C语言版）','严蔚敏','清华大学出版社',2011,'计算机',
   (SELECT id FROM bookshelves WHERE code='A-02'),0,date('now','localtime')),
 ('9787115374301','Effective C++','Scott Meyers','电子工业出版社',2011,'计算机',
   (SELECT id FROM bookshelves WHERE code='A-02'),0,date('now','localtime')),
 ('9787020002207','红楼梦','曹雪芹','人民文学出版社',1996,'文学',
   (SELECT id FROM bookshelves WHERE code='A-01'),0,date('now','localtime')),
 ('9787020051724','呐喊','鲁迅','人民文学出版社',2006,'文学',
   (SELECT id FROM bookshelves WHERE code='A-01'),0,date('now','localtime')),
 ('9787300045542','财务分析','张先治','东北财经大学出版社',2022,'经济管理',
   (SELECT id FROM bookshelves WHERE code='B-01'),0,date('now','localtime')),
 ('9787522300001','管理学原理','周三多','复旦大学出版社',2018,'经济管理',
   (SELECT id FROM bookshelves WHERE code='B-01'),0,date('now','localtime')),
 ('9787111641247','SQLite 权威指南','Grant Allen','人民邮电出版社',2020,'计算机',
   (SELECT id FROM bookshelves WHERE code='A-02'),0,date('now','localtime'));
)SQL";

/*
 * @brief 基础演示数据：2 条借阅记录（1 条正常、1 条逾期），并把对应图书置为借出。
 * @note 借出日、应还日全部用 date('now','localtime',...)，与领域层 today() 口径一致；
 *       最后一条 UPDATE 保证 books.status 与未归还记录严格对应。
 */
const char* const kDemoLoans = R"SQL(
INSERT INTO borrow_records(student_id, book_id, borrow_date, due_date, status, remark)
VALUES
 (1, 1, date('now','localtime'), date('now','localtime','+30 day'), 0, '正常借阅演示'),
 (2, 4, date('now','localtime','-40 day'), date('now','localtime','-10 day'), 2, '逾期演示');
UPDATE books SET status = 1 WHERE id IN (1, 4);
)SQL";

}  // namespace 匿名命名空间结束

/*
 * @brief 创建/升级数据库模式。
 * @param db 已打开的连接。
 * @note 全部 SQL 幂等，故 GUI 每次启动、DemoDataTool 每次生成都可安全调用。
 */
void DbInitializer::createSchema(Database& db) {
    db.exec(kSchema);
}

/*
 * @brief 空库时写入基础演示数据。
 * @param db 已打开且已建表的连接。
 * @note 主要算法（幂等 + 原子性）：
 *       1) 先查 bookshelves 行数，非空说明已有数据（可能是 1000 册演示库），立即返回；
 *       2) 用 Transaction 包住 4 段 INSERT/UPDATE，任一步失败整体回滚；
 *       3) 插入顺序遵循外键依赖：书架 → 学生 → 图书 → 借阅记录。
 */
void DbInitializer::seedDemoData(Database& db) {
    Statement count = db.prepare("SELECT COUNT(*) FROM bookshelves;");  ///< 空库判定语句
    if (count.step() && count.columnInt(0) > 0) return;      ///< 已有数据：不重复播种

    Transaction transaction(db);                             ///< 原子化播种
    db.exec(kDemoShelves);                                    ///< 先插被引用方（书架）
    db.exec(kDemoStudents);                                   ///< 再插学生
    db.exec(kDemoBooks);                                      ///< 图书引用书架
    db.exec(kDemoLoans);                                      ///< 借阅引用学生与图书
    transaction.commit();                                    ///< 提交，析构不再回滚
}

}  // namespace lls
