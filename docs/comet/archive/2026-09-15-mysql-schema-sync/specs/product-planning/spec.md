# 产品规划与界面设计交付规格

## Purpose

本 capability 定义本次文档与设计交付，不把未来产品功能误设为本次实现验收。未来开发由用户另开 change。

## Required deliverables

- PRODUCT.md 记录已确认产品事实：双平台、MySQL 8.0+ 目标、指定技术栈、结构与数据同步、内部使用与设计品质要求。
- docs/design/overview.md 给出总体目标、完整功能清单、模式与非目标；用功能 ID 关联后续阶段。
- docs/design/feasibility.md 给出可行性、官方依据、架构方向、版本与运行边界，以及未运行项。
- docs/design/interaction-design.md 给出页面层级、布局线框、交互、状态、视觉候选、Ant Design 对应、双平台窗口行为。
- docs/design/roadmap.md 给出未来独立 change 的目标、依赖与阶段验收，不启动实现。
- docs/design/README.md 提供阅读入口并明确本轮交付保真度。

## Product scope expressed by the plan

总体方案覆盖连接管理、单表配对、字段/索引/约束/表属性/触发器比对、方向与对象选区、SQL 预览、执行结果、结构复核，以及有可靠键的数据比对与同步。数据同步分别说明补齐、合并与完全对齐；完全对齐限制在明确范围内，无法确认完整扫描时不得据此删除。

总体方案须说明结构漂移、数据并发变化、类型精度、匹配键缺失、依赖、不支持对象、DDL 部分成功和结果未知。安全范围仅为必要凭据保护、防误操作与执行正确性，不扩展为权限审批平台。

## Design scope

以 Ant Design 标准控件和桌面对照工作台为基础，提供结构页、数据页、连接管理、计划预览、执行过程与记录的布局。设计明确未连接、加载、无差异、缺表、错误、计划过期、执行部分失败等状态。视觉规范作为候选，不宣称已有实现截图、对比度实测或运行性能。

## Implementation boundary

本次仅写文档与设计辅助材料；不安装业务依赖、不搭建产品工程、不写 Qt/React 业务代码、不连接数据库执行、不创建开发子 change。正式产物由本次 Comet 流程验收，未来能力通过后续独立 change 验收。

## Acceptance mapping

验收场景统一使用 brief.md 的 A1-A6，不额外重复生成同义场景。

| ID | 文档证据 |
| --- | --- |
| A1 | feasibility.md 的证据、可行性矩阵与未运行项 |
| A2 | overview.md 的流程与 interaction-design.md 的页面/预览/执行 |
| A3 | interaction-design.md 的状态表与恢复动作 |
| A4 | interaction-design.md 的布局、组件映射、视觉规范 |
| A5 | overview.md 的数据同步模式、可靠键、范围与并发边界 |
| A6 | roadmap.md 的阶段与本次完成条件；文件清单无业务实现 |
