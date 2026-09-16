# D1–D7 交付复核

日期：2026-09-15。基线：`main / 0bd8811`。复核请求：检查 D1–D7 的缺漏、问题及偏离项。

结论：最终确认范围的核心功能已落地，现有回归通过；发现 **3 个 P2 问题**，主要集中在跨阶段状态衔接和多实例恢复。尚不能将“全部 change 归档”表述为“双平台日常可用已完整验收”。本报告不是重新执行 Comet 正式验收；不更改已归档状态，也未修改业务实现。

## 发现的问题

### R1 · P2 · 数据写入未锁定上层工作台上下文

- 位置：`frontend/src/main.tsx:48`、`frontend/src/main.tsx:123`、`frontend/src/DataWorkbench.tsx:22`、`frontend/src/SchemaWorkbench.tsx:59`。
- 原因：数据扫描与数据写入共同通过 `onDataRunning` 更新 `dataRunning`；全局 `unavailable` 只包含 `syncRunning`。因此数据写入时，连接编辑、连接切换和库表输入仍可操作。
- 触发：开始较长的数据合并或完全对齐，在执行期间编辑连接或修改表名。前端先调用 `invalidate()`、清除任务并重建数据组件，再请求原生保存；原生因 `mergeExecuting` 拒绝修改，但前端已失去原任务上下文，表名还可能显示尚未保存的新值。重新比对入口依赖 `taskId`，可能随之不可用。
- 影响：执行仍针对原目标，原生保护未被绕过；但违反 D5/D6“执行中锁定上下文”的约定，用户看到的选择可能与正在执行的目标脱节。页脚还会把数据写入显示为“数据读取 / 比对进行中”。
- 证据：静态调用链确认；既有原生 `nativeContextLocked` 测试通过，证明原生拒绝有效。此次未单独复现写入中修改控件的完整鼠标操作。
- 建议：单独上报数据写入状态，纳入连接、库表和上下文操作的禁用条件；保留只读扫描期间可切换连接的既定行为。原生拒绝时不得先破坏前端上下文。

### R2 · P2 · 保存外观设置后，前端仍展示已被删除的扫描结果

- 位置：`desktop/foundation.cpp:289`、`frontend/src/main.tsx:149`、`frontend/src/DataWorkbench.tsx:29`。
- 原因：所有 `settings` 保存都会调用 `invalidateData()`，清空数据状态、计划及临时结果。前端只更新 settings；没有同步清除 `taskId`、完成状态、结果页及预览资格。扫描完成后轮询已停止，无法自行发现失效。
- 复现：完成数据比对 → 设置深色/紧凑 → 保存 → 界面仍显示扫描完成，数据写入预览按钮可点 → 点击后提示“需要当前完整且有可靠键的数据比对”。
- 影响：只改主题/密度也会丢掉现有数据工作；界面保留不再有效的结果，详情或预览随后失败。
- 证据：真实 Qt WebEngine + 独立 MySQL 已复现。`.local/review-d1-d7/settings-stale-repro.json` 保存设置后界面及点击后结果；D4 原有 18 项检查及只读审计同时通过。
- 建议：纯主题/密度变更不失效数据库任务；确需失效的设置变更必须同步清除前端状态并说明需要重新扫描。补“保存设置后继续详情/预览”的回归。

### R3 · P2 · 第二实例会把仍在运行的第一实例记录改成异常恢复

- 位置：`desktop/foundation.cpp:106`、`desktop/sync_bridge.cpp:94`、`desktop/data_bridge.cpp:15`。
- 原因：每次构造 `Foundation` 都调用 `recoverSyncRecords()`，无条件将持久化的 `running` 记录改为 `unknown`。现有 `owner.lock` 只保护各自的数据临时目录；没有约束同一应用数据目录的多实例使用，也不检查记录所属执行方是否仍存活。
- 触发：第一实例正在写入时，第二实例使用相同配置目录启动。第二实例立即把仍在执行的记录写为“上次执行未正常结束”。
- 影响：历史状态失真；两个实例对同一记录的认知不一致。第二实例缓存的状态也不会随第一实例结束自动刷新。恢复逻辑不能仅凭 `running` 推断前次崩溃。
- 证据：使用现有原生生命周期 fixture 和当前 `libfoundation.a`，第一 `Foundation` 开始真实子进程批次后构造第二个。输出：`firstStatus=running`、`firstStillBusy=true`、`secondStatus=unknown`、`secondBatchStatus=unknown`。证据在 `.local/review-d1-d7/multi-instance-repro.json`；这是生命周期复现，不是数据库故障注入。
- 建议：同一配置目录限定单实例，或给持久记录增加执行方存活判定，仅恢复确定已结束的执行。前者实现更小。

