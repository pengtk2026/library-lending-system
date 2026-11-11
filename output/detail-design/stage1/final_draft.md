# 1 引言

## 1.1 编写目的与基线

本文档是《简易图书借阅系统 概要设计说明书》（HLD-LLS-2026-01）的细化，给出可直接据以编码的类成员与方法签名、数据库 DDL、SQL 映射、界面控件清单与事件回调、关键算法伪代码、异常处理与测试用例。凡本文档与概要设计不一致之处，以本文档为准并同步修订概要设计。

## 1.2 详细设计范围

覆盖表现层（FLTK）、业务逻辑层（LibraryService）、领域模型层（Entity 家族）、数据访问层（Database / Statement / Transaction / 四个 DAO）与持久化层（SQLite 单文件）。不包含网络、权限、报表打印与安装包制作。

## 1.3 编码约定

| 项目 | 约定 |
| --- | --- |
| 命名空间 | 全部业务代码位于 `namespace lls` |
| 标准 | C++17；禁用裸 `new/delete`；容器用 `std::vector`、`std::string` |
| 命名 | 类大驼峰；成员后缀 `_`；getter 与属性同名；setter 为 `setXxx()` |
| 头文件 | 每个头文件 `#pragma once`；按层合并为 5 个头文件，避免碎片化 |
| 异常 | 只在 DAO / Service 抛出，UI 回调内必须捕获并转换提示 |
| SQL | 一律预编译 + 参数绑定；SQL 常量集中在各 DAO 实现的匿名命名空间 |
| 日期 | `YYYY-MM-DD` 文本；领域层用 `std::tm`/`mktime` 计算，数据库用 `date()` |
| 编码 | 源文件 UTF-8 无 BOM；界面字体 Microsoft YaHei |
| 回调 | `static void onXxx(Fl_Widget*, void*)`，用 `static_cast<Panel*>(data)` 回指对象 |

# 2 物理结构与构建设计

## 2.1 文件清单

| 文件 | 内容 |
| --- | --- |
| include/domain.h | 枚举、日期工具、异常体系、Entity、Book、Student、Bookshelf、BorrowRecord |
| include/database.h | Database、Statement、Transaction、DbInitializer 声明 |
| include/daos.h | Dao 模板基类、BookDao、StudentDao、BookshelfDao、BorrowRecordDao、展示结构体 |
| include/service.h | Statistics、ShelfStat、LoanDetail、LibraryService |
| include/ui.h | UiUtils、三个输入对话框、四个功能面板、MainWindow |
| src/domain.cpp | 领域类与工具函数实现 |
| src/database.cpp | SQLite 封装、DDL 与演示数据 |
| src/daos.cpp | 四个 DAO 的 SQL 与行映射 |
| src/service.cpp | 业务规则、事务编排、统计与筛选 |
| src/ui.cpp | FLTK 控件、回调与数据绑定 |
| src/main.cpp | 程序入口与全局异常兜底 |
| CMakeLists.txt | 构建配置（sqlite3.c 静态库、FLTK 导入目标、静态链接） |
| build.bat | 一键配置 / 编译 / 运行脚本 |

## 2.2 头文件依赖方向

`ui.h → service.h → daos.h → database.h → domain.h`，反向依赖禁止；`daos.h` 中的展示结构体（LoanDetail / ShelfStat）由 UI 读取，不反向包含 `ui.h`。FLTK 头文件只在 `ui.h` 与 `src/ui.cpp` 中出现，Service 与 DAO 完全不感知 GUI。

## 2.3 CMake 构建设计

```cmake
cmake_minimum_required(VERSION 3.16)
project(LibraryLendingSystem LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(sqlite3 STATIC tools/sqlite-amalgamation-3540100/sqlite3.c)
target_include_directories(sqlite3 PUBLIC tools/sqlite-amalgamation-3540100)
find_package(FLTK CONFIG REQUIRED)     # 由 -DFLTK_DIR=<install>/lib/cmake/FLTK 指定
add_executable(LibraryApp WIN32
  src/main.cpp src/domain.cpp src/database.cpp src/daos.cpp
  src/service.cpp src/ui.cpp)
target_include_directories(LibraryApp PRIVATE include)
target_link_libraries(LibraryApp PRIVATE fltk::fltk sqlite3)
target_link_options(LibraryApp PRIVATE -static)
```

