import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {bundledCalendar,bundledBytes} from '../janus/calendar_browser.mjs';
import {wasmTransport} from '../janus/calendar_wasm.mjs';
import {nativeCalendar} from '../janus/calendar_native.mjs';
const cal=await bundledCalendar(), native=nativeCalendar();
const bytes=bundledBytes(),module=new WebAssembly.Module(bytes);
const nativeFixture=process.env.JANUS_CALENDAR_REFERENCE;
const root=new URL('../',import.meta.url);
test('bundle matches current canonical sources and carries a closed WASM ABI',()=>{
  const bundle=readFileSync(new URL('janus/calendar_browser.mjs',root),'utf8');
  const paths=['core/ariannamethod.c','core/ariannamethod.h','janus/calendar_wasm.c','janus/calendar.mjs','janus/calendar_wasm.mjs','tools/build_calendar_wasm.mjs'];
  const hash=createHash('sha256');
  for(const path of paths)hash.update(path+'\0').update(readFileSync(new URL(path,root))).update('\0');
  assert.ok(bundle.includes(`Source SHA-256: ${hash.digest('hex')}`),'stale generated bundle');
  assert.ok(bundle.includes(`WASM SHA-256: ${createHash('sha256').update(bytes).digest('hex')}`));
  assert.deepEqual(WebAssembly.Module.imports(module),[]);
  assert.deepEqual(WebAssembly.Module.exports(module).map(e=>e.name).sort(),
    ['memory','janus_abi','janus_call','janus_kind','janus_scalar','janus_count','janus_key','janus_text','janus_number'].sort());
});
test('all native independent Gregorian/ICU witnesses also pass WASM',async()=>{
  assert.ok(nativeFixture,'run through test_aml_calendar_wasm.sh');
  const lines=execFileSync(nativeFixture,['--dates'],{encoding:'utf8',maxBuffer:4_000_000}).trim().split('\n');
  for(const line of lines) {
    const [day,gy,gm,gd,hy,hm,hd]=line.split(',').map(Number);
    const v=await cal.date(day);
    assert.deepEqual([v.gregorian_year,v.gregorian_month,v.gregorian_day,v.hebrew_year,v.hebrew_month,v.hebrew_day],
      [gy,gm,gd,hy,hm,hd],`day ${day}`);
    assert.equal(await cal.gregorian(gy,gm,gd),day);
    assert.equal(await cal.hebrew(hy,hm,hd),day);
  }
  console.log(`WASM independent date witnesses: ${lines.length}`);
});
test('seven operations agree with the native transport over historical and modern subjects',async()=>{
  const cases=[['date',[-739161]],['date',[2912897]],['subject',[498,730]],['subject',[498,731]],['subject',[477,731]],
    ['subject',[-50000,-10000]],['local',[19999,43199,0,-1]],['local',[19999,43200,0,-1]],
    ['local',[19998,64800,0,64800]],['local',[19999,0,-1440,-1]],['local',[19999,86399,1440,-1]],
    ['add',[0,0,-1,0,0]],['add',[0,1,7,1,1]],['anniversary',[498,5787,1]],['anniversary',[498,2027,0]],
    ['gregorian',[2024,2,29]],['hebrew',[5787,13,1]]];
  let seed=7291;
  function draw(n){seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed%n;}
  for(let i=0;i<160;i++) {
    const birth=-700000+draw(3500000),day=-700000+draw(3500000);
    cases.push(['subject',[birth,day]]);
    cases.push(['add',[day,draw(5)-2,draw(13)-6,draw(21)-10,draw(2)]]);
  }
  for(const [op,args] of cases)assert.deepEqual(await cal[op](...args),await native[op](...args),`${op} ${args}`);
  console.log(`WASM/native full-value comparisons: ${cases.length}`);
});
test('transport rejects invalid input and recovers without publishing an old result',async()=>{
  for(const [op,args] of [['date',[.5]],['date',['0']],['date',[]],['date',[16777216]],
    ['hebrew',[5786,13,1]],['local',[19999,86400,0,-1]],['add',[0,0,0,0,2]]])
    await assert.rejects(cal[op](...args));
  assert.equal((await cal.date(0)).hebrew_day,1);
  const w=(await WebAssembly.instantiate(module,{})).exports;
  assert.equal(w.janus_call(3,498,731,0,0,0),0);assert.equal(w.janus_count(),31);
  assert.equal(w.janus_call(99,0,0,0,0,0),1);assert.equal(w.janus_count(),0);assert.equal(w.janus_kind(),0);
  assert.equal(w.janus_key(-1),0);assert.equal(w.janus_text(999),0);assert.equal(w.janus_number(999),0);
});
test('result ownership, instance isolation and fixed memory survive repeated calls',async()=>{
  const transport=await wasmTransport(module),other=await bundledCalendar();
  const retained=await cal.subject(498,731),copy=structuredClone(retained);
  const requests=Array.from({length:128},(_,i)=>cal.subject(i,731));
  const results=await Promise.all(requests);
  assert.deepEqual(results.map(v=>v.birth_day),Array.from({length:128},(_,i)=>i));
  const w=(await WebAssembly.instantiate(module,{})).exports,size=w.memory.buffer.byteLength;
  for(let i=0;i<25000;i++)assert.equal(w.janus_call(3,498,731+(i%365),0,0,0),0);
  assert.equal(w.memory.buffer.byteLength,size);assert.equal(size,131072);
  await other.subject(-1000,-500);assert.deepEqual(retained,copy);
  assert.equal(transport({op:'date',args:[0]}).value.day,0);
});
test('standalone HTML contains the complete module and has no external scripts',()=>{
  const html=readFileSync(new URL('janus/calendar-offline.html',root),'utf8');
  assert.ok(html.includes(readFileSync(new URL('janus/calendar_browser.mjs',root),'utf8')));
  assert.equal((html.match(/<script\b/g)??[]).length,1);
  assert.doesNotMatch(html,/<script[^>]+src=/);
  assert.doesNotMatch(html,/^import .* from /m);
});

