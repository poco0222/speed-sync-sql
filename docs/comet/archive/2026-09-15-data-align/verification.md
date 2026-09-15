---
generated_from_state_version: 11
---

# 验证

## 当前结果

- 结果: **已归档**
- 验证情况: **已完成检查，验证结果已确认**
- 目标周期: 2
- 迭代: 1
- 验证器尝试次数: 1
- 完成时间: 2026-09-15T10:21:32.941Z
- 摘要: 独立只读验收通过，A1–A8 全部 passed。核对正式 brief/spec、当前核心实现、Runtime 日志、实库与 Qt 报告及关键测试断言；未发现本轮阻断项。未修改文件、未执行数据库写入、未推进 Comet。

## 验收

| 编号 | 结果 | 来源 | 验收项 | 原因 |
| --- | --- | --- | --- | --- |
| A1 | passed | brief.md | A1：左→右及右→左完全对齐均能插入源独有、更新同键选定差异字段、删除目标范围内独有记录；未选字段和键不更新。默认补齐及既有合并模式仍不删除。 | desktop/data.cpp:323 buildMerge 仅在 align 纳入目标独有删除，UPDATE 排除键并沿用选定差异字段。tests/integration_align.py:27 双向断言目标增改删结果、未选列保留及源端不变；d6-runtime-integration.json 同时通过双向 fill/merge 不删除回归。 |
| A2 | passed | brief.md | A2：仅完整有效扫描和可靠兼容键可生成计划；取消、失败、不完整、无键、非 InnoDB 或依赖不可见均拒绝。共享筛选范围外记录保留，范围外同键按目标真实键语义报冲突；空源范围可列出范围内全部删除，未确认不得执行。 | buildMerge 校验完整成功扫描、任务身份、可靠兼容键及结构基线；mergeGuard 拒绝非 InnoDB 和依赖不可见。Runtime 实库通过 failed/cancelled/incomplete、无键、MyISAM、共享范围保护、排序规则等价范围外同键冲突及空源/空目标/双空场景。mergeOperation:75 与真实 Qt 检查证明非零删除缺少确认不能执行。 |
| A3 | passed | brief.md | A3：预览显示实际目标、方向、模式、键、字段、共享条件或全表、读取时间、增改删数及分页 SQL/样例；删除明细单独可查。存在删除时须明确勾选认可删除数量与范围，再作执行摘要确认；未确认、旧计划、重复请求或迟到响应均不启动额外写入。 | DataMergePanel 展示目标、模式、键字段、范围、读取时间、数量和参数化 SQL，独立删除分页及删除确认后再作摘要确认。tests/desktop_align.mjs:25–68 验证实际 Qt 删除分类、未确认禁用、新计划清除确认、摘要目标与全表范围；真实 Bridge 及 merge_bridge_test.cpp:41 验证缺少确认、失效及重复请求拒绝。已查看 d6-delete-1024.png，删除数量及整行含义可见。 |
| A4 | passed | brief.md | A4：删除前在同批事务中按可靠键锁定目标，核对完整旧值及范围；目标已改、已删或移出范围、结构/权限漂移均停止并回滚本批，不覆盖并发变化。源后续变化不加入已确认快照，新增目标记录不被扩大删除。 | desktop/data.cpp:364 executeMergeBatch 在同批事务中取得目标 MDL、复核结构基线，并按可靠键 SELECT FOR UPDATE 比较全部目标旧值后 DELETE，要求影响一行。Runtime 实库验证目标已改、已删、移出范围与结构漂移均失败回滚；源后续新增与扫描后目标新键不扩大已确认删除集合。权限不足和依赖不可见受既有基线检查及实库回归覆盖。 |
| A5 | passed | brief.md | A5：仅成功提交批次计入已提交；约束/SQL 失败回滚当前批并停止后续批，先前提交保留；停止在可控边界生效。记录区分已提交、已回滚、未执行与待核实，进程异常和未知提交不自动重试。 | executeMergeBatch 仅在 COMMIT 明确成功后返回提交数量；回滚无法确认及提交异常返回 unknown。merge_bridge.cpp:93 advanceMerge 仅累计确认成功批次，失败或未知停止后续批次。Runtime 验证 600 删除三批成功、后批冲突仅保留前 256 条提交，以及真实 Bridge 停止后提交 256/800、实际数据库剩余行匹配。原生进程 fixture 覆盖 failed/unknown/crash 和待执行批次；真实网络 COMMIT 丢包未执行。 |
| A6 | passed | brief.md | A6：删除保持触发器及外键启用。RESTRICT/NO ACTION 引用冲突可解释并回滚；会导致跨表删除/修改的 CASCADE、SET NULL 和无法证明副作用边界的目标触发器阻止自动执行，预览说明具体对象。精确复合键、排序规则等价键、BIGINT/DECIMAL、NULL、二进制及完整旧值校验不失真。 | desktop/data.cpp:290 deleteGuard 对 CASCADE、SET NULL 及未知删除规则按具体外键对象阻止；mergeGuard 保留既有触发器边界。tests/integration_align.py:114 分别断言 RESTRICT/NO ACTION 可预览但引用冲突回滚，CASCADE/SET NULL 预览拒绝且子表不变；删除触发器跨表副作用被阻止。实库精确复合 BIGINT/二进制键、DECIMAL、长文本、NULL、微秒旧值及排序规则键回归通过。 |
| A7 | passed | brief.md | A7：上下文变化使旧计划及删除确认失效，执行与结构写入互斥并保护退出；持久记录含完全对齐模式、删除计划数量、实际批次结果及脱敏错误，不保存可重放业务参数。重启不恢复可执行计划，未完成记录标待核实，临时值按既有生命周期清理。 | 前端 generation 与计划重建清除旧计划和删除确认；后端 mergeGeneration、计划身份、连接摘要及执行互斥阻止过期或并发写入。记录增加 data-align 和删除数量，不保存行参数；sync_bridge.cpp:100 将未完成 alignment 记录恢复为 unknown。原生 deleteConfirmation、nativeContextLocked、stopJobsWaitsForBatch、crashRecovery(align) 通过；真实 Qt 记录、页面重载及 D4 生命周期回归通过。沿用既有临时值清理链，无新增持久业务值。 |
| A8 | passed | brief.md | A8：真实 Qt 数据页完成完全对齐预览、删除确认、执行、失败/停止反馈、记录和原上下文重新比对；成功执行与复核一致分开表达。前端、原生、D3/D4/D5 受影响回归通过，未执行的平台与故障注入如实标记。 | 已核对 Runtime 当前 8 项正式检查日志：前端 18 项及构建、原生构建及 7 组测试、D5/D6 55 项实库与 15 项真实 Qt、D3 14 项实库、D4 实库只读审计与 18 项 Qt、diff-check 全通过。tests/desktop_align.mjs 验证预览、两次确认、执行后同上下文重扫零操作、实际外键失败的回滚反馈、停止记录及删除数量。停止由真实 Bridge 发起，未将其描述为鼠标停止按钮全链验证。 |