生成器使用 Ninja（由便携工具链提供），编译器显式指定为 `tools/msys2root/mingw64/bin/gcc.exe` 与 `g++.exe`；若 `find_package(FLTK CONFIG)` 不可用，则退化为 `FLTK_ROOT` 手动定位头文件与 `libfltk.a` 等静态库，并显式追加 `gdi32 comctl32 ole32 uuid shell32 ws2_32 user32 advapi32`。

# 3 领域模型详细设计

## 3.1 枚举与工具函数（domain.h）

| 定义 | 说明 |
| --- | --- |
| `enum class BookStatus { Available=0, Borrowed=1, Lost=2, Maintenance=3 }` | 图书状态，与 books.status 整数一致 |
| `enum class LoanStatus { Borrowed=0, Returned=1, Overdue=2 }` | 借阅状态，与 borrow_records.status 一致 |
| `enum class LoanFilter { All=0, Active=1, Overdue=2 }` | 借阅页筛选 |
| `const char* toString(BookStatus) / toString(LoanStatus)` | 中文状态名，供界面显示 |
| `std::string today()` | 本地日期 `YYYY-MM-DD`，基于 `std::time` + `std::localtime` |
| `std::string addDays(const std::string&, int)` | 日期加减，基于 `std::tm` + `mktime`，输出同格式 |
| `bool isBefore(const std::string&, const std::string&)` | 定长日期字符串字典序比较 |
| `std::string trim(const std::string&)` | 去除首尾空白 |
| `bool isBlank(const std::string&)` | 判空，供 validate() 复用 |

## 3.2 异常体系

| 类 | 基类 | 触发场景 | 携带信息 |
| --- | --- | --- | --- |
| AppException | std::runtime_error | 所有应用异常的公共基类 | 消息文本 |
| ValidationException | AppException | 实体 validate() 失败、界面输入非法 | 字段名 + 原因 |
| NotFoundException | AppException | findById 无结果 | 实体名 + id |
| DbException | AppException | sqlite3 返回错误码 | SQL + sqlite3_errmsg |
| BusinessException | AppException | 违反 BR-01～BR-07 | 面向用户的业务文案 |

所有异常构造函数显式接收 `std::string`，通过 `what()` 传递；UI 统一捕获 `const AppException&` 与 `const std::exception&`。

## 3.3 Entity 抽象基类

| 成员 | 类型 / 签名 | 说明 |
| --- | --- | --- |
| id_ | int（protected，默认 0） | 0 表示尚未持久化 |
| id() / setId(int) | int / void | 主键访问 |
| tableName() | `virtual std::string tableName() const = 0` | 多态返回表名 |
| validate() | `virtual std::string validate() const = 0` | 返回空串表示合法，否则首条错误 |
| toRow() | `virtual std::vector<std::string> toRow() const = 0` | 多态导出行数据，供日志与列表 |
| 虚析构 | `virtual ~Entity() = default` | 保证派生类正确析构 |

`validate()` 与 `toRow()` 是运行时多态的两个落点：`LibraryService::checkEntity(const Entity&)` 通用校验入口与 `UiUtils::entityRow(const Entity&)` 通用行导出均只依赖基类引用。

## 3.4 Book 类

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| isbn_ | std::string | 非空，允许重复（不同副本） |
| title_ | std::string | 非空 |
| author_ / publisher_ / category_ | std::string | 可空，默认空串 |
| publishYear_ | int | 1450 ～ 当年，0 表示未填 |
| shelfId_ | int | 必须大于 0，外键 |
| status_ | BookStatus | 默认 Available |
| addedAt_ | std::string | 入库日期 `YYYY-MM-DD` |

主要方法：`tableName()` 返回 `books`；`validate()` 依次检查 isbn / title / publishYear / shelfId，返回首个错误文案；`toRow()` 返回 9 列文本；`isAvailable()` 判断 `status_ == Available`；`markBorrowed()` 与 `markReturned()` 只在合法状态下迁移，否则抛 `BusinessException`；`statusText()` 调用 `toString(status_)`。setter 对字符串执行 `trim()`，对 publishYear 直接赋值（范围校验集中在 validate）。

