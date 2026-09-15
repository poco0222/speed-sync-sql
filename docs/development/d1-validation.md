# D1 本机检查记录

日期：2026-09-15。此文件为开发期证据摘要，不替代 Comet Verifier 的验收结论。

当前正式结论：D1 已于 2026-09-15 归档，本次验收 A1-A6 通过。用户接受当前交付并要求归档后，原 A7 转为[后续平台验证](platform-validation-pending.md)，Windows 10/macOS 15 实际运行仍为 NOT RUN。归档不代表目标系统已实测通过。正式逐项结论见 [验收报告](../comet/archive/2026-09-15-desktop-foundation/verification.md)。

2026-09-15，用户明确回复“接受”，接受当前本机交付及已说明的验证限制。A7 仍保留待实测状态；接受交付不构成目标系统已验证的证据。

## 环境

- 本机 macOS 27.0 / arm64，Apple Clang 21、Command Line Tools SDK 27。
- 复用 DevTools Qt 6.10.2、CMake 3.30.5、Ninja 1.12.1、Node 24.19.0、MySQL 8.0.46 客户端。
- React/react-dom 19.2.7、TypeScript 6.0.3、Ant Design 6.4.5，锁文件为 frontend/package-lock.json。
- QMYSQL 由 QtBase 6.10.2 官方源码构建，输出为 arm64。现有 DevTools Qt 安装未被替换或修改。
- 应用与 QMYSQL 的 `otool -l` 输出均为 `minos 15.0`。这不是 macOS 15 运行证据。

## 已完成检查

| 检查 | 命令或方式 | 实际结果 |
| --- | --- | --- |
| 前端类型与构建 | `npm --prefix frontend run build` | 通过；Vite 单 chunk 超 500 kB 警告保留，本地资源约 985 kB，不走 CDN |
| 原生构建 | `cmake --build build --parallel 4`（使用 README 所列 DevTools CMake） | 通过，当前二进制最低版本 15.0 |
| 原生逻辑/真实凭据 | `SPEED_SYNC_TEST_CREDENTIALS=1 build/foundation-tests` | 9 passed，0 failed，0 skipped；含配置 CRUD/损坏/写入失败、输入边界、错误分类、桥接错误、Keychain 保存读取清理、写入失败回滚、凭据缺失删除与取消记住 |
| 迟到响应隔离 | `npm --prefix frontend test` | 1 passed；编辑/新请求使旧响应失效，左右两端互不影响 |
| 真实 QMYSQL | `python3 tests/integration_probe.py ...` | 8 passed；独立 MySQL 8.0.46 临时实例，覆盖 preferred/required/disabled TLS、错误密码、缺库、错误 CA、不可达端口、非法端口；测试后实例与数据目录清理 |
| 实际缺驱动 | 将本次原生探测程序复制到无插件临时目录运行 `--probe` | 返回 `code=driver`，没有假成功 |
| 原生窗口与桥接 | 当前构建运行内置冒烟检查 | 本地 React 加载成功、桥接存在、QMYSQL 可发现；界面没有开发服务器依赖 |
| 窗口尺寸与缩放 | `-qwindowgeometry`，配合内置冒烟检查 | 1440×900 浅色、1280×800 浅色、1024×768 深色紧凑、1024×768 加 `QT_SCALE_FACTOR=1.5` 均加载；截图检查关键按钮与左右区域可达 |
| 实际表单操作 | CUA 操作真实 Qt 窗口 | 填写测试配置，127.0.0.1:9 返回连接失败，输入保留；离线配置保存成功；连接列表显示“尚未测试”，未伪造实时在线 |
| 原生导出 | CUA 操作系统保存对话框 | 写入不可写位置报告失败；保存到项目成功；取消后正常返回且不报错。产出 JSON 已检查不含主机、用户名、密码 |
| 实际设置 | CUA 操作真实 Qt 窗口 | 深色、紧凑、超时 3 秒保存；读取配置核对持久化值，深色界面截图可见 |

CUA 操作使用专用的 `D1 offline fixture`，没有输入真实凭据。其配置在检查后移至项目 `.local/evidence/ui-state.json`，正常应用数据目录恢复无测试配置。原生导出默认位置随后改为 Documents，并已独立复核；成功、失败和取消分支未变。

## 本地证据位置

- `.local/evidence/foundation-credentials.txt`
- `.local/evidence/mysql-probe.json`
- `.local/evidence/missing-driver.json`
- `.local/evidence/1440-light.json` 与同名 `.json.png`
- `.local/evidence/1280-light.json` 与同名 `.json.png`
- `.local/evidence/1024-dark.json` 与同名 `.json.png`
- `.local/evidence/1024-highdpi.json` 与同名 `.json.png`
- `.local/evidence/ui-diagnostics.json`、`ui-state.json`

最初 desktop-smoke/desktop-ready 文件早于最低版本修复，仅供调查历史；以以上最终尺寸检查为当前构建证据。

## 独立复核

