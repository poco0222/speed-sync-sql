# Outcome

完成 D1 desktop-foundation：可启动的真实桌面窗口、Qt/React 桥接、统一 Ant Design 框架和左右两端连接管理及测试。用户指定最低系统 Windows 10、macOS 15；提交最终确认的具体基线为 Windows 10 1809+ x64、macOS 15+ arm64。

# Scope

- 依据 PRODUCT.md、docs/design/overview.md 的 F01/F02，以及 F21/F23 的 D1 部分；交互沿用 docs/design/interaction-design.md，阶段边界沿用 docs/design/roadmap.md。
- 初始化 C++/Qt 与 React/TypeScript 工程；Qt WebEngine 承载本地前端，WebChannel 异步通信，数据库连接任务不阻塞窗口。
- 连接新增、编辑、复制、删除、保存、测试与左右端选择；字段包括名称、主机、端口、用户名、密码、可选默认数据库，以及必要 TLS/超时参数。
- 保存草稿不要求网络可达；测试失败保留输入；修改参数后旧测试结果失效；历史成功不表示当前在线。
- 密码默认仅本次使用，可选记住；使用 Windows Credential Manager 或 macOS Keychain，不在普通配置、前端持久化存储或日志中保存明文密码。
- 基础主题、密度及连接超时设置；中文界面和原生窗口操作；提供原生保存能力与可复核的桥接任务状态。
- 工程构建说明、驱动构建/加载说明与本机实际检查证据；Windows 10/macOS 15 的启动及 QMYSQL 实测转为后续待办。

# Non-goals

- D2-D7 的库表读取、结构/数据比对、同步 SQL、数据库写入、执行记录业务和正式发行安装包。
- 不以示例数据制造连接或比对成功；未实现的后续操作不做成可用按钮。
- 不新增服务端、消息队列或多 change 拆分；不初始化 Git 或 CodeGraph。

# Acceptance examples

- A1：目标平台、架构、Qt 及构建工具版本明确；锁定 React 19.2.7、TypeScript 6.0.3、Ant Design 6.4.5，前端与原生工程可按说明构建。
- A2：真实 Qt 桌面窗口加载本地 React 界面；桥接可返回成功、失败与任务状态；原生文件保存成功和取消均可正确反馈。
- A3：连接可新增、编辑、复制、删除与持久化；两端选择独立，删除配置不操作数据库；保存不依赖网络可达。
- A4：密码默认仅会话使用，选择记住后按平台凭据机制保存与读取；取消记住或删除连接可清理对应凭据，不在普通文件或日志暴露密码。
- A5：真实 MySQL 连接测试可分别报告成功、认证失败、不可达与驱动缺失；任务执行不阻塞窗口，旧请求或参数变更前的结果不覆盖当前状态。
- A6：中文 Ant Design 对照工作台骨架与连接抽屉可用；主题、密度和超时设置生效；缩放、键盘及窄窗口下关键操作可达，无虚假比对结果。

# Constraints and invariants

- Qt 复用 DevTools 内兼容版本；项目依赖与生成物留在项目。系统固定位置安装另行确认。
- 原生数据库连接遵循 Qt 线程归属；桥接限定本地应用调用，不向任意远程页面暴露凭据或数据库接口。
- 测试仅连接明确提供的测试环境或项目专用本地样本，不执行生产写入。
- 尚未取得的跨平台证据明确记为 NOT RUN，不将其标为通过。

# Decisions

- 用户已要求启动 D1；采用 Comet Native 单一 change、当前目录。当前目录不是 Git 仓库。
- 已确认产品方案继续有效：Qt6 + React + Ant Design，布局 B 左右对照工作台，不重新选择布局。
- 用户明确最低系统为 Windows 10、macOS 15。CPU 未另行指定，最终确认采用 Windows x64、macOS arm64；本轮不含 Intel Mac、Windows ARM 或 32 位构建。
- Qt 6.10 官方平台表列出 Windows 10 1809+ x64、macOS 13+ arm64，故 Windows 最低小版本拟为 1809；macOS 产品最低版本设为 15。文档依据不是产品运行证据。
- 本机实际环境：macOS 27.0 arm64；复用 DevTools Qt 6.10.2，WebEngineWidgets 与 WebChannel CMake 配置存在。CMake 3.30.5、Ninja 1.12.1、Node 24.19.0、Apple clang 21.0.0 均已查询版本。Windows 使用 Qt 6.10.2 MSVC 2022 x64 工具链，具体已安装版本在目标环境记录。
- Qt 6.10.2 当前 sqldrivers 目录没有 QMYSQL 插件，需在实施阶段补齐并验证；组件配置存在不等于运行通过。
- 2026-09-15，用户先接受当前交付，再明确要求归档。本次归档验收为已通过的 A1-A6；原 A7 双平台实测移为后续待办，不再阻塞 D1 归档。Windows 10/macOS 15 支持目标保留，实际运行仍为 NOT RUN，不能用本机 macOS 27 或 minos 15.0 替代。
- 本轮范围紧密依赖同一桌面外壳和桥接，采用单 change，不拆 Supervisor/Child。

# Open questions

无额外产品问题。用户已接受当前交付并要求归档；原 A7 在后续取得目标平台环境后补验，见 docs/development/platform-validation-pending.md。

# Verification expectations

- 先完成平台边界和完整目标 Spec，按 Runtime 准备 Shape 确认后进入 Build。
- 实施后运行前端类型检查/构建、原生构建、连接存储及桥接相关测试、真实窗口检查；目标系统 QMYSQL 加载与测试连接检查保留为后续待办。
- Builder 提交前独立只读复核；Verify 使用新的只读 Verifier，结论保留实际环境与未运行项。