## 3.5 Student 类

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| studentNo_ | std::string | 非空，数据库 UNIQUE |
| name_ | std::string | 非空 |
| major_ / phone_ | std::string | 可空 |
| maxBorrow_ | int | 默认 5，范围 1～20 |
| active_ | bool | 默认 true |

主要方法：`validate()` 检查学号、姓名、maxBorrow；`canBorrowMore(int current)` 返回 `active_ && current < maxBorrow_`；`toRow()` 导出 7 列；静态方法 `defaultMaxBorrow()` 返回 5。

## 3.6 Bookshelf 类

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| code_ | std::string | 非空且 UNIQUE |
| location_ | std::string | 非空 |
| category_ | std::string | 可空 |
| capacity_ | int | 大于 0 |
| remark_ | std::string | 可空 |
| bookIds_ | std::vector\<int\> | 内存聚合的藏书 ID 列表（不直接持久化） |

主要方法：`bookCount()` 返回 `bookIds_.size()`；`remaining()` 返回 `capacity_ - bookCount()`；`canAdd()` 返回 `remaining() > 0`；`addBookId(int)` 追加前检查 `canAdd()`；`clearBookIds()`；`validate()` 检查编号、位置、容量。书架与图书为「数据库外键 1:N + 内存聚合」双重表达。

## 3.7 BorrowRecord 类

| 成员 | 类型 | 约束 |
| --- | --- | --- |
| studentId_ / bookId_ | int | 大于 0，双外键 |
| borrowDate_ / dueDate_ | std::string | `YYYY-MM-DD`，dueDate ≥ borrowDate |
| returnDate_ | std::string | 可空，归还时填写 |
| status_ | LoanStatus | 默认 Borrowed |
| remark_ | std::string | 可空 |

主要方法：`isOverdue(const std::string& day)` 返回 `status_ != Returned && isBefore(dueDate_, day)`；`close(const std::string& day)` 设置 returnDate_ 与 status_=Returned，重复归还抛 BusinessException；`validate()` 检查外键 ID、日期格式与先后关系；`toRow()` 导出 8 列。

# 4 数据库详细设计

## 4.1 连接与 PRAGMA

`Database::open()` 调用 `sqlite3_open_v2(path, &db_, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, nullptr)`，随后 `exec("PRAGMA foreign_keys = ON;")`、`exec("PRAGMA journal_mode = WAL;")`、`exec("PRAGMA busy_timeout = 5000;")`。数据库路径由 `Application` 解析为可执行文件同级的 `data/library.db`，目录不存在时以 `std::filesystem::create_directories` 创建。

## 4.2 完整 DDL

```sql
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
```

部分唯一索引 `uq_active_loan` 从数据库层保证「同一册书同时至多一条未归还记录」，是 BR-02 的最后一道防线。

## 4.3 视图定义

```sql
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
```

## 4.4 字段映射表

| 领域类 | 数据表 | 映射要点 |
| --- | --- | --- |
| Book | books | `status_` ↔ `status` 经 `static_cast<int>`；`shelfId_` ↔ `shelf_id` |
| Student | students | `active_` ↔ `active`（0/1）；`studentNo_` ↔ `student_no` |
| Bookshelf | bookshelves | `bookIds_` 不映射，由 `countByShelf` 查询填充 |
| BorrowRecord | borrow_records | `status_` ↔ `status`；空 `returnDate_` ↔ SQL NULL |

DAO 读取时对可空列先 `columnIsNull()` 再 `columnText()`；写入时空字符串统一 `bindText`，可空日期用 `bindNull`。

## 4.5 DAO SQL 清单

