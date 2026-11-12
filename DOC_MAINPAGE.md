# 简易图书借阅系统 · API 文档

本页由 **Doxygen 1.18.0 + Graphviz** 自动生成，是「简易图书借阅系统」的**源码级 API 参考**。
与仓库里的设计说明书不同，这里按类、函数、文件组织，并附带自动生成的类图、调用图与包含关系图。

## 项目概况

- 技术栈：C++17 · FLTK 1.4.5 · SQLite 3.54.1（amalgamation 直接编译进 exe）
- 架构：四层面向对象设计，**表现层 UI → 业务层 LibraryService → 数据访问层 DAO → 领域模型层 Domain**
- 数据库：单文件 `data/library.db`，4 张表、2 个视图、7 个索引
- 平台：Windows 10/11 x64

## 从这里开始

建议使用左侧的 **「文件」→「文件列表」** 或 **「类」→「类列表」** 浏览：

| 想看什么 | 入口 |
| --- | --- |
| 领域模型、枚举、工具函数 | `lls::Book` / `lls::Student` / `lls::Bookshelf` / `lls::BorrowRecord` |
| 业务规则与事务边界 | `lls::LibraryService` |
| SQL 语句与结果行映射 | `lls::BookDao` / `lls::StudentDao` / `lls::ShelfDao` / `lls::LoanDao` |
| 数据库连接、预编译语句 | `lls::Database` / `lls::Statement` |
| 界面与事件回调 | `lls::MainWindow` / `lls::BookPanel` / `lls::LoanPanel` |
| 每个模块的设计思路 | 各文件页顶部的「设计说明」段落 |

所有源码均带中文函数级注释，共 570 个文档块。开启 `SOURCE_BROWSER` 后还能直接查看带行号的源码。

## 图怎么看

- **Class Graph / Collaboration Graph**：继承与组合关系，用于理解四层架构
- **Call Graph / Caller Graph**：函数调用链，用于追踪一次「借书」操作从 UI 到 SQL 的完整路径
- **Include Graph**：头文件依赖，用于检查层次是否被违反（Domain 不应依赖 UI/DAO）

## 相关文档

完整的需求、架构、数据库设计与测试用例，请看仓库内的三份中文说明书：
[概要设计说明书](https://github.com/pengtk2026/library-lending-system/blob/main/docs/概要设计说明书.docx)、
[详细设计说明书](https://github.com/pengtk2026/library-lending-system/blob/main/docs/详细设计说明书.docx)、
[项目源码导读](https://github.com/pengtk2026/library-lending-system/blob/main/docs/项目源码导读.docx)。

项目首页与快速开始见 [README](https://github.com/pengtk2026/library-lending-system#readme)。

## 本地重新生成

```bash
doxygen Doxyfile                 # Windows 下可双击 gen_docs.bat
```

需要 Doxygen 与 Graphviz（`dot` 在 PATH 中，否则调用图不会生成）。输出目录为 `build/doxygen/html/`，已被 Git 忽略。

> 本页面是 Doxygen 专用主页，不含 README 里的相对图片与相对链接，以避免文档生成时的路径解析警告。
