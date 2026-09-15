import type { SyncPlan, SyncRecord } from './sync';
import type { Comparison, Workspace } from './schema';
export type Connection = {
  id?: string; name: string; host: string; port: number; user: string;
  password?: string; database: string; remember: boolean;
  tls: 'preferred' | 'required' | 'verify' | 'disabled'; ca: string; timeout: number | null;
  lastTest?: TestResult;
};
export type Settings = { theme: 'system' | 'light' | 'dark'; density: 'standard' | 'compact'; timeout: number };
export type State = { connections: Connection[]; left: string; right: string; settings: Settings; loadError?: string; qtVersion?: string; driverAvailable?: boolean; workspace?: Partial<Workspace> };
export type TestResult = { ok: boolean; error?: string; code?: string; version?: string; database?: string; encrypted?: boolean; testedAt?: string; elapsedMs?: number; stale?: boolean; storageWarning?: string };
export type Result = TestResult & { state?: State; id?: string; cancelled?: boolean; items?: { name: string; type?: string }[]; comparison?: Comparison; plan?: SyncPlan; blockers?: string[]; running?: boolean; record?: SyncRecord; records?: SyncRecord[] };
type Native = { request: (json: string) => void; response: { connect: (callback: (json: string) => void) => void } };
declare global {
  interface Window {
    qt?: { webChannelTransport: unknown };
    QWebChannel?: new (transport: unknown, callback: (channel: { objects: { foundation: Native } }) => void) => unknown;
  }
}
let sequence = 0;
const pending = new Map<string, { resolve: (result: Result) => void; reject: (error: Error) => void; timer: ReturnType<typeof setTimeout> }>();
let native: Native | undefined;
let readyPromise: Promise<void> | undefined;
export function ready(): Promise<void> {
  return readyPromise ??= new Promise((resolve, reject) => {
    if (!window.qt || !window.QWebChannel) { reject(new Error('请从 Speed Sync SQL 桌面应用打开此界面')); return; }
    const timer = setTimeout(() => reject(new Error('桌面桥接初始化超时，请重启应用')), 10000);
    new window.QWebChannel(window.qt.webChannelTransport, channel => {
      native = channel.objects.foundation;
      native.response.connect(json => {
        const result = JSON.parse(json) as Result & { requestId: string };
        const item = pending.get(result.requestId);
        if (!item) return;
        clearTimeout(item.timer); pending.delete(result.requestId); item.resolve(result);
      });
      clearTimeout(timer); resolve();
    });
  });
}
export async function request(operation: string, args: object = {}): Promise<Result> {
  await ready();
  const requestId = `request-${++sequence}`;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending.delete(requestId); reject(new Error('桌面请求未返回，请检查应用状态')); }, ['export', 'export-schema', 'export-sync', 'export-sync-record'].includes(operation) ? 600000 : ['compare', 'plan-sync'].includes(operation) ? 180000 : 75000);
    pending.set(requestId, { resolve, reject, timer });
    native!.request(JSON.stringify({ requestId, operation, args }));
  });
}
