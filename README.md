# Speed Sync SQL

Qt 6.10.2 + React 的 MySQL 桌面连接工具。D1 已实现桌面外壳、左右连接管理与测试、系统凭据存储、主题/密度和诊断导出。结构与数据比对、同步留给后续阶段。

目标：Windows 10 1809+ x64、macOS 15+ arm64。本机开发环境为 macOS 27 arm64；Windows 10/macOS 15 的运行验证尚未完成。不能把本机构建成功当作双平台验收通过。

## macOS 构建和启动

复用 `/Users/PopoY/Documents/DevTools/Qt/6.10.2/macos`，CMake 3.30.5、Ninja 1.12.1、Node 24.19.0。脚本可通过 `SPEED_SYNC_QT_ROOT`、`SPEED_SYNC_QT_TOOLS`、`SPEED_SYNC_MYSQL_PLUGIN` 覆盖工具位置。

```sh
./scripts/build-macos.sh
open build/speed-sync-sql.app
```

脚本安装项目内锁定的前端依赖，构建本地资源、原生程序及测试。不安装或升级全局 Qt。macOS 应用当前依赖开发机 Qt 和 MySQL 客户端库，不是可直接发给干净机器的发行包。

### 补齐 QMYSQL

现有 Qt 安装不含 QMYSQL。只需用同版本 QtBase 源码构建插件，无须重装 Qt。当前源码与插件放在项目 `.local/`，未写入 DevTools 的 Qt 安装。

源码：[QtBase 6.10.2 官方包](https://download.qt.io/archive/qt/6.10/6.10.2/submodules/qtbase-everywhere-src-6.10.2.tar.xz)。下载至 `.local/sources/` 并解压后运行：

```sh
export PATH="/Users/PopoY/Documents/DevTools/Qt/Tools/CMake/CMake.app/Contents/bin:$PATH"
qt_root=/Users/PopoY/Documents/DevTools/Qt/6.10.2/macos
mysql_root=/Users/PopoY/Documents/DevTools/mysql/current
"$qt_root/bin/qt-cmake" \
  -S .local/sources/qtbase-everywhere-src-6.10.2/src/plugins/sqldrivers \
  -B .local/mysql-driver -G Ninja \
  -DCMAKE_MAKE_PROGRAM=/Users/PopoY/Documents/DevTools/Qt/Tools/Ninja/ninja \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DQT_FORCE_MACOS_ALL_ARCHES_QMYSQLDriverPlugin=ON \
  -DMySQL_INCLUDE_DIR="$mysql_root/include" \
  -DMySQL_LIBRARY="$mysql_root/lib/libmysqlclient.dylib" \
  -DCMAKE_INSTALL_PREFIX="$PWD/.local/qt-plugins"
cmake --build .local/mysql-driver --target QMYSQLDriverPlugin --parallel 4
```

只有 Command Line Tools、没有完整 Xcode 的本机需额外传 `-DQT_NO_XCODE_MIN_VERSION_CHECK=ON`。这只是跳过 Xcode 应用版本探测，不能证明该开发环境获得 Qt 官方支持。本次 SDK 27 超出 Qt 已验证 SDK 26，保留构建警告；最低系统兼容仍待 macOS 15 实测。

Qt 的 MySQL 插件默认可能强制 Intel 架构；`QT_FORCE_MACOS_ALL_ARCHES_QMYSQLDriverPlugin=ON` 保留显式的 arm64 配置。插件、客户端库和应用必须同架构。

## Windows 构建（尚未实测）

在 MSVC 2022 x64 Developer PowerShell 中运行。需要 Qt 6.10.2 的 `msvc2022_64`（含 WebEngine/WebChannel/Sql）、CMake/Ninja、Node，以及同架构 MySQL 客户端开发包。Qt WebEngine 不采用 MinGW。

先使用 QtBase 6.10.2 的 `src/plugins/sqldrivers`，通过该 Qt 的 `qt-cmake.bat` 配置 `MySQL_INCLUDE_DIR`、`MySQL_LIBRARY`，构建 `QMYSQLDriverPlugin` 得到 release `qsqlmysql.dll`。再运行：

```powershell
./scripts/build-windows.ps1 -QtRoot 'C:/Qt/6.10.2/msvc2022_64' `
  -MySqlHome 'C:/tools/mysql' -MySqlPlugin 'C:/build/sqldrivers/qsqlmysql.dll'
./build/speed-sync-sql.exe
```

脚本用 `windeployqt` 放置 Qt/WebEngine 运行资源并复制 `libmysql.dll`。MySQL 发行包中的 SSL/crypto 等间接依赖仍须按实际包核对；检查机器需安装匹配 MSVC 运行库。D1 不承诺正式安装包、签名或干净机器发行验收。

## 行为与存储

- 默认中文工作台。连接保存不要求在线；历史测试结果明确标为“上次测试”，不是实时在线状态。
- 密码默认仅当前会话使用。勾选记住后使用 macOS Keychain / Windows Credential Manager；复制连接不复制密码。取消记住或删除连接会清理凭据。
- 非敏感配置由 Qt `QStandardPaths::AppDataLocation` 定位，用 `QSaveFile` 原子写入。损坏配置保持原样并禁止覆盖。测试可用 `SPEED_SYNC_DATA_DIR` 指向独立目录。
- Qt WebChannel 只对应用的本地资源开放。数据库探测在专用子进程的工作线程运行，密码经标准输入传递；父进程在连接超时额外 5 秒后回收超时任务。两端测试独立，同端防重复。
- 连接测试只读服务器信息和 TLS 状态；不会读取业务表或执行 DDL/DML。测试成功不代表所有 MySQL 未来版本或特性均兼容。
- TLS 有优先、要求、身份验证、关闭四种模式。要求 TLS 必须真正加密；身份验证要求 CA 与主机名有效。错误不包含原始密码或完整连接串。
- 诊断导出使用原生保存对话框，取消不报错。导出包含连接别名及测试结果，不包含主机、用户名或密码。

## 检查

```sh
npm --prefix frontend test
ctest --test-dir build --output-on-failure
build/async-tests
SPEED_SYNC_TEST_CREDENTIALS=1 build/foundation-tests
python3 tests/integration_probe.py \
  --mysql-home /Users/PopoY/Documents/DevTools/mysql/current \
  --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql \
  --output .local/evidence/mysql-probe.json
```

凭据测试使用随机临时目录产生的独立凭据键，结束清理。真实连接测试启动并销毁独立 MySQL 实例（随机 loopback 端口、临时数据目录），不连接现有数据库。输出不含随机测试密码。

开发用桌面冒烟检查：

```sh
SPEED_SYNC_DATA_DIR="$PWD/.local/smoke-empty" \
SPEED_SYNC_SMOKE_REPORT="$PWD/.local/evidence/desktop-smoke.json" \
  build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql
```

生成本地 JSON 和截图后退出。正常启动不设置这些变量。实际检查与限制见 [D1 检查记录](docs/development/d1-validation.md)，完整规格见 [desktop-foundation](docs/comet/changes/desktop-foundation/specs/desktop-foundation/spec.md)。