| DAO 方法 | SQL 摘要 |
| --- | --- |
| BookDao::insert | `INSERT INTO books(isbn,title,author,publisher,publish_year,category,shelf_id,status,added_at) VALUES(?,?,?,?,?,?,?,?,?)` |
| BookDao::update | `UPDATE books SET ... WHERE id=?` |
| BookDao::search | `SELECT ... FROM books b LEFT JOIN bookshelves sh ... WHERE b.title LIKE ? OR b.author LIKE ? OR b.isbn LIKE ? ORDER BY b.id` |
| BookDao::updateStatus | `UPDATE books SET status=? WHERE id=?` |
| BookDao::countByShelf | `SELECT COUNT(*) FROM books WHERE shelf_id=?` |
| StudentDao::insert | `INSERT INTO students(student_no,name,major,phone,max_borrow,active) VALUES(?,?,?,?,?,?)` |
| StudentDao::findByStudentNo | `SELECT ... WHERE student_no=?` |
| BookshelfDao::insert | `INSERT INTO bookshelves(code,location,category,capacity,remark) VALUES(?,?,?,?,?)` |
| BorrowRecordDao::insert | `INSERT INTO borrow_records(student_id,book_id,borrow_date,due_date,status,remark) VALUES(?,?,?,?,?,?)` |
| BorrowRecordDao::close | `UPDATE borrow_records SET return_date=?, status=1 WHERE id=? AND status<>1` |
| BorrowRecordDao::listDetails | `SELECT ... FROM v_loan_detail WHERE ... ORDER BY borrow_date DESC, record_id DESC` |
| BorrowRecordDao::countActiveByStudent | `SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status IN (0,2)` |
| BorrowRecordDao::hasOverdue | `SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status<>1 AND due_date < ?` |
| BorrowRecordDao::markOverdue | `UPDATE borrow_records SET status=2 WHERE status=0 AND due_date < ?` |

所有 `LIKE` 参数以 `%keyword%` 形式绑定，避免拼接注入；列表查询统一 `ORDER BY` 保证界面顺序稳定。

## 4.6 演示数据初始化

`seedDemoData()` 仅在 `SELECT COUNT(*) FROM bookshelves` 为 0 时执行，插入 3 个书架（A-01 文学 / A-02 计算机 / B-01 经管）、8 册图书、4 名学生，并插入 2 条借阅记录（其中 1 条应还日期设为过去以演示逾期）。全部写入包在一个 `Transaction` 中。

# 5 数据访问层详细设计

## 5.1 Database 类

| 成员 / 方法 | 签名 | 行为 |
| --- | --- | --- |
| db_ | sqlite3*（私有） | 连接句柄，析构时 `sqlite3_close` |
| open | `void open(const std::string&)` | 打开或创建数据库，失败抛 DbException |
| close | `void close() noexcept` | 幂等关闭 |
| isOpen | `bool isOpen() const noexcept` | 句柄判空 |
| handle | `sqlite3* handle() const` | 供 Statement / Transaction 使用 |
| exec | `void exec(const std::string&)` | 执行多条 DDL / PRAGMA，错误抛 DbException |
| prepare | `Statement prepare(const std::string&) const` | 预编译并返回可移动 Statement |
| lastInsertId | `long long lastInsertId() const` | `sqlite3_last_insert_rowid` |
| changes | `int changes() const` | `sqlite3_changes` |
| lastError | `std::string lastError() const` | `sqlite3_errmsg` |
| 拷贝控制 | 删除拷贝构造与赋值 | 独占资源（RAII） |

## 5.2 Statement 类

| 方法 | 行为 |
| --- | --- |
| 构造 / 析构 | `sqlite3_prepare_v2` / `sqlite3_finalize`，失败抛 DbException |
| 移动构造 / 移动赋值 | 转移 `sqlite3_stmt*`，源置空 |
| bindInt / bindText / bindNull | 下标从 1 开始；文本按 `SQLITE_TRANSIENT` 拷贝 |
| step | 返回 true 表示 SQLITE_ROW；SQLITE_DONE 返回 false；其他错误抛 DbException |
| execute | 循环 step 至 DONE |
| columnInt / columnText / columnIsNull | 列下标从 0 开始；文本按 UTF-8 构造 std::string |
| reset | `sqlite3_reset` + `sqlite3_clear_bindings` |

## 5.3 Transaction 类

```text
Transaction(Database& db) : db_(db) { db_.exec("BEGIN IMMEDIATE"); }
~Transaction() { if (!committed_) try { db_.exec("ROLLBACK"); } catch (...) {} }
void commit() { db_.exec("COMMIT"); committed_ = true; }
```

禁止拷贝；析构中的回滚吞掉异常，保证栈展开期间不抛异常。`BEGIN IMMEDIATE` 提前获取写锁，避免借书流程的写冲突。

## 5.4 Dao 模板基类

