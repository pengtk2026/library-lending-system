#pragma once
// database.h -- SQLite RAII wrapper, statement, transaction and schema bootstrap
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace lls {

class Database;

class Statement {
    sqlite3_stmt* stmt_ = nullptr;
    std::string sql_;

public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    Statement& bindInt(int index, int value);
    Statement& bindText(int index, const std::string& value);
    Statement& bindNull(int index);
    bool step();                 // true = SQLITE_ROW, false = SQLITE_DONE
    void execute();              // run until SQLITE_DONE
    int columnInt(int column) const;
    std::string columnText(int column) const;
    bool columnIsNull(int column) const;
    void reset();

    sqlite3_stmt* raw() const noexcept { return stmt_; }
    const std::string& sql() const noexcept { return sql_; }
};

class Transaction {
    Database& db_;
    bool committed_ = false;

public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();
};

class Database {
    sqlite3* db_ = nullptr;
    std::string path_;

public:
    Database() = default;
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void open(const std::string& path);
    void close() noexcept;
    bool isOpen() const noexcept { return db_ != nullptr; }
    sqlite3* handle() const noexcept { return db_; }
    const std::string& path() const noexcept { return path_; }

    void exec(const std::string& sql) const;
    Statement prepare(const std::string& sql) const;
    long long lastInsertId() const noexcept;
    int changes() const noexcept;
    std::string lastError() const;
};

class DbInitializer {
public:
    static void createSchema(Database& db);
    static void seedDemoData(Database& db);
};

}  // namespace lls
