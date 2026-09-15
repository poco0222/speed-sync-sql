# Outcome

完成 D7 desktop-release：通过 GitHub Actions 构建 Windows x64 与 macOS arm64 桌面分发包，用户可从工作流运行页面下载，无需依赖本机开发路径。

# Scope

- 来源为 docs/design/roadmap.md 的 D7、总体设计 F21/F22/F23，以及用户明确的“利用git action打包即可”。本轮收敛到 CI 打包交付。
- 新增可手动触发的 GitHub Actions 工作流；Windows x64 与 macOS arm64 独立构建 Release 产物。
- 沿用 Qt 6.10.2、锁定的 React/TypeScript/Ant Design 版本及现有目标 Windows 10 1809+ x64、macOS 15+ arm64。
- 在 runner 上准备匹配的 Qt、QMYSQL 与 MySQL 客户端，构建前端与桌面程序，运行适合 CI 的现有测试。
- 打包本地 React、Qt/WebEngine、QMYSQL、MySQL 客户端与必需间接依赖，检查开发机路径残留及缺失依赖。
- Windows 输出可解压运行的 ZIP；macOS 输出保留应用权限与符号链接的 .app 压缩包。文件名含版本与平台架构，上传为 Actions artifacts。
- 提供工作流触发、产物下载与运行说明、依赖/许可证说明、版本和实际验证范围。

# Non-goals

签名/公证、商店分发、自动更新、自动发布 GitHub Release、安装向导；新增业务功能、性能专项、UI 改版、生产数据库操作。干净机器、最低系统、字体/DPI 和代表性规模专项保留后续待办，不作为本轮 CI 打包完成条件。

# Acceptance examples

- A1：工作流支持手动触发，分别配置 Windows x64 与 macOS arm64 Release 构建；依赖版本及架构明确，前端使用锁文件安装，不依赖开发机绝对路径。
- A2：两平台均构建匹配 Qt 的 QMYSQL，部署 Qt/WebEngine、本地前端、客户端及必要传递依赖；缺失依赖或部署失败使任务失败，不上传伪成功分发包。
- A3：CI 执行前端测试及适合 runner 的原生测试；平台专属或需凭据/图形环境的检查明确区分，不把跳过项算作通过。
- A4：构建成功后上传命名含版本/系统/架构的分发产物；Windows ZIP 可解压，macOS 归档保留 .app 结构、权限和符号链接，不夹带测试数据或凭据。
- A5：文档准确说明触发、下载、解压启动、依赖和未签名限制；明确 CI 构建证据、实际运行证据及最低系统/干净机器 NOT RUN 的区别。

# Constraints and invariants

- Windows 10 与 macOS 15 的既有验证缺口仍按 docs/development/platform-validation-pending.md 保留；CI runner 成功不替代最低系统或干净机器实测。
- 使用最小工作流权限，不引入签名 secrets；不把业务凭据或本地配置放进产物。
- 保留 D1–D6 业务行为；不升级项目技术栈或安装全局工具。
- 本地验证与远程 Actions 实际结果分开报告。没有远程成功运行证据时明确 NOT RUN，不以静态检查代替成功构建。

# Decisions

- 使用 Comet Native，current/main；创建前工作区干净且无其他 active change。
- 用户明确采用 GitHub Actions 打包；因此不要求用户先提供 Windows/macOS 15 验证机器。
- 采用手动触发和 artifacts 下载，默认不自动发布 Release。ZIP/.app 归档是最小分发形式，不增加安装器与签名服务。
- 本轮为同一打包交付链，保持单个 change；平台构建由 Actions jobs 执行，不引入 Supervisor 拆分。

# Open questions

无待澄清问题，等待完整 Shape 确认。

# Verification expectations

检查工作流语法、脚本错误传播、依赖部署、归档结构及命名；复用前端与原生测试。本机可执行的构建/部署检查实际运行，远程 CI 未执行时明确标记。独立复核及 Verifier 按 Runtime 执行。干净机器和最低系统验证独立保留，不能据本轮结果声明已通过。
