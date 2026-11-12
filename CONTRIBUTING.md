# 贡献指南

欢迎提交 Issue、改进建议与 Pull Request。这是一个按软件工程流程完成的课程级项目，代码与文档同样重视。

## 反馈问题前，请先准备

- 操作系统与版本（本项目面向 Windows 10/11 x64）
- 编译器 / CMake 版本，以及用的是**项目自带便携工具链**还是系统工具链
- 复现步骤：执行了哪个脚本、点击了哪个按钮、期望结果与实际结果
- `data/demo_data_report.txt` 或 `build/test_library.db` 的相关信息（如有）
- 若涉及界面，请截图并**先给姓名、学号、电话打码**

请在提交前先搜索现有 Issue，避免重复。

## 开发环境

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows 10/11 x64 |
| 编译器 | 支持 C++17 的 MinGW-w64 GCC |
| 构建 | CMake 3.16+、Ninja |
| 脚本 | Python 3.10+（仅用于 `tools/*.py` 依赖恢复脚本） |

FLTK 1.4.5 与 SQLite 3.54.1 源码已随仓库分发，无需额外安装依赖库。编译方式见 README 的「构建」章节：

```bat
build.bat            :: 一键构建 LibraryApp.exe 与 DemoDataTool.exe
generate_demo.bat    :: 生成 1000 册 / 100 学生 / 500 借阅记录的演示库
run_tests.bat        :: 编译并运行核心层 16 条断言
```

## 代码约定

- 保持四层架构：`include/` + `src/` 下 `domain`（领域模型）→ `database` / `daos`（数据访问）→ `service`（业务逻辑）→ `ui`（表现层），依赖方向不要倒置。
- 沿用现有 **Doxygen 风格注释**：文件头说明所属层次与职责，类与函数写 `@brief / @param / @return`，关键变量用 `///<`。
- 数据库操作统一通过 `Database / Statement / Transaction` 的 RAII 封装；借还与初始化必须放在显式事务中。
- 新增业务规则时，同步更新《详细设计说明书》对应章节与 `CHANGELOG.md`。
- 中文界面文案保持简洁一致；日期计算复用 `addDays()` 等既有工具函数。

## 提交规范

提交信息使用约定前缀，一行说清改动：

```
feat: 新增预约排队功能
fix: 修复逾期判断在闰年的边界情况
docs: 补充源码导读第 5 章
test: 增加借书上限的回归断言
chore: 调整 CMake 编译选项
```

建议小步提交，一个提交只做一件事。

## 不要把以下内容提交进仓库

- `data/*.db`、`*.db-wal`、`*.db-shm`、`*.ini`、`*.log`：运行数据可能含读者信息，属隐私
- `build/`、`tools/fltk-install/`、`*.exe`、`*.o`、`*.a`：编译产物
- `tools/msys2root/`、`tools/cmake-*/`、`tools/_downloads/`：体积巨大的便携工具链，可由脚本重新下载
- 含真实姓名、学号、电话或内网 IP 的截图；公开展示前请先脱敏
- 直接修改 `tools/fltk-1.4.5/` 或 `tools/sqlite-amalgamation-3540100/` 里的上游源码——如需升级，请在 Issue 中说明版本与原因

`.gitignore` 已覆盖前三类，提交前仍请执行 `git status` 复核。

## Pull Request 流程

1. 从 `main` 建分支，完成改动并自测（`build.bat` + `run_tests.bat`）
2. 更新相关文档与 `CHANGELOG.md`
3. 提交 PR，说明动机、改动范围与验证结果
4. 保持一个 PR 对应一个主题，便于审阅

所有参与者请遵守 [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)。