```cpp
template <typename T>
class Dao {
protected:
    Database& db_;
public:
    explicit Dao(Database& db) : db_(db) {}
    virtual ~Dao() = default;
    virtual int insert(const T& e) = 0;
    virtual void update(const T& e) = 0;
    virtual bool removeById(int id) = 0;
    virtual T findById(int id) const = 0;
    virtual std::vector<T> listAll() const = 0;
};
```

模板基类体现静态多态与代码复用：公共的前置校验 `requirePersisted(e.id())`、异常包装 `wrapDbError()` 在基类实现，具体 SQL 与 `mapRow(Statement&)` 由四个派生类重写。`removeById` 返回是否实际删除（`changes() > 0`）。

## 5.5 派生 DAO 附加方法

| DAO | 附加方法 |
| --- | --- |
| BookDao | `search(keyword)`、`updateStatus(id, status)`、`countByShelf(id)`、`availableList()` |
| StudentDao | `findByStudentNo(no)`、`countActiveLoans(id)`、`existsNo(no, excludeId)` |
| BookshelfDao | `countBooks(id)`、`listWithStats()`、`existsCode(code, excludeId)` |
| BorrowRecordDao | `close(id, date)`、`listDetails(LoanFilter)`、`countActiveByStudent(id)`、`hasOverdue(id, today)`、`markOverdue(today)`、`activeForBook(bookId)` |

`listDetails()` 直接查询 `v_loan_detail`，一次 JOIN 完成界面所需全部列，避免 N+1 查询。

# 6 业务逻辑层详细设计

## 6.1 展示结构体

| 结构体 | 字段 |
| --- | --- |
| Statistics | totalBooks、availableBooks、borrowedBooks、overdueLoans、totalStudents、totalShelves |
| ShelfStat | Bookshelf shelf、int bookCount、int availableCount |
| LoanDetail | BorrowRecord record、studentNo、studentName、isbn、bookTitle、shelfCode、`statusText()` |

## 6.2 LibraryService 接口表

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| initialize | `void initialize()` | 建表 + 演示数据 + 逾期标记 |
| addBook | `int addBook(const Book&)` | 校验 → 注入 addedAt → insert → 返回 id |
| updateBook | `void updateBook(const Book&)` | 校验 id 与实体后 update |
| removeBook | `void removeBook(int id)` | 预检未归还记录后 delete |
| listBooks | `std::vector<Book> listBooks(const std::string& kw)` | 空关键字走 listAll，否则 search |
| addStudent / updateStudent / removeStudent | 同图书 | 附加学号唯一性预检 |
| addShelf / updateShelf / removeShelf | 同图书 | 删除前 `countBooks()==0` 检查 |
| shelfStats | `std::vector<ShelfStat> shelfStats()` | 读取 v_shelf_stat |
| borrowBook | `int borrowBook(int studentId, int bookId)` | 事务，见 6.3 |
| returnBook | `void returnBook(int recordId)` | 事务，见 6.4 |
| listLoans | `std::vector<LoanDetail> listLoans(LoanFilter)` | 视图查询 |
| availableBooks | `std::vector<Book> availableBooks()` | 借书下拉数据源 |
| statistics | `Statistics statistics()` | 6 个聚合查询合并为一条 SELECT 子查询 |
| refreshOverdue | `int refreshOverdue()` | `markOverdue(today())` 返回影响行数 |

## 6.3 借书算法（含事务边界）

```text
输入：studentId, bookId
1  s ← studentDao.findById(studentId)           // 不存在 → NotFoundException
2  若 s.active() == false            → BusinessException("该学生已停用")
3  若 recordDao.hasOverdue(sid,today) → BusinessException("该生存在逾期未还图书")
4  n ← recordDao.countActiveByStudent(sid)
5  若 !s.canBorrowMore(n)            → BusinessException("已达可借上限 N 册")
6  b ← bookDao.findById(bookId)
7  若 !b.isAvailable()               → BusinessException("该图书当前不可借出")
8  r ← BorrowRecord(sid,bid,today,addDays(today,30),LoanStatus::Borrowed)
9  r.validate() 非空                → ValidationException
10 tx ← Transaction(db)             // BEGIN IMMEDIATE
11 rid ← recordDao.insert(r)
12 bookDao.updateStatus(bid, BookStatus::Borrowed)
13 tx.commit()                       // COMMIT
14 返回 rid
异常路径：步骤 10 之后任意异常 → tx 析构自动 ROLLBACK
```

