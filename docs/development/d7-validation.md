# D7 本地验证记录

2026-09-15。D7 经用户确认收敛为 GitHub Actions 双平台打包，远程 CI 未执行时明确标记；最低系统/干净机器验收继续保留后续待办。

## 当前实测

环境：macOS 27.0 arm64、Qt 6.10.2、MySQL 客户端 8.0.46、CMake 3.30.5、Ninja 1.12.1。仅有 Command Line Tools，使用 `--allow-command-line-tools`；SDK 27 不作为最低系统兼容证据。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| actionlint 1.7.12 | passed | 检查 `.github/workflows/desktop-release.yml`；未启用可选 ShellCheck/Pyflakes |
| Python 编译及打包失败路径测试 | passed | `python3 -m unittest discover -s tests -p test_release.py`，8/8 |
| 前端构建与测试 | passed | 锁文件安装、TypeScript/Vite 构建，18/18；保留既有 chunk 大小警告 |
| 原生 Release 与 CTest | passed | 7/7 套件；系统凭据测试仍为显式 opt-in，未运行 |
| macOS 依赖部署 | passed | Qt/WebEngine、本地前端、QMYSQL/QSQLITE、MySQL/SSL/crypto；检查实际 LC_RPATH 和依赖文件，拒绝包外路径 |
| 干净环境变量下驱动加载 | passed | `--deployment-check` 返回 `mysqlValid=true`、`sqliteValid=true`、Qt 6.10.2；未连接数据库 |
| 归档/解压 | passed | 376 个条目，保留可执行位和符号链接，SHA-256 匹配，未含测试记录或凭据 |
| 解压后异目录启动 | passed | 实际 Qt WebEngine 窗口、本地页面、桥接、driverAvailable 均成功 |
| 缺少 QMYSQL 负例 | passed | 从解压包移走插件后返回退出码 1 和 `mysqlValid=false`；未借用开发机插件，随后恢复 |
| 独立代码复核 | passed | `d7_review-20260915-r2`；RPATH 误判问题已修复并补负例 |

本地完整命令（工具目录先加入 PATH）：

```sh
python3 scripts/release.py \
  --qt-root /Users/PopoY/Documents/DevTools/Qt/6.10.2/macos \
  --mysql-root /Users/PopoY/Documents/DevTools/mysql/current \
  --qtbase-source .local/sources/qtbase-everywhere-src-6.10.2 \
  --allow-command-line-tools
```

本地输出 `.local/release/artifacts/speed-sync-sql-0.1.0-macos-arm64.tar.gz`，241605771 字节；本次 SHA-256：`43e9cac672fb834b1d3df2b88fa6d88deb2694822e752273cbf5f23b7986d05e`。重新构建会因归档时间等因素改变哈希，以各自产物附带文件为准。

本机详细日志位于 `.local/d7-build.log`、`.local/d7-deploy.log`、`.local/d7-archive-check.log`；桌面摘要与截图位于 `.local/d7-desktop-smoke.json` 及同名 `.png` 后缀文件。上述生成物被 Git 忽略。

## 发现并修复

- Release 优化构建暴露 `tests/merge_bridge_test.cpp` 两处临时 QJsonObject 派生的 `QJsonValueRef` 悬空引用；仅将测试局部变量改为 `QJsonValue` 拷贝。复现退出码 139，系统崩溃栈定位于 `MergeBridgeTest::deleteConfirmation` → `QJsonValueConstRef::concrete`；修复后 Release CTest 7/7 通过。
- Qt 默认部署无关 SQL 驱动会引入未安装的供应商库；macOS 只复制实际需要的插件。
- MySQL 客户端的相对 SSL/crypto 依赖须随包复制与改写。独立复核要求按真实 LC_RPATH 验证，避免有同名库但不可达的误判。

## NOT RUN 与交付边界

- GitHub Actions 远程执行、Windows 编译/部署/ZIP 实跑、GitHub artifacts 上传下载：NOT RUN。本轮尚未推送或触发远程工作流。
- Windows 10 最低系统、无开发工具机器、macOS 15 目标环境、真实数据库连接及系统凭据专项：NOT RUN。
- 字体/DPI、主题/密度与代表性数据规模专项不是本轮打包验收范围；继续保留后续验证。
- 无 Developer ID/Windows 发行签名、公证或 GitHub Release；macOS 仅使用部署所需的 ad-hoc 签名。

本机打包及窗口启动成功不代表双平台正式发行完成。远程工作流上线后，须检查两个 job 的实际结果后再提供对应产物。
