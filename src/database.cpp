// database.cpp -- SQLite C API wrapper (RAII), schema and demo data
#include "database.h"

#include "domain.h"

#include <sqlite3.h>

#include <filesystem>
#include <string>

namespace lls {

// ---------------------------------------------------------------- Statement
Statement::Statement(sqlite3* db, const std::string& sql) : sql_(sql) {
    if (!db) throw DbException("数据库未打开");
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = sqlite3_errmsg(db);
        stmt_ = nullptr;
        throw DbException("SQL 预编译失败：" + message + "\n" + sql);
    }
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept
    : stmt_(other.stmt_), sql_(std::move(other.sql_)) {
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_) sqlite3_finalize(stmt_);
        stmt_ = other.stmt_;
        sql_ = std::move(other.sql_);
        other.stmt_ = nullptr;
    }
    return *this;
}

Statement& Statement::bindInt(int index, int value) {
    if (sqlite3_bind_int(stmt_, index, value) != SQLITE_OK)
        throw DbException("参数绑定失败（int）：" + sql_);
    return *this;
}

Statement& Statement::bindText(int index, const std::string& value) {
    if (sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        throw DbException("参数绑定失败（text）：" + sql_);
    return *this;
}

Statement& Statement::bindNull(int index) {
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK)
        throw DbException("参数绑定失败（null）：" + sql_);
    return *this;
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    std::string message = sqlite3_errmsg(sqlite3_db_handle(stmt_));
    throw DbException("SQL 执行失败：" + message + "\n" + sql_);
}

void Statement::execute() {
    while (step()) {
    }
}

int Statement::columnInt(int column) const {
    return sqlite3_column_int(stmt_, column);
}

std::string Statement::columnText(int column) const {
    const unsigned char* value = sqlite3_column_text(stmt_, column);
    return value ? reinterpret_cast<const char*>(value) : std::string();
}

bool Statement::columnIsNull(int column) const {
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ---------------------------------------------------------------- Transaction
Transaction::Transaction(Database& db) : db_(db) {
    db_.exec("BEGIN IMMEDIATE;");
}

Transaction::~Transaction() {
    if (!committed_) {
        try {
            db_.exec("ROLLBACK;");
        } catch (...) {
            // never throw from a destructor during stack unwinding
        }
    }
}

void Transaction::commit() {
    db_.exec("COMMIT;");
    committed_ = true;
}

// ---------------------------------------------------------------- Database
Database::Database(const std::string& path) {
    open(path);
}

Database::~Database() {
    close();
}

void Database::open(const std::string& path) {
    close();
    std::filesystem::path file(path);
    if (file.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
    }
    sqlite3* handle = nullptr;
    int rc = sqlite3_open_v2(path.c_str(), &handle,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = handle ? sqlite3_errmsg(handle) : "内存不足";
        if (handle) sqlite3_close(handle);
        throw DbException("无法打开数据库：" + message);
    }
    db_ = handle;
    path_ = path;
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA busy_timeout = 5000;");
    exec("PRAGMA journal_mode = WAL;");
}

void Database::close() noexcept {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::exec(const std::string& sql) const {
    if (!db_) throw DbException("数据库未打开，无法执行：" + sql);
    char* error = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db_);
        if (error) sqlite3_free(error);
        throw DbException("SQL 执行失败：" + message + "\n" + sql);
    }
}

Statement Database::prepare(const std::string& sql) const {
    if (!db_) throw DbException("数据库未打开，无法预编译：" + sql);
    return Statement(db_, sql);
}

long long Database::lastInsertId() const noexcept {
    return db_ ? sqlite3_last_insert_rowid(db_) : 0;
}

int Database::changes() const noexcept {
    return db_ ? sqlite3_changes(db_) : 0;
}

std::string Database::lastError() const {
    return db_ ? std::string(sqlite3_errmsg(db_)) : std::string("数据库未打开");
}

// ---------------------------------------------------------------- DbInitializer
namespace {

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
SELECT sh.id, sh.code, sh.location, sh.category, sh.capacity,
       COUNT(b.id) AS book_count,
       SUM(CASE WHEN b.status = 0 THEN 1 ELSE 0 END) AS available_count
FROM bookshelves sh
LEFT JOIN books b ON b.shelf_id = sh.id
GROUP BY sh.id, sh.code, sh.location, sh.category, sh.capacity;
)SQL";

const char* const kDemoShelves = R"SQL(
INSERT INTO bookshelves(code, location, category, capacity, remark) VALUES
 ('A-01','一层东侧','文学',30,'文学类藏书'),
 ('A-02','二层东侧','计算机',40,'计算机与信息技术'),
 ('B-01','二层西侧','经济管理',30,'经管类藏书');
)SQL";

const char* const kDemoStudents = R"SQL(
INSERT INTO students(student_no, name, major, phone, max_borrow, active) VALUES
 ('20240001','张明','经济学 2024 级','13800000001',5,1),
 ('20240002','李华','人力资源管理 2024 级','13800000002',5,1),
 ('20240003','王芳','工商管理 2023 级','13800000003',3,1),
 ('20240004','赵强','信息管理 2022 级','13800000004',5,0);
)SQL";

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

const char* const kDemoLoans = R"SQL(
INSERT INTO borrow_records(student_id, book_id, borrow_date, due_date, status, remark)
VALUES
 (1, 1, date('now','localtime'), date('now','localtime','+30 day'), 0, '正常借阅演示'),
 (2, 4, date('now','localtime','-40 day'), date('now','localtime','-10 day'), 2, '逾期演示');
UPDATE books SET status = 1 WHERE id IN (1, 4);
)SQL";

}  // namespace

void DbInitializer::createSchema(Database& db) {
    db.exec(kSchema);
}

void DbInitializer::seedDemoData(Database& db) {
    Statement count = db.prepare("SELECT COUNT(*) FROM bookshelves;");
    if (count.step() && count.columnInt(0) > 0) return;

    Transaction transaction(db);
    db.exec(kDemoShelves);
    db.exec(kDemoStudents);
    db.exec(kDemoBooks);
    db.exec(kDemoLoans);
    transaction.commit();
}

}  // namespace lls
