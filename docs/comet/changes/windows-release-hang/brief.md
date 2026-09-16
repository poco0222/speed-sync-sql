# Outcome

修复 Windows CI 在 QtBase 解压阶段无限等待的问题，使打包过程可定位、可限时失败；消除 macOS 官方 Actions 的 Node 20 弃用警告。

# Scope

Windows 源码解压实现及超时、下载和执行阶段日志、CI Python 3.12 绑定、四个官方 Actions 更新、相应回归测试与发行说明。

# Non-goals

不改变业务代码、Qt/MySQL 版本、平台矩阵、产物格式、签名或发布策略；不自动推送或触发远程运行。

# Acceptance examples

- A1：Windows QtBase 使用官方 ZIP 与 SHA-256 校验，由指定 Python 解压并有超时；macOS 保留 tar.xz。解压失败或超时终止流程，不上传产物。
- A2：下载、解压、编译、测试和打包日志立即显示阶段开始、结果及耗时；失败和超时可定位，成功流程仍生成原格式产物。
- A3：Qt 安装不覆盖 Python 3.12，发布脚本使用 setup-python 的明确路径；四个官方 Actions 使用已核实的 Node 24 版本，原构建矩阵与上传门控保留。
- A4：既有打包测试与新增解压、超时、日志回归通过，工作流检查通过；明确区分本地验证与未运行的 Windows/macOS 远程打包。

# Constraints and invariants

保留 60 分钟 job 上限。源码包 SHA-256 校验不得移除。沿用标准库和既有工具，无新增依赖。平台实跑不得用本地测试代替。

# Decisions

用户在上一轮四项具体修复建议后回复“可以”，已授权该范围实施和必要验证，不重复索要相同确认。当前工作区干净，使用 main 当前目录。Windows 改用 Qt 官方 ZIP，避免依赖 PATH 中的 tar/xz 实现。远程推送、重跑与归档接受仍按各自授权边界处理。

# Open questions

无。

# Verification expectations

运行 Python 打包回归、真实 ZIP 解压检查、工作流静态检查和 diff 检查；由新的只读复核与 Verifier 核验。远程 Windows/macOS 完整打包标记 NOT RUN。
