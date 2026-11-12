# 1 导读总览

## 1.1 编写目的

本文档是「简易图书借阅系统」的**项目源码导读**，面向需要阅读、评审、维护或二次开发本项目的读者（课程设计答辩、代码评审、后续开发）。它不是设计说明书的复述，而是以「源码文件 → 类 / 函数 → 关键算法 → 调用链」为主线，把三套源码串起来：

1. **本项目自研源码**：`include/`（6 个头文件）与 `src/`（8 个实现文件）、`tests/`、构建与脚本；
2. **SQLite 3.54.1 源码**：`tools/sqlite-amalgamation-3540100/`，本项目通过它实现全部持久化；
3. **FLTK 1.4.5 源码**：`tools/fltk-1.4.5/`，本项目通过它实现全部图形界面。

阅读本文档后，应能回答：一次点击「借出」从 `Fl_Widget` 回调一路走到 `sqlite3_step()` 经过哪些源码；为什么借书必须包在事务里；中文为什么能正常显示；要改一个字段或加一个页签应改哪些文件。

## 1.2 三层源码地图

| 层次 | 源码位置 | 体量（实测） | 在本项目中的角色 |
| --- | --- | --- | --- |
| 自研应用 | `include/` 6 文件、`src/` 8 文件、`tests/` 1 文件 | 约 3,667 行（src），头文件 1,702 行（均含逐项注释） | 领域模型、DAO、业务规则、GUI、演示数据生成 |
| SQLite | `tools/sqlite-amalgamation-3540100/` | `sqlite3.c` 271,131 行 / 9.58 MB；`sqlite3.h` 14,447 行；另含 `shell.c`、`sqlite3ext.h` | 编译为静态库 `sqlite3`，提供 C API |
| FLTK | `tools/fltk-1.4.5/` | `FL/` 166 文件、`src/` 260 文件、WinAPI 驱动 10 文件、GDI 驱动 14 文件、内置 zlib/png/jpeg 139 文件 | 静态编译为 `libfltk.a` 等，提供窗口、控件与事件循环 |
| 构建工具 | `tools/msys2root/mingw64/`、`tools/cmake-3.31.6-windows-x86_64/` | 便携 GCC 16.2 / CMake 3.31 / Ninja | 编译上述三套源码，不写入系统 PATH |

三者由 `CMakeLists.txt` 组装：`lls_core` 静态库聚合自研核心层，`LibraryApp` 链接 `lls_core + fltk::fltk`，`DemoDataTool` 只链接 `lls_core`，而 `lls_core` 通过 `sqlite3` 目标连到 SQLite amalgamation。

## 1.3 关于「包含 SQLite 与 FLTK 源码」的处理方式

用户要求导读中包含这两个第三方库的源码。考虑到实际体量：`sqlite3.c` 单文件 27 万行、9.58 MB，FLTK 源码树数百个文件、数万行，若逐字排入 Word 将产生数千页、数十 MB 的文档，无法阅读也无法分发。因此本文档采用**「全文索引 + 逐字关键摘录 + 模块级源码解读」**的方式包含它们：

- **全文随项目交付**：两库完整源码原样保留在 `tools/` 下，可直接打开、检索、设断点；
- **精确索引**：附录 B、C 给出关键文件的路径、行数与建议阅读入口，正文各章给出精确到行号的定位（例如 `sqlite3.c:94224` 是 `sqlite3_step` 实现、`Fl.cxx:651` 是 `Fl::run`）；
- **逐字摘录**：第 8、9 章引用 SQLite 与 FLTK 的真实源码片段（结构体、API、事件循环、消息分发、组事件处理），并逐行解释；
- **调用链贯通**：第 10 章把本项目源码与两库源码放在同一条调用链上，明确「本项目哪一行调用了第三方哪个函数」。

若需要把两库全文另做成独立的源码合订文件（TXT / HTML），可在本文档之外继续生成，不影响本导读的完整性。

## 1.4 建议阅读顺序

| 阶段 | 读什么 | 目标 |
| --- | --- | --- |
| ① 骨架 | `main.cpp`、`CMakeLists.txt` | 明白程序如何启动、三套源码如何链接 |
| ② 领域 | `include/domain.h`、`src/domain.cpp` | 掌握四类实体、枚举、异常与日期算法 |
| ③ 数据 | `include/database.h`、`include/daos.h`、`src/database.cpp`、`src/daos.cpp` | 掌握 RAII、SQL、行映射、DDL |
| ④ 业务 | `src/service.cpp` | 掌握借书/还书事务与统计 |
| ⑤ 界面 | `include/ui.h`、`src/ui.cpp` | 掌握控件树、回调、列表渲染 |
| ⑥ 第三方深读 | 第 8、9 章 + 附录 B、C | 理解 SQLite 执行引擎与 FLTK 事件循环 |
| ⑦ 贯通 | 第 10 章 | 把一次点击还原为端到端调用链 |

# 2 构建系统与源码组织

## 2.1 项目目录树

```text
E:/githubp/s4 -cpp-FLTK/
├─ CMakeLists.txt              构建根：sqlite3 / lls_core / LibraryApp / DemoDataTool
├─ build.bat                   一键构建（先编 FLTK，再编本项目）
├─ generate_demo.bat           一键重建 1000/100/500 演示数据
├─ run_tests.bat               一键编译并运行冒烟测试
├─ include/                    6 个头文件：domain/database/daos/service/ui/demo_data
├─ src/                       8 个实现文件（共 3,667 行，含详细注释）
├─ tests/smoke_test.cpp        16 条自动化用例
├─ docs/                       概要设计、详细设计、本源码导读
├─ data/                       library.db、备份库、演示数据报告
├─ build/cmake/                LibraryApp.exe、DemoDataTool.exe
├─ tools/
│  ├─ sqlite-amalgamation-3540100/   SQLite 3.54.1 完整源码
│  ├─ fltk-1.4.5/                    FLTK 1.4.5 完整源码
│  ├─ fltk-install/                  静态编译后的 FLTK 头文件与库
│  ├─ msys2root/mingw64/             便携 MinGW-w64 工具链
│  └─ cmake-3.31.6-windows-x86_64/   便携 CMake
└─ output/                     文档流水线中间产物
```

## 2.2 CMakeLists.txt 逐段解读

```cmake
add_library(sqlite3 STATIC "${SQLITE_DIR}/sqlite3.c")   # ① SQLite 全文编译为静态库
target_include_directories(sqlite3 PUBLIC "${SQLITE_DIR}")

add_library(lls_core STATIC                              # ② 自研核心层聚合
  src/domain.cpp src/database.cpp src/daos.cpp
  src/service.cpp src/demo_data.cpp)
target_link_libraries(lls_core PUBLIC sqlite3)

find_package(FLTK CONFIG REQUIRED)                       # ③ 导入本地编译的 FLTK
add_executable(LibraryApp WIN32 src/main.cpp src/ui.cpp)
target_link_libraries(LibraryApp PRIVATE lls_core fltk::fltk)

add_executable(DemoDataTool src/demo_tool_main.cpp)      # ④ 无界面数据工具
target_link_libraries(DemoDataTool PRIVATE lls_core)
target_link_options(LibraryApp DemoDataTool PRIVATE -static)  # ⑤ 全静态，零 DLL 依赖
```

| 段落 | 设计意图 |
| --- | --- |
| ① | amalgamation 只有一个翻译单元，直接编译进 exe，避免运行时 `sqlite3.dll` |
| ② | `lls_core` 让 GUI 与数据工具共享同一份业务代码，避免重复编译与逻辑分叉 |
| ③ | `fltk::fltk` 是 FLTK 安装时导出的 CMake 目标，自动传递 include 路径与 Windows 系统库 |
| ④ | `DemoDataTool` 不链接 FLTK，可在无显示器环境生成数据 |
| ⑤ | `-static` 把 libstdc++/libgcc/winpthread 一并静态链接，程序目录可整体拷贝 |

## 2.3 三个批处理脚本