执行标识 `/root/d1_review`，只读 Reviewer，结论 passed。已关闭：最低版本缓存未生效、凭据不存在与读取失败混淆、Windows 测试 DLL 搜索路径。最后补充检查设置控件 aria-label 与 Documents 默认保存目录，无新增必要问题。

## 未验证与限制

- **Windows 10 x64：NOT RUN**。没有可用执行环境；Windows 工具链/凭据分支/部署脚本仅静态审查。
- **macOS 15 arm64：NOT RUN**。本机为 27，设置 deployment target 和检查 Mach-O 不能代替最低版本实测。
- Qt 官方已验证的 SDK 上限为 26，本机 SDK 27 构建带警告；未安装额外 Xcode 或降级系统工具。
- Keychain 拒绝授权/锁定故障分支仅代码审查，未故障注入。
- TLS 身份验证错误 CA 路径已实测；有效证书与正确主机名的成功组合尚未单独生成样本。
- 尚未做正式发行包、签名、干净机器部署；应用当前依赖本机 DevTools 库。
- 未测试真实生产数据库，未执行任何业务库写入。

D1 全量通过仍需要目标平台证据。缺失项保留，不以本机检查或静态审查替代。

## Runtime 验收检查

Comet Runtime 对当前候选已执行 5 项检查，均 exit 0 / passed：native-credentials、frontend-typecheck、frontend-freshness、real-qmysql、desktop-smoke。其真实连接和原生窗口产物分别为 `.local/evidence/runtime-mysql.json`、`.local/evidence/runtime-desktop.json`。独立 Verifier 的最终逐项结论以 Comet 正式报告为准。

## 第二轮：A5/A6 修复与补证

- editor/left/right 通道按实际开始时间显示运行中耗时，250ms 刷新，不使用虚假百分比。
- 新增 `build/async-tests`：6 passed、0 failed、0 skipped。实际启动 Foundation 管理的子进程，以仅存在于测试程序中的延迟探测替身控制时间，验证同端防重复、双端独立、事件循环 heartbeat、编辑/删除后的 stale 响应不回填、stopJobs 回收和超时回收。替身不替代真实 MySQL 测试，真实 MySQL 8 项证据另保留。日志：`.local/evidence/async-processes.txt`。
- 本轮 CUA 使用 `SPEED_SYNC_DATA_DIR=.local/ui-verify`、`QT_SCALE_FACTOR=1.5`、`-qwindowgeometry 1024x768` 启动真实窗口。下列原始 AX 文本和截图见本任务 CUA 工具记录，不是仅关闭抽屉的工作台截图。
- Tab 从关闭按钮经取消、连接名称、主机、端口到用户名；键盘填入长连接名、127.0.0.1、测试端口和 fixture 用户，Cmd+A 替换端口成功。
- Tab 经密码、密码显示按钮、记住密码、默认数据库到高级选项，Return 展开；继续 Tab 进入 TLS、超时，页面自动滚动，底部测试与保存按钮始终可见。
- Tab 到测试按钮并 Return 发起测试。真实隔离 loopback TCP 端点接受连接但不发送 MySQL 握手，窗口仍可滚动；截图与 AX 读到“正在测试连接… 已用时 10.8 秒”。
- 失败时截图显示完整中文错误，表单保留，底部按钮可达；全程未输入真实密码。
- 运行中点击原生关闭，出现“仍有连接测试运行。结束测试并退出？”确认；确认后原进程 exit 0。随后 CUA 自动重新打开的是无测试配置的正常应用，不能误记为退出失败。
- 退出发现 profile 先于 page 销毁的警告，已把 profile 归 QApplication 管理，使 Window/page 先销毁。退出确认按钮补中文，继续测试为默认。

第二轮只修复本地 A5/A6 并补证；A7 目标系统缺失边界不变。

原始 CUA 证据已从本次任务的工具输出日志提取至 `.local/evidence/cua-repair/trace.json`，包含 13 次调用的时间、动作、原始 AX 文本及 9 张截图的文件名/SHA-256。截图字节未修改；这使独立 Verifier 可直接复核表单、错误、键盘焦点、10.8 秒计时和关闭确认，而非仅依赖上述摘要。

## 第三轮：确认框期间完成的退出竞态

独立 Verifier 第二轮通过 A6，但发现关闭确认框打开期间任务完成后，再确认退出可能不再收到 activityChanged。修复为确认后再次检查 busy；若任务已空，排队调用 close，避免丢失关闭动作。

重编译后使用独立 `.local/ui-race` 配置与真实 loopback 慢连接测试：在运行时打开确认框，等待任务自行失败；确认前读取到 lastTest，耗时 1083ms；随后点击中文“结束并退出”，原进程 PID 48773 正常 exit 0。

原始 CUA 记录/截图：`.local/evidence/cua-exit-race/trace.json`。确认前状态：`.local/evidence/exit-race-completed-before-confirm.json`。退出记录：`.local/evidence/exit-race-process.json`。本轮无生产数据或真实凭据。
