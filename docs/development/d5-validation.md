# D5 数据补齐与合并验证

日期：2026-09-15。工作区：当前 main。完整规格见 [data-merge](../comet/specs/data-merge/spec.md)；本轮验收矩阵及最终结果见 [归档验收记录](../comet/archive/2026-09-15-data-merge/verification.md)。最终全量验收 A1–A8 通过，用户已接受结果。

## 环境与范围

开发机 macOS 27 arm64，Qt 6.10.2、MySQL 8.0.46。复用既有 Qt/MySQL/Node，无全局升级或新业务依赖。所有实库写入均发生在测试脚本自行创建和销毁的临时实例，不连接业务库。

## 开发检查

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| TypeScript/Vite 构建 | 通过 | `npm --prefix frontend run build`；保留既有 bundle 大小提示 |
| 前端测试 | 17 项通过 | `npm --prefix frontend test` |
| 原生构建 | 通过 | 既有 CMake build，含新增 merge-bridge-tests |
| CTest | 7 组通过 | foundation、async-processes、schema-comparison、schema-sync、sync-lifecycle、data-comparison、data-merge-lifecycle |
| D5 实库 | 29 项通过 | `.local/evidence/d5-integration.json` |
| D5 真实 Qt | 10 项通过 | `.local/evidence/d5-desktop.json`；1440/1024 预览截图 |
| D3 实库回归 | 14 项通过 | `.local/evidence/d5-d3-regression.json` |
| D4 实库/只读审计与 Qt 回归 | 通过；Qt 18 项 | `.local/evidence/d5-d4-regression.json`、`d4-desktop.json` |
| 独立 Builder 代码复核 | 通过 | `candidate-review-d5-20260915-01a0a439-r1`（业务）及 `r2`（A8 检查修复） |

执行命令：

```sh
npm --prefix frontend test
npm --prefix frontend run build
/Users/PopoY/Documents/DevTools/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --parallel 4
/Users/PopoY/Documents/DevTools/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build --output-on-failure
python3 tests/integration_merge.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d5-integration.json
python3 tests/integration_sync.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d5-d3-regression.json
python3 tests/integration_data.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d5-d4-regression.json
```

## 验收证据对应

- A1：左右两方向 fill/merge、字段子集、目标独有记录、排序规则等价键且保留目标键字节；自增键 0 实写。
- A2：无键、MyISAM、必填列、类型不兼容与权限不足拒绝；扫描不具备写入资格时保持只读能力。
- A3：真实 UI 默认补齐、分页预览、目标/范围/时间/SQL、单次确认、零写入预览；原生拒绝未确认、过期及重复提交。
- A4：目标修改/删除/插入、未选筛选字段漂移、范围外同键、结构漂移均拒绝；实库核对整批回滚。
- A5：600 项三批成功；后批冲突保留 256 项已提交；真实 Qt 800 项任务停止于批次边界；进程结果 fixture 覆盖回滚失败/未知提交及进程崩溃的状态，不冒充真实链路 COMMIT 丢包试验。
- A6：BIGINT、DECIMAL、NULL/空串、空/非空二进制、长 Unicode/NUL、微秒时间、JSON、BIT、FLOAT/DOUBLE 实写核对；CHECK 失败回滚。安全简单触发器保持启用且披露；大小写写键、跨表触发器和未选引用列级联均拒绝。
- A7：原生锁定配置、数据/结构互斥、失效/重复保护、退出请求不硬杀当前批次；初始/后续记录保存故障；重启检查内存和磁盘 unknown 状态、已提交计数保留；记录不含密码、主机和业务行参数。
- A8：真实 Qt 的预览→确认→执行→重新比对→记录路径；补齐后已有字段差异仍独立显示。1440/1024 宽度执行按钮可达；D3/D4 回归通过。

## 本轮修复及证据限制

- 实库发现空 QString/空二进制被驱动识别为 NULL，改为显式非 NULL 空值绑定后全矩阵通过。
- 实库发现 status.json 先于扫描进程退出报告 complete 的窗口；桥接等待正常进程结束才公开完整结果。
- 独立复核发现触发器列名大小写绕过和未选引用列级联缺口；限制已补，新增实库回归通过。
- 记录保存失败与重启恢复的内存/磁盘状态已统一，新增生命周期断言通过。
- D4 Qt 密度选项点击先出现超时，独立复跑曾通过，随后 Runtime 再现失败，不能归结为并行。A1–A7 独立验收通过，A8 返回 Build；修正 `desktop_data.mjs`，点击前等待弹窗动画结束且控件未遮挡，选择后等待选中值及弹层关闭。未改业务行为、未削弱断言；本地完整 D4 修复复跑通过，正式结果由 Runtime 记录。
- 复杂触发器、外部引用生成列、无法证明依赖可见性的授权与非 InnoDB 自动写入明确受限。
- 实测样本最多 800 项 D5 操作、2500 行 D4 回归；不推断百万行吞吐。
- Windows 10 x64、最低 macOS 15 实机、干净机器发行及真实网络层 COMMIT 响应丢失注入：NOT RUN。
