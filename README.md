# Speed Sync SQL

Qt 6.10.2 + React 的 MySQL 桌面结构比对工具。D1 提供连接管理、系统凭据、主题/密度；D2 增加单表结构比对与差异摘要导出。D3 增加常规结构同步、SQL 预览、受控执行与记录；数据比对/同步留给后续阶段。

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

## 结构比对（D2）

在工作台选择左右连接，输入库表名或显式读取清单，再点击“开始比对”。同名默认配对，也可手动选择不同名表。清单只代表当前账号可见范围，不表示已检查整个库。

按字段、索引、约束、触发器、表属性查看左右属性；原始定义页及触发器详情可复制完整 DDL。搜索和“只看差异”不会隐藏未读取、失败或不支持原因。只有全部必需类别完整且可比较时，才可能显示“结构相同”。

读取只使用元数据，不读业务行、不生成或执行同步 SQL。取消、换连接或换库表后旧结果失效；重启恢复最近配对与导航宽度，不自动联网或恢复旧结论。读取期间窗口仍可操作，退出会提示结束后台任务。

JSON 导出包含完整对象状态与差异，不受当前筛选影响。摘要不包含连接密码、主机或登录账号；对象的 DEFINER 仍作为结构属性保留，原始 DDL 不默认附在报告中。

当前对明确的整表 SELECT/TRIGGER 授权证明元数据完整性；角色或复杂授权无法证明时保守提示受限。分区、未知特殊选项、仅大小写不同的歧义对象保持不支持，不猜测相同。实际数据库版本证据为本地 MySQL 8.0.46；其他版本不作未经验证的兼容承诺。

## 结构同步（D3）

完成比对后选择方向及对象，点击“预览选中对象 SQL”；“预览整表对齐”包含全部新增、修改与删除。首次不默认全选；相同索引和触发器可作为关联操作显式纳入。预览显示目标别名/库表、增改删数、逐对象语义、SQL 及风险，复制或导出后也不会自动执行。

常规范围：普通字段与顺序、普通/唯一/主键/全文/空间索引、表注释/默认字符集/排序规则/行格式、基本触发器，以及确证缺表时建表。不同名配对保留目标名称。外键/CHECK、生成列、函数索引、分区、引擎转换及未知特性仅展示；相关依赖不明则阻止计划。触发器保留原 DEFINER、正文与上下文，同事件顺序不能可靠保持或非 UTF-8 客户端编码时仅展示。

依赖完整性目前需要明确全局 SELECT 与目标库级 TRIGGER 可见性（实际变更另需相应 DDL 权限）；角色/复杂授权不能证明完整时保守阻止，不把不可见对象当不存在。不自动修改外部表，不关闭外键检查，不删除整表。

确认一次摘要后，原生端重新核对两端结构并保存原始结构和初始记录，再按语句顺序执行。执行中锁定上下文，停止请求在当前语句结束后生效。DDL 不保证整体回滚；原结构不是业务数据备份。失败、未执行及结果待核实分别记录，断线/异常不自动重试。

执行后重新比对；语句成功不等于两端全部一致。执行记录保存在应用数据目录的 `sync-records/`，可按时间/状态/连接过滤、查看原结构与步骤、导出 JSON。重启将未完成执行标待核实；历史记录仅能恢复工作台选择，不可直接重放。

本机验证使用独立临时 MySQL 8.0.46 和真实 Qt WebEngine，不接现有数据库。验证细节见 [D3 验证记录](docs/development/d3-validation.md)。

## 检查命令

```sh
npm --prefix frontend test
ctest --test-dir build --output-on-failure
build/async-tests
SPEED_SYNC_TEST_CREDENTIALS=1 build/foundation-tests
python3 tests/integration_probe.py \
  --mysql-home /Users/PopoY/Documents/DevTools/mysql/current \
  --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql \
  --output .local/evidence/mysql-probe.json
python3 tests/integration_schema.py \
  --mysql-home /Users/PopoY/Documents/DevTools/mysql/current \
  --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql \
  --output .local/evidence/d2-mysql.json \
  --desktop-script tests/desktop_schema.mjs
```

凭据测试使用随机临时目录产生的独立凭据键，结束清理。真实连接测试启动并销毁独立 MySQL 实例（随机 loopback 端口、临时数据目录），不连接现有数据库。输出不含随机测试密码。

D2 集成脚本也只使用临时 MySQL 实例。可选桌面脚本会打开真实 Qt 窗口，以临时只读账号比对样本，使用随机 loopback 调试端口检查界面并保存截图；退出后清理实例。需 Node 24；当前复制审计使用 macOS AppKit/Swift，完整保留恢复剪贴板，辅助程序仅生成到 `.local/test-tools/`。不在正常启动开启调试。检查结果见 [D2 验证记录](docs/development/d2-validation.md)。

开发用桌面冒烟检查：

```sh
SPEED_SYNC_DATA_DIR="$PWD/.local/smoke-empty" \
SPEED_SYNC_SMOKE_REPORT="$PWD/.local/evidence/desktop-smoke.json" \
  build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql
```

生成本地 JSON 和截图后退出。正常启动不设置这些变量。实际检查与限制见 [D1 检查记录](docs/development/d1-validation.md)，完整规格见 [desktop-foundation](docs/comet/specs/desktop-foundation/spec.md)。

D3 完整隔离集成检查：

```sh
python3 tests/integration_sync.py \
  --mysql-home /Users/PopoY/Documents/DevTools/mysql/current \
  --app build/speed-sync-sql.app/Contents/MacOS/speed-sync-sql \
  --output .local/evidence/d3-integration.json \
  --desktop-script tests/desktop_sync.mjs
```