| 脚本 | 作用 | 关键命令 |
| --- | --- | --- |
| `build.bat` | 首次构建：CMake 配置 FLTK（`-DFLTK_BUILD_SHARED_LIBS=OFF` 等）→ `cmake --build ... --target install` → 配置并编译本项目 | `cmake -S ... -G Ninja`、`cmake --build` |
| `generate_demo.bat` | 重建演示数据并显示报告 | `DemoDataTool.exe` + `type data\demo_data_report.txt` |
| `run_tests.bat` | 独立编译并运行核心层测试 | `gcc -c sqlite3.c`、`g++ ... smoke_test.cpp ... -static` |

## 2.4 便携依赖树

| 组件 | 版本 | 位置 | 是否运行时依赖 |
| --- | --- | --- | --- |
| MinGW-w64 GCC | 16.2.0 (UCRT) | `tools/msys2root/mingw64/bin` | 否（仅构建） |
| CMake / Ninja | 3.31.6 / 1.13 | `tools/cmake-.../bin`、`tools/msys2root/mingw64/bin` | 否 |
| FLTK | 1.4.5 静态 | `tools/fltk-install/lib/libfltk.a` | 否（静态进 exe） |
| SQLite | 3.54.1 | `tools/sqlite-amalgamation-3540100/sqlite3.c` | 否（静态进 exe） |

# 3 领域模型层源码导读

## 3.1 include/domain.h 的组织

`domain.h`（486 行，含逐项 Doxygen 注释）是唯一不依赖其它层的头文件，按「枚举 → 工具函数 → 异常族 → Entity 抽象基类 → 四个派生实体」排列。所有实体属性私有、setter 内执行 `trim()`，getter 返回 `const std::string&` 或值类型，体现封装；头文件本身即为一份可读的领域模型说明书。

| 类型 | 关键成员 / 方法 | 源码导读要点 |
| --- | --- | --- |
| `BookStatus` / `LoanStatus` / `LoanFilter` | 枚举值 0..3 / 0..2 / 0..2 | 整数值与数据库列严格对应，DAO 用 `statusFromInt` 安全转换 |
| `Entity` | `id_`、`virtual validate()`、`virtual toRow()`、`virtual tableName()` | 运行时多态的两个落点；`LibraryService::checkEntity(const Entity&)` 只依赖基类引用 |
| `Book` | isbn / title / author / publisher / publishYear / category / shelfId / status / addedAt | `markBorrowed()` 只做状态守卫，不触碰数据库 |
| `Student` | studentNo / name / major / phone / maxBorrow / active | `canBorrowMore(int)` 把「业务规则」留在领域对象内 |
| `Bookshelf` | code / location / category / capacity / remark / `vector<int> bookIds_` | 内存聚合藏书 ID；真实归属由 `books.shelf_id` 持久化 |
| `BorrowRecord` | studentId / bookId / borrowDate / dueDate / returnDate / status / remark | `isOverdue(day)`、`close(day)` 实现状态迁移与幂等保护 |
| 异常族 | `AppException` → `ValidationException` 等 4 类 | 统一继承 `std::runtime_error`，UI 只需捕获 `AppException` |

## 3.2 src/domain.cpp 关键算法：日期归一化

```cpp
std::string addDays(const std::string& date, int days) {
    std::tm value{};
    if (!parseDate(date, value)) return date;   // 长度/格式/范围三重校验
    value.tm_mday += days;                      // 允许出现 "32 日" 等越界值
    value.tm_isdst = -1;
    std::time_t stamp = std::mktime(&value);    // 标准库完成跨月/跨年/闰年归一化
    std::tm normalized{};
    normalized = *std::localtime(&stamp);
    return formatDate(normalized);              // 输出定长 YYYY-MM-DD
}
```

**导读要点**：全系统日期统一为定长 `YYYY-MM-DD`，因此 `isBefore(a,b)` 可直接用字符串字典序比较，等价于时间先后；`addDays` 把复杂的闰年/月末规则交给 `mktime`，既避免自造轮子，也保证与 SQLite `date('now','localtime','+30 day')` 的口径一致。该函数被 `service.cpp` 的借书流程（计算应还日）与 `demo_data.cpp`（生成历史数据）共同复用。

## 3.3 不变式校验的 fail-fast 结构

```cpp
std::string Book::validate() const {
    if (isBlank(isbn_)) return "ISBN 不能为空";
    if (isBlank(title_)) return "题名不能为空";
    if (publishYear_ < 0) return "出版年不能为负数";
    if (publishYear_ != 0 && (publishYear_ < 1450 || publishYear_ > currentYear()))
        return "出版年应在 1450 至今年之间";
    if (shelfId_ <= 0) return "必须选择所属书架";
    return "";                                  // 空串代表合法
}
```

领域层**只返回错误文案、不抛异常**；由 `LibraryService::checkEntity()` 统一转成 `ValidationException`，由 `BookDialog::collect()` 统一转成弹窗。这样同一套校验规则在 GUI、CLI 数据工具、单元测试三处都能复用。

# 4 数据访问层源码导读

## 4.1 include/database.h：三个 RAII 类型

| 类型 | 构造 | 析构 | 设计要点 |
| --- | --- | --- | --- |
| `Database` | `sqlite3_open_v2` + 三个 PRAGMA | `sqlite3_close` | 独占句柄、禁止拷贝；`prepare()` 按值返回 Statement |
| `Statement` | `sqlite3_prepare_v2` | `sqlite3_finalize` | 支持移动语义；`reset()` 复用预编译语句 |
| `Transaction` | `BEGIN IMMEDIATE` | 未提交则 `ROLLBACK` | 异常栈展开自动回滚；析构不抛异常 |

## 4.2 src/database.cpp：连接与事务（源码摘录）

```cpp
void Database::open(const std::string& path) {
    close();                                     // 幂等
    std::filesystem::create_directories(file.parent_path(), ec);
    sqlite3* handle = nullptr;
    int rc = sqlite3_open_v2(path.c_str(), &handle,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) { ... throw DbException(...); }
    db_ = handle;
    exec("PRAGMA foreign_keys = ON;");           // 外键逐连接开启
    exec("PRAGMA busy_timeout = 5000;");
    exec("PRAGMA journal_mode = WAL;");          // 读写并发
}

Transaction::~Transaction() {
    if (!committed_) {
        try { db_.exec("ROLLBACK;"); } catch (...) { /* 析构不抛异常 */ }
    }
}
```

**导读要点**：`BEGIN IMMEDIATE` 提前取得写锁，避免借书时「先读库存、后写记录」之间被其它连接插入；析构中的 `try/catch(...)` 是异常安全的必要措施，否则栈展开期间的二次异常会直接 `terminate`。

## 4.3 DDL 与视图（src/database.cpp 的 kSchema 常量）

```sql
CREATE TABLE books (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    isbn TEXT NOT NULL, title TEXT NOT NULL,
    shelf_id INTEGER NOT NULL REFERENCES bookshelves(id) ON DELETE RESTRICT,
    status INTEGER NOT NULL DEFAULT 0 CHECK (status IN (0,1,2,3)), ...);

CREATE UNIQUE INDEX IF NOT EXISTS uq_active_loan
    ON borrow_records(book_id) WHERE status IN (0,2);   -- 部分唯一索引

CREATE VIEW IF NOT EXISTS v_loan_detail AS
SELECT r.id AS record_id, ..., s.name AS student_name, b.title, sh.code AS shelf_code
FROM borrow_records r
JOIN students s ON s.id = r.student_id
JOIN books b ON b.id = r.book_id
LEFT JOIN bookshelves sh ON sh.id = b.shelf_id;
```

| 结构 | 为什么这样设计 |
| --- | --- |
| `ON DELETE RESTRICT` | 删除被引用记录时由数据库兜底，业务层另做预检给出中文提示 |
| `CHECK (status IN ...)` | 枚举值在数据库层再校验一次，防止脏数据 |
| `uq_active_loan` | **部分唯一索引**只约束未归还记录，是「同一册书不可重复借出」的最后防线 |
| `v_loan_detail` | 一次 JOIN 出借阅页全部展示列，避免界面层 N+1 查询 |
| `v_shelf_stat` | `LEFT JOIN + GROUP BY` 聚合每架藏书数与在馆数（含 `remark` 列保持行完整） |

