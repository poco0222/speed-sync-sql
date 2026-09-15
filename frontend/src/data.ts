import type { TestResult } from './bridge';
export type DataFilter = { field: string; op: string; value: string };
export type Preparation = { fields: {name:string;type:string;compatible:boolean;reason?:string}[]; keys:{name:string;fields:string[]}[]; defaultFields:string[];defaultKey:string[];canCompare:boolean };
export type DataTask = {id:string;state:'running'|'complete'|'failed'|'cancelled';phase:string;leftScanned:number;rightScanned:number;batches:number;counts:{same:number;different:number;leftOnly:number;rightOnly:number};complete:boolean;error?:string;leftStartedAt?:string;rightStartedAt?:string;leftFinishedAt?:string;rightFinishedAt?:string;consistency:string};
export type DataRow = {id:string;key:string[];status:string;changedFields:string[]};
export type DataValue = {isNull:boolean;text:string;length:number;encoding:string;truncated:boolean};
export type DataField = {name:string;type:string;changed:boolean;left:DataValue|null;right:DataValue|null};
export type DataResult = TestResult & {preparation?:Preparation;taskId?:string;task?:DataTask;rows?:DataRow[];total?:number;fields?:DataField[];value?:DataValue;offset?:number;nextOffset?:number};
export const dataLabels:Record<string,string> = {same:'相同',different:'不同','left-only':'仅左','right-only':'仅右',unmatched:'未匹配',running:'扫描中',complete:'扫描结束',failed:'扫描失败',cancelled:'已停止'};
export const filterOperators = ['=', '!=', '>', '>=', '<', '<=', 'IS NULL', 'IS NOT NULL'];
export function dataConclusion(task:DataTask, browse:boolean):string {
 if (browse) return '左右独立浏览 · 未匹配，不推断差异';
 if (!task.complete || task.state !== 'complete') return '比对未完整';
 if (!task.leftScanned && !task.rightScanned) return '两端范围均为空';
 if (!task.leftScanned || !task.rightScanned) return `${!task.leftScanned ? '左' : '右'}端范围为空 · 存在数据差异`;
 return task.counts.different || task.counts.leftOnly || task.counts.rightOnly ? '所选字段与范围存在数据差异' : '所选字段与范围相同';
}
export function displayValue(value:DataValue|null):string {
 if (!value) return '该端无记录';
 if (value.isNull) return 'NULL';
 return `${value.encoding === 'hex' ? 'HEX: ' : ''}${value.text === '' ? '（空字符串）' : value.text}${value.truncated ? '…（缩略，按需加载原值）' : ''}`;
}
export function exactKey(values:Record<string,string>, fields:string[]):string[] {
 return fields.map(field => values[field] ?? '');
}
export function operatorsForType(type:string):string[] {
 return /^json\b/i.test(type) ? ['IS NULL','IS NOT NULL'] : filterOperators;
}
export function filterPlaceholder(type:string):string {
 if (/binary|blob|bit/i.test(type)) return '完整 HEX 字节，例如 00ff（不加 0x）';
 if (/datetime|timestamp/i.test(type)) return 'YYYY-MM-DD HH:mm:ss.ffffff（TIMESTAMP 使用 UTC）';
 if (/^date\b/i.test(type)) return 'YYYY-MM-DD';
 if (/^time\b/i.test(type)) return 'HH:mm:ss.ffffff';
 return '原值，空字符串合法';
}
export function validFilter(filter:DataFilter,type:string):DataFilter {
 const operators=operatorsForType(type);
 return operators.includes(filter.op) ? filter : {...filter,op:operators[0]};
}
export function copyOriginalText(text:string):void {
 let copied=false;
 const onCopy=(event:ClipboardEvent)=>{
  event.preventDefault();event.stopPropagation();
  if(event.clipboardData){event.clipboardData.clearData();event.clipboardData.setData('text/plain',text);copied=true;}
 };
 // Qt WebEngine can leave the async Clipboard API pending; use Ant Design's copy-event fallback.
 document.addEventListener('copy',onCopy,{capture:true});
 try{document.execCommand('copy');if(!copied)throw new Error('复制失败，请聚焦应用后重试');}
 finally{document.removeEventListener('copy',onCopy,{capture:true});}
}