## 检查

| 检查 | 命令 | 工作目录 | 状态 | 退出码 | 耗时 |
| --- | --- | --- | --- | ---: | ---: |
| frontend-tests | --prefix frontend test | . | passed | 0 | 178 ms |
| frontend-build | --prefix frontend run build | . | passed | 0 | 1223 ms |
| native-build | --build build --parallel 4 | . | passed | 0 | 608 ms |
| native-tests | --test-dir build --output-on-failure | . | passed | 0 | 24626 ms |
| d6-integration | tests/integration_merge.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d6-runtime-integration.json --align | . | passed | 0 | 27673 ms |
| d3-regression | tests/integration_sync.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d6-runtime-d3.json | . | passed | 0 | 5528 ms |
| d4-regression | tests/integration_data.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d6-runtime-d4.json | . | passed | 0 | 8550 ms |
| diff-check | diff --check | . | passed | 0 | 19 ms |

## 阻塞项

_无。_

## 风险与跳过的工作

- Windows 10 x64、最低 macOS 15 实机及发行验证 NOT RUN；不属于本轮新增发行承诺。
- 真实网络层 COMMIT 丢包 NOT RUN；unknown 和进程异常采用原生进程 fixture 验证，不能替代网络故障注入。
- 停止已通过真实 Bridge、数据库实际剩余行和 Qt 停止记录验证；鼠标点击停止按钮的完整链路 NOT RUN。
- 前端构建存在大于 500 kB 的 chunk 提示；构建通过，不影响本轮验收。

## 之前的迭代

| 目标周期 | 迭代 | 尝试 | 结果 | 未解决项 | 摘要 | 完成时间 |
| ---: | ---: | ---: | --- | --- | --- | --- |
| 1 | 1 | 0 | recovery | — | Native Shape artifacts changed | 2026-09-15T10:14:08.560Z |
| 2 | 1 | 1 | pass | — | 独立只读验收通过，A1–A8 全部 passed。核对正式 brief/spec、当前核心实现、Runtime 日志、实库与 Qt 报告及关键测试断言；未发现本轮阻断项。未修改文件、未执行数据库写入、未推进 Comet。 | 2026-09-15T10:21:32.941Z |



## 结论

独立只读验收通过，A1–A8 全部 passed。核对正式 brief/spec、当前核心实现、Runtime 日志、实库与 Qt 报告及关键测试断言；未发现本轮阻断项。未修改文件、未执行数据库写入、未推进 Comet。
