# D6 范围内数据完全对齐验证

日期：2026-09-15。当前 main，Comet Native `data-align`。本轮验收矩阵见 [brief](../comet/archive/2026-09-15-data-align/brief.md)，完整目标见 [spec](../comet/archive/2026-09-15-data-align/specs/data-align/spec.md)。A1–A8 经新的只读 Verifier 全部通过，8 项 Runtime 正式检查全部通过；用户已接受结果，本次按当前 main 工作区完成归档。详见 [验收报告](../comet/archive/2026-09-15-data-align/verification.md)。

## 环境与边界

开发机 macOS 27 arm64，沿用 Qt 6.10.2、MySQL 8.0.46、Node 24.19.0。未安装或升级工具。实库写入只在脚本自行创建并销毁的独立临时 MySQL；未连接业务库。

## 已执行检查

| 检查 | 当前证据 |
| --- | --- |
| 前端构建 | TypeScript/Vite 通过；保留既有 bundle 大小提示 |
| 前端测试 | 18 项通过 |
| 原生构建 | 通过 |
| CTest | 7 组通过，含删除确认与 data-align 重启恢复 |
| D5/D6 实库 | 55 项通过，含停止后实际剩余行数核对；全流程证据 `.local/evidence/d6-integration.json` |
| D3 实库回归 | 14 项通过，`.local/evidence/d6-d3-regression.json` |
| D4 实库与 Qt 回归 | 通过，含真实 Qt 与只读审计；`.local/evidence/d6-d4-regression.json` |
| D6 真实 Qt | 15 项通过，`.local/evidence/d6-desktop.json`，含 1440/1024 截图 |
| 独立代码复核 | passed，`/root/d6_candidate_review#2026-09-15-d6-overall-final-04` |

Runtime 最终实库证据：`.local/evidence/d6-runtime-integration.json`、`d6-runtime-d3.json`、`d6-runtime-d4.json`。Verifier：`/root/d6_verifier`，执行引用 `skill-coordinated:verifier:5ac6b4b9-db9f-4eb6-9272-ce83dd9804f8`。

## 验收证据映射

- A1：两方向的 insert/update/delete；只写选定字段、目标键不修改；D5 fill/merge 保留目标独有行回归。
- A2：共享范围外行保留；全表空源/空目标/双空；无键、MyISAM、未完整或失败/取消扫描阻止；排序规则等价的范围外同键冲突。
- A3：Qt 默认 fill、显式 align、单独删除分类、确认重置、摘要确认；原生桥接拒绝未确认、过期和重复执行。
- A4：目标删除、全值变化、未选范围字段变化与结构漂移；同批回滚；源后续变化与目标新增键不扩大既有删除集合。
- A5：600 条删除三批提交；后批冲突只保留先前 256 条删除。真实 Qt Bridge 停止于事务边界；已有进程 fixture 覆盖失败/未知提交/进程崩溃，不冒充网络层 COMMIT 丢包注入。
- A6：BIGINT/二进制复合键、DECIMAL/长值/NULL/微秒旧值实测；RESTRICT/NO ACTION 引用冲突回滚；CASCADE/SET NULL 及跨表 DELETE 触发器明确阻止。
- A7：删除确认、失效和重复保护，完全对齐记录识别、unknown 重启恢复；D5 生命周期与存储故障回归复用同一执行链。
- A8：Qt 预览→删除确认→执行→重新比对→记录；停止和外键失败反馈；1440/1024 宽度截图；D3/D4/D5 回归。

## 命令

```sh
npm --prefix frontend test
npm --prefix frontend run build
/Users/PopoY/Documents/DevTools/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --parallel 4
/Users/PopoY/Documents/DevTools/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build --output-on-failure
python3 tests/integration_merge.py --align --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d6-integration.json
python3 tests/integration_sync.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d6-d3-regression.json
python3 tests/integration_data.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d6-d4-regression.json
```

## 限制与修复记录

- 本轮 Qt 自动化曾因下拉框未命中、读取隐藏结构面板以及把最近批次表计入删除明细而失败；修正等待和选择器，保留操作与数据库结果断言，未改变产品行为来适配测试。
- D5 分页回归发现未传 action 时 null QString 被绑定为 SQLite NULL，导致全部操作空页；改为明确非 NULL 空串，保留未传 action 的 20 行/800 总数真实 Bridge 断言。
- 测试专用 WebChannel 探测后重新加载页面恢复应用通道，再验证失败反馈与停止记录，避免测试通道接管页面响应。
- 批次 SQL 错误文案不预先声称回滚成功，实际状态依据 rollback/commit 结果判定。
- 不支持自动跨表级联删除、复杂触发器或不可证明的依赖可见性；删除的是范围内整行，包括未参与比较的列。
- Windows 10 x64、最低 macOS 15 实机、干净机器发行、真实网络层 COMMIT 响应丢失注入、鼠标点击停止按钮全链：NOT RUN。停止已验证真实 Bridge、实际数据库剩余行及 Qt 记录反馈。样本规模不构成大数据吞吐承诺。
