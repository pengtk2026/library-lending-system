# 1 引言

## 1.1 编写目的

本文档为「简易图书借阅系统」的概要设计说明书（HLD），用于明确系统的总体架构、模块划分、面向对象模型、数据库模型、界面结构、开发与运行环境和实施策略。本文档是后续《详细设计说明书》与编码实现的直接依据，也是课程设计验收、代码评审和测试设计的基础文档。

本文档面向的读者包括：项目开发人员、课程指导教师、测试与验收人员。

## 1.2 项目范围

系统定位为**单机桌面应用**：面向小型图书室或教学演示场景，由一名管理员在本机完成图书、学生、书架的基础数据维护，以及借书、还书、查询和统计业务。系统不提供多用户并发登录、网络服务、权限分级、预约、罚金结算和报表打印功能，这些均列为后续可扩展范围。

系统必须交付：可运行的 Windows 桌面可执行程序、完整 C++ 源代码、SQLite 数据库（首次运行自动创建）、构建脚本与示例数据、概要设计与详细设计文档。

## 1.3 术语与缩略语

| 术语 | 含义 |
| --- | --- |
| 馆藏副本（Book） | 系统中一条图书记录代表一册可借的实体书，ISBN 可重复 |
| 书架（Bookshelf） | 图书的物理存放位置，具有编号、位置、分类和容量 |
| 借阅记录（BorrowRecord） | 一次借书行为产生的业务单据，记录借出日、应还日、归还日与状态 |
| 在馆 / 借出 | 图书状态，分别对应可借与不可借 |
| 逾期 | 未归还且应还日期早于当前日期的借阅记录 |
| DAO | Data Access Object，数据访问对象，封装 SQL 与结果集映射 |
| HLD / LLD | 概要设计说明书 / 详细设计说明书 |
| FLTK | Fast Light Toolkit，跨平台轻量级 C++ GUI 工具库 |
| UCRT | Universal C Runtime，MinGW-w64 工具链使用的 Windows 运行时 |

## 1.4 参考资料

| 编号 | 资料 |
| --- | --- |
| R-01 | GB/T 8567—2006《计算机软件文档编制规范》 |
| R-02 | FLTK 1.4.5 官方编程手册（FLTK Programmer's Manual） |
| R-03 | SQLite 官方文档：SQL Syntax、C/C++ Interface、Transaction |
| R-04 | ISO/IEC 14882:2017（C++17）语言标准 |
| R-05 | CMake 3.31 官方文档；MinGW-w64 GCC 工具链说明 |

# 2 需求概述

## 2.1 业务目标

以面向对象方法建立一个可运行、可维护、可演示的图书借阅管理系统：数据持久化到本机 SQLite 单文件，界面使用 FLTK 原生窗口，业务逻辑与界面、数据访问解耦，使后续扩展（如网络版、权限管理）不需要推翻现有结构。

## 2.2 用户与用例

系统只有一类使用者：**图书管理员**（本机单用户）。典型用例包括：登记新书、登记学生、维护书架、办理借书、办理还书、按关键字检索图书、查看当前借阅与逾期、查看统计信息。

## 2.3 功能需求清单

| 编号 | 功能需求 | 说明 | 优先级 |
| --- | --- | --- | --- |
| FR-01 | 图书新增 / 编辑 / 删除 | 维护 ISBN、题名、作者、出版社、出版年、分类、所属书架、状态 | 高 |
| FR-02 | 图书检索与列表 | 按题名、作者、ISBN 模糊检索，列表展示状态与所在书架 | 高 |
| FR-03 | 学生新增 / 编辑 / 删除 | 维护学号、姓名、专业班级、电话、可借上限、启用状态 | 高 |
| FR-04 | 书架新增 / 编辑 / 删除 | 维护编号、位置、分类、容量，显示当前藏书数与容量占用 | 高 |
| FR-05 | 办理借书 | 选择学生与在馆图书，生成借阅记录，更新图书状态 | 高 |
| FR-06 | 办理还书 | 选择未归还记录，登记归还日期，恢复图书在馆状态 | 高 |
| FR-07 | 借阅记录查询 | 按「当前借阅 / 全部 / 逾期」筛选，展示学生、图书、日期与状态 | 高 |
| FR-08 | 统计信息 | 藏书总数、在馆数、借出数、逾期数、学生数、书架数 | 中 |
| FR-09 | 数据初始化 | 首次启动自动建库建表并写入演示数据 | 中 |
| FR-10 | 输入校验与提示 | 必填项、数值范围、唯一性、业务规则违反时给出明确提示 | 高 |
| FR-11 | 逾期批量标记 | 刷新数据时把到期未还记录标记为逾期 | 中 |
| FR-12 | 事务一致性 | 借书、还书、级联检查在事务内完成，失败整体回滚 | 高 |

