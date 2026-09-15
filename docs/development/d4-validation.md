# D4 开发验证

环境：macOS 27 arm64、Qt 6.10.2、MySQL 8.0.46、Node 24.19.0。复用现有 DevTools 工具链，不升级全局组件。目标平台仍为 Windows 10 1809+ x64 / macOS 15+ arm64；对应实机待办不变。

## 实现与边界

数据标签复用现有连接与库表配对；显式准备、可靠主键/非空唯一键/复合键、同名兼容字段选择、共享 AND 条件、只读分批扫描、后端 SQLite 临时结果、分页与完整键定位、左右详情、长值分块和原值复制。无可靠键仅左右独立浏览。

每批 256 行；前端不接收整表。精确整数和小数保留文本，JSON 保留数值精度，FLOAT 先提升 DOUBLE 再文本化，二进制与 BIT 使用明确 HEX，时间保留微秒、TIMESTAMP 会话时区 UTC。临时定宽 Unicode BLOB 保留 NUL 与 emoji，不用缩略文本或单一哈希判定相同。

中断/失败保持未完整；换连接、表、比对设置或执行结构变更会失效并清理数据任务。非 InnoDB 明确提示快照局限；两端不代表全局同一时刻。不新增数据写入、同步计划或业务执行记录。

## 检查与证据

- `npm --prefix frontend test`：15/15 通过，覆盖完整性结论、原值表示、精确键、类型筛选和同步复制后备路径；包含既有前端回归。
- `npm --prefix frontend run build`：TypeScript/Vite 通过，保留既有大包提示。
- DevTools CMake 构建通过；CTest 6/6 通过。后续 FLOAT/BIT 与原值修复的 `data-comparison` 再次通过。
- `tests/integration_data.py`：18 项真实 Qt WebEngine/原生桥接检查通过，另通过只读 SQL 审计与样本校验和核验。证据在忽略的 `.local/evidence/d4-integration.json`、`d4-desktop.json` 与 `d4-*.png`。
- `tests/integration_sync.py --desktop-script tests/desktop_sync.mjs`：D3 回归 15 项真实 MySQL、8 项真实桌面检查通过，证据在 `.local/evidence/d4-d3-regression.json`。

D4 样本包括 BIGINT 超出 JavaScript 安全整数、DECIMAL(40,15)、NULL/空串、含 NUL/emoji 的文本、24,001 codepoint 长值尾部差异、BLOB、BIT(9)、DATETIME/TIMESTAMP/TIME 微秒、JSON 大整数、相邻 FLOAT/DOUBLE、字符键大小写与 PAD SPACE、非空唯一键、可空键拒绝、无键浏览、2,500 行跨批次差异、参数筛选、取消、失效、权限与缺表。

真实 UI 检查覆盖显式准备/扫描、1440×900 和 1024×800 主动作可达、左右详情、BIGINT 原值复制、深色/紧凑设置。剪贴板核验先完整保留用户剪贴板，结束恢复；测试报告不保存原剪贴板内容。

所有数据库仅来自脚本创建的临时 loopback MySQL 实例。数据读取账号仅有 SELECT；审计 Query/Prepare/Execute 语句，样本前后校验和不变。初始化样本由脚本管理员完成，未连接现有业务库。

## 修复证据

- FLOAT：原始 `CONVERT(flt USING utf8mb4)` 把两个相邻值都转成 `1`；提升 DOUBLE 后分别为 `1.0000001192092896`、`1.000000238418579`，端到端变化字段检查通过。
- SQLite TEXT 的 length/substr 对 NUL 有截断风险；改原值为定宽 Unicode BLOB，并按 codepoint 分块，真实 NUL/emoji/长值验证通过。
- 扫描初期无可分页结果不再残留错误横幅；完成后再请求分页并清理分页错误。
- Qt 异步 Clipboard API 会在测试环境悬挂；改用既有 Ant Design 同类同步 copy 事件后备路径，真实系统剪贴板值核验通过。
- 页脚独立显示数据读取状态，不因数据扫描锁住连接切换。

## 限制

深页 LIMIT/OFFSET 增加服务器工作；定宽 Unicode 临时存储高于原 UTF-8 磁盘占用。仅报告上述样本，不承诺百万行耗时或吞吐。非 InnoDB 不能保证一致性快照，两端不是同一全局瞬间。

Windows 10/macOS 15 实机、其他 MySQL 版本、正式安装包：NOT RUN。沿用已接受平台待办；本机通过不等于双平台通过。

独立 Builder 复核已通过，执行标识 `/root/d4_review`。最终验收与工作流状态以 Comet Runtime 为准，本文不替代 Verifier 结果。
