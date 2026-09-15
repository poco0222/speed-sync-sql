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
- 完成时间: 2026-09-15T08:26:14.086Z
- 摘要: 独立验收 A1–A8 通过。已核对确认范围、实现、测试断言、现有运行证据和真实桌面截图；未发现阻塞交付的问题。未修改文件，未推进 Comet，未重复运行已通过检查。

## 验收

| 编号 | 结果 | 来源 | 验收项 | 原因 |
| --- | --- | --- | --- | --- |
| A1 | passed | brief.md | A1：数据页复用连接和库表配对，进入不自动扫描；换连接、表、设置或执行结构变更后旧结果失效，迟到响应不能覆盖新上下文。 | 核对 SchemaWorkbench/DataWorkbench 的显式准备、任务代次和失效逻辑，以及 data_bridge.cpp 的 generation/taskId 校验。真实 Qt 检查覆盖显式启动、旧任务拒绝、连接变更与恢复；进入数据页不会自动扫描业务行。 |
| A2 | passed | brief.md | A2：两端可验证的主键、非空唯一键和复合键可建议与选择；不可靠键说明原因，无键表仅分别分页浏览，不按行号生成匹配结论。 | data.cpp metadata/prepare 校验两端完整非空唯一键、复合键及类型/排序规则；排除可空、前缀及不支持键。真实 MySQL 检查覆盖复合主键、非空唯一键、可空键拒绝、无键独立浏览和字符键身份语义。 |
| A3 | passed | brief.md | A3：默认同名兼容字段，可选择参与字段；共享 AND 条件参数化应用两端，原生端拒绝非法字段、操作符和值，不兼容项明确列出。 | 默认同名兼容字段，未参与字段及原因可见。filterSql 使用字段/操作符白名单、类型值校验和绑定参数，两端复用同一 AND 条件。真实检查覆盖精确 BIGINT/BIT 筛选、空范围、非法操作符拒绝及字段子集。 |
| A4 | passed | brief.md | A4：跨批次识别相同、不同、仅左、仅右；两端完整扫描才给出最终计数和完整结论。双端空、单端空、缺表、权限失败、断线和取消不混淆。 | scan 每批 256 行；仅两端成功结束后发布最终缺失状态和计数。真实 2500 行样本验证跨批次四种结果，另验证双端空、取消、缺表、权限失败。查询中断和子进程异常路径均保持 complete=false；单端空结论有前端回归覆盖。 |
| A5 | passed | brief.md | A5：BIGINT、DECIMAL、NULL、空串、0、二进制、时间和 JSON 按类型无损处理；长值差异不因显示截断或哈希相等被漏报，不支持值明确提示。 | 核对完整 canonical 值比较、精确数文本、JSON 精确数解析、FLOAT 提升、HEX 二进制、UTC TIMESTAMP 和定宽 Unicode 原值存储。单元及真实 MySQL 证据覆盖 BIGINT、DECIMAL、NULL/空串、NUL/emoji、微秒、JSON、相邻 FLOAT/DOUBLE 和长值尾部差异。 |
| A6 | passed | brief.md | A6：结果分页，支持状态过滤、完整键定位、变化字段查看；详情左右对齐，长值按需加载，复制原值，搜索无匹配不显示两端一致。 | 结果分页、状态过滤、完整键定位和变化字段均从当前临时结果读取；详情按字段左右对齐，长值按 codepoint 分块，复制完整原值。真实桥接验证分页/定位/长值，真实桌面验证详情和系统剪贴板；无匹配提示不改变整体结论。 |
| A7 | passed | brief.md | A7：后台分批扫描，真实扫描量和阶段，可停止与回收任务，界面可操作；前端不接收整表，失效/关闭清理临时结果，普通配置和日志不保存业务行或密码。 | 扫描运行于独立 QProcess，逐批发布实际扫描量；取消结束子进程并保持未完整，失效/关闭/启动清理临时目录。前端仅接收页和按需详情。真实取消检查通过；731 条读取账号 SQL 审计及样本校验和确认只读，业务行仅进入任务临时存储。 |
| A8 | passed | brief.md | A8：原生/前端回归、隔离 MySQL 和真实 Qt WebEngine 检查覆盖只读比对及 D1–D3 相关行为；记录实际样本、环境与未运行平台项。 | 复用当前 Runtime 四项 exit 0 检查；核对 D4/D3 实测脚本及报告、D4 验证记录和 1024 深色截图。覆盖前端回归、6 个 CTest 套件、18 项 D4 Qt 检查及 D3 MySQL/桌面回归；记录 macOS 27 arm64、Qt 6.10.2、MySQL 8.0.46、实际样本与平台待办。 |

## 检查

| 检查 | 命令 | 工作目录 | 状态 | 退出码 | 耗时 |
| --- | --- | --- | --- | ---: | ---: |
| Frontend regression | --prefix frontend test | . | passed | 0 | 196 ms |
| Native suites | --test-dir build --output-on-failure | . | passed | 0 | 17756 ms |
| D4 real Qt MySQL read-only | tests/integration_data.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d4-integration.json | . | passed | 0 | 7577 ms |
| D3 real regression | tests/integration_sync.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d4-d3-regression.json --desktop-script tests/desktop_sync.mjs | . | passed | 0 | 8578 ms |

## 阻塞项

_无。_

## 风险与跳过的工作

- Windows 10、macOS 15 实机及正式安装包仍为 NOT RUN，沿用已接受的平台待办。
- 实测最大样本为每端 2500 行；未验证百万行性能。深页 LIMIT/OFFSET 与定宽 Unicode 临时存储存在额外成本。
- 非 InnoDB 不保证一致快照；两端快照不是全局同一瞬间，界面已明确说明。
- 现有真实检查未单独注入扫描中途网络断开；本项错误完整性依据查询中断及子进程失败路径核查，不宣称该场景已实测。

## 之前的迭代

| 目标周期 | 迭代 | 尝试 | 结果 | 未解决项 | 摘要 | 完成时间 |
| ---: | ---: | ---: | --- | --- | --- | --- |
| 1 | 1 | 0 | recovery | — | Native Shape artifacts changed | 2026-09-15T08:19:39.242Z |
| 2 | 1 | 1 | pass | — | 独立验收 A1–A8 通过。已核对确认范围、实现、测试断言、现有运行证据和真实桌面截图；未发现阻塞交付的问题。未修改文件，未推进 Comet，未重复运行已通过检查。 | 2026-09-15T08:26:14.086Z |



## 结论

独立验收 A1–A8 通过。已核对确认范围、实现、测试断言、现有运行证据和真实桌面截图；未发现阻塞交付的问题。未修改文件，未推进 Comet，未重复运行已通过检查。