## 2.4 核心业务规则

| 编号 | 规则 |
| --- | --- |
| BR-01 | 学生须处于启用状态，且当前未归还册数小于其可借上限（默认 5，上限范围 1～20） |
| BR-02 | 仅「在馆」状态的图书可借出 |
| BR-03 | 学生存在逾期未还记录时，禁止继续借书 |
| BR-04 | 借期固定 30 天，应还日 = 借出日 + 30 天 |
| BR-05 | 借书与还书必须在一个事务内同时更新借阅记录与图书状态 |
| BR-06 | 书架上仍有藏书时禁止删除该书架（数据库外键 RESTRICT + 业务预检） |
| BR-07 | 学生或图书存在未归还借阅记录时禁止删除，仅允许停用／标记遗失 |
| BR-08 | 学号、书架编号全局唯一；ISBN 允许重复以表示同一书目的不同副本 |
| BR-09 | 逾期判定：状态未归还且应还日期早于当天；刷新时批量更新状态 |
| BR-10 | 题名、姓名、学号、书架编号、位置为必填；出版年范围 1450～当年；容量为正整数 |

## 2.5 非功能需求

| 编号 | 类别 | 要求 |
| --- | --- | --- |
| NFR-01 | 可移植性 | 仅依赖操作系统与静态链接库，程序目录可整体拷贝运行 |
| NFR-02 | 易用性 | 中文界面，常用操作不超过 3 次点击，操作结果在状态栏即时反馈 |
| NFR-03 | 健壮性 | 所有 SQL 使用预编译语句与参数绑定；异常不得导致进程崩溃或数据半更新 |
| NFR-04 | 性能 | 千册图书规模下列表刷新与检索响应时间不超过 1 秒 |
| NFR-05 | 可维护性 | 四层架构、单一职责类、统一异常体系，界面层不得出现 SQL 语句 |
| NFR-06 | 数据安全 | 外键约束开启，删除采用业务预检 + 受限外键，关键写操作使用事务 |
| NFR-07 | 编码 | 全部源文件 UTF-8 无 BOM，数据库文本 UTF-8，界面指定中文字体 |

# 3 总体设计

## 3.1 设计原则与技术选型

| 层次 | 选型 | 选择理由 |
| --- | --- | --- |
| 语言 | C++17 | 支持智能指针、结构化绑定、std::optional 等，便于 RAII 与异常安全 |
| GUI | FLTK 1.4.5（静态库） | 轻量、原生 Win32 绘制、无运行时依赖、适合课程级桌面项目 |
| 数据库 | SQLite 3.54.1（amalgamation 源码内嵌） | 零配置、单文件、支持事务与外键，sqlite3.c 直接编译避免 DLL 依赖 |
| 构建 | CMake 3.31 + MinGW-w64 GCC 16.2 (UCRT, POSIX, SEH) | 本机未安装 MSVC，采用便携工具链，不修改系统环境变量 |
| 链接 | 全静态（-static） | 生成单一 exe，避免运行库 DLL 缺失 |
| 编码 | UTF-8 + Microsoft YaHei 界面字体 | 保证中文显示与数据库存取一致 |

设计原则：**分层解耦、面向接口、单一职责、RAII 资源管理、异常安全、界面与 SQL 隔离**。

## 3.2 系统总体架构

系统采用四层架构，自上而下为表现层、业务逻辑层、领域模型层、数据访问层，底层为 SQLite 数据库文件。上层可依赖下层，禁止反向依赖；界面层不得直接访问 Database 或拼接 SQL。

