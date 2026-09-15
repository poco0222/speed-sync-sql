import type { SchemaRow } from './schema';
export type Direction = 'left-to-right' | 'right-to-left';
export type SyncEndpoint = { connectionId: string; name: string; database: string; table: string };
export type SyncStep = { sql: string; summary: string; category: string; name: string; risk?: string; context?: object; status?: 'pending' | 'running' | 'passed' | 'failed' | 'unknown'; error?: string };
export type SyncOperation = { category: string; name: string; action: 'add' | 'modify' | 'delete'; summary: string };
export type SyncCounts = { add: number; modify: number; delete: number };
export type SyncPlan = { id: string; direction: Direction; left: SyncEndpoint; right: SyncEndpoint; steps: SyncStep[]; sql: string; operations?: SyncOperation[]; counts?: SyncCounts };
export type SyncRecord = SyncPlan & { startedAt: string; finishedAt?: string; status: 'running' | 'passed' | 'failed' | 'stopped' | 'unknown' | 'blocked'; error?: string; verification?: {status: string; error?: string}; snapshots?: object; storageWarning?: string };
export const syncLabels: Record<string, string> = { pending: '未执行', running: '执行中', passed: '成功', failed: '失败', stopped: '已停止', unknown: '待核实', blocked: '已阻止', same: '结构一致', different: '仍有差异', incomplete: '复核未完整' };
export const rowKey = (row: SchemaRow) => `${row.category}:${row.name}`;
export function selectionReason(row: SchemaRow): string {
 if (!['different','left-only','right-only'].includes(row.status) && !(row.status === 'same' && ['indexes','triggers'].includes(row.category))) return row.reason || '相同或未完整读取的对象不可选择';
 if (row.category === 'constraints') return '外键与 CHECK 仅展示；主键及唯一性请在索引分类选择';
 const props = [row.left?.properties, row.right?.properties].filter(Boolean).flatMap(p => [p, ...(Array.isArray(p!.parts) ? p!.parts as Record<string, unknown>[] : [])]);
 if (props.some(p => Object.entries(p!).some(([key,value]) => /generation.?expression|expression/i.test(key) && !!value))) return '生成列与函数索引仅展示';
 if (row.category === 'table' && [row.name,...row.changed].some(name => /engine|partition/i.test(name))) return '引擎转换与分区仅展示';
 return '';
}
export function selectVisible(selected: string[], rows: SchemaRow[], checked: boolean): string[] {
 const keys = new Set(rows.filter(row => !selectionReason(row)).map(rowKey));
 return checked ? [...new Set([...selected,...keys])] : selected.filter(key => !keys.has(key));
}
export type RecordFilter = {status: string; connection: string; from: string; to: string};
export function filterRecords(records: SyncRecord[], filter: RecordFilter): SyncRecord[] {
 return records.filter(record => (!filter.status || record.status === filter.status) && (!filter.connection || [record.left.name,record.right.name,record.left.connectionId,record.right.connectionId].some(value => value?.toLocaleLowerCase().includes(filter.connection.toLocaleLowerCase()))) && (!filter.from || record.startedAt.slice(0,10) >= filter.from) && (!filter.to || record.startedAt.slice(0,10) <= filter.to));
}

export function isSyncRecord(value: unknown): value is SyncRecord {
 if (!value || typeof value !== 'object') return false;
 const record = value as Partial<SyncRecord>;
 return typeof record.id === 'string' && !!record.id && typeof record.startedAt === 'string' && !!record.left && !!record.right && Array.isArray(record.steps) && ['running','passed','failed','stopped','unknown','blocked'].includes(record.status ?? '');
}
export function changeSummary(plan: Pick<SyncPlan, 'counts'>): string {
 return plan.counts ? `新增 ${plan.counts.add} · 修改 ${plan.counts.modify} · 删除 ${plan.counts.delete}` : '增改删统计未提供';
}
