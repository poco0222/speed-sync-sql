import { forwardRef, useEffect, useImperativeHandle, useRef, useState, type ReactNode } from 'react';
import { Alert, AutoComplete, Button, Checkbox, Empty, Drawer, Input, Space, Table, Tabs, Tag, Tree, Segmented, Typography, message as messageService } from 'antd';
import { CheckOutlined, ClearOutlined, CloseOutlined, CopyOutlined, DatabaseOutlined, EditOutlined, ExportOutlined, RedoOutlined, ReloadOutlined, SearchOutlined } from '@ant-design/icons';
import { request, type State } from './bridge';
import { SyncPanel } from './SyncPanel';
import { DataWorkbench, type DataHandle } from './DataWorkbench';
import { rowKey, selectionReason, selectVisible } from './sync';
import { Freshness } from './freshness';
import { categories, filterRows, pairTable, propertyRows, statusLabels, summary, type Comparison, type SchemaRow, type Side, type Workspace } from './schema';

export type SchemaHandle = { invalidate: () => void; prepareSwap: () => Promise<void> };
const sides: Side[] = ['left', 'right'];
const sideLabel = (side: Side) => side === 'left' ? '来源' : '目标';
const format = (value: unknown): string => value === undefined ? '—' : typeof value === 'string' ? value : JSON.stringify(value, null, 2);
const statusColor = (status: string) => ({ same: 'success', different: 'warning', 'left-only': 'blue', 'right-only': 'purple', failed: 'error', unsupported: 'orange' }[status]);