```text
┌──────────────────────────────────────────────────────────┐
│ 表现层 UI (FLTK)                                          │
│  MainWindow / BookPanel / StudentPanel / ShelfPanel /      │
│  LoanPanel / BookDialog / StudentDialog / ShelfDialog      │
└───────────────────────┬──────────────────────────────────┘
                        │ 调用公共方法、接收 DTO / 结构体
┌───────────────────────▼──────────────────────────────────┐
│ 业务逻辑层 Service                                        │
│  LibraryService（规则校验、事务边界、流程编排）            │
└──────────┬────────────────────────────┬──────────────────┘
           │ 使用领域对象                 │ 调用 DAO
┌──────────▼──────────────┐  ┌───────────▼──────────────────┐
│ 领域模型层 Domain        │  │ 数据访问层 DAO                │
│ Entity / Book / Student /│  │ Database / Statement /        │
│ Bookshelf / BorrowRecord │  │ Transaction / *Dao / 初始化器 │
│ 枚举与异常体系           │  │                               │
└─────────────────────────┘  └───────────┬──────────────────┘
                                          │ sqlite3 C API
                              ┌───────────▼──────────────────┐
                              │ data/library.db（SQLite）     │
                              └──────────────────────────────┘
```

## 3.3 模块划分与职责

| 模块 | 主要类 / 文件 | 职责 |
| --- | --- | --- |
| M-01 应用入口 | main.cpp、Application | 初始化数据库、构造服务与窗口、进入 FLTK 事件循环 |
| M-02 领域模型 | Entity、Book、Student、Bookshelf、BorrowRecord、Types、Exceptions | 业务实体、状态枚举、不变式校验、异常类型 |
| M-03 数据库基础 | Database、Statement、Transaction、DbInitializer | 连接管理、预编译语句、RAII 事务、建表与演示数据 |
| M-04 数据访问 | BookDao、StudentDao、BookshelfDao、BorrowRecordDao | 单表 CRUD、条件查询、统计 SQL、行到对象映射 |
| M-05 业务逻辑 | LibraryService、Statistics、LoanDetail | 规则校验、事务编排、组合查询与统计、逾期标记 |
| M-06 图形界面 | MainWindow、四个 Panel、三个 Dialog、UiUtils | 界面布局、控件事件回调、数据绑定、消息提示 |

## 3.4 目录结构与源文件规划

```text
E:/githubp/s4 -cpp-FLTK/
├─ docs/                     概要设计说明书.docx、详细设计说明书.docx
├─ include/                  全部 .h 头文件（按层组织命名）
├─ src/                      全部 .cpp 实现文件、main.cpp
├─ data/                     运行时自动生成的 library.db
├─ build/                    CMake 构建输出与最终 exe
├─ tools/                    便携工具链、依赖源码与构建辅助脚本
├─ output/                   文档流水线中间产物
└─ CMakeLists.txt、build.bat、README.md
```

命名约定：类名大驼峰，成员后缀下划线，访问器 `getter()` / `setter()`，SQL 常量集中在各 DAO 的匿名命名空间，UI 回调统一采用静态函数 + `static_cast` 取得控件所属对象。

## 3.5 关键运行流程

**启动流程**：Application 构造 → Database::open(data/library.db) → 打开外键与 WAL → DbInitializer::createSchema()（幂等 CREATE TABLE IF NOT EXISTS / 视图 / 索引）→ 空库时 seedDemoData() → 构造 LibraryService → 构造 MainWindow 并刷新四个页签 → Fl::run()。

**借书流程**：用户在借阅页选择学生与在馆图书 → UI 调用 `LibraryService::borrowBook()` → 校验学生启用、逾期、可借上限、图书在馆 → 开启事务 → 插入 borrow_records、更新 books.status=1 → 提交；任一环节异常则回滚并以对话框提示。

**还书流程**：选择未归还记录 → `returnBook()` → 事务内更新记录归还日与状态、恢复 books.status=0 → 提交后刷新统计与列表。

## 3.6 部署视图

交付物为 `build/LibraryApp.exe` 与 `data/` 目录。程序首次运行按相对路径在自身工作目录创建 `data/library.db`；数据库为唯一持久状态，无需注册表或服务。工具链仅用于构建，不属于运行时依赖。

# 4 面向对象模型概要设计