步骤 11 的插入受 `uq_active_loan` 约束保护，若并发或脏数据导致重复借出，`DbException` 触发回滚。

## 6.4 还书算法

```text
输入：recordId
1 r ← recordDao.findById(recordId)
2 若 r.status() == Returned → BusinessException("该记录已归还")
3 tx ← Transaction(db)
4 recordDao.close(recordId, today)
5 bookDao.updateStatus(r.bookId(), BookStatus::Available)
6 tx.commit()
异常路径：任一步失败 → ROLLBACK，图书状态保持借出
```

## 6.5 删除预检规则

| 操作 | 预检 | 失败提示 |
| --- | --- | --- |
| removeBook(id) | `activeForBook(id)` 为 0，且状态非 Borrowed | 「该图书存在未归还借阅记录」 |
| removeStudent(id) | `countActiveLoans(id) == 0` | 「该学生尚有未归还图书」 |
| removeShelf(id) | `countBooks(id) == 0` | 「该书架上仍有藏书」 |

预检之外仍保留数据库外键 RESTRICT，双保险；UI 层先用 `fl_choice` 二次确认再调用 Service。

## 6.6 统计与逾期刷新

`statistics()` 以单条 SQL 的 6 个标量子查询组装 Statistics，避免多次往返；`refreshOverdue()` 在启动和每次「刷新」按钮点击时执行 `markOverdue(today())`，返回被标记条数并写入状态栏。

# 7 界面详细设计

## 7.1 控件树

```text
MainWindow (Fl_Double_Window 1024x680, "简易图书借阅系统")
├─ topBar    Fl_Box(10,10,760,30)          统计信息文本
├─ btnRefresh Fl_Button(790,10,100,30)     刷新全部
├─ btnAbout   Fl_Button(905,10,100,30)     关于
├─ tabs       Fl_Tabs(10,52,1004,570)
│  ├─ BookPanel   (Fl_Group "图书管理")
│  ├─ StudentPanel(Fl_Group "学生管理")
│  ├─ ShelfPanel  (Fl_Group "书架管理")
│  └─ LoanPanel   (Fl_Group "借阅管理")
└─ statusBar Fl_Box(10,630,1004,36)        操作结果 / 错误提示
```

## 7.2 MainWindow 成员与回调

| 成员 | 类型 | 说明 |
| --- | --- | --- |
| svc_ | LibraryService& | 业务门面引用，由 main 注入 |
| bookPanel_ 等 | 四个面板指针 | 由 FLTK 父子关系托管生命周期 |
| topBar_ / statusBar_ | Fl_Box* | 统计与消息展示 |
| onRefresh | 静态回调 | 调用 `refreshAll()` |
| onAbout | 静态回调 | 显示版本与技术栈信息 |
| refreshAll | 公有方法 | 依次刷新统计条、四个面板，末尾刷新逾期 |
| setStatus | 公有方法 | 设置状态栏文本并 `redraw()` |

构造函数中调用 `Fl::set_font(FL_HELVETICA, "Microsoft YaHei")` 与 `Fl::set_font(FL_HELVETICA_BOLD, "Microsoft YaHei")`，并调用 `Fl::scheme("gtk+")` 统一外观。

## 7.3 图书管理页控件表

| 变量 | 类型 | 位置 (x,y,w,h) | 作用 / 回调 |
| --- | --- | --- | --- |
| search_ | Fl_Input | 20,90,260,28 | 关键字输入，回车触发 onSearch |
| btnSearch_ | Fl_Button | 290,90,80,28 | onSearch → refresh(keyword) |
| btnAdd_ | Fl_Button | 390,90,80,28 | onAdd → 打开 BookDialog |
| btnEdit_ | Fl_Button | 480,90,80,28 | onEdit → 解析选中行 ID |
| btnDel_ | Fl_Button | 570,90,80,28 | onDel → 确认后 removeBook |
| btnRefresh_ | Fl_Button | 660,90,80,28 | onRefresh → refresh() |
| list_ | Fl_Hold_Browser | 20,130,970,470 | 列宽 `{60,130,260,150,120,110,90}`、`column_char('\t')` |