`kSchema` 全部使用 `IF NOT EXISTS`，因此 GUI 每次启动、数据工具每次运行都可安全调用 `createSchema()`。

## 4.4 include/daos.h：Dao 模板与四个派生类

```cpp
template <typename T>
class Dao {
protected:
    Database& db_;
    static void requirePersisted(int id) { if (id <= 0) throw ValidationException(...); }
public:
    explicit Dao(Database& db) : db_(db) {}
    virtual ~Dao() = default;
    virtual int insert(const T&) = 0;
    virtual void update(const T&) = 0;
    virtual bool removeById(int) = 0;
    virtual T findById(int) const = 0;
    virtual std::vector<T> listAll() const = 0;
};
```

模板基类给出统一契约与公共守卫，具体 SQL 由 `BookDao / StudentDao / BookshelfDao / BorrowRecordDao` 提供；`LoanDetail`（借阅明细）与 `ShelfStat`（书架统计）是只读展示结构，供界面直接消费。

## 4.5 src/daos.cpp：列清单常量、映射函数与筛选算法

```cpp
const char* const kBookColumns =
    "SELECT b.id,b.isbn,b.title,b.author,b.publisher,b.publish_year,"
    "b.category,b.shelf_id,b.status,b.added_at FROM books b ";

Book mapBook(Statement& s) {
    Book book;
    book.setId(s.columnInt(0));  book.setIsbn(s.columnText(1));  /* ... */
    book.setStatus(Book::statusFromInt(s.columnInt(8)));
    return book;
}

std::vector<LoanDetail> BorrowRecordDao::listDetails(LoanFilter filter) const {
    std::string sql = kDetailColumns;
    switch (filter) {
        case LoanFilter::Active:  sql += "WHERE status IN (0,2) "; break;
        case LoanFilter::Overdue: sql += "WHERE status<>1 AND due_date < date('now','localtime') "; break;
        default: break;
    }
    sql += "ORDER BY borrow_date DESC, record_id DESC;";
    ...
}
```

| 算法点 | 说明 |
| --- | --- |
| 列清单常量 | 所有查询拼「列清单 + WHERE/ORDER BY」后缀；**列序即契约**，改 SELECT 必须同步改 `mapXxx` |
| 参数绑定 | 用户输入一律 `?` 绑定；`bindText` 用 `SQLITE_TRANSIENT` 让 SQLite 复制字符串 |
| 模糊检索 | `%kw%` 绑定三次；空关键字由业务层分流到 `listAll()`，避免全表 LIKE |
| 逾期筛选 | 用 SQLite `date('now','localtime')` 与领域层 `today()` 保持同一时区口径 |
| 幂等批量更新 | `markOverdue` 只处理 `status=0 AND due_date < ?`，可安全重复执行 |

## 4.6 本项目 → SQLite C API 对照表

| 本项目调用点 | 使用的 SQLite C API | SQLite 源码实现位置 |
| --- | --- | --- |
| `Database::open` | `sqlite3_open_v2` / `sqlite3_close` | `sqlite3.c:192111` / `sqlite3.c:189735` |
| `Database::exec` | `sqlite3_exec` / `sqlite3_free` | `sqlite3.c:142838` |
| `Database::prepare` | `sqlite3_prepare_v2` | `sqlite3.c:149388`（声明 `sqlite3.h:4651`） |
| `Statement::bindInt/Text/Null` | `sqlite3_bind_int/text/null` | `sqlite3.c:95104 / 95178 / 95149` |
| `Statement::step` | `sqlite3_step` → `sqlite3Step` → `sqlite3VdbeExec` | `sqlite3.c:94224` / `97179` |
| `Statement::columnText/Int` | `sqlite3_column_text/int` | `sqlite3.c:94717` 附近 |
| `Statement::~Statement` | `sqlite3_finalize` | `sqlite3.c:93349` |
| `Database::changes` | `sqlite3_changes` | `sqlite3.c:189530` |

这张表是第 8 章深入 SQLite 源码的入口：从任一项目调用点，都能直接跳到 amalgamation 中的具体实现行。

# 5 业务逻辑层源码导读

## 5.1 include/service.h：门面接口

`LibraryService` 是界面层唯一可调用的下层类型，构造时接收 `Database&` 并把同一连接注入四个 DAO。除 CRUD 与查询外，关键接口有：`borrowBook(int,int)`、`returnBook(int)`、`listLoans(LoanFilter)`、`statistics()`、`refreshOverdue()`、`activeLoanCount(int)`。`Statistics` 是 6 个计数的纯数据结构，`summary()` 负责格式化统计条。

## 5.2 核心源码：borrowBook 的校验链与事务（src/service.cpp）

```cpp
int LibraryService::borrowBook(int studentId, int bookId) {
    Student student = studentDao_.findById(studentId);          // ① 取学生
    if (!student.active()) throw BusinessException("该学生已停用，不能借书");
    const std::string day = today();
    if (loanDao_.hasOverdue(studentId, day))                    // ② 逾期禁借（直接比 due_date）
        throw BusinessException("该生存在逾期未还图书，请先归还");
    const int active = loanDao_.countActiveByStudent(studentId);
    if (!student.canBorrowMore(active))                          // ③ 规则留在领域对象内
        throw BusinessException("已达可借上限 ...");
    Book book = bookDao_.findById(bookId);
    if (!book.isAvailable()) throw BusinessException("该图书当前不可借出");  // ④ 库存守卫

    BorrowRecord record;                                         // ⑤ 组装单据
    record.setBorrowDate(day);
    record.setDueDate(addDays(day, 30));                         // 借期 30 天
    checkEntity(record);                                         // 多态校验

    Transaction transaction(db_);                                // ⑥ BEGIN IMMEDIATE
    const int recordId = loanDao_.insert(record);                // 写借阅记录
    bookDao_.updateStatus(bookId, BookStatus::Borrowed);         // 同步库存状态
    transaction.commit();                                        // ⑦ COMMIT
    return recordId;
}
```

| 步骤 | 导读要点 |
| --- | --- |
| ①～④ | 全部校验在**开事务之前**完成，减少无效事务与锁占用时间 |
| ② | `hasOverdue` 用 `due_date < day` 实时判断，不依赖批量逾期标记是否执行过 |
| ③ | 「取数」在 DAO、「判定」在领域对象，职责分离 |
| ⑤ | 应还日由 `addDays()` 计算，与 SQLite `date()` 生成的演示数据口径一致 |
| ⑥～⑦ | 两步写操作必须在同一事务；任一步抛异常，栈展开时 `Transaction` 析构自动 `ROLLBACK`，杜绝「有单据但书仍在馆」 |

`returnBook()` 是镜像流程：`findById` → 拒绝重复归还 → 事务内 `loanDao_.close()` + `bookDao_.updateStatus(Available)` → 提交。

## 5.3 统计与删除预检算法

`statistics()` 用**一条 SQL 的 6 个标量子查询**一次取回藏书/在馆/借出/逾期/学生/书架数，比 6 条独立 COUNT 少 5 次语句编译；逾期数用 `status<>1 AND due_date < date('now','localtime')` 实时计算，即使尚未执行批量标记也准确。

三个删除方法都遵循「业务预检 → 中文提示 → 数据库 RESTRICT 兜底」：`removeBook` 查 `activeForBook`，`removeStudent` 查 `countActiveLoans`，`removeShelf` 查 `countBooks`。

# 6 表现层源码导读

## 6.1 include/ui.h 的类清单

| 类 | 基类 | 职责 |
| --- | --- | --- |
| `UiUtils` | — | 弹窗、控件文本读取、列表配置与行拼接、选中 ID 解析 |
| `DialogBase` | `Fl_Window` | 模态事件循环 `exec()`、`confirmed_` 标记 |
| `BookDialog / StudentDialog / ShelfDialog` | `DialogBase` | 采集并回填实体，`collect()` 内调 `validate()` |
| `BookPanel / StudentPanel / ShelfPanel` | `Fl_Group` | 工具栏 + `Fl_Hold_Browser` 列表 + 增删改 |
| `LoanPanel` | `Fl_Group` | 两个 `Fl_Choice`、筛选、借出 / 还书 |
| `MainWindow` | `Fl_Double_Window` | 统计条、`Fl_Tabs`、状态栏、`refreshAll()` |

