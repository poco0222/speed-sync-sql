import test from 'node:test';
import assert from 'node:assert/strict';
import {dataConclusion,displayValue,exactKey,copyOriginalText,operatorsForType,filterPlaceholder,validFilter} from '../src/data.ts';
test('only complete reliable scan can conclude equal; empty sides and browse stay explicit',()=>{
 const t={state:'running',complete:false,leftScanned:0,rightScanned:0,counts:{same:0,different:0,leftOnly:0,rightOnly:0}};
 assert.equal(dataConclusion(t,false),'比对未完整');
 assert.match(dataConclusion({...t,state:'failed'},false),/未完整/);
 assert.match(dataConclusion({...t,state:'cancelled'},false),/未完整/);
 assert.match(dataConclusion({...t,state:'complete',complete:true},false),/两端范围均为空/);
 assert.match(dataConclusion({...t,state:'complete',complete:true,rightScanned:1},false),/左端范围为空/);
 assert.match(dataConclusion({...t,state:'complete',complete:true,leftScanned:1,rightScanned:1},false),/所选字段与范围相同/);
 assert.match(dataConclusion(t,true),/未匹配/);
});
test('precision and null/empty/binary presentation survive the UI',()=>{
 const v={isNull:false,text:'9007199254740993',length:16,encoding:'text',truncated:false};
 assert.equal(displayValue(v),'9007199254740993');
 assert.equal(displayValue({...v,isNull:true}),'NULL');
 assert.equal(displayValue({...v,text:''}),'（空字符串）');
 assert.equal(displayValue({...v,text:'00ff',encoding:'hex'}),'HEX: 00ff');
 assert.match(displayValue({...v,truncated:true}),/按需加载/);
 assert.deepEqual(exactKey({id:v.text,part:'',amount:'1.0000000000000001'},['id','part','amount']),[v.text,'','1.0000000000000001']);
});

test('type-aware filters restrict JSON and repair operators when fields change',()=>{
 assert.deepEqual(operatorsForType('json'),['IS NULL','IS NOT NULL']);
 assert.deepEqual(validFilter({field:'payload',op:'=',value:'123'},'JSON'),{field:'payload',op:'IS NULL',value:'123'});
 assert.equal(validFilter({field:'id',op:'>=',value:'9007199254740993'},'bigint unsigned').op,'>=');
 assert.match(filterPlaceholder('varbinary(32)'),/完整 HEX 字节/);
 assert.match(filterPlaceholder('datetime(6)'),/YYYY-MM-DD HH:mm:ss/);
 assert.equal(filterPlaceholder('date'),'YYYY-MM-DD');
 assert.equal(filterPlaceholder('time(6)'),'HH:mm:ss.ffffff');
});

test('copy event preserves full text including empty strings and always removes listener',()=>{
 const original=globalThis.document;
 let listener,removed=0,copied;
 globalThis.document={
  addEventListener(_event,fn){listener=fn;},
  removeEventListener(){removed++;},
  execCommand(){listener({preventDefault(){},stopPropagation(){},clipboardData:{clearData(){},setData(format,text){assert.equal(format,'text/plain');copied=text;}}});return true;}
 };
 try{
  const text='9007199254740993\n'+'x'.repeat(70000);
  copyOriginalText(text);assert.equal(copied,text);
  copyOriginalText('');assert.equal(copied,'');
  globalThis.document.execCommand=()=>false;
  assert.throws(()=>copyOriginalText('no'),/复制失败/);
  assert.equal(removed,3);
 }finally{globalThis.document=original;}
});

test('draft validation preserves precision and rejects malformed typed values',async()=>{
 const {validateDataSettings,sameDataSettings}=await import('../src/data.ts');
 const prep={fields:[{name:'id',type:'bigint',compatible:true},{name:'day',type:'date',compatible:true},{name:'bytes',type:'blob',compatible:true}],keys:[{name:'PRIMARY',fields:['id']}]};
 const config={key:['id'],fields:['id'],filters:[{field:'id',op:'=',value:'9007199254740993'}]};
 assert.equal(validateDataSettings(config,prep),'');
 assert.equal(sameDataSettings(config,structuredClone(config)),true);
 for(const filter of [{field:'id',op:'=',value:'abc'},{field:'day',op:'=',value:'2026-02-30'},{field:'bytes',op:'=',value:'abc'},{field:'missing',op:'=',value:''}])assert.notEqual(validateDataSettings({...config,filters:[filter]},prep),'');
 assert.equal(sameDataSettings(config,{...config,filters:[]}),false);
 assert.equal(validateDataSettings({...config,fields:[]},prep),'请选择参与字段');
});

test('filter bounds use full definitions and exact integers, decimals and BIT bytes',async()=>{
 const {validateDataSettings}=await import('../src/data.ts');
 const check=(type,value)=>validateDataSettings({key:[],fields:['v'],filters:[{field:'v',op:'=',value}]},{fields:[{name:'v',type,compatible:true}],keys:[]});
 for(const [type,bits] of [['tinyint',8],['smallint',16],['mediumint',24],['int',32],['bigint',64]]){
  const half=1n<<BigInt(bits-1),maxUnsigned=(1n<<BigInt(bits))-1n;
  for(const value of [String(-half),String(half-1n),'00000','-0'])assert.equal(check(`${type}(20)`,value),'',`${type} ${value}`);
  for(const value of [String(-half-1n),String(half)])assert.notEqual(check(type,value),'',`${type} ${value}`);
  for(const value of ['0','000001',String(maxUnsigned)])assert.equal(check(`${type} unsigned`,value),'',`${type} unsigned ${value}`);
  for(const value of ['-0','-1',String(maxUnsigned+1n)])assert.notEqual(check(`${type} unsigned`,value),'',`${type} unsigned ${value}`);
 }
 for(const value of ['999.99','-999.99','00000999.99','0','00000.00','-0.00'])assert.equal(check('decimal(5,2)',value),'',value);
 for(const value of ['1000','0.001','1.230','1e2'])assert.notEqual(check('decimal(5,2)',value),'',value);
 assert.equal(check('decimal(2,2)','000.99'),'');assert.notEqual(check('decimal(2,2)','1.00'),'');
 for(const value of ['-0','-0.00','-1.00'])assert.notEqual(check('decimal(5,2) unsigned',value),'');
 assert.equal(check('decimal(65,30)','9'.repeat(35)+'.'+'9'.repeat(30)),'');
 assert.notEqual(check('decimal(65,30)','9'.repeat(36)+'.'+'9'.repeat(30)),'');
 for(const [type,accepted,rejected] of [['bit(1)',['00','01','0001'],['','02','1']],['bit(9)',['0000','01ff'],['0200']],['bit(64)',['ffffffffffffffff','0000ffffffffffffffff'],['010000000000000000']]]){
  for(const value of accepted)assert.equal(check(type,value),'',`${type} ${value}`);
  for(const value of rejected)assert.notEqual(check(type,value),'',`${type} ${value}`);
 }
 assert.equal(check("enum('bit','binary','blob')",'bit'),'');
 assert.equal(check('blob',''),'');assert.equal(check('varbinary(2)','00ff'),'');
});
