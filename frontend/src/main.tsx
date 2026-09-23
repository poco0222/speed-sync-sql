import { SyncRecords } from './SyncRecords';
import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Alert, Button, Checkbox, Collapse, ConfigProvider, Drawer, Empty, Form, Input, InputNumber, Layout, Menu, Modal, Select, Space, Table, Tag, Typography, theme, message as messageService } from 'antd';
import zhCN from 'antd/locale/zh_CN';
import { ApiOutlined, CloseOutlined, CopyOutlined, DeleteOutlined, EditOutlined, ExportOutlined, LeftOutlined, PlusOutlined, RightOutlined, SaveOutlined, SettingOutlined, StopOutlined, SwapOutlined } from '@ant-design/icons';
import { request, type Connection, type State, type Settings, type TestResult } from './bridge';
import { Freshness } from './freshness';
import { SchemaWorkbench, type SchemaHandle } from './SchemaWorkbench';
import './style.css';
const { Text, Title } = Typography;
const blank = (): Connection => ({ name: '', host: '', port: 3306, user: '', database: '', remember: false, tls: 'preferred', ca: '', timeout: null, password: '' });
const initial: State = { connections: [], left: '', right: '', settings: { theme: 'system', density: 'standard', timeout: 10 } };
const tlsOptions = [{ value: 'preferred', label: '优先加密' }, { value: 'required', label: '要求加密' }, { value: 'verify', label: '验证服务器身份' }, { value: 'disabled', label: '关闭 TLS' }];
function ResultText({ result }: { result?: TestResult }) {
  if (!result) return <Text type="secondary">尚未测试</Text>;
  return <span className="test-result"><Tag color={result.ok ? 'success' : 'error'}>{result.ok ? '上次测试成功' : '上次测试失败'}</Tag><span>{result.ok ? `MySQL ${result.version} · ${result.encrypted ? 'TLS 已加密' : '未加密'}` : result.error}</span>{result.testedAt && <time>{new Date(result.testedAt).toLocaleString('zh-CN')}</time>}</span>;
}
function Application() {
  const [state, setState] = useState<State>(initial);
  const [booting, setBooting] = useState(true);
  const [fatal, setFatal] = useState('');
  const [schemaReading, setSchemaReading] = useState(false);
  const [swapping, setSwapping] = useState(false);
  const swappingRef = useRef(false);
  const [syncRunning, setSyncRunning] = useState(false);
  const [dataRunning, setDataRunning] = useState(false);
  const [dataWriteRunning, setDataWriteRunning] = useState(false);
  const [stopping, setStopping] = useState(false);
  const stoppingRef = useRef(false);
  const [restoreKey, setRestoreKey] = useState(0);
  const [page, setPage] = useState('workbench');
  const [editing, setEditing] = useState<Connection | null>(null);
  const [drawerOpen, setDrawerOpen] = useState(false);
  const [editorResult, setEditorResult] = useState<TestResult>();
  const [busy, setBusy] = useState<Record<string, boolean>>({});
  const [startedAt, setStartedAt] = useState<Record<string, number>>({});
  const [clock, setClock] = useState(Date.now());
  const [saving, setSaving] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [settingsDraft, setSettingsDraft] = useState<Settings>(initial.settings);
  const [systemDark, setSystemDark] = useState(matchMedia('(prefers-color-scheme: dark)').matches);
  const [form] = Form.useForm<Connection>();
  const freshness = useRef(new Freshness());
  const schemaWorkbench = useRef<SchemaHandle>(null);
  const [message, messageHolder] = messageService.useMessage();
  const [modal, modalHolder] = Modal.useModal();
  const running = Object.values(busy).some(Boolean);
  useEffect(() => {
    if (!running) return;
    const timer = setInterval(() => setClock(Date.now()), 250);
    return () => clearInterval(timer);
  }, [running]);
  const elapsed = (lane: string) => Math.max(0, (clock - (startedAt[lane] ?? clock)) / 1000).toFixed(1);
  const unavailable = swapping || syncRunning || booting || !!fatal || !!state.loadError;
  const contextLocked = unavailable || dataWriteRunning;
  useEffect(() => {
    const media = matchMedia('(prefers-color-scheme: dark)');
    const change = () => setSystemDark(media.matches);
    media.addEventListener('change', change);
    request('snapshot').then(result => { if (!result.ok || !result.state) throw new Error(result.error ?? '无法读取应用状态'); setState(result.state); }).catch(e => setFatal(e.message)).finally(() => setBooting(false));
    return () => media.removeEventListener('change', change);
  }, []);
  const dark = state.settings.theme === 'dark' || (state.settings.theme === 'system' && systemDark);
  useEffect(() => { document.documentElement.dataset.theme = dark ? 'dark' : 'light'; }, [dark]);
  const refresh = async () => { const result = await request('snapshot'); if (result.state) setState(result.state); };
  const action = async (operation: string, args: object) => {
    const result = await request(operation, args);
    if (!result.ok) throw new Error(result.error ?? '操作失败');
    if (result.state) setState(result.state);
    return result;
  };
  const report = (error: unknown) => { void message.error(error instanceof Error ? error.message : '操作失败'); };
  const stopCurrentTask = async () => {
    if (stoppingRef.current) return;
    stoppingRef.current = true; setStopping(true);
    try {
      const status = await action(syncRunning ? 'sync-status' : 'merge-status', {});
      if (status.running) await action(syncRunning ? 'stop-sync' : 'merge-stop', {});
      else void message.info('当前没有正在执行的写入任务');
    } catch (error) { report(error); }
    finally { stoppingRef.current = false; setStopping(false); }
  };
  const openEditor = (connection?: Connection, copy = false) => {
    if (connection && !copy) schemaWorkbench.current?.invalidate();
    freshness.current.invalidate('editor'); setEditorResult(undefined);
    const value = connection ? { ...connection, ...(copy ? { id: undefined, name: `${connection.name} 副本`, remember: false, password: '' } : { password: undefined }), lastTest: undefined } : blank();
    setEditing(value); form.resetFields(); form.setFieldsValue(value); setDrawerOpen(true);
  };
  const closeEditor = () => { freshness.current.invalidate('editor'); setDrawerOpen(false); };
  const test = async (lane: string, connection?: Connection) => {
    let value: Connection;
    try { value = connection ?? { ...editing!, ...await form.validateFields() }; } catch { return; }
    const generation = freshness.current.invalidate(lane);
    const start = Date.now(); setClock(start);
    setStartedAt(previous => ({ ...previous, [lane]: start }));
    setBusy(previous => ({ ...previous, [lane]: true }));
    try {
      const result = await request('test', { lane, connection: value });
      if (freshness.current.current(lane, generation) && !result.stale) {
        if (lane === 'editor') setEditorResult(result);
        else if (!result.ok) void message.error(result.error ?? '连接失败');
        if (result.storageWarning) void message.warning(result.storageWarning);
      }
      await refresh();
    } catch (error) { if (freshness.current.current(lane, generation)) report(error); }
    finally { setBusy(previous => ({ ...previous, [lane]: false })); }
  };
  const save = async () => {
    let value: Connection;
    try { value = { ...editing!, ...await form.validateFields() }; } catch { return; }
    schemaWorkbench.current?.invalidate();
    setSaving(true);
    try { await action('save', value); closeEditor(); void message.success('连接已保存'); }
    catch (error) { report(error); } finally { setSaving(false); }
  };
  const select = async (side: 'left' | 'right', id: string) => {
    schemaWorkbench.current?.invalidate();
    freshness.current.invalidate(side);
    try { await action('select', { side, id }); } catch (error) { report(error); }
  };
  const swap = async () => {
    if (contextLocked || schemaReading || dataRunning || running || swappingRef.current || !state.left || !state.right) return;
    swappingRef.current = true; setSwapping(true);
    try {
      await schemaWorkbench.current?.prepareSwap();
      await action('swap-endpoints', {});
      setRestoreKey(value => value + 1);
    } catch (error) { report(error); }
    finally { swappingRef.current = false; setSwapping(false); }
  };
  const remove = (connection: Connection) => {
    modal.confirm({ title: `删除“${connection.name}”？`, content: '仅删除本地连接配置及记住的密码，不影响数据库。', okText: '删除配置', okButtonProps: { danger: true }, cancelText: '取消', onOk: async () => { schemaWorkbench.current?.invalidate(); try { await action('delete', { id: connection.id }); } catch (error) { report(error); throw error; } } });
  };
  const endpoint = (side: 'left' | 'right') => {
    const connection = state.connections.find(item => item.id === state[side]);
    return <section className="endpoint" aria-label={side === 'left' ? '来源连接' : '目标连接'}>
      <Typography.Title level={5}>{side === 'left' ? '来源' : '目标'}连接</Typography.Title>
      <div className="endpoint-controls"><Select aria-label={side === 'left' ? '选择来源连接' : '选择目标连接'} placeholder="选择一个连接" value={state[side] || undefined} allowClear showSearch optionFilterProp="label" disabled={contextLocked || busy[side]} options={state.connections.map(c => ({ value: c.id, label: c.name }))} onChange={id => void select(side, id ?? '')} /><Button icon={<ApiOutlined />} disabled={!connection || contextLocked} loading={busy[side]} onClick={() => void test(side, connection)}>测试连接</Button><Button icon={connection ? <EditOutlined /> : <PlusOutlined />} disabled={contextLocked} onClick={() => openEditor(connection)}>{connection ? '编辑' : '新建'}</Button></div>
      <div className="endpoint-detail">{busy[side] ? <Text>正在测试连接… 已用时 {elapsed(side)} 秒，窗口仍可操作</Text> : connection ? <><span className="mono">{connection.host}:{connection.port}{connection.database ? ` / ${connection.database}` : ''}</span><ResultText result={connection.lastTest} /></> : <Text type="secondary">选择已有连接或新建连接；测试连接可选</Text>}</div>
    </section>;
  };
  return <ConfigProvider locale={zhCN} theme={{ algorithm: [dark ? theme.darkAlgorithm : theme.defaultAlgorithm, ...(state.settings.density === 'compact' ? [theme.compactAlgorithm] : [])], token: { colorPrimary: '#2764d7', borderRadius: 6, fontSize: 14, fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif' } }}>
    {messageHolder}{modalHolder}<Layout className="app-shell">
      <header className="topbar"><div className="brand"><span className="brand-mark">S<span>↔</span></span><strong>Speed Sync <span>SQL</span></strong></div><Menu mode="horizontal" selectedKeys={[page]} items={[{ key: 'workbench', label: '比对工作台' }, { key: 'connections', label: '连接管理' }, { key: 'records', label: '执行记录' }]} onClick={({ key }) => setPage(key)} /><Button icon={<SettingOutlined />} disabled={contextLocked} onClick={() => { setSettingsDraft(state.settings); setSettingsOpen(true); }}>设置</Button></header>
      <main>
        {(fatal || state.loadError) && <Alert className="banner" type="error" showIcon title={fatal || state.loadError} />}
        {!fatal && !booting && !state.driverAvailable && <Alert className="banner" type="warning" showIcon title="QMYSQL 驱动未就绪" description="可先保存连接配置。连接测试需要安装匹配的 MySQL 驱动及客户端库。" />}
        {<div style={{ display: page === 'workbench' ? 'contents' : 'none' }}>
          {!booting && <SchemaWorkbench ref={schemaWorkbench} key={JSON.stringify([restoreKey, state.left, state.right, ...state.connections.filter(c => c.id === state.left || c.id === state.right).map(({ lastTest, ...connection }) => connection)])} state={state} disabled={unavailable} onRunning={setSyncRunning} onReading={setSchemaReading} onDataRunning={setDataRunning} onDataWriteRunning={setDataWriteRunning}
            connectionControls={<div className="connection-bar">{endpoint('left')}<Button icon={<SwapOutlined />} disabled={contextLocked || schemaReading || dataRunning || running || !state.left || !state.right} loading={swapping} onClick={() => void swap()}>对换</Button>{endpoint('right')}</div>}
          />}
        </div>}
        {page === 'records' && <SyncRecords disabled={contextLocked} onRestore={next => { schemaWorkbench.current?.invalidate(); setState(next); setRestoreKey(value => value + 1); setPage('workbench'); }} />}
        {page === 'connections' && <section className="connections-page"><div className="page-heading"><div><div className="eyebrow">连接管理</div><Title level={3}>常用数据库</Title><Text type="secondary">保存配置与测试连接相互独立。</Text></div><Space wrap><Button icon={<ExportOutlined />} disabled={contextLocked} onClick={() => { void action('export', {}).then(result => { if (!result.cancelled) void message.success('诊断摘要已保存'); }).catch(report); }}>导出诊断</Button><Button type="primary" icon={<PlusOutlined />} disabled={contextLocked} onClick={() => openEditor()}>新建连接</Button></Space></div>
          <Table<Connection> rowKey="id" dataSource={state.connections} pagination={false} scroll={{ x: 920 }} locale={{ emptyText: <Empty description="还没有保存的连接"><Button icon={<PlusOutlined />} onClick={() => openEditor()} disabled={unavailable}>添加第一个连接</Button></Empty> }} columns={[
            { title: '连接名称', dataIndex: 'name', width: 190, render: (name, c) => <Button type="link" className="name-button" disabled={contextLocked} onClick={() => openEditor(c)}>{name}</Button> },
            { title: '服务器', key: 'server', width: 230, render: (_, c) => <div><span className="mono">{c.host}:{c.port}</span><div className="subtle">{c.database || '未指定默认数据库'}</div></div> },
            { title: '上次测试', key: 'test', width: 280, render: (_, c) => <ResultText result={c.lastTest} /> },
            { title: '操作', key: 'actions', width: 280, render: (_, c) => <Space size={4} wrap><Button size="small" icon={<LeftOutlined />} disabled={contextLocked || busy.left} onClick={() => void select('left', c.id!)}>用作左侧</Button><Button size="small" icon={<RightOutlined />} disabled={contextLocked || busy.right} onClick={() => void select('right', c.id!)}>用作右侧</Button><Button size="small" icon={<CopyOutlined />} disabled={contextLocked} onClick={() => openEditor(c, true)}>复制</Button><Button size="small" icon={<DeleteOutlined />} disabled={contextLocked} danger onClick={() => remove(c)}>删除</Button></Space> }
          ]} />
        </section>}
      </main>
      <footer><Space><span>{syncRunning ? '结构同步执行中' : dataWriteRunning ? '数据写入执行中' : dataRunning ? '数据读取 / 比对进行中' : Object.values(busy).some(Boolean) ? '连接测试进行中' : '当前无运行任务'}</span>{(syncRunning || dataWriteRunning) && <Button size="small" danger icon={<StopOutlined />} loading={stopping} onClick={() => void stopCurrentTask()}>停止当前任务</Button>}</Space><span>本地桌面应用</span></footer>
      <Drawer title={editing?.id ? '编辑连接' : '新建连接'} open={drawerOpen} onClose={closeEditor} width={480} maskClosable={!saving&&!contextLocked} closable={!saving&&!contextLocked} extra={<Button icon={<CloseOutlined />} disabled={saving||contextLocked} onClick={closeEditor}>取消</Button>} footer={<div className="drawer-footer"><Button icon={<ApiOutlined />} loading={busy.editor} disabled={saving||contextLocked} onClick={() => void test('editor')}>测试连接</Button><Button type="primary" icon={<SaveOutlined />} loading={saving} disabled={saving||contextLocked} onClick={() => void save()}>保存连接</Button></div>}>
        <Form form={form} layout="vertical" requiredMark="optional" onValuesChange={() => { freshness.current.invalidate('editor'); setEditorResult(undefined); }}>
          <Form.Item name="name" label="连接名称" rules={[{ required: true, whitespace: true, message: '输入便于辨认的连接名称' }, { max: 255 }]}><Input placeholder="例如：开发环境" maxLength={255} /></Form.Item>
          <div className="form-row"><Form.Item name="host" label="主机" rules={[{ required: true, whitespace: true, message: '请输入主机地址' }]}><Input placeholder="127.0.0.1" maxLength={255} /></Form.Item><Form.Item name="port" label="端口" rules={[{ required: true, type: 'integer', min: 1, max: 65535, message: '端口范围 1–65535' }]}><InputNumber min={1} max={65535} precision={0} /></Form.Item></div>
          <Form.Item name="user" label="用户名" rules={[{ required: true, whitespace: true, message: '请输入用户名' }]}><Input autoComplete="off" maxLength={255} /></Form.Item>
          <Form.Item name="password" label="密码" extra={editing?.id ? '留空且未编辑时保留当前密码；清空输入可替换为空密码。' : '默认仅本次会话使用。'}><Input.Password autoComplete="new-password" placeholder={editing?.id ? '密码已隐藏，输入以替换' : '输入数据库密码'} /></Form.Item>
          <Form.Item name="remember" valuePropName="checked"><Checkbox>记住密码（存入系统凭据库）</Checkbox></Form.Item>
          <Form.Item name="database" label="默认数据库"><Input placeholder="可留空" maxLength={255} /></Form.Item>
          <Collapse items={[{ key: 'advanced', label: '高级选项', children: <><Form.Item name="tls" label="TLS 加密"><Select options={tlsOptions} /></Form.Item><Form.Item noStyle shouldUpdate={(a, b) => a.tls !== b.tls}>{({ getFieldValue }) => getFieldValue('tls') === 'verify' ? <Form.Item name="ca" label="CA 证书文件路径" rules={[{ required: true, message: '请输入 CA 文件路径' }]}><Input placeholder="完整本地路径" /></Form.Item> : null}</Form.Item><Form.Item name="timeout" label="连接超时（秒）" extra={`留空跟随全局设置，当前 ${state.settings.timeout} 秒。整个测试有额外 5 秒清理时间。`}><InputNumber min={1} max={60} precision={0} placeholder="跟随设置" /></Form.Item></> }]} />
          {busy.editor && <Alert className="editor-status" type="info" title={`正在测试连接… 已用时 ${elapsed('editor')} 秒`} description="可继续编辑；修改后的表单不会采用旧测试结果。" />}
          {editorResult && <Alert className="editor-status" showIcon type={editorResult.ok ? 'success' : 'error'} title={editorResult.ok ? `连接成功 · MySQL ${editorResult.version}` : editorResult.error} description={editorResult.ok ? `${editorResult.encrypted ? 'TLS 已加密' : '当前连接未加密'} · ${editorResult.elapsedMs ?? 0} ms` : undefined} />}
        </Form>
      </Drawer>
      <Modal title="工作台设置" open={settingsOpen} onCancel={() => setSettingsOpen(false)} okText="保存设置" cancelText="取消" confirmLoading={saving} onOk={() => { setSaving(true); void action('settings', settingsDraft).then(() => setSettingsOpen(false)).catch(report).finally(() => setSaving(false)); }}>
        <Form layout="vertical"><Form.Item label="主题"><Select aria-label="主题" value={settingsDraft.theme} onChange={value => setSettingsDraft(d => ({ ...d, theme: value }))} options={[{ value: 'system', label: '跟随系统' }, { value: 'light', label: '浅色' }, { value: 'dark', label: '深色' }]} /></Form.Item><Form.Item label="密度"><Select aria-label="密度" value={settingsDraft.density} onChange={value => setSettingsDraft(d => ({ ...d, density: value }))} options={[{ value: 'standard', label: '标准' }, { value: 'compact', label: '紧凑' }]} /></Form.Item><Form.Item label="默认连接超时（秒）"><InputNumber aria-label="默认连接超时（秒）" min={1} max={60} precision={0} value={settingsDraft.timeout} onChange={value => setSettingsDraft(d => ({ ...d, timeout: value ?? 10 }))} /></Form.Item></Form>
      </Modal>
    </Layout>
  </ConfigProvider>;
}
createRoot(document.getElementById('root')!).render(<Application />);
