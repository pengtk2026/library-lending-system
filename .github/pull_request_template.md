## 改动目的

<!-- 这个 PR 解决什么问题，关联哪个 Issue -->

## 改动范围

- [ ] `src/` / `include/` 业务代码
- [ ] `CMakeLists.txt` 或构建脚本
- [ ] 文档（`docs/`、`README.md`、`CHANGELOG.md`）
- [ ] 测试（`tests/`、`run_tests.bat`）
- [ ] 仅配置 / 排版

## 验证结果

- [ ] `build.bat` 构建通过
- [ ] `run_tests.bat`：16 passed, 0 failed
- [ ] `generate_demo.bat` 生成后一致性校验全部 PASS
- [ ] 界面手动验证（如涉及 UI）
- [ ] `python tools/check_repo.py` 通过

## 提交前自查

- [ ] 提交信息使用了 `feat / fix / docs / test / chore` 前缀
- [ ] 已更新 `CHANGELOG.md` 与相关设计文档
- [ ] **未提交** `data/*.db`、`build/`、`*.exe`、`tools/fltk-install/`、便携编译器
- [ ] 截图与日志中无真实姓名、学号、电话、内网 IP
- [ ] 未直接修改 `tools/` 下 FLTK / SQLite 的上游源码

## 说明

<!-- 设计取舍、已知限制、后续计划 -->