test('complete inline HTML module runs with network and clock functions disabled',async()=>{
  const {SourceTextModule,createContext}=await import('node:vm');
  const html=readFileSync(new URL('janus/calendar-offline.html',root),'utf8');
  const source=html.match(/<script type="module">([\s\S]*?)<\/script>/)[1];
  let networkCalls=0,changed;
  const blocked=()=>{networkCalls++;throw new Error('network disabled in offline fixture');};
  const listeners={},form={elements:{year:{value:'2026'},month:{value:'10'},day:{value:'4'}},
    addEventListener:(name,listener)=>{listeners[name]=listener;}};
  let rendered='';
  const output={get textContent(){return rendered;},set textContent(value){rendered=value;changed?.();}};
  const document={documentElement:{dataset:{}},querySelector(selector){
    if(selector==='#query')return form;if(selector==='#result')return output;
    throw new Error(`unknown fixture element ${selector}`);
  }};
  const context=createContext({document,WebAssembly,TextDecoder,atob,fetch:blocked,
    XMLHttpRequest:blocked,WebSocket:blocked,Date:class {constructor(){throw new Error('clock disabled');}static now(){throw new Error('clock disabled');}}});
  const script=new SourceTextModule(source,{context});
  await script.link(()=>{throw new Error('external module loading disabled');});
  await script.evaluate();
  assert.equal(document.documentElement.dataset.ready,'true');
  assert.deepEqual(JSON.parse(rendered),await native.subject(498,731));
  for(const [year,month,day,expected] of [['2024','10','3',0],['2023','2','29',null]]) {
    form.elements.year.value=year;form.elements.month.value=month;form.elements.day.value=day;
    const next=new Promise(resolve=>{changed=resolve;});
    listeners.submit({preventDefault(){}});await next;
    if(expected===null)assert.match(rendered,/outside contract/);
    else assert.equal(JSON.parse(rendered).day,expected);
  }
  assert.equal(networkCalls,0);
});
