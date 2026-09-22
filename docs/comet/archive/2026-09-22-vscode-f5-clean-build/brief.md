# Outcome

每次 F5 都强制重新构建当前 macOS Debug 应用，无论源码和资源内容或哈希是否变化。

# Scope

修改 scripts/build-macos.sh 的原生构建命令，使用 CMake --clean-first；更新 README。F5 和直接运行该脚本都执行清理后重建。前端继续每次执行 npm ci 与 npm run build，重新生成本地资源。

# Non-goals

不增加发行包、签名、Windows 调试或第三方 Qt/MySQL 库的源码重编译；不修改业务功能。

# Acceptance examples

- A1：在源码未改变时连续执行两次 F5 对应构建任务，两次均清理原生目标并重新编译项目 C++、重建资源及链接应用，前端构建每次执行；不因产物哈希相同跳过这些步骤。
- A2：构建或测试失败时仍中止启动；现有原生及前端测试通过，README 明确强制重建及时间成本。

# Constraints and invariants

复用现有工具和脚本；保留配置及调试器行为。清理范围为 CMake 管理的构建产物，不删除用户数据、Qt 安装或 QMYSQL 外部插件。

# Decisions

用 --clean-first 在共享 macOS 构建入口实现强制重建；手动调用脚本同样生效。打包指当前 F5 Debug 应用及内嵌前端资源，沿用既有非发行包范围。

# Open questions

无待澄清问题。

# Verification expectations

不改源码连续运行两次实际构建，核对两次编译和链接日志；运行现有测试并独立验收。GUI 按键未经实测时明确报告。
