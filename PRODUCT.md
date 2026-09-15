# Speed Sync SQL

本文保留产品规划阶段的需求与授权边界。D1 的已确认实施范围见 [desktop-foundation 规格](docs/comet/changes/desktop-foundation/specs/desktop-foundation/spec.md)，当前实现与验证状态见 [README](README.md)。

<!-- impeccable:product-schema 1 -->

## Platform

web

Qt6 桌面容器内的 React 界面；正式产品同时支持 Windows 与 macOS，不作为独立网站发布。移动端不在本次规划范围。

## Stack

用户指定 Qt6、React 19.2.7、TypeScript 6.0.3、Ant Design 6.4.5。以 Ant Design 原生组件为主体，避免过度自制。Qt 次版本、处理器架构、最低系统版本及构建工具在后续工程 change 中验证并确定。

## Users

内部使用。需要在两端 MySQL 数据库之间检查、理解并同步单表结构和数据的人员。具体岗位未限定，界面不能要求所有操作都手写 SQL。

## Product Purpose

让用户连接两端数据库、选择表、识别差异、确定同步方向和范围，并清楚了解执行结果。核心目标是缩短重复比对和同步的操作路径，同时保留对实际变更的控制。

## Operating Context

- Windows、macOS 均需可用。
- 数据库类型限定 MySQL，以 8.0 及以上为目标；未来版本与不同特性的兼容性须验证，不等于无条件兼容所有 8.0+ 版本。
- 结构比对包括字段、触发器以及相关结构对象；总体范围包含表数据同步。
- 本次 change 只做产品规划、功能可行性、页面布局与设计。用户将另开 change 分阶段开发。

## Capabilities and Constraints

- 两端连接、单表选择、结构差异识别、可选择方向的结构同步、表数据同步。
- 具体数据同步模式、特殊结构对象的自动同步范围由本次设计提出建议；建议不自动成为未来数据库写入授权。
- 内部工具，不做过度安全设计；保留防误同步、失败反馈和结果复核所需的基础能力。
- 本次不编写业务代码、安装业务依赖、搭建可运行工程或连接真实数据库执行操作。

## Brand Commitments

中文界面，专业、精致、便利。审美与使用意愿同样重要；Ant Design 组件保持可识别和一致。Speed Sync SQL 暂沿用项目目录名称，不代表用户已确认品牌命名。

## Evidence on Hand

现有依据为用户需求、官方技术文档与已核实的包版本。没有真实数据库样本、现有界面或用户性能基准。设计示例必须标记为示例数据。

## Product Principles

1. 总体设计先完整，实施按独立 change 逐步完成。
2. 先让差异被理解，再让变更被执行。
3. 减少重复输入和无意义跳转，写入对象与方向始终可见。
4. 重用成熟组件，用布局、密度和细节建立品质。
5. 明确能力边界，不把预览、结构快照或未验证状态包装成执行保障。