列表行格式：`id \t isbn \t title \t author \t shelfCode \t status \t publishYear`，首列 ID 供选中解析。表头行以 `@b` 前缀加粗并插入列表首行，选中时跳过表头行。

## 7.4 学生 / 书架页控件表

| 页面 | 列表列 | 按钮 | 附加 |
| --- | --- | --- | --- |
| 学生管理 | id、学号、姓名、专业、电话、可借上限、状态、已借册数 | 新增 / 编辑 / 删除 / 刷新 | 编辑对话框含「启用」勾选 |
| 书架管理 | id、编号、位置、分类、容量、藏书数、可用余量 | 新增 / 编辑 / 删除 / 刷新 | 删除前显示藏书数，>0 直接拒绝 |

## 7.5 借阅管理页控件表

| 变量 | 类型 | 作用 |
| --- | --- | --- |
| studentChoice_ | Fl_Choice | 学生下拉，平行数组 `studentIds_` 保存 ID |
| bookChoice_ | Fl_Choice | 在馆图书下拉，平行数组 `bookIds_` 保存 ID |
| btnBorrow_ | Fl_Button | onBorrow → `svc_.borrowBook()` |
| filterChoice_ | Fl_Choice | 全部 / 当前借阅 / 逾期，onFilter → refresh() |
| list_ | Fl_Hold_Browser | 列：记录ID、学号、姓名、ISBN、题名、借出日、应还日、状态 |
| btnReturn_ | Fl_Button | onReturn → 选中未归还记录 → `svc_.returnBook()` |
| btnRefresh_ | Fl_Button | 刷新下拉与列表 |

借书成功后同时刷新在馆图书下拉、记录列表和统计条，防止界面残留已借出图书。

## 7.6 输入对话框设计

| 对话框 | 字段控件 | 校验规则 |
| --- | --- | --- |
| BookDialog | isbn / title / author / publisher / publishYear(Fl_Int_Input) / category / shelf(Fl_Choice) / status(Fl_Choice，编辑时) | 必填、年份范围、必须选择书架 |
| StudentDialog | studentNo / name / major / phone / maxBorrow(Fl_Int_Input) / active(Fl_Check_Button) | 学号姓名必填，上限 1～20 |
| ShelfDialog | code / location / category / capacity(Fl_Int_Input) / remark | 编号位置必填，容量 > 0 |

三个对话框共同基类 `DialogBase`：

```text
DialogBase::exec():
    set_modal(); show();
    while (shown()) Fl::wait();     // 模态事件循环
    return confirmed_;             // true=确定，false=取消/关闭
onOk():  读取控件 → 组装实体 → 实体.validate()
         通过：confirmed_=true; hide();
         失败：fl_alert(错误信息)，保持对话框
onCancel(): confirmed_=false; hide();
```

对话框不直接访问数据库，只负责采集与返回领域对象，由面板调用 Service 写库。

## 7.7 事件回调与异常边界

所有 `onXxx` 回调体形如：

```cpp
void BookPanel::onAdd(Fl_Widget*, void* data) {
    auto* self = static_cast<BookPanel*>(data);
    try { self->addBook(); }
    catch (const AppException& e) { UiUtils::error(e.what()); self->status(e.what()); }
    catch (const std::exception& e) { UiUtils::error(e.what()); }
}
```

FLTK 回调中禁止异常外泄；`UiUtils::error` 使用 `fl_alert`，`UiUtils::confirm` 使用 `fl_choice("是","否",nullptr,...)`。

# 8 关键算法与数据结构

## 8.1 列表渲染

`UiUtils::fillBrowser(Fl_Browser*, const std::vector<std::string>& rows, const char* header)`：`clear()` → 设置 `column_widths()` 与 `column_char('\t')` → `add(header)` → 逐行 `add(row)`。行内字段中的 `\t` 与换行预先替换为空格，防止列错位。ID 恒为首列，`UiUtils::selectedId(browser)` 取 `text(value())` 后按 `\t` 切分并 `std::stoi` 首段，捕获异常返回 0。

## 8.2 关键字检索

`LIKE '%' || :kw || '%'`，关键字在 Service 层 `trim()` 后传入 DAO；DAO 对三列分别绑定同一参数。空关键字直接走 `listAll()`，避免全表 LIKE 扫描。

## 8.3 统计聚合

