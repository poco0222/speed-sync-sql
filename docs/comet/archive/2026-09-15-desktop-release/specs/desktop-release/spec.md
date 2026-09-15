# Desktop release

## Purpose

通过 GitHub Actions 为现有 Speed Sync SQL 生成 Windows x64 和 macOS arm64 分发包。本 capability 定义 CI 打包交付，不代表最低系统或干净机器验收通过。

## Workflow

工作流支持 workflow_dispatch，两个平台独立执行 Release 构建。前端按锁文件安装；Qt 固定 6.10.2，QMYSQL 使用匹配源码、工具链和架构构建，客户端依赖版本明确。工作流及脚本不引用开发机绝对路径，适合 runner 的前端/原生检查失败时不得继续生成成功产物。

## Deployment

包包含本地 React 资源、Qt/WebEngine 资源与辅助进程、平台插件、QMYSQL、MySQL 客户端及必要间接依赖。部署检查识别缺失依赖和开发路径残留，失败必须返回非零。运行不要求用户安装开发用 Qt 或 Node。

Windows 输出可解压分发 ZIP；macOS 输出保留 .app 目录、可执行权限与符号链接的压缩包。产物名称包含版本、系统与架构，成功构建后上传为 Actions artifacts。排除源目录、测试程序、测试数据和凭据；提供依赖及许可证说明。

## Delivery boundary

默认只提供手动触发和 Actions 下载，不自动发布 GitHub Release，不增加安装向导、签名、公证、更新服务或 secrets。保留现有业务语义和目标 Windows 10 1809+ x64、macOS 15+ arm64。

发行文档说明触发、下载、解压启动和未签名限制。记录真实检查结果，将本地构建、远程 CI、应用启动、最低系统和干净机器证据分开；未执行项写 NOT RUN。目标环境、字体/DPI、主题/密度和性能专项保留后续验证，不以 CI 成功替代。