`ui.h` 是唯一包含 FLTK 头文件的项目头文件；Service 与 DAO 完全不感知 GUI。

## 6.2 控件树与 FLTK 父子机制（src/ui.cpp）

```cpp
MainWindow::MainWindow(int w, int h, const char* title, LibraryService& svc)
    : Fl_Double_Window(w, h, title), svc_(svc) {
    topBar_ = new Fl_Box(10, 10, 760, 30, "");        // 统计条
    tabs_   = new Fl_Tabs(10, 52, 1004, 570);         // 自动 begin()
    bookPanel_    = new BookPanel(10, 82, 1004, 535, svc_, this);
    studentPanel_ = new StudentPanel(...);
    shelfPanel_   = new ShelfPanel(...);
    loanPanel_    = new LoanPanel(...);               // 各 Panel 构造末尾 end()
    tabs_->end();                                    // 结束 Tabs 组
    statusBar_ = new Fl_Box(10, 630, 1004, 36, "就绪");
    end();                                           // 结束主窗口组
    resizable(tabs_);
    refreshAll();
}
```

**导读要点**：FLTK 的 `Fl_Group` 构造会自动 `begin()`，之后 `new` 的控件自动成为其子控件；构造末尾必须 `end()` 把「当前组」还给父级。控件由父子关系托管生命周期，所以项目代码只 `new` 不 `delete`。四个 Panel 的坐标与 `Fl_Tabs` 内容区对齐，label 即页签标题。

## 6.3 列表渲染与「选中行 → 主键」算法

```cpp
void UiUtils::setupBrowser(Fl_Hold_Browser* browser, const int* widths) {
    browser->column_widths(widths);   // 成员数组保存，指针必须长期有效
    browser->column_char('\t');       // 制表符分列，'@' 仍为格式符
    browser->textsize(13);
}

int UiUtils::selectedId(Fl_Hold_Browser* browser) {
    int line = browser->value();                    // 行号从 1 开始
    if (line <= 1) return 0;                        // 第 1 行恒为表头
    std::string row(browser->text(line));
    auto tab = row.find('\t');
    return std::stoi(row.substr(0, tab));            // 首列恒为数据库主键
}
```

| 约定 | 原因 |
| --- | --- |
| 首列必须是 ID | 编辑 / 删除 / 还书都通过 `selectedId()` 把界面选中行还原为数据库主键 |
| 表头用 `@b` 前缀 | FLTK 行内格式符实现加粗；用户数据中的 `@` 必须用 `@@` 转义 |
| 制表符与换行替换为空格 | 防止用户数据破坏列分隔造成错位 |
| `clean()` 统一清洗 | 保证 1000 册演示数据中含特殊字符时列表仍对齐 |

## 6.4 回调统一异常边界与模态循环

```cpp
template <typename Panel, void (Panel::*Method)()>
void invoke(Fl_Widget*, void* data) {
    Panel* self = static_cast<Panel*>(data);
    try { (self->*Method)(); }
    catch (const std::exception& e) { UiUtils::error(e.what()); self->status(e.what()); }
}

bool DialogBase::exec() {
    show();
    while (shown()) Fl::wait();     // 局部事件循环，按钮 hide() 后退出
    return confirmed_;
}
```

`invoke` 模板用「非类型模板参数（成员函数指针）」把 20 多个按钮回调的 `try/catch` 收敛到一处，保证异常绝不穿透 FLTK 回调；`DialogBase::exec()` 是 FLTK 官方推荐的模态实现，与第 9 章的 `Fl::wait()` 直接衔接。

## 6.5 src/main.cpp：字体与入口包装

```cpp
Fl::set_font(FL_HELVETICA, "Microsoft YaHei");   // 四个变体全部映射中文字体
std::filesystem::create_directories("data");
Database database("data/library.db");             // 栈对象，RAII
LibraryService service(database);
service.initialize();                            // 建表 + 播种 + 逾期刷新
MainWindow window(1024, 680, "...", service);
window.show(argc, argv);
return Fl::run();                                 // 进入 FLTK 事件循环
```

`WinMain` 包装（Windows GUI 子系统入口）转调 `main(__argc, __argv)`，同时支持 `--generate-demo` 无界面模式。中文字体映射必须在控件创建之前完成，这是「界面不出现方框」的关键一行。

# 7 演示数据生成器与测试源码导读

## 7.1 src/demo_data.cpp 的生成算法（摘录）

```cpp
const int currentLoans = std::min(loans * 2 / 5, books);   // 500 → 200 条当前未还
const int step = std::max(1, books / currentLoans);        // 1000/200 = 5
for (int k = 0; k < currentLoans; ++k) {
    int id = 1 + (k * step) % books;                       // 1,6,11,... 互不重复
    reserved.insert(id); currentBooks.push_back(id);
}
std::vector<char> isBorrowed(books + 1, 0);
for (int id : currentBooks) isBorrowed[id] = 1;            // 这些书 status=1

auto pickStudent = [&](int seed) {                          // 轮转 + 上限检查
    for (int t = 0; t < students; ++t) {
        int id = 1 + (seed + t) % students;
        if (used[id] < maxBorrow[id]) { ++used[id]; return id; }
    }
    return 1;
};

Transaction transaction(db_);                              // 单事务重建
DELETE borrow_records → books → students → bookshelves（外键顺序）
插入书架(20) → 学生(100) → 图书(1000) → 借阅(500)         // 每条 Statement 复用 reset()
transaction.commit();
```

| 设计点 | 说明 |
| --- | --- |
| 确定性公式 | 不使用随机数，同一天重复生成结果一致，便于演示与回归 |
| 位图 `isBorrowed` | 保证 `books.status` 与未归还记录严格对应 |
| 部分唯一索引兼容 | 历史记录只从「未被当前借出」的图书中取，避免 `uq_active_loan` 冲突 |
| 上限约束 | `pickStudent` 保证任一学生当前借阅数不超过 `max_borrow`（实测最多 2 册 / 上限 3～8） |
| 事务 | 清空与四类插入全部原子化，失败保持原库 |

## 7.2 EAN-13 与一致性自检

`makeIsbn()` 用 1/3 交替权重求前 12 位加权和，校验位 = `(10 - sum%10)%10`；`makeTitle()` 用 40 主题 × 5 变体 × 5 分册组合出 1000 个互异题名。`verify()` 用 4 条「反例计数 SQL」加 `PRAGMA foreign_key_check` 检查库存一致性、可借上限、逾期日期与外键，结果写入 `data/demo_data_report.txt`（当前 5/5 PASS）。

`src/demo_tool_main.cpp` 是控制台入口：`SetConsoleOutputCP(CP_UTF8)` 解决中文乱码，退出码 0/1/2 分别表示成功、异常、校验失败。

## 7.3 tests/smoke_test.cpp

16 条用例覆盖 TC-01～TC-12：实体验证、日期计算、建库、学号唯一、停用/逾期/上限/重复借出拦截、还书恢复库存、删除预检、检索与统计。它使用独立的 `build/test_library.db`，不会污染正式演示库。

# 8 SQLite 3.54.1 源码导读

## 8.1 amalgamation 是什么

`tools/sqlite-amalgamation-3540100/` 内含 4 个文件，是 SQLite 官方发布的「合并版」源码：把上百个独立 C 文件按依赖顺序拼接成单个 `sqlite3.c`，使本项目只需编译一个翻译单元。

| 文件 | 行数 | 大小 | 作用 |
| --- | ---: | ---: | --- |
| `sqlite3.c` | 271,131 | 9.58 MB | 全部引擎实现 + 内嵌头文件（本项目编译它） |
| `sqlite3.h` | 14,447 | 696 KB | 公开 C API、结构体前向声明、错误码常量 |
| `sqlite3ext.h` | 745 | 39 KB | 扩展机制 API（本项目未使用） |
| `shell.c` | 39,939 | 1.27 MB | `sqlite3.exe` 命令行外壳源码（本项目未编译） |

