export type Side = 'left' | 'right';
export type Selection = { database: string; table: string };
export type Workspace = { left: Selection; right: Selection; width?: number };
export type Category = 'columns' | 'indexes' | 'constraints' | 'triggers' | 'table';
export type ObjectStatus = 'same' | 'different' | 'left-only' | 'right-only' | 'unread' | 'failed' | 'unsupported';
export type SchemaItem = { name: string; properties: Record<string, unknown>; ddl?: string };
export type SchemaRow = { category: Category; name: string; status: ObjectStatus; left: SchemaItem | null; right: SchemaItem | null; changed: string[]; reason?: string };
export type SchemaEndpoint = Selection & { version?: string; startedAt?: string; finishedAt?: string; ddl?: string; categories?: Record<string, unknown> };
export type Comparison = { complete: boolean; status: 'same' | 'different' | 'incomplete'; rows: SchemaRow[]; left: SchemaEndpoint; right: SchemaEndpoint };
export const categories: { key: Category; label: string }[] = [{ key: 'columns', label: '字段' }, { key: 'indexes', label: '索引' }, { key: 'constraints', label: '约束' }, { key: 'triggers', label: '触发器' }, { key: 'table', label: '表属性' }];
export const statusLabels: Record<ObjectStatus, string> = { same: '相同', different: '不同', 'left-only': '仅左', 'right-only': '仅右', unread: '未读取', failed: '读取失败', unsupported: '不支持' };
export function filterRows(rows: SchemaRow[], category: string, search: string, differences: boolean): SchemaRow[] {
  return rows.filter(row => row.category === category && (!differences || row.status !== 'same') && (['unread', 'failed', 'unsupported'].includes(row.status) || row.name.toLocaleLowerCase().includes(search.toLocaleLowerCase())));
}
export function summary(comparison: Pick<Comparison, 'complete' | 'status' | 'rows'>): string {
  if (!comparison.complete || comparison.status === 'incomplete' || comparison.rows.some(row => ['unread', 'failed', 'unsupported'].includes(row.status))) return '比对未完整';
  return comparison.status === 'same' ? '结构相同' : '存在结构差异';
}
export function pairTable(workspace: Workspace, side: Side, table: string): Workspace {
  const other = side === 'left' ? 'right' : 'left';
  return { ...workspace, [side]: { ...workspace[side], table }, ...(!workspace[other].table || (side === 'left' && workspace[other].table === workspace[side].table) ? { [other]: { ...workspace[other], table } } : {}) };
}

export function propertyRows(row: SchemaRow): { name: string; left: unknown; right: unknown; changed: boolean }[] {
  const names = [...new Set([...Object.keys(row.left?.properties ?? {}), ...Object.keys(row.right?.properties ?? {})])];
  return names.map(name => ({ name, left: row.left?.properties[name], right: row.right?.properties[name], changed: row.changed.includes(name) }));
}