## 范围与偏离核对

| 阶段 | 复核结论 |
| --- | --- |
| D1 | 桌面、桥接、连接与凭据实现存在；Windows 10/macOS 15 实机验证经用户接受移为待办。R2 涉及设置与后续数据能力衔接。 |
| D2 | 结构化元数据、缺失/失败/不支持语义、浏览及导出已实现；本次结构单元测试和 D3 实库链路回归通过。 |
| D3 | 计划、选区、依赖、预览、顺序执行、快照与复核已实现；外键/CHECK、生成列、函数索引、分区等仅展示属于明确范围，不能算漏做。R3 影响记录恢复。 |
| D4 | 可靠键、精确值、共享筛选、完整性、分页及临时结果已实现；真实 Qt/实库/只读审计通过。存在 R2。 |
| D5 | 补齐/合并、旧值冲突检查、分批事务、停止与记录已实现；存在 R1、R3。 |
| D6 | 显式删除确认、范围内对齐、并发旧值校验及跨表删除限制已实现；存在 R1、R3。 |
| D7 | 最终确认收敛为 GitHub Actions 打包，工作流、脚本及本机包证据存在；不是原路线中全部日常可用专项均已验收。 |

D1 的范围调整见 `platform-validation-pending.md`；D7 的调整见 `docs/comet/archive/2026-09-15-desktop-release/brief.md:7`、`:17`、`:32`。这些是已确认的延期，不是本次发现的擅自偏离。

尚需闭环：

- Windows/macOS 两个 Actions job 的实际成功记录、产物上传下载和 Windows 解压运行。D7 文档标为 NOT RUN；本次未查询或触发远程工作流。
- Windows 10 1809+ x64、macOS 15 arm64、无开发工具机器的启动、连接、凭据、导出与缩放。
- 字体/DPI、主题/密度专项与代表性数据规模。既有局部窗口检查、2500 行比对样本和数百项写入不能替代专项验收。
- 真实网络层 COMMIT 响应丢失注入、D6 鼠标点击停止按钮全链。现有边界停止和未知结果 fixture 有效，但不等于上述两项已经实测。

## 本次验证

| 检查 | 结果 |
| --- | --- |
| Comet Native 状态 | 8 个 change 均 done/archived；D1–D7 为其中 7 个，其余为规划 change |
| 前端测试 | 18/18 通过 |
| TypeScript/Vite 构建 | 通过；保留既有大于 500 kB 的 chunk 警告 |
| 原生构建 | 通过 |
| CTest | 7/7 套件通过；平台凭据专项仍为 opt-in，不因套件通过视为已运行 |
| 打包脚本 unittest | 8/8 通过；非完整双平台打包实跑 |
| D1 独立临时 MySQL/QMYSQL | 8 项连接、TLS 和错误分类检查通过 |
| D3 独立临时 MySQL | 14 项通过 |
| D5/D6 独立临时 MySQL | 53 项通过，`--align --skip-desktop`；此数量不含真实桌面附加检查 |
| D4 真实 Qt + MySQL | 原有 18 项及只读审计通过，另增加 R2 复现检查 |
| R3 生命周期复现 | 已确认活跃执行被第二实例标 unknown |

主要证据：`.local/evidence/d1-d7-review-probe.json`、`.local/evidence/d1-d7-review-schema-sync.json`、`.local/evidence/d1-d7-review-merge.json`、`.local/review-d1-d7/settings-integration.json`、`.local/review-d1-d7/d4-desktop.json`。复现程序及输出全部位于已忽略的 `.local/`，不加入项目测试或提交；数据库仅为脚本自建临时实例，未使用现有业务连接。

建议先修 R1/R2，再补 R3 和相应回归；之后推进双平台 CI 与目标系统补验。此次未发现可确证的 P0/P1 问题，但测试通过不代表所有平台、规模与故障路径均已覆盖。