`sqlite3.c` 开头用形如 `/**** Begin file sqliteInt.h ***/` 的分段标记保留原始文件边界，这正是阅读它的「目录」。

## 8.2 sqlite3.c 的模块地图（真实行号）

| 行号 | 合并前文件 | 职责 |
| ---: | --- | --- |
| 31 | `sqliteInt.h` | 内部结构体、宏、跨模块函数声明 |
| 321 | `sqlite3.h` | 公开 API 声明（与独立头文件一致） |
| 14,788 | `sqliteLimit.h` | 各类上限常量 |
| 15,642 | `parse.h` | 语法分析器产生的 token / 语法树定义 |
| 16,635 | `os.h` | VFS（虚拟文件系统）接口 |
| 16,960 | `pager.h` | 页缓存与事务日志接口 |
| 17,232 | `btree.h` | B 树接口 |
| 17,672 | `vdbe.h` | 虚拟机（VDBE）接口 |
| 17,867 | `opcodes.h` | 虚拟机指令集定义 |
| 24,556 | `vdbeInt.h` | VDBE 内部结构 |
| 25,757 | `date.c` | `date('now','localtime',...)` 等日期函数（本项目 DDL 用到） |
| 27,665 | `os.c` | VFS 实现与平台适配 |
| 18,864 | `struct sqlite3` | 连接对象定义 |
| 25,014 | `struct Vdbe` | 预编译语句对象定义 |
| 59,578 | `struct Pager` | 页管理器定义 |
| 72,028 | `struct Btree` | B 树对象定义 |
| 93,349 | `sqlite3_finalize` | 释放语句 |
| 94,224 | `sqlite3_step` | 执行语句 |
| 97,179 | `sqlite3VdbeExec` | 虚拟机主循环（引擎核心） |
| 142,838 | `sqlite3_exec` | 便捷执行入口 |
| 149,388 | `sqlite3_prepare_v2` | SQL 编译入口 |
| 189,530 | `sqlite3_changes` | 影响行数 |
| 192,111 | `sqlite3_open_v2` | 打开连接 |

## 8.3 分层架构：从 SQL 文本到磁盘页

```text
应用层（本项目 Database / Statement / DAO）
        │  sqlite3_prepare_v2 / bind_* / step / column_*
        ▼
SQL 接口层        sqlite3_prepare_v2 → sqlite3RunParser → sqlite3CodeRunJustOnce
        ▼
SQL 编译器        tokenizer（parse.h / tokenize.c）→ parser → code generator（expr.c / select.c / insert.c）
        ▼
虚拟机 VDBE       struct Vdbe（25,014）→ sqlite3VdbeExec（97,179）逐条执行 opcode
        ▼
B-Tree 层         struct Btree（72,028）→ 定位记录、游标、索引（含 uq_active_loan）
        ▼
Pager 层          struct Pager（59,578）→ 页缓存、事务、WAL、回滚日志
        ▼
OS / VFS 层       os.c（27,665）+ Windows 文件 API → data/library.db 文件
```

本项目开启的 `PRAGMA journal_mode = WAL` 与 `BEGIN IMMEDIATE` 分别作用在 Pager 层与事务层；`PRAGMA foreign_keys = ON` 在编译/执行 `DELETE` 时由引擎追加隐式检查，这正是「删除被引用记录会失败」的底层原因。

## 8.4 公开头文件的关键声明（sqlite3.h）

```c
typedef struct sqlite3 sqlite3;              /* sqlite3.h:275  连接句柄（不透明） */
typedef struct sqlite3_stmt sqlite3_stmt;    /* sqlite3.h:4340 预编译语句（不透明） */
SQLITE_API int sqlite3_prepare_v2(...);      /* sqlite3.h:4651 */
SQLITE_API int sqlite3_step(sqlite3_stmt*);  /* sqlite3.h:5361 */
```

「不透明结构体」是 SQLite 的关键封装手法：调用者只持有指针，内部字段定义藏在 `sqlite3.c` 内；本项目 `database.h` 也用 `struct sqlite3;` 前向声明保持同样的封装风格。

## 8.5 逐字源码摘录：sqlite3_step（sqlite3.c:94224）

```c
SQLITE_API int sqlite3_step(sqlite3_stmt *pStmt){
  int rc = SQLITE_OK;      /* Result from sqlite3Step() */
  Vdbe *v = (Vdbe*)pStmt;  /* the prepared statement */
  int cnt = 0;             /* Counter to prevent infinite loop of reprepares */
  sqlite3 *db;             /* The database connection */

  if( vdbeSafetyNotNull(v) ){
    return SQLITE_MISUSE_BKPT;
  }
  db = v->db;
  sqlite3_mutex_enter(db->mutex);
  while( (rc = sqlite3Step(v))==SQLITE_SCHEMA
         && cnt++ < SQLITE_MAX_SCHEMA_RETRY ){
    int savedPc = v->pc;
    rc = sqlite3Reprepare(v);      /* 表结构变更时自动重新编译 */
    ...
    sqlite3_reset(pStmt);
    ...
  }
  ...
}
```

**逐行解读**：

1. `sqlite3_stmt*` 被强转为内部 `Vdbe*`——这就是「不透明结构体」背后的真实类型，也解释了为何 `Statement::~Statement` 必须调用 `sqlite3_finalize` 释放它；
2. `sqlite3_mutex_enter(db->mutex)` 保证同一连接上的语句执行互斥；
3. `while` 处理 `SQLITE_SCHEMA`：若表结构在执行期间变化（例如另一连接建表），自动 `sqlite3Reprepare` 重新编译并 `sqlite3_reset`，`cnt` 防止无限重试；
4. 真正逐条执行字节码的是更底层的 `sqlite3Step()` → `sqlite3VdbeExec()`（`sqlite3.c:97179`）；
5. 本项目 `Statement::step()` 把 `SQLITE_ROW` 映射为 `true`、`SQLITE_DONE` 映射为 `false`、其余状态码连同 `sqlite3_errmsg` 抛成 `DbException`——这一层薄封装正是「C API → C++ 异常」的桥。

## 8.6 一次 SQL 在本项目与 SQLite 之间的完整链路

| 序 | 本项目源码 | SQLite 源码 | 说明 |
| --- | --- | --- | --- |
| 1 | `BookDao::findById` 构造 SQL | — | `kBookColumns + WHERE b.id=?` |
| 2 | `Database::prepare` | `sqlite3.c:149388 sqlite3_prepare_v2` | 词法/语法分析、生成 VDBE 字节码 |
| 3 | `Statement::bindInt` | `sqlite3.c:95104 sqlite3_bind_int` | 绑定主键参数 |
| 4 | `Statement::step` | `sqlite3.c:94224 sqlite3_step` → `97179 sqlite3VdbeExec` | 执行字节码，经 Btree/Pager 读取页 |
| 5 | `Statement::columnText/Int` | `sqlite3.c:94717` 附近 | 取当前行各列 |
| 6 | `mapBook(Statement&)` | — | 按列序还原为 C++ 实体 |
| 7 | `Statement` 析构 | `sqlite3.c:93349 sqlite3_finalize` | 释放 VDBE 对象 |

## 8.7 本项目用到的 SQLite 特性对应源码

| 特性 | 项目使用位置 | SQLite 源码模块 |
| --- | --- | --- |
| 外键 `RESTRICT` | `kSchema` 的 4 张表 | `foreign key` 约束检查（VDBE 生成的 `OP_FkCounter` 等指令） |
| 部分唯一索引 | `uq_active_loan ... WHERE status IN (0,2)` | `where.c` / `btree.c` 的索引维护 |
| WAL 日志 | `PRAGMA journal_mode=WAL` | `wal.c` + `pager.c`（struct Pager `sqlite3.c:59578`） |
| 视图 | `v_loan_detail` / `v_shelf_stat` | `select.c` 的视图展开 |
| 日期函数 | `date('now','localtime')` | `date.c`（`sqlite3.c:25757`） |
| 事务 | `Transaction` 的 BEGIN/COMMIT/ROLLBACK | `OP_Transaction` 指令 + Pager 状态机 |