## 4.1 领域类清单

| 类 | 层次 | 主要属性 | 主要行为 |
| --- | --- | --- | --- |
| Entity（抽象） | 领域 | id | tableName()、validate()、toRow()、虚析构 |
| Book : Entity | 领域 | isbn、title、author、publisher、publishYear、category、shelfId、status、addedAt | validate()、statusText()、borrow()/giveBack() 状态迁移 |
| Student : Entity | 领域 | studentNo、name、major、phone、maxBorrow、active | validate()、canBorrowMore(current) |
| Bookshelf : Entity | 领域 | code、location、category、capacity、bookIds | validate()、bookCount()、remaining()、canAdd() |
| BorrowRecord : Entity | 领域 | studentId、bookId、borrowDate、dueDate、returnDate、status、remark | validate()、isOverdue(today)、close() |
| BookStatus / LoanStatus | 领域 | 枚举值 | 在馆 / 借出 / 遗失 / 维修；借出 / 已还 / 逾期 |
| AppException 家族 | 领域 | what() | ValidationException、NotFoundException、DbException、BusinessException |

## 4.2 类关系

| 关系 | 说明 |
| --- | --- |
| 泛化（继承） | Book、Student、Bookshelf、BorrowRecord 公有继承 Entity；四类异常公有继承 AppException |
| 关联 | BorrowRecord 同时关联 Student 与 Book（以 ID 表示，数据库外键约束） |
| 聚合 | Bookshelf 在内存中聚合 Book 的 ID 列表，用于界面展示藏书数，数据库侧由 books.shelf_id 承载 |
| 依赖 | LibraryService 依赖各 DAO 与领域对象；UI 依赖 LibraryService 与 DTO 结构 |
| 组合 | MainWindow 组合四个 Panel 与状态栏；Database 独占管理 sqlite3* 句柄 |

## 4.3 继承与多态设计

Entity 定义纯虚 `tableName()` 与 `toRow()`、虚 `validate()`，派生类各自实现。多态体现在两个位置：其一，通用日志与校验入口可接收 `const Entity&` 并动态调用派生类实现；其二，DAO 模板基类 `Dao<T>` 定义统一的 insert / update / removeById / findById / listAll 骨架，具体 SQL 与行映射由派生 DAO 的虚函数提供，从而复用遍历与异常包装逻辑。

## 4.4 封装与不变式

所有实体属性私有，setter 内执行非空、范围与格式校验；实体提供只读 getter 返回 `const std::string&` 或值类型。关键不变式：Book 的 shelfId 必须指向存在的书架；BorrowRecord 的 dueDate 不早于 borrowDate；Student 的 maxBorrow 在 1～20；Bookshelf 的 capacity 大于 0。

## 4.5 异常体系

统一基类 `AppException : std::runtime_error`，派生 `ValidationException`（输入非法）、`NotFoundException`（记录不存在）、`DbException`（SQL 执行失败，携带 sqlite3 错误信息）、`BusinessException`（业务规则冲突）。DAO 与 Service 只抛出异常，UI 层统一捕获并转换为友好提示，保证异常不穿透 FLTK 回调。

## 4.6 资源管理与 RAII

- `Database`：构造打开、析构关闭，禁止拷贝，移动语义可选；单例由 Application 持有引用。
- `Statement`：构造时 sqlite3_prepare_v2，析构时 sqlite3_finalize，禁止拷贝。
- `Transaction`：构造执行 BEGIN，显式 commit() 提交；析构时若未提交则自动 ROLLBACK，保证异常路径回滚。
- 容器：`std::vector` 值语义返回查询结果，避免裸指针；UI 控件由 FLTK 父子关系管理生命周期。

# 5 数据库概要设计

## 5.1 存储策略

采用 SQLite 单文件数据库 `data/library.db`，以 amalgamation 源码 `sqlite3.c` 静态编译进程序。连接建立后执行 `PRAGMA foreign_keys = ON` 与 `PRAGMA journal_mode = WAL`；文本统一 UTF-8；日期使用 `YYYY-MM-DD` 文本，便于 `date()` 函数比较与计算。

## 5.2 实体关系

