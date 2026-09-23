# 目标

为产品页面中缺少图标的按钮补齐语义对应的 Ant Design 图标，使操作入口在视觉上一致、可辨识。当前全部产品页面（比对工作台、连接管理、执行记录及各抽屉）的 Button 均未使用图标，也没有任何代码导入 `@ant-design/icons`。

# 范围

- 产品页面中渲染的全部交互 Ant Design Button 组件按 D4 覆盖范围补图标（排除作为内容标识的行内文字链接）：
  - 比对工作台：`SchemaWorkbench.tsx`、`SyncPanel.tsx`、`DataWorkbench.tsx`、`DataMergePanel.tsx`；
  - 连接管理与全局框架：`main.tsx`（顶部连接条、设置、对换、连接表格、连接编辑抽屉、底部停止）；
  - 执行记录：`SyncRecords.tsx`（刷新、查看、回到工作台、导出）。
- 图标统一使用 `@ant-design/icons` 6.x outlined 线性图标，通过 Button 的 `icon` 属性注入，语义与按钮文案对应（如刷新用 ReloadOutlined、复制用 CopyOutlined、停止用 StopOutlined、删除用 DeleteOutlined、新建/添加用 PlusOutlined）。
- 将 `@ant-design/icons` 加入 `frontend/package.json` 直接依赖（当前仅作为 antd 6.4.5 的传递依赖存在，版本 6.3.4）。
- 更新受影响前端测试的模块桩（`application.test.mjs`、`schema-workbench.test.mjs`、`data-workbench.test.mjs`、`sync-panel.test.mjs`、`merge.test.mjs`），为 `@ant-design/icons` 提供组件桩；按钮文本断言不受影响。

## 来源覆盖

用户口头请求："页面上这些按钮，缺图标的都给我补一下"。无文件类需求来源，不适用源文档完整覆盖模式；覆盖边界为用户当前所指产品页面上的按钮。

# 非目标

- 不改变任何按钮的文案、行为、禁用条件、加载状态、危险样式（type/danger）与层级位置。
- 不新增、删除或合并按钮；不调整布局、间距、主题与密度体系。
- 不处理 `prototype.tsx` / `prototype.html`（设计原型页，非产品页面，仅被原型 HTML 引用）。
- 不为非按钮控件（Menu、Checkbox、Input.Search、Segmented、Tree、链接文字）添加图标；Alert 的 showIcon 为内置行为，保持现状。
- 不引入自定义 SVG 或第三方图标库。

# 验收示例

- Scenario: A1 工作台结构模式按钮图标：在比对工作台表结构模式下，开始/重新比对、取消读取、载入库清单、载入/刷新表（来源与目标）、选择数据库、修改映射、应用映射、预览同步、查看同步详情、返回差异、重新比对、执行到目标、复制完整 SQL、导出 SQL、导出 JSON 摘要、清空选择、复制完整定义/对象完整定义按钮均显示语义对应的 Ant Design 图标，按钮文案与行为不变。
- Scenario: A2 工作台数据模式按钮图标：在表数据模式下，开始数据比对、开始独立浏览、停止扫描、编辑比对条件、条件抽屉内取消/应用条件/添加 AND 条件/删除条件、完整键定位、清除定位、关闭详情、加载完整值、复制完整原文/HEX、预览同步、请求边界停止、按当前上下文重新比对、查看执行结果、执行数据写入按钮均显示语义对应的图标，文案与行为不变。
- Scenario: A3 全局框架与连接管理按钮图标：顶部与连接管理页的测试连接、编辑/新建、设置、对换、导出诊断、新建连接、添加第一个连接、用作左侧/右侧、复制、删除、停止当前任务、连接抽屉内取消/测试连接/保存连接按钮均显示语义对应的图标，文案与行为不变。
- Scenario: A4 执行记录按钮图标：执行记录页的刷新记录、查看记录、回到工作台重新比对、导出记录 JSON 按钮均显示语义对应的图标，文案与行为不变。
- Scenario: A5 构建与测试回归：`frontend` 全部现有测试通过，`npm run build`（tsc --noEmit + vite build）通过；图标仅通过 Button 的 icon 属性注入，不改变按钮文本内容，测试的按文本匹配按钮断言不需要改写语义。

# 约束与不变量

- 图标选择遵循 antd 语义惯例，同一动作在不同页面使用同一图标（例如所有"重新比对"类按钮同一图标、所有"停止"类按钮同一图标、所有"导出"类按钮同一图标）。
- 保持浅深主题、标准/紧凑密度下正常渲染；图标不引入额外横向溢出（1920×1080 基准）。
- 保持既有失效链、锁定与运行状态行为完全不变；本变更仅添加视觉图标。

# 决策

- D1: 图标库采用 `@ant-design/icons`（antd 官方图标包，已随 antd 6.4.5 安装，版本 6.3.4），使用 outlined 线性风格，与现有 Ant Design 组件体系一致。
- D2: 图标通过 Button `icon` 属性注入，不改动 children 文本，保证测试按文本匹配按钮的逻辑不变。
- D3: 本次作为独立能力 `button-icons` 编写完整目标规格，不并入 workbench-experience（涉及连接管理、执行记录等跨页面视觉规则）。
- D4: 覆盖范围为全部交互按钮：三个页面全部 Button（含表格行内小按钮、抽屉内次级按钮、底部停止），仅排除结构对象名、连接名称这类作为内容标识的行内文字链接（`name-button` 样式的 type="link" 按钮）。用户于 2026-09-23 确认。

# 待解决问题

（无）

# 验证预期

- `cd frontend && npm test` 全部通过；`npm run build` 通过。
- 现有 workbench 模拟页面（tests/workbench.html）可在浏览器人工核对图标显示、主题与密度。
- 验收项逐条对照代码中的 Button 位置核对图标存在与语义对应。