## 8.8 继续深读 SQLite 源码的入口

建议顺序：`sqlite3.h` 的 C API 列表 → `struct sqlite3`（18,864）→ `sqlite3_open_v2`（192,111）→ `sqlite3_prepare_v2`（149,388）→ `sqlite3_step`（94,224）→ `sqlite3VdbeExec`（97,179）→ `struct Btree`（72,028）→ `struct Pager`（59,578）。调试时可在 `Database::prepare` 与 `Statement::step` 两处下断点，再单步进入 amalgamation，观察 SQL 如何变成字节码。

# 9 FLTK 1.4.5 源码导读

## 9.1 源码树与体量（实测）

| 目录 | 文件数 | 内容 |
| ---: | --- | --- |
| `FL/` | 166 | 公开头文件（`Fl_Widget.H`、`Fl_Group.H`、`Fl.H` 等），安装后复制到 `tools/fltk-install/include/FL/` |
| `src/` | 260 | 核心控件与平台无关实现 |
| `src/drivers/WinAPI/` | 10 | Windows 窗口 / 系统驱动（本项目使用） |
| `src/drivers/GDI/` | 14 | Windows GDI 绘制与字体驱动（中文显示关键） |
| `src/xutf8/` | 18 | UTF-8 文本处理 |
| `zlib/ png/ jpeg/` | 34 / 36 / 69 | 内置图像库，静态编译为 `libfltk_z.a` 等 |
| `CMake/` | 21 | CMake 配置模块，`FLTKConfig.cmake` 由这里生成 |
| `documentation/` | 9 | 官方文档源 |

关键文件行数：`FL/Fl_Widget.H` 1,290、`FL/Fl.H` 1,620、`FL/Fl_Group.H` 289、`FL/Fl_Browser_.H` 417、`src/Fl.cxx` 2,516、`src/Fl_Group.cxx` 1,038、`src/Fl_Window.cxx` 1,049、`src/Fl_Browser_.cxx` 1,101、`src/Fl_win32.cxx` 2,940、`src/Fl_Input_.cxx` 1,564、`CMakeLists.txt` 411。

## 9.2 类继承树（本项目涉及的分支）

```text
Fl_Widget                       FL/Fl_Widget.H:112（抽象基类，draw() 纯虚，handle() 虚）
├─ Fl_Group                     FL/Fl_Group.H:56（begin()/end() 管理子控件）
│  ├─ Fl_Window                 FL/Fl_Window.H（顶层窗口）
│  │  ├─ Fl_Double_Window       MainWindow 的基类（双缓冲）
│  │  └─ DialogBase             项目对话框基类（set_modal）
│  ├─ Fl_Tabs                   MainWindow 的页签容器
│  ├─ BookPanel/StudentPanel/ShelfPanel/LoanPanel（项目自定义 Fl_Group）
│  ├─ Fl_Choice                 借阅页下拉框
│  ├─ Fl_Input / Fl_Int_Input   对话框输入控件
│  └─ Fl_Button / Fl_Box / Fl_Check_Button
└─ Fl_Browser_ → Fl_Hold_Browser  FL/Fl_Browser_.H（列宽、格式符、选中行）
```

`Fl_Widget` 的两个虚函数是理解 FLTK 的钥匙：`draw()`（`Fl_Widget.H:258`）负责绘制，`handle(int event)`（`Fl_Widget.H:289`）负责事件。项目中的四个 Panel 继承 `Fl_Group` 而**不重写 draw()**，因为只需组合标准控件；这也是 FLTK「组合优于自定义绘制」的典型用法。

## 9.3 关键源码定位表

| 位置 | 内容 | 与项目的关系 |
| --- | --- | --- |
| `FL/Fl_Widget.H:112` | `class FL_EXPORT Fl_Widget` | 所有控件的基类 |
| `FL/Fl_Widget.H:258 / 289` | `virtual void draw()=0` / `virtual int handle(int)` | 绘制与事件分发的抽象入口 |
| `FL/Fl_Group.H:56 / 90 / 91` | `class Fl_Group`、`begin()`、`end()` | `MainWindow` / 各 Panel 控件树依赖 begin/end |
| `src/Fl.cxx:631` | `double Fl::wait(double)` | 平台事件等待与派发主函数 |
| `src/Fl.cxx:651` | `int Fl::run()` | `main.cpp` 最后一行进入的事件循环 |
| `src/Fl.cxx:672` | `int Fl::wait()` | `DialogBase::exec()` 的模态循环调用它 |
| `src/Fl.cxx:1359` | `int Fl::handle(int, Fl_Window*)` | 把平台事件分发到控件 |
| `src/Fl_Group.cxx:145` | `int Fl_Group::handle(int event)` | 子控件事件分发与焦点导航 |
| `src/Fl_win32.cxx:435 / 1226` | `DispatchMessageW` / `WndProc` | Windows 原生消息进入 FLTK 的入口 |
| `src/Fl_arg.cxx:287` | `Fl_Window::show(int argc, char** argv)` | `main.cpp` 的 `window.show(argc, argv)` |
| `src/fl_ask.cxx`（699 行） | `fl_alert / fl_choice / fl_message` | `UiUtils` 的三个弹窗封装 |
| `src/fl_font.cxx`（71 行）与 GDI 驱动 | `Fl::set_font`、`fl_draw` 字体选择 | 微软雅黑中文显示 |

## 9.4 逐字源码摘录：事件循环（src/Fl.cxx:651）

```cpp
int Fl::run() {
  while (Fl_X::first) wait(FOREVER);   // 只要还有窗口就持续等待/派发事件
  return 0;
}

int Fl::wait() {
  if (!Fl_X::first) return 0;
  wait(FOREVER);
  return Fl_X::first != 0;
}
```

**逐行解读**：`Fl_X::first` 是 FLTK 维护的「顶层窗口链表头」，窗口全部关闭后链表为空，`Fl::run()` 返回，程序结束。`wait(FOREVER)`（`Fl.cxx:631`）内部依次执行：空闲回调 → 定时器 → `Fl::flush()` 重绘 → 等待平台事件 → 把事件交给 `Fl::handle()` → 触发控件回调。本项目 `main.cpp` 的 `return Fl::run();` 就是把控制权永久交给这段源码，直到用户关闭主窗口。

`DialogBase::exec()` 中的 `while (shown()) Fl::wait();` 复用同一个 `Fl::wait()`：这是「模态对话框不阻塞整个程序、却能阻止父窗口输入」的原因——`set_modal()` 由 WinAPI 驱动在窗口层设置，事件循环仍在持续派发。

## 9.5 逐字源码摘录：组事件分发（src/Fl_Group.cxx:145）

```cpp
int Fl_Group::handle(int event) {
  Fl_Widget*const* a = array();
  int i;
  Fl_Widget* o;
  switch (event) {
  case FL_FOCUS:
    switch (navkey()) { ... }                       // 方向键焦点导航
    return 0;
  case FL_KEYBOARD:
    return navigation(navkey());
  case FL_SHORTCUT:
    for (i = children(); i--;) {
      o = a[i];
      if (o->takesevents() && Fl::event_inside(o) && send(o, FL_SHORTCUT))
        return 1;                                   // 子控件已处理
    }
    ...
  }
}
```

**导读要点**：FLTK 采用「事件自顶向下、按子控件数组遍历」的分发模型；`send()` 最终调用控件的 `handle()`，按钮在 `FL_PUSH`/`FL_RELEASE` 时触发注册的 `callback`。项目所有 `static void onXxx(Fl_Widget*, void*)` 回调正是由这条链路调用，因此 `invoke` 模板中的异常边界必须包裹整个回调体。

## 9.6 Windows 平台与中文字体

