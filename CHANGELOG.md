# 更新日志

本文件记录项目的重要变更，格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

## [未发布]

### 计划中

- 提供 GitHub Release 附件，让未安装编译环境的用户直接运行 `LibraryApp.exe`
- 补充界面演示动图（GIF），展示借书 / 还书 / 排队流程
- 增加更多核心层自动化断言，并接入持续集成

## [1.0.0] - 2026-11-12

首个公开发布版本。

### 新增

- **四层面向对象架构**：Domain（Entity / Book / Student / Bookshelf / BorrowRecord）、Database / DAO、Service、FLTK UI，含 RAII 连接与事务、异常体系、STL 值语义
- **图书 / 学生 / 书架 / 借阅四大模块**：模糊检索、30 天借期、停用与逾期禁借、可借上限、重复借出防护、删除引用预检
- **SQLite 单文件数据库**：4 张表、6 个索引、1 个部分唯一索引、2 个视图，首次启动自动建库
- **可复现的大批量演示数据**：`DemoDataGenerator` 在单事务中生成 20 书架 / 1000 册图书 / 100 名学生 / 500 条借阅记录，含一致性自检与报告
- **核心层自动化测试**：`run_tests.bat` 编译 `tests/smoke_test.cpp`，16 条断言全部通过

### 文档

- 《概要设计说明书》《详细设计说明书》《项目源码导读》三份中文文档
- README：界面截图、30 秒快速开始、两种构建方式、文档导航、数据库与测试说明
- `tools/THIRD_PARTY_NOTICES.md`：FLTK 与 SQLite 的来源及授权说明
- 源码全量 Doxygen 风格注释（570 个函数级文档块）

### 工程化

- `.gitignore` / `.gitattributes`：排除运行数据、编译产物与 633 MB 便携编译器；统一换行规则
- MIT 许可证、贡献指南、行为准则、Issue / PR 模板、更新日志
- GitHub Actions 仓库完整性检查
- FLTK 1.4.5 与 SQLite 3.54.1 源码随仓库分发，clone 后可一次编译

### 说明

- 便携编译器（MinGW-w64）与 CMake 发行包不随仓库分发；本机已装工具链时 `build.bat` 自动回退到系统 PATH，否则可运行 `tools/*.py` 恢复
- 本项目仅面向 Windows 10/11 x64；程序入口使用 `WinMain`
