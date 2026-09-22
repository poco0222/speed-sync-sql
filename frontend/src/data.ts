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

export type DataSettings = {key:string[];fields:string[];filters:DataFilter[]};
export function sameDataSettings(a:DataSettings,b:DataSettings):boolean {
 const effective=(settings:DataSettings)=>({...settings,filters:settings.filters.map(filter=>({...filter,value:filter.op.includes('NULL')?'':filter.value}))});
 return JSON.stringify(effective(a))===JSON.stringify(effective(b));
}
export function validateDataSettings(settings:DataSettings,preparation:Preparation):string {
 if(!settings.fields.length)return '请选择参与字段';
 if(settings.key.length&&!preparation.keys.some(key=>JSON.stringify(key.fields)===JSON.stringify(settings.key)))return '请选择有效的完整可靠键';
 if(settings.fields.some(name=>!preparation.fields.some(field=>field.name===name&&field.compatible)))return '参与字段已失效';
 if(settings.filters.length>32)return '最多支持 32 个共享条件';
 for(const filter of settings.filters){
  const field=preparation.fields.find(field=>field.name===filter.field&&field.compatible);
  if(!field||!operatorsForType(field.type).includes(filter.op))return '筛选字段或操作符无效';
  if(filter.op.includes('NULL'))continue;
  const value=filter.value,type=field.type.toLowerCase();
  if(value.length>4096)return '筛选值最多 4096 字符';
  const integer=/^(tinyint|smallint|mediumint|int|bigint)\b/.exec(type);
  if(integer){
   if(!/^-?[0-9]+$/.test(value))return '整数筛选值格式无效';
   const bits=BigInt(({tinyint:8,smallint:16,mediumint:24,int:32,bigint:64} as Record<string,number>)[integer[1]]),unsigned=type.includes('unsigned');
   const number=BigInt(value),bound=1n<<(unsigned?bits:bits-1n);
   if(unsigned?value.startsWith('-')||number>=bound:number < -bound||number>=bound)return '整数筛选值超出字段范围';
  }
  if(/^decimal\b/.test(type)){
   const definition=/decimal\(([0-9]+),([0-9]+)\)/.exec(type),number=/^-?([0-9]+)(?:\.([0-9]+))?$/.exec(value);
   if(!definition)return 'DECIMAL 定义不支持';
   if(!number)return 'DECIMAL 筛选应使用普通十进制文本';
   const whole=number[1].replace(/^0+/,''),scale=Number(definition[2]);
   if(whole.length>Number(definition[1])-scale||(number[2]?.length??0)>scale||type.includes('unsigned')&&value.startsWith('-'))return 'DECIMAL 筛选值超出精度或范围';
  }
  if(/^(float|double|real)\b/.test(type)&&(!/^-?[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?$/.test(value)||!Number.isFinite(Number(value))))return '浮点筛选值格式或范围无效';
  if(/^(binary|varbinary|tinyblob|blob|mediumblob|longblob|bit)\b/.test(type)&&! /^(?:[0-9a-fA-F]{2})*$/.test(value))return '二进制筛选值须为完整 HEX 字节';
  if(/^bit\b/.test(type)){
   const bits=Number(/^bit\(([0-9]+)\)/.exec(type)?.[1]);
   if(!value||bits<1||bits>64||!Number.isInteger(bits)||BigInt(`0x${value}`)>=(1n<<BigInt(bits)))return 'BIT 筛选超出字段范围';
  }
  if(/^(date|datetime|timestamp)\b/.test(type)){
   const date=value.slice(0,10),parsed=new Date(`${date}T00:00:00Z`);
   if(!/^\d{4}-\d{2}-\d{2}$/.test(date)||date.startsWith('0000')||Number.isNaN(parsed.getTime())||parsed.toISOString().slice(0,10)!==date)return '日期筛选值无效';
   if(type==='date'?value.length!==10:!/^\d{4}-\d{2}-\d{2} ([01]\d|2[0-3]):[0-5]\d:[0-5]\d(?:\.\d{1,6})?$/.test(value))return '时间筛选值格式无效';
  }
  if(/^time\b/.test(type)){const match=/^-?(\d{1,3}):[0-5]\d:[0-5]\d(?:\.\d{1,6})?$/.exec(value);if(!match||Number(match[1])>838)return 'TIME 筛选值无效';}
  if(/^year\b/.test(type)&&(!/^\d{4}$/.test(value)||(value!=='0000'&&(Number(value)<1901||Number(value)>2155))))return 'YEAR 筛选值无效';
 }
 return '';
}