一个书架容纳多册图书（1:N，books.shelf_id）；一名学生产生多条借阅记录（1:N）；一册图书在不同时间可产生多条借阅记录（1:N，历史留痕），但同一时刻至多一条未归还记录，由业务规则与唯一部分索引共同保证。

## 5.3 数据表清单

| 表名 | 用途 | 主要字段 | 约束 |
| --- | --- | --- | --- |
| students | 学生 / 读者 | id、student_no、name、major、phone、max_borrow、active、created_at | 学号 UNIQUE，max_borrow CHECK 1～20 |
| bookshelves | 书架 | id、code、location、category、capacity、remark | 编号 UNIQUE，capacity > 0 |
| books | 馆藏副本 | id、isbn、title、author、publisher、publish_year、category、shelf_id、status、added_at | shelf_id 外键 RESTRICT，status CHECK 枚举 |
| borrow_records | 借阅单据 | id、student_id、book_id、borrow_date、due_date、return_date、status、remark | 双外键 RESTRICT，状态 CHECK，日期完整性 |
| v_loan_detail | 借阅明细视图 | 关联学生、图书、书架的展示列 | 只读视图 |
| v_shelf_stat | 书架统计视图 | 书架、藏书数、在馆数、容量 | 只读视图 |

## 5.4 完整性与事务策略

**约束**：主键自增；学号、书架编号唯一索引；外键删除 RESTRICT；CHECK 控制状态枚举、容量、可借上限与出版年；图书题名、学生姓名等非空。

**索引**：books(title)、books(isbn)、books(status)、books(shelf_id)、borrow_records(student_id, status)、borrow_records(book_id)、borrow_records(due_date)。

**事务**：借书、还书、批量逾期标记、演示数据初始化使用显式事务；单表 CRUD 使用自动提交；所有写操作使用参数绑定，杜绝字符串拼接 SQL。

## 5.5 DAO 划分

| DAO | 主要方法 |
| --- | --- |
| BookDao | insert / update / removeById / findById / listAll / search(keyword) / updateStatus / countByShelf |
| StudentDao | insert / update / removeById / findById / listAll / findByStudentNo / countActiveLoans |
| BookshelfDao | insert / update / removeById / findById / listAll / countBooks / listWithStats |
| BorrowRecordDao | insert / close / findById / listAll / listActive / listOverdue / listByStudent / countActiveByStudent / markOverdue |

# 6 界面概要设计

## 6.1 界面结构

主窗口为 `Fl_Double_Window`（1024 × 680），自上而下分为：统计信息条（藏书、在馆、借出、逾期、刷新）、`Fl_Tabs` 四个功能页签、底部状态栏。所有对话框为模态 `Fl_Window`，取消操作不产生任何写入。

```text
┌─────────────────────────────────────────────────────────┐
│ 简易图书借阅系统   藏书 24 | 在馆 20 | 借出 4 | 逾期 1  [刷新] │
├─────────────────────────────────────────────────────────┤
│ [图书管理] [学生管理] [书架管理] [借阅管理]                │
│  检索：[__________]  [查询]  [新增] [编辑] [删除] [刷新]   │
│  ┌───────────────────────────────────────────────────┐  │
│  │ ID  ISBN  题名      作者   书架   状态              │  │
│  │ 1   7-... C++ Primer ...  A-01  在馆               │  │
│  └───────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────┤
│ 状态栏：操作成功 / 失败原因                               │
└─────────────────────────────────────────────────────────┘
```

## 6.2 页面清单

| 页面 | 主要控件 | 主要操作 |
| --- | --- | --- |
| 图书管理 | 检索输入、Fl_Browser 表格化列表、5 个按钮 | 新增/编辑/删除图书、按关键字检索 |
| 学生管理 | 学生列表、新增/编辑/删除/刷新按钮 | 维护读者信息、查看已借册数 |
| 书架管理 | 书架列表（含藏书数/容量）、增删改按钮 | 维护书架、删除前藏书数校验 |
| 借阅管理 | 学生下拉、在馆图书下拉、借出按钮、记录列表、筛选下拉、还书按钮 | 借书、还书、逾期筛选 |
| 输入对话框 | Fl_Input / Fl_Choice / Fl_Int_Input、确定与取消 | 采集并回传实体数据 |

