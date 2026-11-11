# 简易图书借阅系统（C++17 / FLTK / SQLite）

一个按软件工程流程完成的课程级桌面应用：**概要设计说明书 → 详细设计说明书 → 编码实现 → 构建验证**。
采用 C++ 面向对象四层架构，FLTK 1.4.5 原生界面，SQLite 3 单文件数据库，全部依赖便携化，不修改系统环境变量。

## 交付物

| 文件 | 说明 |
| --- | --- |
| `docs/概要设计说明书.docx` | 概要设计（需求、架构、类模型、数据库、界面、追溯矩阵） |
| `docs/详细设计说明书.docx` | 详细设计（类签名、完整 DDL、DAO SQL、控件与回调、算法、测试用例） |
| `include/` `src/` | 领域模型、数据访问、业务逻辑、FLTK 界面源码 |
| `build/cmake/LibraryApp.exe` | 可执行程序（构建后生成） |
| `data/library.db` | 运行后自动生成的数据库（含演示数据） |
| `build.bat` | 一键构建脚本 |

## 技术架构

```
表现层 UI (FLTK)         MainWindow / 四个 Panel / 三个 Dialog
        ↓
业务逻辑层 Service       LibraryService（规则校验 + 事务编排）
        ↓
领域模型层 Domain        Entity ← Book / Student / Bookshelf / BorrowRecord
        ↓
数据访问层 DAO           Database / Statement / Transaction / 四个 DAO
        ↓
SQLite                   data/library.db（4 表 + 2 视图 + 部分唯一索引）
```

面向对象特性：封装（私有成员 + 校验 setter）、继承（Entity 抽象基类、异常族、Dao 模板基类）、
多态（validate() / toRow() 动态绑定）、RAII（连接、语句、事务）、异常体系、STL 容器与值语义。

## 构建（Windows 10/11 x64）

项目自带便携工具链，位于 `tools/`：

- `tools/msys2root/mingw64/bin` — MinGW-w64 GCC 16.2（gcc / g++ / ninja / make）
- `tools/cmake-3.31.6-windows-x86_64/bin` — CMake 3.31.6
- `tools/fltk-1.4.5` — FLTK 1.4.5 源码（首次构建时静态编译并安装到 `tools/fltk-install`）
- `tools/sqlite-amalgamation-3540100` — SQLite 3.54.1 amalgamation（直接编译进 exe）

双击或在命令行执行：

```
build.bat
```

脚本会：① 静态编译 FLTK；② 用 Ninja 配置本项目；③ 编译链接生成 `build/cmake/LibraryApp.exe`。
之后在项目根目录运行该 exe（保证工作目录内有 `data/`），首次启动自动建库并写入演示数据。

## 功能

- 图书管理：新增 / 编辑 / 删除、按 ISBN / 题名 / 作者模糊检索
- 学生管理：读者信息维护、可借上限、启用状态、已借册数
- 书架管理：编号 / 位置 / 分类 / 容量、藏书数与在馆数统计
- 借阅管理：借书（30 天借期）、还书、全部 / 当前借阅 / 逾期筛选
- 业务规则：停用学生禁借、逾期禁借、可借上限、重复借出防护、删除引用预检
- 事务一致性：借还与初始化在显式事务内完成，异常自动回滚
- 统计条：藏书 / 在馆 / 借出 / 逾期 / 学生 / 书架数量

## 数据库

首次启动执行 `DbInitializer::createSchema()` 创建 4 张表、6 个索引、1 个部分唯一索引与 2 个视图，
并在空库时写入 3 个书架、8 册图书、4 名学生、2 条借阅记录（含 1 条逾期演示）。
DDL 全文见《详细设计说明书》第 4 章，也在 `src/database.cpp` 中。

## 测试用例

《详细设计说明书》第 9.2 节给出 TC-01～TC-12，覆盖实体校验、日期计算、建库、
唯一约束、借书上限、逾期禁借、还书状态恢复、界面操作与干净环境重建。

## 常见问题

- **中文显示为方框**：确认系统已安装「微软雅黑」字体；程序启动时通过 `Fl::set_font` 指定。
- **启动时报数据库错误**：删除 `data/library.db`（及 `-wal`/`-shm` 文件）让程序重建。
- **换机器运行**：复制 `LibraryApp.exe` 与 `data/` 即可，无需安装任何运行库（全静态链接）。
