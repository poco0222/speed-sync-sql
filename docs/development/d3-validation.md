# D3 开发验证

环境：macOS 27 arm64，Qt 6.10.2，MySQL 8.0.46，Node 24.19.0。沿用项目工具链，无全局安装/升级。目标仍为 Windows 10 1809+ x64 / macOS 15+ arm64。

## 实现

原生端生成与持有计划；左右单向写入、对象选区/整表对齐、确证缺表创建、依赖限制、逐对象增改删摘要、只读 SQL 预览/复制/导出、写前两端漂移核对和原结构持久化、顺序执行/停止、逐步结果、复核及本地记录。前端沿用 Ant Design 布局 B，提供执行记录入口，不提供任意 SQL 执行。

常规字段、索引/主键/全文/空间索引、表属性与基本触发器已实现。外键/CHECK、生成列、函数索引、分区、引擎转换及未知特性仅展示。依赖无法证实时阻止；触发器保留原 DEFINER，不自动替换正文引用，同事件顺序与非 UTF-8 客户端编码保守受限。

## 检查与证据

- `npm --prefix frontend test`：11 项通过；桥接、迟到请求、差异展示、选区、显式相同依赖、统计、记录边界。
- `npm --prefix frontend run build`：TypeScript/Vite 通过；沿用大包警告。
- DevTools CMake `cmake --build build --parallel 4`：原生程序及测试构建通过。
- `ctest --test-dir build --output-on-failure`：5 组，覆盖 D1/D2 与新增 SQL 规则、同步生命周期。
- `python3 tests/integration_sync.py --mysql-home /Users/PopoY/Documents/DevTools/mysql/current --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql --output .local/evidence/d3-integration.json --desktop-script tests/desktop_sync.mjs`：15 项真实 MySQL、8 项真实 Qt WebEngine 检查通过，完整输出与截图保存在忽略的 `.local/evidence/`。

真实 MySQL 检查含字段属性/顺序/增删、普通/唯一/主键/全文/空间索引、表属性、反向与异名配对、缺表创建、源缺失不删除、触发器新增/替换/删除及分号正文（用实际 INSERT 验证触发效果）、DEFINER/sql_mode/字符集上下文、漂移后目标不变、入向外键/生成列阻止及部分选区不扩大。所有写入仅在脚本创建的临时 loopback 实例，退出清理。

真实桌面检查通过控件选择对象、预览、一次确认、执行、复核、记录详情与页面重载；1440×900、1024×800、1024 深色/紧凑布局主动作可达。PNG 为本机 Qt WebEngine 截图，不是浏览器模拟产品数据。

生命周期测试用独立测试可执行文件的可控子进程，验证重复提交/上下文锁、计划失效不取消比对、语句边界停止、失败后保留未执行步骤、进程丢失标未知、写前记录失败零启动、步骤结果落盘失败停止、跨 Foundation 重建恢复未完成记录，以及结构指纹忽略自增计数但保留身份/类型变更。该故障注入不替代真实 SQL 证据。

## 修复证据

- 触发器仅删除路径：避免非 const JSON 下标访问把空对象变为非空，独立删除测试及真实 MySQL 复测通过。
- 分离比对与计划代次，选区失效不使正在读取的比对无故过期；原生回归覆盖。
- 初始状态不返回伪空快照/记录，前端另校验记录形状。
- 相同索引/触发器可作为明确关联操作选择；普通选区不自动扩展。
- 同步依赖权限仅匹配真实全局/库级授权，不以虚构对象名模拟授权范围。
- 新测试曾因临时 QJsonValueRef 悬空崩溃；改持有 QJsonValue，生命周期组通过。此为测试代码故障。

## 限制

Windows 10/macOS 15 实机、其他 MySQL 版本、压力规模、正式安装包：NOT RUN。沿用已接受平台待办，本机通过不等于双平台通过。现有角色/复杂授权保守处理。

写前检查只证明可观察时刻，两端不是统一快照，不保证检测与执行间没有外部变更。多条 DDL 不保证整体回滚；原结构不能恢复删除的数据。断线/异常结果保留待核实，不自动重试。

独立 Builder 复核与最终 Verifier 由 Comet Runtime 分别记录；本文仅记录开发证据，不替代验收状态。