## 6.3 交互约定

界面只调用 `LibraryService` 的公共方法，接收 `std::vector<T>` 或展示结构体；列表采用 `Fl_Browser` 的列宽设置与 `@B` 表头语法；所有失败通过 `fl_alert` 与状态栏双重提示；任何写操作成功后统一刷新统计条、相关列表与状态栏，保证界面与数据库一致。

# 7 接口设计

## 7.1 业务层公共接口（概要）

| 方法 | 输入 | 输出 | 说明 |
| --- | --- | --- | --- |
| addBook / updateBook / removeBook | Book 或 id | void / int | 图书维护，含校验与外键预检 |
| listBooks(keyword) | string | vector of Book | 空关键字返回全部 |
| addStudent / updateStudent / removeStudent | Student 或 id | void / int | 学生维护 |
| addShelf / updateShelf / removeShelf | Bookshelf 或 id | void / int | 书架维护，删除前检查藏书 |
| borrowBook(studentId, bookId) | int, int | int（记录 id） | 事务，违反规则抛 BusinessException |
| returnBook(recordId) | int | void | 事务，恢复图书状态 |
| listLoans(filter) | LoanFilter | vector\<LoanDetail\> | 支持全部/未还/逾期 |
| statistics() | 无 | Statistics | 首页统计条数据源 |
| refreshOverdue() | 无 | int | 批量标记逾期，返回影响行数 |

## 7.2 层间数据契约

界面层与业务层之间传递领域对象或只读展示结构（LoanDetail、ShelfStat、Statistics）；业务层与 DAO 之间传递领域对象与基本类型；DAO 与 SQLite 之间只传递参数绑定的 SQL。禁止跨层传递 `sqlite3_stmt*` 或 FLTK 控件指针。

## 7.3 错误与提示约定

| 异常 | 界面提示策略 |
| --- | --- |
| ValidationException | 定位到对应输入项，提示具体字段与合法范围 |
| NotFoundException | 刷新列表并提示「记录已不存在」 |
| DbException | 提示数据库操作失败并附错误摘要，保持界面原数据 |
| BusinessException | 直接展示业务规则文案，例如「该生存在逾期未还图书」 |

# 8 开发与运行环境

## 8.1 环境清单

| 项 | 规格 |
| --- | --- |
| 操作系统 | Windows 10/11 64 位 |
| 编译器 | MinGW-w64 GCC 16.2.0（x86_64, UCRT, POSIX threads, SEH），C++17 |
| 构建工具 | CMake 3.31.6（便携版）与 MinGW Makefiles 生成器 |
| GUI 库 | FLTK 1.4.5，静态编译，启用 UTF-8 与 Win32 后端 |
| 数据库 | SQLite 3.54.1 amalgamation，静态编译 |
| 编辑器 / 版本管理 | 任意 UTF-8 编辑器；Git 可选 |
| 运行时依赖 | 仅 Windows 系统 DLL，程序目录可拷贝运行 |

## 8.2 构建与部署方案

工具链以便携方式存放于 `tools/`，不写入系统 PATH：`tools/mingw64/bin`、`tools/cmake/bin`、`tools/fltk-install`、`tools/sqlite-amalgamation-3540100`。CMakeLists.txt 通过 `FLTK_DIR` 定位 FLTK 配置包，把 `sqlite3.c` 编为静态库，最后以 `-static` 链接生成 `LibraryApp.exe`。`build.bat` 一键完成配置、编译与数据库目录准备。

## 8.3 数据初始化策略

`DbInitializer::createSchema()` 使用 `CREATE TABLE IF NOT EXISTS`、`CREATE INDEX IF NOT EXISTS`、`CREATE VIEW IF NOT EXISTS` 保证幂等；当 `books` 表为空且为首次启动时，写入 3 个书架、8 册图书、4 名学生和 2 条借阅记录作为演示数据，便于开箱即用验证全部功能。

# 9 质量保障与实施计划

## 9.1 测试策略

