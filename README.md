# 简易图书借阅系统（C++17 / FLTK / SQLite）

一个按软件工程流程完成的课程级桌面应用：**概要设计说明书 → 详细设计说明书 → 编码实现 → 构建验证**。
采用 C++ 面向对象四层架构，FLTK 1.4.5 原生界面，SQLite 3 单文件数据库，全部依赖便携化，不修改系统环境变量。

## 交付物

| 文件 | 说明 |
| --- | --- |
| `docs/概要设计说明书.docx` | 概要设计（需求、架构、类模型、数据库、界面、追溯矩阵） |
| `docs/详细设计说明书.docx` | 详细设计（类签名、完整 DDL、DAO SQL、控件与回调、算法、测试用例） |
| `docs/项目源码导读.docx` | 源码导读（本项目四层源码 + SQLite 3.54.1 / FLTK 1.4.5 源码解析与行号索引） |
| `include/` `src/` | 领域模型、数据访问、业务逻辑、FLTK 界面源码 |
| `build/cmake/LibraryApp.exe` | 桌面程序（构建后生成） |
| `build/cmake/DemoDataTool.exe` | 演示数据生成工具（构建后生成） |
| `data/library.db` | 运行后自动生成的数据库（含演示数据） |
| `build.bat` / `generate_demo.bat` / `run_tests.bat` | 一键构建 / 生成演示数据 / 运行测试 |

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

### 从 Git 仓库克隆后：先恢复便携工具链

**仓库不含约 858 MB 的第三方便携工具链**（见 `.gitignore`），只保留源码、设计文档与 `tools/*.py` 获取脚本。首次克隆后按顺序恢复：

```
python tools/fetch_deps.py        # 下载 CMake / FLTK / SQLite 归档到 tools/_downloads
python tools/setup_mingw.py       # 从 MSYS2 镜像下载并解包 MinGW-w64 到 tools/msys2root
python tools/extract_deps.py      # 解压归档，并校验四项依赖是否齐全
build.bat                         # 一键编译
```

> 下载慢时可用 `tools/fetch_deps_parallel.py`、`tools/fetch_pkgs.py` 加速；本机已装好工具链的可直接跳到 `build.bat`。这些目录随时可删除重建，**不要手工编辑，也不要提交到 Git**。

### 工具链组成（恢复后）

- `tools/msys2root/mingw64/bin` — MinGW-w64 GCC 16.2（gcc / g++ / ninja / make）
- `tools/cmake-3.31.6-windows-x86_64/bin` — CMake 3.31.6
- `tools/fltk-1.4.5` — FLTK 1.4.5 源码（首次构建时静态编译并安装到 `tools/fltk-install`）
- `tools/sqlite-amalgamation-3540100` — SQLite 3.54.1 amalgamation（直接编译进 exe）

恢复完成后，双击或在命令行执行：

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

## 大批量演示数据

执行 `generate_demo.bat`（或 `build\cmake\DemoDataTool.exe`）可重建一份完整演示库：

| 项目 | 数量 |
| --- | --- |
| 书架 | 20 个（每架容量 60 册） |
| 图书 | 1000 册（在馆 800 / 借出 200） |
| 学生 | 100 人（5 人停用，可借上限 3～8 册） |
| 借阅记录 | 500 条（当前未还 200、历史已归还 300、其中逾期 40） |

数据由 `DemoDataGenerator` 在单个事务中生成，固定公式可复现：ISBN 带 EAN-13 校验位、
题名由 40 主题 × 5 变体 × 5 分册组合、日期按 `addDays()` 计算。生成器内置一致性校验
（图书状态与未归还记录对应、学生不超可借上限、逾期日期合法、外键完整），
结果写入 `data/demo_data_report.txt`。GUI 程序也支持 `LibraryApp.exe --generate-demo` 无界面生成。

注意：该操作会**清空并重建**四张业务表，请先关闭正在运行的程序。

## 测试

《详细设计说明书》第 9.2 节给出 TC-01～TC-12，覆盖实体校验、日期计算、建库、
唯一约束、借书上限、逾期禁借、还书状态恢复、界面操作与干净环境重建。

其中核心层已自动化：执行 `run_tests.bat`，脚本会用便携 g++ 编译
`tests/smoke_test.cpp` 并运行 16 条断言（使用独立的 `build/test_library.db`，
不会污染正式数据）。当前结果：**16 passed, 0 failed**。

## 源码注释

`src/` 下 8 个 `.cpp` 与 `include/` 下 6 个 `.h` **全部**采用统一的 Doxygen 风格注释：
每个文件头部有设计说明（所属层次、职责、依赖、关键算法与异常策略），
每个类、每个函数（含内联 getter/setter）有 `@brief / @param / @return`，
每个成员变量与关键局部变量有行内 `///<` 用途注释，
主要算法（借书事务、日期归一化、ISBN 校验位、列表渲染、演示数据生成与一致性自检）
均附步骤化说明。共 570 个函数级文档块（src 232 + include 338）。

## 常见问题

- **中文显示为方框**：确认系统已安装「微软雅黑」字体；程序启动时通过 `Fl::set_font` 指定。
- **启动时报数据库错误**：删除 `data/library.db`（及 `-wal`/`-shm` 文件）让程序重建。
- **换机器运行**：复制 `LibraryApp.exe` 与 `data/` 即可，无需安装任何运行库（全静态链接）。

## 版本管理约定

本仓库用 Git 管理，`.gitignore` 明确排除三类内容：

| 排除项 | 原因 |
| --- | --- |
| `tools/` 下的便携工具链与 `tools/_downloads/` | 约 858 MB，可由脚本重新下载，属于第三方依赖 |
| `build/`、`*.o`、`*.exe` 等编译产物 | 每次构建都会变，且无法做文本 diff |
| `data/`、`*.db`、`*.ini`、`*.log` | 运行数据含读者姓名、学号与借阅记录，属个人信息 |

`.gitattributes` 统一了源码换行（`.bat` 用 CRLF，其余用 LF），并把 docx / 图片标为二进制。
日常提交建议：`git status` → `git add .` → `git commit -m "..."` →（若已关联远程）`git push`。