export const SchemaWorkbench = forwardRef<SchemaHandle, { state: State; disabled: boolean; onRunning: (running: boolean) => void; onReading?: (running: boolean) => void; onDataRunning: (running: boolean) => void; onDataWriteRunning: (running: boolean) => void; connectionControls?: ReactNode }>(function SchemaWorkbench({ state, disabled, onRunning, onReading, onDataRunning, onDataWriteRunning, connectionControls }, ref) {
  const [workspace, setWorkspace] = useState<Workspace>(() => ({ left: state.workspace?.left ?? { database: state.connections.find(c => c.id === state.left)?.database ?? '', table: '' }, right: state.workspace?.right ?? { database: state.connections.find(c => c.id === state.right)?.database ?? '', table: '' }, width: state.workspace?.width ?? 280 }));
  const [lists, setLists] = useState<Record<string, { name: string; type?: string }[]>>({});
  const [comparison, setComparison] = useState<Comparison>();
  const [selectedKeys, setSelectedKeys] = useState<string[]>([]);
  const [strategy, setStrategy] = useState('fill');
  const alignAll = strategy === 'align';
  const [tableSearch, setTableSearch] = useState('');
  const [connectionsOpen, setConnectionsOpen] = useState(false);
  const [inspected, setInspected] = useState<SchemaRow>();
  const [mode, setMode] = useState('data');
  const [dataOpened, setDataOpened] = useState(true);
  const [dataWriteBusy, setDataWriteBusy] = useState(false);
  const contextLocked = disabled || dataWriteBusy;
  const syncWasRunning = useRef(false);
  const [dataEpoch, setDataEpoch] = useState(0);
  const dataWorkbench = useRef<DataHandle>(null);
  const [category, setCategory] = useState('columns');
  const [search, setSearch] = useState('');
  const [differences, setDifferences] = useState(true);
  const [mappingOpen, setMappingOpen] = useState(false);
  const [mappingDraft, setMappingDraft] = useState('');
  const [busy, setBusy] = useState('');
  const [started, setStarted] = useState(0);
  const [clock, setClock] = useState(0);
  const [notice, setNotice] = useState<{ type: 'info' | 'error' | 'warning'; text: string }>();
  const [exporting, setExporting] = useState(false);
  const freshness = useRef(new Freshness());
  const pendingWorkspace = useRef<Promise<void>>(Promise.resolve());
  const [message, messageHolder] = messageService.useMessage();
  const report = (error: unknown) => void message.error(error instanceof Error ? error.message : '操作失败');
  const invalidateData = () => { dataWorkbench.current?.invalidate(); setDataEpoch(value => value + 1); };
  const invalidate = () => {
    invalidateData();
    freshness.current.invalidate('schema');
    setComparison(undefined); setSelectedKeys([]); setInspected(undefined); setBusy(''); setNotice(undefined);
    void request('cancel-schema').catch(report);
  };
  useImperativeHandle(ref, () => ({ invalidate, prepareSwap: async () => { await pendingWorkspace.current; invalidate(); } }));
  useEffect(() => { onReading?.(!!busy); return () => onReading?.(false); }, [busy, onReading]);
  useEffect(() => () => { freshness.current.invalidate('schema'); void request('cancel-schema').catch(() => {}); }, []);
  useEffect(() => {
    if (!busy) return;
    const timer = setInterval(() => setClock(Date.now()), 250);
    return () => clearInterval(timer);
  }, [busy]);
  const saveWorkspace = (next: Workspace) => {
    const save = request('workspace', next).then(result => { if (!result.ok) throw new Error(result.error ?? '工作台选择保存失败'); });
    pendingWorkspace.current = Promise.all([pendingWorkspace.current.catch(() => {}), save]).then(() => {});
    void pendingWorkspace.current.catch(report);
  };
  const choose = (side: Side, field: 'database' | 'table', value: string) => {
    if (contextLocked) return;
    invalidate();
    const next = field === 'table' ? side === 'left' ? { ...workspace, left: { ...workspace.left, table: value }, right: { ...workspace.right, table: value } } : pairTable(workspace, side, value) : { ...workspace, [side]: { database: value, table: '' } };
    if (field === 'database') setLists(previous => ({ ...previous, [`${side}-tables`]: [] }));
    setWorkspace(next); saveWorkspace(next);
  };
  const run = async (operation: 'schema' | 'compare', args: object, label: string, listKey?: string) => {
    if (contextLocked) return;
    invalidate();
    const generation = freshness.current.invalidate('schema');
    const now = Date.now(); setStarted(now); setClock(now); setBusy(label);
    try {
      // Native selection persistence invalidates its result cache; finish it before reading.
      await pendingWorkspace.current;
      if (!freshness.current.current('schema', generation)) return;
      const result = await request(operation, args);
      if (!freshness.current.current('schema', generation)) return;
      if (result.stale || result.cancelled) { setNotice({ type: 'info', text: '读取已取消或上下文已变化，请重新读取。' }); return; }
      if (result.comparison) { setComparison(result.comparison); setSelectedKeys(defaultSelection(result.comparison, strategy)); }
      if (!result.ok) throw new Error(result.error ?? '结构读取失败');
      if (listKey) {
        setLists(previous => ({ ...previous, [listKey]: result.items ?? [] }));
        setNotice({ type: 'info', text: `读取完成：${result.items?.length ?? 0} 项。清单仅含账户可见对象；未列出不代表不存在，尚未比对结构。` });
      }
    } catch (error) {
      if (freshness.current.current('schema', generation)) {
        setNotice({ type: 'error', text: error instanceof Error ? error.message : '结构读取失败' });
        void request('cancel-schema').catch(report);
      }
    } finally { if (freshness.current.current('schema', generation)) setBusy(''); }
  };
  const exportSummary = async () => {
    if (!comparison || busy) return;
    setExporting(true);
    try {
      const result = await request('export-schema');
      if (!result.ok) throw new Error(result.error ?? '差异摘要保存失败');
      if (!result.cancelled) void message.success('完整差异摘要已保存');
    } catch (error) { report(error); } finally { setExporting(false); }
  };
  const copy = async (side: Side, category?: string, name?: string) => {
    try {
      const result = await request('copy-schema', { side, category, name });
      if (result.stale) throw new Error('比对结果已失效，请重新读取后复制');
      if (!result.ok) throw new Error(result.error ?? '复制失败');
      void message.success('已复制完整定义');
    } catch (error) { report(error); }
  };
  const visible = comparison ? filterRows(comparison.rows, category, search, differences) : [];
  const selected = sides.every(side => state[side] && workspace[side].database && workspace[side].table);
  const identity = (side: Side) => `${state.connections.find(c => c.id === state[side])?.name ?? '未选连接'} / ${workspace[side].database || '未选库'} / ${workspace[side].table || '未选表'}`;
  const properties = (row: SchemaRow) => propertyRows(row).map(property => ({ ...property, left: format(property.left), right: format(property.right) }));
  const definition = (side: Side) => <section className="definition"><div className="definition-heading"><strong>{sideLabel(side)} · {identity(side)}</strong><Button size="small" icon={<CopyOutlined />} disabled={!comparison?.[side].ddl} onClick={() => void copy(side)}>复制完整定义</Button></div><pre tabIndex={0}>{comparison?.[side].ddl || '未读取到原始定义'}</pre><Typography.Text type="secondary">MySQL {comparison?.[side].version ?? '未知'} · 开始 {comparison?.[side].startedAt ?? '未知'} · 结束 {comparison?.[side].finishedAt ?? '未知'}</Typography.Text></section>;
  const defaultSelection = (value: Comparison, next: string) => value.rows.filter(row => !selectionReason(row) && (row.status === 'left-only' || next === 'merge' && row.status === 'different')).map(rowKey);
  const changeStrategy = (next: string) => { setStrategy(next); setSelectedKeys(comparison ? defaultSelection(comparison, next) : []); };
  const modeControl = <Segmented aria-label="比对内容" value={mode} options={[{label:'表结构',value:'schema'},{label:'表数据',value:'data'}]} onChange={value => { setMode(value); if (value === 'data') setDataOpened(true); }} />;
  const included = (row: SchemaRow) => !selectionReason(row) && (alignAll || selectedKeys.includes(rowKey(row)));
  const renderDetail = (row: SchemaRow) => <div className="property-detail"><div className="property-detail-header"><strong>完整属性</strong><span>{identity('left')}</span><span>{identity('right')}</span></div>{properties(row).map(property => <div className={property.changed ? 'changed-property' : ''} key={property.name}><strong>{property.name}</strong><pre tabIndex={0}>{property.left}</pre><pre tabIndex={0}>{property.right}</pre></div>)}{row.reason && <p>{row.reason}</p>}{(row.left?.ddl || row.right?.ddl) && <section className="definition-pair">{sides.map(side => <section className="definition" key={side}><div className="definition-heading"><strong>{sideLabel(side)} · {identity(side)} · {row.name}</strong><Button size="small" icon={<CopyOutlined />} disabled={!row[side]?.ddl} onClick={() => void copy(side, row.category, row.name)}>复制对象完整定义</Button></div><pre tabIndex={0}>{row[side]?.ddl ?? '未读取到对象定义'}</pre></section>)}</section>}</div>;
  const sourceTables = (lists['left-tables'] ?? []).filter(item => item.name.toLocaleLowerCase().includes(tableSearch.trim().toLocaleLowerCase()));
  return <section className="schema-workbench">
    {messageHolder}
    {connectionControls}
    <div className="workbench-connections"><Typography.Text type="secondary" title={`${workspace.left.database} → ${workspace.right.database}`}>来源库：{workspace.left.database || '未选库'} → 目标库：{workspace.right.database || '未选库'}</Typography.Text><Button type="link" icon={<DatabaseOutlined />} onClick={() => setConnectionsOpen(true)}>选择数据库</Button></div>
    <Drawer title="选择数据库" open={connectionsOpen} onClose={() => setConnectionsOpen(false)} size={760}><div className="database-bar">
      {sides.map(side => <section className="database-picker" key={side}>
        <label htmlFor={`${side}-database`}>{sideLabel(side)}数据库</label>
        <AutoComplete id={`${side}-database`} aria-label={`${sideLabel(side)}数据库`} value={workspace[side].database} options={(lists[`${side}-databases`] ?? []).map(item => ({ value: item.name }))} filterOption={(input, option) => String(option?.value ?? '').toLocaleLowerCase().includes(input.toLocaleLowerCase())} onChange={value => choose(side, 'database', value)} disabled={contextLocked || !state[side]} placeholder="输入或选择数据库" />
        <Button icon={<ReloadOutlined />} disabled={contextLocked || !state[side] || !!busy} onClick={() => void run('schema', { action: 'databases', side }, `正在读取${sideLabel(side)}数据库`, `${side}-databases`)}>载入库清单</Button>
      </section>)}
    </div></Drawer>
    <div className="workbench-body"><aside className="source-table-nav"><Typography.Text strong>来源表</Typography.Text><Input.Search aria-label="搜索来源表" placeholder="搜索表名" allowClear value={tableSearch} onChange={event => setTableSearch(event.target.value)} /><Button icon={<ReloadOutlined />} disabled={contextLocked || !state.left || !workspace.left.database || !!busy} onClick={() => void run('schema', {action:'tables',side:'left',database:workspace.left.database}, '正在读取来源表清单', 'left-tables')}>载入 / 刷新表</Button><Tree aria-label="来源表" blockNode disabled={contextLocked || !!busy} selectedKeys={[workspace.left.table]} treeData={sourceTables.map(item => ({key:item.name,title:<span title={item.name}>{item.name}{item.type === 'VIEW' ? '（视图）' : ''}</span>,isLeaf:true}))} onSelect={keys => { const name = String(keys[0] ?? ''); if (name && name !== workspace.left.table) choose('left', 'table', name); }} />{!sourceTables.length && <Typography.Text type="secondary">{lists['left-tables'] ? '未找到表' : '请先载入表清单'}</Typography.Text>}</aside><div className="workbench-content">
    <div className="single-table-heading">
      <div><Typography.Title level={4} title={workspace.left.table}>{workspace.left.table || '选择一张表'}</Typography.Title><Typography.Text type="secondary" title={workspace.right.table}>目标表：{workspace.right.table || '默认与来源同名'}</Typography.Text><Button type="link" icon={<EditOutlined />} disabled={contextLocked || !state.right || !workspace.right.database} onClick={() => { setMappingDraft(workspace.right.table); setMappingOpen(true); }}>修改映射</Button></div>

    </div>
    <Drawer title="目标表映射" open={mappingOpen} onClose={() => setMappingOpen(false)} extra={<Button type="primary" icon={<CheckOutlined />} disabled={contextLocked || !mappingDraft.trim()} onClick={() => { if (mappingDraft !== workspace.right.table) choose('right', 'table', mappingDraft); setMappingOpen(false); }}>应用映射</Button>}>
      <Typography.Paragraph>默认与来源同名，可指定不同名目标。清单仅含可见对象，未列出不代表不存在。</Typography.Paragraph>
      <label htmlFor="right-table">目标表</label><AutoComplete id="right-table" aria-label="目标表" style={{width:'100%'}} value={mappingDraft} onChange={setMappingDraft} disabled={contextLocked} options={(lists['right-tables'] ?? []).map(item => ({value:item.name}))} filterOption={(input, option) => String(option?.value ?? '').toLocaleLowerCase().includes(input.toLocaleLowerCase())} />
      <Button icon={<ReloadOutlined />} disabled={contextLocked || !!busy} onClick={() => void run('schema', { action: 'tables', side: 'right', database: workspace.right.database }, '正在读取目标表清单', 'right-tables')}>载入 / 刷新目标表</Button>
    </Drawer>

    <div className="schema-layout">
      <div className="schema-results">
        <div style={{display:mode === "schema" ? undefined : "none"}}>
        <SyncPanel target={identity('right')} modeControl={modeControl} strategy={strategy} onStrategyChange={changeStrategy} action={<Space><Button icon={comparison ? <RedoOutlined /> : <SearchOutlined />} disabled={contextLocked || !selected || !!busy} type={comparison ? 'default' : 'primary'} loading={!!busy} onClick={() => void run('compare', { left: workspace.left, right: workspace.right }, '正在读取两端元数据并比对')}>{comparison ? '重新比对' : '开始比对'}</Button>{busy && <Button icon={<CloseOutlined />} onClick={() => { invalidate(); setNotice({ type: 'info', text: '已取消读取，旧结果已失效。' }); }}>取消读取</Button>}</Space>} onRecompare={() => void run('compare', { left: workspace.left, right: workspace.right }, '正在重新比对')} comparison={comparison} selected={selectedKeys} disabled={contextLocked || !!busy} onRunning={running => { if (running && !syncWasRunning.current) invalidateData(); syncWasRunning.current = running; onRunning(running); }} onComparison={value => { setComparison(value); setSelectedKeys([]); setInspected(undefined); }} >
        <div className="schema-toolbar"><Typography.Text type="secondary">差异优先 · 查看筛选不改变同步范围</Typography.Text><Button icon={<ExportOutlined />} disabled={!comparison || !!busy || disabled} loading={exporting} onClick={() => void exportSummary()}>导出 JSON 摘要</Button></div>
        {busy && <Alert type="info" showIcon title={`${busy}… 已用时 ${Math.max(0, (clock - started) / 1000).toFixed(1)} 秒`} description="后台执行，可取消；两端分别读取，不保证统一快照。" />}
        {notice && <Alert type={notice.type} showIcon title={notice.text} />}
        {!comparison ? <div className="schema-empty"><Empty description={busy ? '正在读取，尚无可用比对结果' : '尚未比对'} /><Typography.Paragraph type="secondary">选择两端数据库和表，再点击“开始比对”。无需先测试连接。</Typography.Paragraph></div> : <>
          <Alert type={summary(comparison) === '结构相同' ? 'success' : 'warning'} showIcon title={summary(comparison)} description={<Space wrap>{Object.entries(statusLabels).map(([status, label]) => <span key={status}>{label} {comparison.rows.filter(row => row.status === status).length}</span>)}</Space>} />
          <div id="schema-differences" /><Tabs activeKey={category} onChange={setCategory} items={[...categories.map(item => ({ ...item, label: `${item.label} (${comparison.rows.filter(row => row.category === item.key).length})` })), { key: 'ddl', label: '原始定义' }]} />
          {category === 'ddl' ? <><Typography.Paragraph type="secondary">原始 DDL（数据定义语言）仅供查看；文本格式不同不直接决定结构差异。</Typography.Paragraph><div className="definition-pair">{definition('left')}{definition('right')}</div></> : <>
            <div className="schema-filters"><Input.Search aria-label="搜索结构对象" placeholder="搜索对象名称" allowClear value={search} onChange={event => setSearch(event.target.value)} /><Checkbox checked={differences} onChange={event => setDifferences(event.target.checked)}>只看差异</Checkbox><Checkbox disabled={contextLocked || !!busy || alignAll || !visible.some(row => !selectionReason(row))} checked={visible.some(row => !selectionReason(row)) && visible.filter(row => !selectionReason(row)).every(row => selectedKeys.includes(rowKey(row)))} onChange={event => setSelectedKeys(previous => selectVisible(previous, visible, event.target.checked))}>全选当前可见可执行项</Checkbox><Button size="small" icon={<ClearOutlined />} disabled={contextLocked || !!busy || alignAll || !selectedKeys.length} onClick={() => setSelectedKeys([])}>清空选择</Button><Typography.Text type="secondary">已选 {selectedKeys.length} 项 · 显示 {visible.length} / {comparison.rows.filter(row => row.category === category).length} 项</Typography.Text></div>
            <Table<SchemaRow> rowKey={rowKey} rowSelection={{ selectedRowKeys: selectedKeys, preserveSelectedRowKeys: true, hideSelectAll: true, getCheckboxProps: row => ({ disabled: contextLocked || !!busy || alignAll || !!selectionReason(row), title: selectionReason(row) }), onSelect: (row, checked) => setSelectedKeys(previous => checked ? [...previous, rowKey(row)] : previous.filter(key => key !== rowKey(row))) }} dataSource={visible} pagination={{ pageSize: 30, showSizeChanger: false, hideOnSinglePage: true }} scroll={{ x: 700, y: 'max(240px, calc(100vh - 490px))' }} locale={{ emptyText: '当前分类或筛选无匹配项；总体结论见上方摘要' }} columns={[
              { title: '对象 / 状态', key: 'object', width: 190, render: (_, row) => <div><Button type="link" className="name-button" onClick={() => setInspected(row)}>{row.name || '类别读取状态'}</Button><div><Tag color={statusColor(row.status)}>{row.status === 'same' && !selectionReason(row) ? '相同，可作为关联操作' : statusLabels[row.status]}</Tag></div>{(row.reason || selectionReason(row)) && <Typography.Text type="secondary">{row.reason || selectionReason(row)}</Typography.Text>}{row.changed.length > 0 && <div className="schema-hint">差异属性：{row.changed.join('、')}</div>}</div> },
              { title: '目标现在 / 同步后（预计结果，以真实预览为准）', key: 'properties', render: (_, row) => {
                const values = properties(row).filter(property => row.status !== 'same' && (row.status !== 'different' || property.changed));
                return values.length ? <div className="property-summary"><div className="property-summary-heading"><strong>属性</strong><strong>目标现在</strong><strong>同步后{!included(row) ? ' · 保留' : ''}</strong></div>{values.map(property => <div key={property.name} className={property.changed ? 'changed-property' : ''}><span>{property.name}</span><pre>{row.right === null ? row.status === 'left-only' ? '已确认缺失' : '无可用属性' : property.right}</pre><pre>{!included(row) ? row.right === null ? row.status === 'left-only' ? '保持缺失' : '无法预计' : property.right : row.left === null ? '预计删除' : property.left}</pre></div>)}</div> : <Typography.Text type="secondary">{row.status === 'same' ? '属性相同，点击对象查看完整属性' : row.reason || '暂无可用变化属性，点击对象查看完整信息'}</Typography.Text>;
              } },
            ] } expandable={{ expandedRowKeys: inspected ? [rowKey(inspected)] : [], onExpand: (expanded, row) => setInspected(expanded ? row : undefined), expandRowByClick: true, expandedRowRender: renderDetail }} />
          </>}
        </>}
        </SyncPanel>
        </div>
        <div style={{display:mode === "data" ? undefined : "none"}}>{dataOpened && <DataWorkbench modeControl={modeControl} key={dataEpoch} ref={dataWorkbench} workspace={workspace} disabled={disabled || !!busy} selected={selected} beforeRead={() => pendingWorkspace.current} onRunning={onDataRunning} onMergeRunning={running => { setDataWriteBusy(running); onDataWriteRunning(running); }} />}</div>
      </div>
    </div></div></div>
  </section>;
});