| 层级 | 内容 | 判定标准 |
| --- | --- | --- |
| 单元测试 | 实体 validate()、状态迁移、日期与逾期计算 | 合法/非法输入均返回预期 |
| 集成测试 | DAO 与 SQLite 建表、CRUD、视图与外键约束 | SQL 执行成功，约束确实生效 |
| 业务测试 | 借书上限、逾期禁借、重复借书、还书恢复库存 | 事务回滚后数据保持一致 |
| 界面测试 | 四个页签增删改查、对话框取消、异常提示 | 无崩溃、无乱码、状态栏反馈正确 |
| 回归测试 | 一键构建脚本 + 冒烟用例集 | 干净环境可完整重建并运行 |

## 9.2 实施阶段

| 阶段 | 产物 | 状态 |
| --- | --- | --- |
| S1 需求与概要设计 | 本概要设计说明书 | 本阶段完成 |
| S2 详细设计 | 详细设计说明书（类、DDL、界面、算法、用例） | 后续阶段 |
| S3 编码实现 | 完整源码、CMakeLists.txt、构建脚本 | 后续阶段 |
| S4 构建与测试 | 可执行程序、数据库、测试记录 | 后续阶段 |
| S5 交付 | 文档 + 源码 + 可执行程序 + 使用说明 | 后续阶段 |

## 9.3 风险与对策

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 本机缺少 C++ 编译器与第三方库 | 无法编译验证 | 使用便携工具链，项目内自带依赖源码与构建脚本 |
| FLTK 中文显示乱码 | 界面不可用 | 源文件统一 UTF-8，启动时设置 Microsoft YaHei 字体 |
| 静态链接遗漏系统库 | 链接失败 | 采用 CMake 导入目标 fltk::fltk，自动传递 Windows 依赖库 |
| 借书并发 / 异常导致数据不一致 | 库存错误 | 单事务 + 外键 + 状态 CHECK + 业务预检 |
| 数据库文件被占用或损坏 | 启动失败 | WAL 模式、异常提示、保留原文件不自动删除 |
| 日期计算跨平台差异 | 逾期判断错误 | 统一 `YYYY-MM-DD` 文本与 SQLite date() 函数 |

# 10 需求追溯矩阵

| 需求 | 界面模块 | 业务方法 | 领域类 / DAO | 数据表 |
| --- | --- | --- | --- | --- |
| FR-01 | BookPanel / BookDialog | addBook / updateBook / removeBook | Book、BookDao | books |
| FR-02 | BookPanel | listBooks | Book、BookDao | books、bookshelves |
| FR-03 | StudentPanel | addStudent / updateStudent / removeStudent | Student、StudentDao | students |
| FR-04 | ShelfPanel | addShelf / updateShelf / removeShelf | Bookshelf、BookshelfDao | bookshelves |
| FR-05 | LoanPanel | borrowBook | BorrowRecord、Book、两个 DAO | borrow_records、books |
| FR-06 | LoanPanel | returnBook | BorrowRecord、Book | borrow_records、books |
| FR-07 | LoanPanel | listLoans / refreshOverdue | BorrowRecordDao | borrow_records、v_loan_detail |
| FR-08 | MainWindow 统计条 | statistics | 各 DAO | 全表 |
| FR-09 | Application | DbInitializer | DbInitializer | 全表 |
| FR-10 | 全部对话框 | validate() | 全部实体 | — |
| FR-11 | MainWindow 刷新 | refreshOverdue | BorrowRecordDao | borrow_records |
| FR-12 | 全局 | LibraryService 事务边界 | Transaction、Database | 全部写操作 |

# 附录 A 交付物清单

| 序号 | 交付物 | 形式 |
| --- | --- | --- |
| 1 | 概要设计说明书 | docs/概要设计说明书.docx |
| 2 | 详细设计说明书 | docs/详细设计说明书.docx |
| 3 | 系统源代码 | include/、src/、CMakeLists.txt |
| 4 | 可执行程序 | build/LibraryApp.exe |
| 5 | 数据库与示例数据 | data/library.db（运行生成） |
| 6 | 构建与使用说明 | README.md、build.bat |

# 附录 B 约定

本文档中的类名、方法名、表名、字段名与后续《详细设计说明书》及源代码保持一致；若实现阶段需要调整，须同步更新三处，保证文档与代码可追溯。数据库 DDL 全文、类成员精确定义、控件清单与事件回调、算法伪代码在详细设计说明书中给出。
