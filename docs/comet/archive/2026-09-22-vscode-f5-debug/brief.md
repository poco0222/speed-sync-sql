# Outcome

在当前 macOS arm64 开发机，用 VS Code 打开项目根目录后按 F5，构建并调试 Qt/C++ 桌面应用。

# Scope

新增项目级 VS Code 启动与构建任务配置，复用现有 Qt 6.10.2、CMake、Ninja、QMYSQL 和已安装的 C/C++ 扩展；补充 README 使用说明。F5 构建前端资源和 Debug 原生程序，构建失败时不启动旧程序。

# Non-goals

不增加 Windows 调试配置、React 浏览器断点、子进程自动附加或发行打包功能；不修改业务行为。

# Acceptance examples

- A1：VS Code 默认启动配置指向当前项目的 macOS Debug 桌面程序，使用已安装的 C/C++ 扩展和 LLDB；任务与配置引用正确，无敏感信息。
- A2：启动前任务按顺序构建前端资源、配置及编译原生程序，包含 QMYSQL 插件；任一步失败使任务失败，不自动启动旧程序。
- A3：在当前 macOS 上验证构建、调试器启动及 C++ 源码断点；README 明确 F5 操作、前提和前端/子进程调试边界。未能实测的环节明确报告，不以静态检查替代。

# Constraints and invariants

沿用当前 main 分支与工作目录；工具复用 DevTools 路径和现有环境变量，不安装全局工具。保留现有构建测试入口和应用数据行为；验证时使用独立临时数据目录，不连接现有数据库。

# Decisions

按当前环境和项目 README 将本次范围限定为 macOS 原生主进程调试；使用已安装的 ms-vscode.cpptools，其目录内已有 lldb-mi。配置采用工作区相对路径，工具路径沿用现有脚本约定。最终范围待用户确认。

# Open questions

无待澄清问题。

# Verification expectations

检查 JSON、任务顺序和路径；运行配置对应的构建命令，实测调试器及源码断点，并由独立 Verifier 核对全部验收项。若 VS Code 交互验证受环境限制，保留明确边界。
