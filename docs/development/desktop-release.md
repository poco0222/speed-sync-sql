# GitHub Actions 桌面打包

在仓库 **Actions → Desktop release → Run workflow** 中选择分支并运行。工作流文件首次合入默认分支后，才会出现手动触发入口。

两个独立 job 使用 `windows-2022`（x64/MSVC 2022）和 `macos-15`（arm64），固定 Qt 6.10.2、Node 24.19.0、MySQL 客户端 8.0.46。前端按 `package-lock.json` 安装。QMYSQL 从匹配的 QtBase 源码构建，QtBase 下载后校验官方 SHA-256。

## 下载和运行

job 成功后，在该次运行的 Artifacts 下载对应系统产物，保留 14 天。外层是 GitHub artifact ZIP；解开后得到分发包和 SHA-256 文件：

- `speed-sync-sql-<版本>-windows-x64.zip`：解压完整目录，运行 `speed-sync-sql.exe`，不能只复制 EXE。
- `speed-sync-sql-<版本>-macos-arm64.tar.gz`：解压完整目录，将 `speed-sync-sql.app` 复制到用户选定位置后打开。tar 保留应用权限和符号链接；不要从未解压的归档中启动。

包内有 `build-info.json`、本说明和 `licenses/`。版本来自根目录 `CMakeLists.txt`。应用不要求目标机器安装 Qt、Node 或 MySQL 服务端；连接的 MySQL 服务由用户另行提供。

这些是未作发行签名的内部测试包。macOS 使用 ad-hoc 本地签名恢复改写依赖后的 arm64 可加载性，不是 Developer ID 签名，也未公证；系统可能要求在“隐私与安全性”中手动允许打开。Windows 可能显示未知发布者提示。仅运行来源可信、校验匹配的包，不关闭系统全局安全检查。

工作流不创建 GitHub Release、不自动发布、不使用签名账号或 secrets，也不制作安装向导。

## 实际检查与边界

每个 job 执行：前端构建与单元测试、原生 Release 构建、CTest、运行依赖部署与检查、清理 Qt/动态库环境变量后的 `--deployment-check`。该命令实际加载 QMYSQL/QSQLITE 插件，不连接数据库。

凭据生命周期测试仅在显式设置 `SPEED_SYNC_TEST_CREDENTIALS=1` 时运行，CI 默认跳过；不会读取 runner 的用户凭据。已有独立 MySQL 集成、交互桌面测试不在本工作流运行，不能把 CI 通过描述为这些测试通过。

目标仍为 Windows 10 1809+ x64、macOS 15+ arm64。CI runner 不是最低 Windows 或无开发工具的用户机器；打包/驱动加载不等于完整窗口、连接、系统凭据、字体/DPI、主题/密度或性能验收。缺失验证继续见 [平台待办](platform-validation-pending.md)（仓库路径 `docs/development/platform-validation-pending.md`）。

## 依赖与许可证

应用动态链接 Qt；分发含 Qt Widgets、SQL、WebChannel、WebEngine 及部署工具选取的运行模块，WebEngine 含 Chromium 第三方组件。QMYSQL 动态链接 MySQL 客户端，其包自带的许可证与说明一起保留；Windows 同时部署所需 MSVC CRT。前端包含 React、Ant Design 及其锁定依赖，Vite 产物中的许可证注释保留。

`licenses/QtBase` 来自匹配版本 QtBase 源码；MySQL 的 LICENSE/README 随包复制。完整上游许可证及第三方归属以对应版本源码为准：

- Qt 6.10.2 源码：https://download.qt.io/archive/qt/6.10/6.10.2/submodules/
- Qt 许可与第三方组件：https://doc.qt.io/qt-6/licensing.html
- Qt WebEngine 第三方组件：https://doc.qt.io/qt-6/qtwebengine-licensing.html
- MySQL 8.0.46：https://dev.mysql.com/downloads/mysql/8.0.html
- 前端准确依赖：仓库 `frontend/package-lock.json`。

本工作流提供内部测试分发，不作签名发行或完整再分发许可审核结论。

## 本地复现

使用兼容的 CMake、Ninja、Node、Python 3.9+ 与 Qt 6.10.2；Windows 在 MSVC 2022 x64 开发者终端运行：

```sh
python scripts/release.py --qt-root <Qt目录>
```

默认下载固定版本 MySQL 和 QtBase 到项目 `.local/release/`。可用 `--mysql-root <MySQL目录> --qtbase-source <QtBase源码目录>` 复用相同版本依赖。本机仅有 Command Line Tools 时可增加 `--allow-command-line-tools`，这不代表该 SDK/最低系统已获验证；Actions 不使用此选项。

构建、暂存及产物均位于 `.local/release/`；失败返回非零，不执行 artifact 上传。日志保留在 Actions 运行页；不要把数据库密码加入命令行或日志。