```sql
SELECT
 (SELECT COUNT(*) FROM books),
 (SELECT COUNT(*) FROM books WHERE status=0),
 (SELECT COUNT(*) FROM books WHERE status=1),
 (SELECT COUNT(*) FROM borrow_records WHERE status<>1 AND due_date < date('now','localtime')),
 (SELECT COUNT(*) FROM students),
 (SELECT COUNT(*) FROM bookshelves);
```

## 8.4 日期计算

`addDays()` 解析 `YYYY-MM-DD` 到 `std::tm`（`tm_year = year-1900`），`tm_mday += days` 后 `mktime()` 归一化再 `strftime()`；`today()` 同样基于本地时间。全部日期字符串定长 10 位，可直接字典序比较。

# 9 错误处理与测试设计

## 9.1 错误码与提示映射

| 场景 | 异常 | 界面文案 |
| --- | --- | --- |
| 必填为空 | ValidationException | 「题名不能为空」等具体字段 |
| 学号重复 | BusinessException | 「学号已存在」 |
| 借书超上限 | BusinessException | 「已达可借上限 5 册」 |
| 逾期未还 | BusinessException | 「该生存在逾期未还图书，请先归还」 |
| 删除被引用 | BusinessException / DbException | 「该记录被引用，无法删除」 |
| 数据库损坏 | DbException | 「数据库操作失败：...」 |

## 9.2 测试用例

| 编号 | 类型 | 步骤 | 预期结果 |
| --- | --- | --- | --- |
| TC-01 | 单元 | 构造 isbn 为空的 Book 并 validate() | 返回非空错误串 |
| TC-02 | 单元 | `addDays("2026-01-31", 30)` | `2026-03-02` |
| TC-03 | 单元 | BorrowRecord 未还且 dueDate 早于今天 | `isOverdue()==true` |
| TC-04 | 集成 | 启动空目录程序 | 自动创建 data/library.db、4 张表、2 个视图与演示数据 |
| TC-05 | 集成 | 插入重复学号 | 抛出 DbException 或业务预检拦截 |
| TC-06 | 业务 | 对已停用学生借书 | BusinessException |
| TC-07 | 业务 | 同一学生借满 5 册后再次借书 | BusinessException |
| TC-08 | 业务 | 对已借出图书借书 | BusinessException，books 状态不变 |
| TC-09 | 业务 | 借书后归还 | 记录状态=已还，图书状态=在馆，统计条同步 |
| TC-10 | 业务 | 存在逾期记录的学生借书 | BusinessException |
| TC-11 | 界面 | 四个页签切换、新增/编辑/删除/取消 | 无崩溃、无乱码、状态栏有反馈 |
| TC-12 | 回归 | 执行 build.bat 后启动程序 | 干净环境可重建，中文显示正常 |

## 9.3 构建与运行步骤

1. 便携工具链解压到 `tools/`（MinGW-w64、CMake、Ninja、FLTK 安装目录、SQLite amalgamation）。
2. 使用 CMake 以 Ninja 生成器编译 FLTK 静态库并安装到 `tools/fltk-install`。
3. 执行 `build.bat`：配置项目 → 编译 `sqlite3` 静态库与 6 个源文件 → 链接生成 `build/LibraryApp.exe`。
4. 运行 exe，程序自动创建 `data/library.db` 并加载演示数据。
5. 交付时保留 `build/LibraryApp.exe` 与 `data/`，其余构建中间产物可清理。

# 10 附录：类到文件对照表

| 类名 | 声明文件 | 实现文件 |
| --- | --- | --- |
| 枚举、工具函数、异常、Entity、Book、Student、Bookshelf、BorrowRecord | include/domain.h | src/domain.cpp |
| Database、Statement、Transaction、DbInitializer | include/database.h | src/database.cpp |
| Dao<T>、四个 DAO、LoanDetail、ShelfStat | include/daos.h | src/daos.cpp |
| Statistics、LibraryService | include/service.h | src/service.cpp |
| UiUtils、DialogBase、三个 Dialog、四个 Panel、MainWindow | include/ui.h | src/ui.cpp |
| main() | — | src/main.cpp |

本详细设计与源代码、概要设计三者使用同一套类名、字段名与表名；任何实现期调整必须同步更新三处，保持可追溯性。