- `src/Fl_win32.cxx:1226` 的 `WndProc` 接收 Win32 消息（鼠标、键盘、绘制、关闭），`src/Fl_win32.cxx:435` 的 `DispatchMessageW` 是消息泵的一环；
- `src/drivers/WinAPI/`（10 文件）封装窗口创建与系统事件，`src/drivers/GDI/`（14 文件）负责实际绘制与字体；
- `Fl::set_font(FL_HELVETICA, "Microsoft YaHei")` 最终落到 GDI 字体驱动，把 FLTK 的字体编号映射到 Windows 字体；项目启动时对 4 个变体统一映射，保证标题、按钮、列表与数据库中文文本都不出现方框；
- `src/fl_draw.cxx`（781 行）与 `src/fl_labeltype.cxx` 负责文本绘制，`@b` 加粗、`@@` 转义等格式符也在这里解析——这正是 `UiUtils::clean()` 需要转义 `@` 的底层原因。

## 9.7 本项目控件 → FLTK 源码映射

| 项目用法 | FLTK 源码文件 | 关键实现 |
| --- | --- | --- |
| `Fl_Hold_Browser` 列表 | `src/Fl_Browser.cxx`（955）、`src/Fl_Browser_.cxx`（1101） | `column_widths()` / `column_char()` / `add()` / `text(int)` / `value()` |
| `Fl_Choice` 下拉 | `src/Fl_Choice.cxx`（229） | 菜单项管理与 `add()` / `value()` |
| `Fl_Input / Fl_Int_Input` | `src/Fl_Input_.cxx`（1564） | 文本缓冲、数字过滤、`value()` 复制语义 |
| `Fl_Tabs` 页签 | `src/Fl_Tabs.cxx` | 标签绘制与子 Group 切换 |
| `fl_alert / fl_choice / fl_message` | `src/fl_ask.cxx`（699） | 模态消息窗口 |
| `Fl::set_font / Fl::run / Fl::wait` | `src/Fl.cxx`（2516）、`src/fl_font.cxx`（71） | 字体表、事件循环 |

## 9.8 FLTK 的静态构建与本项目链接

`tools/fltk-1.4.5/CMakeLists.txt`（411 行）中 `FLTK_BUILD_SHARED_LIBS`（第 159、325 行附近）决定构建静态或动态库；`build.bat` 传入 `-DFLTK_BUILD_SHARED_LIBS=OFF -DFLTK_BUILD_GL=OFF -DFLTK_BUILD_FORMS=OFF -DFLTK_BUILD_TEST=OFF`，生成并安装：

| 产物 | 作用 |
| --- | --- |
| `tools/fltk-install/lib/libfltk.a` | 核心静态库（`fltk::fltk` 目标） |
| `libfltk_images.a` / `libfltk_png.a` / `libfltk_jpeg.a` / `libfltk_z.a` | 内置图像库（本项目未直接使用，但目标依赖链会传递） |
| `tools/fltk-install/CMake/FLTKConfig.cmake` | 供本项目 `find_package(FLTK CONFIG REQUIRED)` 定位 |
| `tools/fltk-install/include/FL/` | 166 个头文件 + 构建时生成的 `fl_config.h` |

# 10 端到端调用链全景

## 10.1 启动链（跨三套源码）

| 序 | 源码位置 | 动作 |
| --- | --- | --- |
| 1 | `src/main.cpp` `WinMain` → `main` | Windows GUI 入口转调标准入口 |
| 2 | `src/main.cpp` `runApplication` | `Fl::set_font` 设中文字体（FLTK `fl_font.cxx`） |
| 3 | `src/database.cpp` `Database::open` | `sqlite3_open_v2`（`sqlite3.c:192111`）+ 三个 PRAGMA |
| 4 | `src/service.cpp` `initialize` | `createSchema`（`sqlite3_exec`）、`seedDemoData`、`refreshOverdue` |
| 5 | `src/ui.cpp` `MainWindow` 构造 | 创建控件树（`Fl_Group::begin/end`），`refreshAll()` 首屏取数 |
| 6 | `src/main.cpp` `Fl::run()` | 进入 `src/Fl.cxx:651` 事件循环 |

## 10.2 一次「点击借出」的完整调用链

| 序 | 层 / 源码 | 动作 |
| --- | --- | --- |
| 1 | Win32 → `Fl_win32.cxx:1226 WndProc` | 鼠标消息进入 FLTK |
| 2 | `Fl.cxx:631 Fl::wait(double)` → `Fl.cxx:1359 Fl::handle` → `Fl_Group.cxx:145` | 事件分发到按钮 |
| 3 | `Fl_Button` 回调 → `src/ui.cpp` `LoanPanel::onBorrow` | `invoke<LoanPanel,&doBorrow>` 建立异常边界 |
| 4 | `LoanPanel::doBorrow` | 下拉下标 → `studentIds_ / bookIds_` 真实 ID |
| 5 | `src/service.cpp` `borrowBook` | 学生/逾期/上限/库存四项校验 |
| 6 | `src/daos.cpp` 四个 DAO | `findById / hasOverdue / countActiveByStudent / findById` |
| 7 | `src/database.cpp` `Statement` | `sqlite3_prepare_v2`、`bind_int`、`step`、`column_*` |
| 8 | `sqlite3.c:94224 sqlite3_step` → `97179 sqlite3VdbeExec` → Btree/Pager | 引擎执行查询与写入 |
| 9 | `Transaction`（`BEGIN IMMEDIATE`） | 插入 `borrow_records` + 更新 `books.status` |
| 10 | `sqlite3.c` Pager/WAL | `COMMIT` 落盘到 `data/library.db` |
| 11 | `MainWindow::refreshAll` | 统计条、四个列表、下拉框同步刷新 |
| 12 | `Fl::flush()`（在 `Fl::wait` 内） | 重绘窗口，状态栏显示「借书成功，借期 30 天」 |

## 10.3 一次列表刷新的数据流

`refreshAll()` → `svc_.refreshOverdue()`（`UPDATE ... status=2`，`sqlite3_step`）→ `svc_.statistics()`（6 个子查询一次取回）→ `BookDao::listAll()`（`SELECT ... FROM books ORDER BY id`）→ `mapBook()` 按列序构造 1000 个 `Book` → `svc_.listShelves()` 建 `id → code` 映射 → `UiUtils::join/clean` 拼行 → `Fl_Hold_Browser::add()` → `Fl::flush()` 绘制。整个链路只发起 3～5 条 SQL，避免 N+1 查询，这是 1000 册数据下列表仍能快速刷新的原因。

# 11 源码阅读路线与调试建议

## 11.1 分阶段阅读路线

| 阶段 | 建议文件与断点 | 观察目标 |
| --- | --- | --- |
| 入门 | `main.cpp:runApplication`、`CMakeLists.txt` | 对象组装顺序、三套源码如何链接 |
| 领域 | `domain.h` 的 `Entity` / `Book`、`domain.cpp:addDays` | 多态调用、日期归一化 |
| 数据 | `database.cpp:Statement::step`、`daos.cpp:mapBook` | C API 状态码映射、列序契约 |
| 业务 | `service.cpp:borrowBook` | 事务边界与异常回滚 |
| 界面 | `ui.cpp:setupBrowser / selectedId / invoke` | 事件回调与数据绑定 |
| SQLite | `sqlite3.c:149388 → 94224 → 97179` | SQL 编译 → 虚拟机执行 |
| FLTK | `Fl.cxx:631 → 1359`、`Fl_Group.cxx:145` | 事件从 Win32 消息到按钮回调 |

## 11.2 调试与验证建议

- 在 `Database::prepare` 打印 `sql_`，即可看到界面每个操作对应的全部 SQL；
- 在 `Statement::step` 断点单步进入 `sqlite3.c`，观察 VDBE 执行；
- 在 `invoke` 模板的 catch 处设断点，可捕获所有业务异常的原始文案；
- 用 `run_tests.bat` 做回归（16 条），用 `generate_demo.bat` 恢复演示数据；
- 修改实体字段时，务必同步 `domain.h` → `validate/toRow` → DAO 的 SQL 列清单与 `mapXxx` → DDL → 界面列宽与表头，共 5 处（文档中「列序即契约」的由来）。

## 11.3 常见二次开发入口

