# 按钮图标规范

## 目的与边界

为产品三个页面（比对工作台、连接管理、执行记录）及其中抽屉的全部交互按钮提供语义对应的 Ant Design 图标，形成跨页面一致的视觉操作语言。本能力只约束按钮图标的选取、注入方式与一致性，不改变任何按钮的文案、行为、禁用条件与布局。验收统一引用 brief.md A1–A5。

原型页（prototype.html/prototype.tsx）与非按钮控件不在本能力范围内。

## 图标体系

- 统一使用 `@ant-design/icons` outlined 线性图标，作为 `frontend/package.json` 直接依赖管理。
- 图标仅通过 Button 的 `icon` 属性注入，按钮文本保持不变；不在文本内嵌图标，不为文本添加前缀字符。
- 同一动作跨页面使用同一图标；语义相近的动作按动作本质选图标（读取/刷新、复制、导出、执行、停止、删除、新增、编辑）。

## 语义映射

- 载入与刷新：载入库清单、载入/刷新表（来源与目标）、刷新记录使用 ReloadOutlined；完整键定位使用 AimOutlined，清除定位使用 CloseCircleOutlined。
- 比对与浏览：开始比对、开始数据比对使用 SearchOutlined；重新比对（含结构工作台同一按钮在已有结果时的状态）使用 RedoOutlined；开始独立浏览使用 EyeOutlined；取消读取、关闭详情使用 CloseOutlined。
- 预览与执行：预览同步、查看同步详情、查看执行结果使用 EyeOutlined（预览类）与 ProfileOutlined（详情类）；执行到目标、执行数据写入使用 PlayCircleOutlined；返回差异使用 ArrowLeftOutlined；按当前上下文重新比对/回到工作台重新比对使用 RedoOutlined。
- 停止：请求停止、请求边界停止、停止扫描、停止当前任务统一使用 StopOutlined。
- 复制与导出：复制完整定义、复制对象完整定义、复制完整 SQL、复制完整原文/HEX、复制（连接）使用 CopyOutlined；导出 SQL、导出 JSON 摘要、导出诊断、导出记录 JSON、加载完整值使用 ExportOutlined。
- 条件编辑：编辑比对条件、修改映射、编辑连接使用 EditOutlined；新建连接、新建（编辑器）、添加第一个连接、添加 AND 条件使用 PlusOutlined；删除条件、删除（连接）使用 DeleteOutlined；应用条件、应用映射使用 CheckOutlined。
- 连接与全局：测试连接使用 ApiOutlined；对换使用 SwapOutlined；设置使用 SettingOutlined；选择数据库使用 DatabaseOutlined；用作左侧/右侧分别使用 LeftOutlined/RightOutlined；保存连接使用 SaveOutlined；取消使用 CloseOutlined。
- 排除项：结构对象名称、连接名称等作为内容标识的行内文字链接按钮不加图标。

具体组件文件：`SchemaWorkbench.tsx`、`SyncPanel.tsx`、`DataWorkbench.tsx`、`DataMergePanel.tsx`、`main.tsx`、`SyncRecords.tsx`。

## 一致性与回归

- 图标不改变按钮可用状态逻辑、danger/type 样式和加载反馈；浅深主题与标准/紧凑密度下均正常渲染，不引入页面横向溢出（1920×1080 基准）。
- 前端测试为 `@ant-design/icons` 提供模块桩；按文本匹配按钮的断言语义不变。
- `npm test` 与 `npm run build`（tsc --noEmit + vite build）必须通过。
