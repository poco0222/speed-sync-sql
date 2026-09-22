import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from 'react';
import { Alert, AutoComplete, Button, Checkbox, Empty, Input, Slider, Space, Table, Tabs, Tag, Typography, message as messageService } from 'antd';
import { request, type State } from './bridge';
import { SyncPanel } from './SyncPanel';
import { DataWorkbench, type DataHandle } from './DataWorkbench';
import { rowKey, selectionReason, selectVisible } from './sync';
import { Freshness } from './freshness';
import { categories, filterRows, pairTable, propertyRows, statusLabels, summary, type Comparison, type SchemaRow, type Side, type Workspace } from './schema';

export type SchemaHandle = { invalidate: () => void };
const sides: Side[] = ['left', 'right'];
const sideLabel = (side: Side) => side === 'left' ? '左侧' : '右侧';
const format = (value: unknown): string => value === undefined ? '—' : typeof value === 'string' ? value : JSON.stringify(value, null, 2);
const statusColor = (status: string) => ({ same: 'success', different: 'warning', 'left-only': 'blue', 'right-only': 'purple', failed: 'error', unsupported: 'orange' }[status]);

export const SchemaWorkbench = forwardRef<SchemaHandle, { state: State; disabled: boolean; onRunning: (running: boolean) => void; onDataRunning: (running: boolean) => void; onDataWriteRunning: (running: boolean) => void }>(function SchemaWorkbench({ state, disabled, onRunning, onDataRunning, onDataWriteRunning }, ref) {
  const [workspace, setWorkspace] = useState<Workspace>(() => ({ left: state.workspace?.left ?? { database: state.connections.find(c => c.id === state.left)?.database ?? '', table: '' }, right: state.workspace?.right ?? { database: state.connections.find(c => c.id === state.right)?.database ?? '', table: '' }, width: state.workspace?.width ?? 280 }));
  const [lists, setLists] = useState<Record<string, { name: string; type?: string }[]>>({});
  const [comparison, setComparison] = useState<Comparison>();
  const [selectedKeys, setSelectedKeys] = useState<string[]>([]);
  const [mode, setMode] = useState('schema');
  const [dataOpened, setDataOpened] = useState(false);
  const [dataWriteBusy, setDataWriteBusy] = useState(false);
  const contextLocked = disabled || dataWriteBusy;
  const syncWasRunning = useRef(false);
  const [dataEpoch, setDataEpoch] = useState(0);
  const dataWorkbench = useRef<DataHandle>(null);
  const [category, setCategory] = useState('columns');
  const [search, setSearch] = useState('');
  const [differences, setDifferences] = useState(false);
  const [collapsed, setCollapsed] = useState(() => window.matchMedia('(max-width: 1050px)').matches);
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
    setComparison(undefined); setSelectedKeys([]); setBusy(''); setNotice(undefined);
    void request('cancel-schema').catch(report);
  };
  useImperativeHandle(ref, () => ({ invalidate }));
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
    const next = field === 'table' ? pairTable(workspace, side, value) : { ...workspace, [side]: { database: value, table: '' } };
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
      if (result.comparison) setComparison(result.comparison);
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
  const definition = (side: Side) => <section className="definition"><div className="definition-heading"><strong>{sideLabel(side)} · {identity(side)}</strong><Button size="small" disabled={!comparison?.[side].ddl} onClick={() => void copy(side)}>复制完整定义</Button></div><pre tabIndex={0}>{comparison?.[side].ddl || '未读取到原始定义'}</pre><Typography.Text type="secondary">MySQL {comparison?.[side].version ?? '未知'} · 开始 {comparison?.[side].startedAt ?? '未知'} · 结束 {comparison?.[side].finishedAt ?? '未知'}</Typography.Text></section>;
  return <section className="schema-workbench">
    {messageHolder}
    <Tabs className="workbench-tabs" tabBarExtraContent={<Button aria-expanded={!collapsed} aria-controls="workbench-navigation" onClick={() => setCollapsed(value => !value)}>{collapsed ? '展开库表导航' : '收起库表导航'}</Button>} activeKey={mode} onChange={value => { setMode(value); if (value === "data") setDataOpened(true); }} items={[{key:"schema",label:"结构比对"},{key:"data",label:"数据比对"}]} />
    <div className="schema-toolbar" style={{display:mode === "schema" ? undefined : "none"}}><Space wrap><Typography.Text strong>单表结构比对</Typography.Text><Tag>比对只读 · 同步需确认</Tag></Space><Space wrap><Button disabled={contextLocked || !selected || !!busy} type="primary" onClick={() => void run('compare', { left: workspace.left, right: workspace.right }, '正在读取两端元数据并比对')}>{comparison ? '刷新比对' : '开始比对'}</Button>{busy && <Button onClick={() => { invalidate(); setNotice({ type: 'info', text: '已取消读取，旧结果已失效。' }); }}>取消读取</Button>}<Button disabled={!comparison || !!busy || disabled} loading={exporting} onClick={() => void exportSummary()}>导出 JSON 摘要</Button></Space></div>
    <div className="schema-layout" style={{ gridTemplateColumns: collapsed ? 'minmax(0, 1fr)' : `${workspace.width ?? 280}px minmax(0, 1fr)` }}>
      {!collapsed && <aside id="workbench-navigation" className="schema-navigation" aria-label="库表导航">
        {sides.map(side => <section className="schema-picker" key={side}><Typography.Text strong>{sideLabel(side)} · {state.connections.find(c => c.id === state[side])?.name ?? '未选连接'}</Typography.Text>
          <label htmlFor={`${side}-database`}>数据库</label><AutoComplete id={`${side}-database`} aria-label={`${sideLabel(side)}数据库`} value={workspace[side].database} options={(lists[`${side}-databases`] ?? []).map(item => ({ value: item.name }))} filterOption={(input, option) => String(option?.value ?? '').toLocaleLowerCase().includes(input.toLocaleLowerCase())} onChange={value => choose(side, 'database', value)} disabled={contextLocked || !state[side]} placeholder="输入或选择数据库" />
          <Button size="small" disabled={contextLocked || !state[side] || !!busy} onClick={() => void run('schema', { action: 'databases', side }, `正在读取${sideLabel(side)}数据库`, `${side}-databases`)}>读取 / 刷新库清单</Button>
          <label htmlFor={`${side}-table`}>表 · 可搜索或手动输入</label><AutoComplete id={`${side}-table`} aria-label={`${sideLabel(side)}表`} value={workspace[side].table} options={(lists[`${side}-tables`] ?? []).map(item => ({ value: item.name, label: `${item.name}${item.type === 'VIEW' ? '（视图，不支持）' : ''}` }))} filterOption={(input, option) => String(option?.value ?? '').toLocaleLowerCase().includes(input.toLocaleLowerCase())} onChange={value => choose(side, 'table', value)} disabled={contextLocked || !state[side] || !workspace[side].database} placeholder="输入或搜索表名" />
          <Button size="small" disabled={contextLocked || !state[side] || !workspace[side].database || !!busy} onClick={() => void run('schema', { action: 'tables', side, database: workspace[side].database }, `正在读取${sideLabel(side)}表清单`, `${side}-tables`)}>读取 / 刷新表清单</Button>
        </section>)}
        <Typography.Paragraph type="secondary" className="schema-hint">默认同名配对；可手动改为不同名。清单仅含可见对象，不据此判断缺表。启动不会自动连接。</Typography.Paragraph>
        <label>导航宽度<Slider disabled={contextLocked} aria-label="导航宽度" min={220} max={360} value={workspace.width ?? 280} onChange={width => setWorkspace(previous => ({ ...previous, width }))} onChangeComplete={width => saveWorkspace({ ...workspace, width })} /></label><Button size="small" disabled={contextLocked} onClick={() => { const next = { ...workspace, width: 280 }; setWorkspace(next); setCollapsed(false); saveWorkspace(next); }}>重置布局</Button>
      </aside>}
      <div className="schema-results">
        <div className="pair-context">{sides.map(side => <div key={side}><span className="eyebrow">{sideLabel(side)}</span><strong title={identity(side)}>{identity(side)}</strong></div>)}</div>
        <div style={{display:mode === "schema" ? undefined : "none"}}>
        {busy && <Alert type="info" showIcon title={`${busy}… 已用时 ${Math.max(0, (clock - started) / 1000).toFixed(1)} 秒`} description="后台执行，可取消；两端分别读取，不保证统一快照。" />}
        {notice && <Alert type={notice.type} showIcon title={notice.text} />}
        {!comparison ? <div className="schema-empty"><Empty description={busy ? '正在读取，尚无可用比对结果' : '尚未比对'} /><Typography.Paragraph type="secondary">选择两端数据库和表，再点击“开始比对”。无需先测试连接。</Typography.Paragraph></div> : <>
          <Alert type={summary(comparison) === '结构相同' ? 'success' : 'warning'} showIcon title={summary(comparison)} description={<Space wrap>{Object.entries(statusLabels).map(([status, label]) => <span key={status}>{label} {comparison.rows.filter(row => row.status === status).length}</span>)}</Space>} />
          <div id="schema-differences" /><Tabs activeKey={category} onChange={setCategory} items={[...categories.map(item => ({ ...item, label: `${item.label} (${comparison.rows.filter(row => row.category === item.key).length})` })), { key: 'ddl', label: '原始定义' }]} />
          {category === 'ddl' ? <><Typography.Paragraph type="secondary">原始 DDL（数据定义语言）仅供查看；文本格式不同不直接决定结构差异。</Typography.Paragraph><div className="definition-pair">{definition('left')}{definition('right')}</div></> : <>
            <div className="schema-filters"><Input.Search aria-label="搜索结构对象" placeholder="搜索对象名称" allowClear value={search} onChange={event => setSearch(event.target.value)} /><Checkbox checked={differences} onChange={event => setDifferences(event.target.checked)}>只看差异</Checkbox><Checkbox disabled={contextLocked || !!busy || !visible.some(row => !selectionReason(row))} checked={visible.some(row => !selectionReason(row)) && visible.filter(row => !selectionReason(row)).every(row => selectedKeys.includes(rowKey(row)))} onChange={event => setSelectedKeys(previous => selectVisible(previous, visible, event.target.checked))}>全选当前可见可执行项</Checkbox><Button size="small" disabled={contextLocked || !!busy || !selectedKeys.length} onClick={() => setSelectedKeys([])}>清空选择</Button><Typography.Text type="secondary">已选 {selectedKeys.length} 项 · 显示 {visible.length} / {comparison.rows.filter(row => row.category === category).length} 项</Typography.Text></div>
            <Table<SchemaRow> rowKey={rowKey} rowSelection={{ selectedRowKeys: selectedKeys, preserveSelectedRowKeys: true, hideSelectAll: true, getCheckboxProps: row => ({ disabled: contextLocked || !!busy || !!selectionReason(row), title: selectionReason(row) }), onSelect: (row, checked) => setSelectedKeys(previous => checked ? [...previous, rowKey(row)] : previous.filter(key => key !== rowKey(row))) }} dataSource={visible} pagination={{ pageSize: 30, showSizeChanger: false, hideOnSinglePage: true }} scroll={{ x: 700, y: 'max(240px, calc(100vh - 490px))' }} locale={{ emptyText: '当前分类或筛选无匹配项；总体结论见上方摘要' }} columns={[
              { title: '对象 / 状态', key: 'object', width: 190, render: (_, row) => <div><strong>{row.name || '类别读取状态'}</strong><div><Tag color={statusColor(row.status)}>{row.status === 'same' && !selectionReason(row) ? '相同，可作为关联操作' : statusLabels[row.status]}</Tag></div>{(row.reason || selectionReason(row)) && <Typography.Text type="secondary">{row.reason || selectionReason(row)}</Typography.Text>}{row.changed.length > 0 && <div className="schema-hint">差异属性：{row.changed.join('、')}</div>}</div> },
              { title: '属性 · 左右对照（展开查看完整属性）', key: 'properties', render: (_, row) => {
                const values = properties(row).filter(property => row.status !== 'same' && (row.status !== 'different' || property.changed));
                return values.length ? <div className="property-summary"><div className="property-summary-heading"><strong>属性</strong><strong>左侧 · {workspace.left.table}</strong><strong>右侧 · {workspace.right.table}</strong></div>{values.map(property => <div key={property.name} className={property.changed ? 'changed-property' : ''}><span>{property.name}</span>{sides.map(side => <pre key={side}>{row[side] === null ? (row.status === (side === 'left' ? 'right-only' : 'left-only') ? '已确认缺失' : '无可用属性') : property[side]}</pre>)}</div>)}</div> : <Typography.Text type="secondary">{row.status === 'same' ? '属性相同，展开查看完整属性' : row.reason || '暂无可用变化属性，展开查看完整信息'}</Typography.Text>;
              } },
            ]} expandable={{ expandedRowRender: row => <div className="property-detail"><div className="property-detail-header"><strong>完整属性</strong><span>{identity('left')}</span><span>{identity('right')}</span></div>{properties(row).map(property => <div className={property.changed ? 'changed-property' : ''} key={property.name}><strong>{property.name}</strong><pre tabIndex={0}>{property.left}</pre><pre tabIndex={0}>{property.right}</pre></div>)}{row.reason && <p>{row.reason}</p>}{(row.left?.ddl || row.right?.ddl) && <section className="definition-pair">{sides.map(side => <section className="definition" key={side}><div className="definition-heading"><strong>{sideLabel(side)} · {identity(side)} · {row.name}</strong><Button size="small" disabled={!row[side]?.ddl} onClick={() => void copy(side, row.category, row.name)}>复制对象完整定义</Button></div><pre tabIndex={0}>{row[side]?.ddl ?? '未读取到对象定义'}</pre></section>)}</section>}</div>, rowExpandable: row => !!row.left || !!row.right }} />
          </>}
        </>}
        <SyncPanel comparison={comparison} selected={selectedKeys} disabled={contextLocked || !!busy} onRunning={running => { if (running && !syncWasRunning.current) invalidateData(); syncWasRunning.current = running; onRunning(running); }} onComparison={value => { setComparison(value); setSelectedKeys([]); }} />
        </div>
        <div style={{display:mode === "data" ? undefined : "none"}}>{dataOpened && <DataWorkbench key={dataEpoch} ref={dataWorkbench} workspace={workspace} disabled={disabled || !!busy} selected={selected} beforeRead={() => pendingWorkspace.current} onRunning={onDataRunning} onMergeRunning={running => { setDataWriteBusy(running); onDataWriteRunning(running); }} />}</div>
      </div>
    </div>
  </section>;
});