| 需求 | 主要改动文件 |
| --- | --- |
| 新增图书字段 | `domain.h/cpp`、`daos.cpp`、`database.cpp` 的 DDL、`ui.cpp` 的 BookDialog 与列表 |
| 新增一个页签 | `ui.h` 新增 Panel 类、`ui.cpp` 构造与回调、`MainWindow` 控件树 |
| 修改借期 / 可借上限 | `service.cpp` 的 `addDays(day, 30)`、`domain.h` 的 `defaultMaxBorrow()` 与 DDL CHECK |
| 更换数据库文件 | `main.cpp` 与 `demo_tool_main.cpp` 中的 `"data/library.db"` |
| 换 GUI 主题 / 字体 | `main.cpp` 的 `Fl::scheme` 与 `Fl::set_font` |

# 附录 A 本项目源码文件清单（实测行数）

`include/` 下 6 个头文件与 `src/` 下 8 个实现文件均已完成逐项 Doxygen 注释；
下表行数为注释完成后的实测值（头文件由原 720 行增至 1,702 行，共 338 个函数级文档块）。

| 文件 | 行数 | 职责 |
| --- | ---: | --- |
| `include/domain.h` | 486 | 枚举、工具函数、异常、Entity 与四个实体声明 |
| `include/database.h` | 230 | Database / Statement / Transaction / DbInitializer |
| `include/daos.h` | 266 | Dao 模板、四个 DAO、LoanDetail / ShelfStat |
| `include/service.h` | 202 | Statistics、LibraryService |
| `include/ui.h` | 437 | UiUtils、三个对话框、四个 Panel、MainWindow |
| `include/demo_data.h` | 81 | DemoDataReport、DemoDataGenerator |
| `src/domain.cpp` | 413 | 领域实现与日期算法 |
| `src/database.cpp` | 500 | SQLite 封装、DDL、基础演示数据 |
| `src/daos.cpp` | 687 | SQL 与行映射 |
| `src/service.cpp` | 363 | 业务规则与事务 |
| `src/ui.cpp` | 1045 | FLTK 控件与回调 |
| `src/main.cpp` | 126 | 入口、字体、WinMain、CLI 模式 |
| `src/demo_data.cpp` | 462 | 1000/100/500 生成器与一致性校验 |
| `src/demo_tool_main.cpp` | 71 | 控制台数据工具 |
| `tests/smoke_test.cpp` | 141 | 16 条自动化用例 |
| `CMakeLists.txt` / 三个 .bat / `README.md` | 48 / 94 / 134 | 构建、脚本与说明 |

# 附录 B SQLite 关键源码索引

| 路径（相对 `tools/sqlite-amalgamation-3540100/`） | 行号 / 行数 | 定位内容 |
| --- | --- | --- |
| `sqlite3.h` | 275 / 4340 / 4651 / 5361 | 不透明结构体 typedef、`prepare_v2`、`step` 声明 |
| `sqlite3.h` | 14,447 行 | 完整公开 API 与错误码 |
| `sqlite3.c` | 31 / 321 | 合并分段起点：sqliteInt.h、内嵌 sqlite3.h |
| `sqlite3.c` | 18,864 / 25,014 / 59,578 / 72,028 | `struct sqlite3` / `Vdbe` / `Pager` / `Btree` |
| `sqlite3.c` | 93,349 / 94,224 / 97,179 | `finalize` / `step` / `sqlite3VdbeExec` |
| `sqlite3.c` | 142,838 / 149,388 | `exec` / `prepare_v2` 实现 |
| `sqlite3.c` | 189,530 / 189,735 / 192,111 | `changes` / `close` / `open_v2` 实现 |
| `sqlite3.c` | 25,757 | `date.c`：日期函数 |
| `sqlite3.c` | 271,131 行 / 9.58 MB | 全文规模 |
| `shell.c` | 39,939 行 | 命令行外壳（未编译进本项目） |
| `sqlite3ext.h` | 745 行 | 扩展 API（未使用） |

# 附录 C FLTK 关键源码索引

| 路径（相对 `tools/fltk-1.4.5/`） | 行数 / 行号 | 定位内容 |
| --- | --- | --- |
| `FL/Fl_Widget.H` | 1,290 行；112 / 258 / 289 | 基类声明、`draw()` 纯虚、`handle()` 虚 |
| `FL/Fl_Group.H` | 289 行；56 / 90 / 91 | 组类、`begin()` / `end()` |
| `FL/Fl_Browser_.H` | 417 行 | 浏览器基类与列宽 API |
| `FL/Fl.H` | 1,620 行 | 全局 API（`run` / `wait` / `set_font`） |
| `src/Fl.cxx` | 2,516 行；631 / 651 / 672 / 1359 | `wait(double)` / `run()` / `wait()` / `handle()` |
| `src/Fl_Group.cxx` | 1,038 行；145 | 组事件分发 |
| `src/Fl_Widget.cxx` | 412 行 | 控件基类实现 |
| `src/Fl_Window.cxx` / `Fl_arg.cxx` | 1,049 行 / 287 | 窗口显示与命令行参数 |
| `src/Fl_win32.cxx` | 2,940 行；435 / 1226 | `DispatchMessageW` / `WndProc` |
| `src/Fl_Browser.cxx` / `Fl_Browser_.cxx` | 955 / 1,101 行 | 列表控件绘制与选中逻辑 |
| `src/Fl_Input_.cxx` / `Fl_Choice.cxx` | 1,564 / 229 行 | 输入与下拉控件 |
| `src/fl_ask.cxx` / `fl_font.cxx` / `fl_draw.cxx` | 699 / 71 / 781 行 | 弹窗、字体、文本绘制 |
| `src/drivers/WinAPI/` / `src/drivers/GDI/` | 10 / 14 文件 | Windows 窗口与绘制驱动 |
| `CMakeLists.txt` | 411 行；159 / 325 | 静态/动态库构建开关 |
| `tools/fltk-install/CMake/FLTKConfig.cmake` | — | 本项目 `find_package(FLTK CONFIG)` 的入口 |

# 附录 D 本文档包含的关键源码摘录清单

| 章 | 摘录内容 | 来源 |
| --- | --- | --- |
| 2 | `CMakeLists.txt` 五个目标段落 | 本项目 |
| 3 | `addDays()`、`Book::validate()` | `src/domain.cpp` |
| 4 | `Database::open`、`Transaction` 析构、DDL 片段、`Dao<T>`、`mapBook`、`listDetails` | 本项目 |
| 5 | `borrowBook()` 全流程 | `src/service.cpp` |
| 6 | `MainWindow` 构造、`setupBrowser` / `selectedId`、`invoke` / `DialogBase::exec`、`main()` | 本项目 |
| 7 | 演示数据规划与事务代码 | `src/demo_data.cpp` |
| 8 | `sqlite3.h` 不透明结构体、`sqlite3_step` 实现逐字引用 | `sqlite3.h:275/4340`、`sqlite3.c:94224` |
| 9 | `Fl::run` / `Fl::wait` 逐字引用、`Fl_Group::handle` 逐字引用 | `src/Fl.cxx:651/672`、`src/Fl_Group.cxx:145` |
| 10 | 三套源码的完整调用链 | 综合 |

# 附录 E 完整第三方源码的交付与后续选项

- **完整源码位置**：SQLite 全文在 `tools/sqlite-amalgamation-3540100/`（4 文件，约 32.6 万行），FLTK 全文在 `tools/fltk-1.4.5/`（数百文件），构建后的头文件与静态库在 `tools/fltk-install/`。二者均随项目目录整体交付，可直接检索、设断点、重新编译。
- **为何不逐字排入 Word**：仅 `sqlite3.c` 就 27 万行，按每页 50 行估算约 5,400 页；加上 FLTK 将达上万页、数十 MB，失去「导读」意义且无法正常打开。故采用「模块地图 + 精确行号 + 逐字关键摘录 + 端到端调用链」的方式包含两库源码内容。
- **后续可选项**：如需一份可检索的「源码全文合订本」，可另外生成独立 HTML/TXT 文件（本项目 8 个 .cpp + SQLite/FLTK 指定文件全文），或把本项目源码全文另做成一个 Word 附录卷；两者都不影响本导读的结构。

本导读中的路径、行数、行号均基于当前工作区实测；若升级 SQLite / FLTK 版本，需重新核对附录 B、C 的定位表。
