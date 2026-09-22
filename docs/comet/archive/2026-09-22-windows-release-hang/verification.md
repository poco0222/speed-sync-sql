---
generated_from_state_version: 25
---

# 验证

## 当前结果

- 结果: **已归档**
- 验证情况: **已完成检查，验证结果已确认**
- 目标周期: 2
- 迭代: 1
- 验证器尝试次数: 1
- 完成时间: 2026-09-22T02:05:31.851Z
- 摘要: 独立核验当前候选与A1-A4，无阻断发现。复用3项有效Runtime检查，补充1项真实超时及校验失败检查；核实同HEAD两平台远程成功记录与四个官方Action的Node24声明。可以进入归档。

## 验收

| 编号 | 结果 | 来源 | 验收项 | 原因 |
| --- | --- | --- | --- | --- |
| A1 | passed | brief.md | A1：Windows QtBase 使用官方 ZIP 与 SHA-256 校验，由指定 Python 解压并有超时；macOS 保留 tar.xz。解压失败或超时终止流程，不上传产物。 | scripts/release.py:22,60-65,107-118 使用 Qt 官方 ZIP（Windows）或 tar.xz（macOS），校验 SHA-256 后解压；ZIP 使用当前 Python，解压上限300秒。异常向上传播，工作流上传步骤保留默认成功门控。Runtime bounded-timeout-checksum 实测超时传播及校验失败阻断通过。远程运行35062158066记录 Windows ZIP 6.8秒、macOS tar.xz 8.3秒解压成功。 |
| A2 | passed | brief.md | A2：下载、解压、编译、测试和打包日志立即显示阶段开始、结果及耗时；失败和超时可定位，成功流程仍生成原格式产物。 | scripts/release.py:25-41,48-57,319-340,349-350,375-384 对下载、外部命令、部署及归档输出 flush=True 的开始、成功/失败和耗时。既有失败日志回归与 Runtime 真实超时检查通过；超时日志含 timed out。两平台远程日志展示阶段完成，产物继续使用 Windows ZIP 和 macOS tar.gz。 |
| A3 | passed | brief.md | A3：Qt 安装不覆盖 Python 3.12，发布脚本使用 setup-python 的明确路径；四个官方 Actions 使用已核实的 Node 24 版本，原构建矩阵与上传门控保留。 | .github/workflows/desktop-release.yml 保留60分钟上限、windows-2022 x64与macos-15 ARM64矩阵；setup-python固定3.12，Qt安装设置setup-python:false，发布入口使用steps.python.outputs.python-path。远程日志两平台均为明确路径的Python3.12.10。通过GitHub官方仓库对应标签的action.yml核实 checkout@v7.0.1、setup-node@v7.0.0、setup-python@v7.0.0、upload-artifact@v7.0.1均声明node24。上传步骤无always条件，保留失败不上传门控。 |
| A4 | passed | brief.md | A4：既有打包测试与新增解压、超时、日志回归通过，工作流检查通过；明确区分本地验证与未运行的 Windows/macOS 远程打包。 | 复用并核验本轮Runtime记录：release-tests共10项通过，actionlint1.7.12与diff-check均exit0；补充bounded-timeout-checksum通过，填补原10项套件仅断言超时参数、未实际触发超时的证据缺口。独立查询远程35062158066为success，headSha与本地HEAD均为18d2fa2a20be697280a6c9540610267d82410dcc；两平台日志各显示10项发布回归、7项CTest通过，并完成打包上传。更新brief及builder handoff明确本轮未重跑远程，未下载或启动产物。 |

## 检查

