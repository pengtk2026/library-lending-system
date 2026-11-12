# 第三方源码说明

本项目自身代码采用 **MIT 许可证**（见根目录 `LICENSE`）。本文件只说明随 `tools/` 分发的第三方源码。

这些第三方源码让 clone 后能直接一次编译。各组件版权归原作者所有，**授权条款以各源码目录内自带的许可证文件为准**，不受本项目 MIT 许可证覆盖。

| 目录 | 组件 | 版本 | 上游 | 体积 |
| --- | --- | --- | --- | --- |
| `fltk-1.4.5/` | FLTK 图形界面库 | 1.4.5 | https://www.fltk.org · GitHub `fltk/fltk` | 约 23 MB / 1519 文件 |
| `sqlite-amalgamation-3540100/` | SQLite amalgamation | 3.54.1 | https://www.sqlite.org | 约 11 MB / 4 文件 |

## 使用方式

- **FLTK**：`build.bat` 首次运行时把这份源码静态编译并安装到 `tools/fltk-install/`，该产物目录不入库。授权说明请查阅 `fltk-1.4.5/COPYING` 与 `fltk-1.4.5/ANNOUNCEMENT`；项目以静态方式链接 FLTK，如需重新分发二进制程序，请自行核对上游对静态链接与再分发的要求。
- **SQLite**：`sqlite3.c` 由 `CMakeLists.txt` 直接编译进最终 exe，无运行时 DLL 依赖。SQLite 属于公有领域，说明见 https://www.sqlite.org/copyright.html 及目录内 `sqlite3.h` 头部声明。

## 其他依赖

MinGW-w64 编译器与 CMake 发行包体积较大（约 633 MB），**不随仓库分发**：

- 本机已安装 gcc / g++ / cmake / ninja 时，`build.bat` 会自动使用系统 PATH 中的工具；
- 否则可运行 `tools/fetch_deps.py`、`tools/setup_mingw.py`、`tools/extract_deps.py` 重新下载便携工具链。

这些脚本及自动生成的目录均为项目自建内容或可重建产物，不含第三方专有代码。请保留各上游的版权与授权声明。
