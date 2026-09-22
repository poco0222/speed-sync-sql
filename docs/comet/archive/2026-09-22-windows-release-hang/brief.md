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

运行 Python 打包回归、真实 ZIP 解压检查、工作流静态检查和 diff 检查；由新的只读复核与 Verifier 核验。远程完整打包已有下列成功证据，替代此前 NOT RUN 的执行记录；原验收条目及独立验收状态不在本次证据补充中修改。

## 远程运行补充证据（2026-09-22 核实）

- 运行：[GitHub Actions 35062158066](https://github.com/poco0222/speed-sync-sql/actions/runs/35062158066)，由 workflow_dispatch 触发，2026-09-16 06:05:15–06:09:19 UTC，整体 completed / success。
- 提交：`18d2fa2a20be697280a6c9540610267d82410dcc`，分支 main；核实时与本地 HEAD 一致。
- [Windows x64](https://github.com/poco0222/speed-sync-sql/actions/runs/35062158066/job/104684572758) 与 [macOS ARM64](https://github.com/poco0222/speed-sync-sql/actions/runs/35062158066/job/104684572529) 的 Build, test and package、Upload distribution 均成功。
- 产物 `speed-sync-sql-windows-x64-14`（183014987 bytes）、`speed-sync-sql-macos-arm64-14`（243521773 bytes）上传成功，核实时 expired=false。本次未下载或启动产物。

| 验收项 | 本次查证的远程证据 | 仍需独立验收核对的边界 |
| --- | --- | --- |
| A1 | Windows 下载 QtBase 6.10.2 ZIP，使用明确的 Python 3.12.10 路径执行 `-m zipfile -e`，6.8 秒完成；macOS 使用 tar.xz，8.3 秒完成解压。 | 成功路径不能单独证明 SHA-256 拒绝错误包、超时和失败禁止上传，需结合代码与回归证据。 |
| A3 | Qt 安装后发布脚本打印 Python 3.12.10 明确路径，Windows 解压及测试均使用该路径；checkout v7.0.1、setup-node v7.0.0、setup-python v7.0.0 步骤成功，两平台矩阵和上传成功。 | Actions 的 Node 24 声明、第四个官方 Action 版本及失败上传门控仍应结合工作流和 Action 元数据核对。 |
| A4 | 两平台日志均显示运行10项发布脚本测试并成功继续；CTest 均为7项全部通过；随后打包和上传成功。 | 与既有本地 actionlint、diff 等检查记录一起交由独立 Verifier 判断，不将此补充材料当作最终验收结论。 |

查证方式：`gh run view 35062158066 --repo poco0222/speed-sync-sql --json conclusion,status,headSha,headBranch,event,jobs,url,createdAt,updatedAt`、对应 `--log`、`gh api repos/poco0222/speed-sync-sql/actions/runs/35062158066/artifacts` 及 `git rev-parse HEAD`。仅查询现有运行，没有触发重跑、推送或修改 GitHub 状态。