| 检查 | 命令 | 工作目录 | 状态 | 退出码 | 耗时 |
| --- | --- | --- | --- | ---: | ---: |
| 发布回归 | -m unittest discover -s tests -p [REDACTED] | . | passed | 0 | 116 ms |
| 工作流静态检查 | — | . | passed | 0 | 12 ms |
| 差异检查 | diff --check | . | passed | 0 | 11 ms |
| 实际超时与校验失败阻断 | -B -c import importlib.util,io,subprocess,sys,time from pathlib import Path from unittest.mock import patch spec=importlib.util.spec_from_file_location('release',Path('scripts/release.py')) r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r) started=time.monotonic() with patch('builtins.print') as p: try: r.run(sys.executable,'-B','-c','import time; time.sleep(10)',timeout=0.1) except subprocess.TimeoutExpired: pass else: raise AssertionError('timeout did not propagate') assert time.monotonic()-started<5 lines=[c.args[0] for c in p.call_args_list] assert lines[0].startswith('[START]') and lines[-1].startswith('[FAIL]') assert 'timed out' in lines[-1] and all(c.kwargs.get('flush') is True for c in p.call_args_list) with patch.object(r,'download',return_value=Path('fixture.zip')),patch.object(r,'sha256',return_value='wrong'),patch.object(r.urllib.request,'urlopen',return_value=io.BytesIO(b'expected fixture.zip')),patch.object(r,'extract') as extract: try: r.prepare_qtbase() except RuntimeError as e: assert 'checksum mismatch' in str(e) else: raise AssertionError('bad checksum accepted') extract.assert_not_called() print('PASS: real timeout propagation, immediate failure logs, checksum rejection before extraction') | . | passed | 0 | 205 ms |

### Builder 报告的证据

以下为 Builder 报告，不等同于 Runtime 检查凭据或独立验收结果。

- release-tests: passed — 10 tests OK
- actionlint: passed — exit 0
- diff-check: passed — exit 0
- 已知限制: 本轮未触发新的远程构建；复核既有运行35062158066。未下载或启动分发产物，不代表最低系统或干净机器验证。

## 阻塞项

_无。_

## 风险与跳过的工作

- CI成功仅证明对应runner完成构建、检查及打包；未验证最低系统、干净机器启动、字体/DPI或性能。
- 实际超时及错误校验和检查已保存为本轮Runtime证据，尚未成为tests/test_release.py中的持久回归用例。
- 本轮未修改代码、触发远程运行、下载产物或改变工作流状态；归档及工作区核查由根代理执行。

## 之前的迭代

| 目标周期 | 迭代 | 尝试 | 结果 | 未解决项 | 摘要 | 完成时间 |
| ---: | ---: | ---: | --- | --- | --- | --- |
| 1 | 1 | 1 | blocked | A1, A3, A4 | 代码变更方向正确，但当前证据不足以宣称完整通过。 | 2026-09-16T01:25:18.485Z |
| 1 | 1 | 1 | recovery | — | 用户确认继续修改实现：补充解压、超时和日志回归测试，并重新验证；远程 Windows/macOS 重跑仍待推送后执行。 | 2026-09-16T01:38:15.612Z |
| 1 | 2 | 1 | execution-error | — | 已派发的只读 Verifier execution ref skill-coordinated:verifier:b488ef42-022f-428c-bd18-209c007541e9 自 2026-09-16T01:41:22Z 后未返回结果；当前无对应运行进程，已超过正常等待窗口。 | 2026-09-16T04:34:53.324Z |
| 1 | 2 | 2 | execution-error | — | 第二次只读 Verifier execution ref skill-coordinated:verifier:e2b02f38-3aac-4bc5-b97b-8b7f75f6319a 自 2026-09-16T04:36:54Z 后未返回结果；当前已超过正常等待窗口。 | 2026-09-16T04:49:29.352Z |
| 1 | 2 | 3 | execution-error | — | 第三次只读 Verifier execution ref skill-coordinated:verifier:41470800-9567-419e-8245-62aff999b39a 自 2026-09-16T04:50:31Z 后未返回结果；本机无对应执行进程。 | 2026-09-16T04:53:34.623Z |
| 1 | 2 | 4 | recovery | — | Native Shape artifacts changed | 2026-09-22T01:54:48.201Z |
| 2 | 1 | 1 | pass | — | 独立核验当前候选与A1-A4，无阻断发现。复用3项有效Runtime检查，补充1项真实超时及校验失败检查；核实同HEAD两平台远程成功记录与四个官方Action的Node24声明。可以进入归档。 | 2026-09-22T02:05:31.851Z |



## 结论

独立核验当前候选与A1-A4，无阻断发现。复用3项有效Runtime检查，补充1项真实超时及校验失败检查；核实同HEAD两平台远程成功记录与四个官方Action的Node24声明。可以进入归档。
