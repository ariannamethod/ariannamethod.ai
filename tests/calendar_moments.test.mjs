import test from 'node:test';
import assert from 'node:assert/strict';
import {createServer} from 'node:http';
import {bundledCalendar} from '../janus/calendar_browser.mjs';
import {nativeCalendar,calendarHandler} from '../janus/calendar_native.mjs';
import {createCalendar,httpTransport,splitUnixMilliseconds,joinUnixMilliseconds} from '../janus/calendar.mjs';
import {cases,instant,duration,span,total,low,high} from './calendar_moments_reference.mjs';
const wasm=await bundledCalendar(),native=nativeCalendar();
const vectors=cases(500);
test('WASM exact moments match the independent BigInt oracle over the complete domain',async()=>{
 for(const [op,args,expected]of vectors)assert.deepEqual(await wasm[op](...args),expected,`${op} ${JSON.stringify(args)}`);
 console.log(`WASM BigInt witnesses: ${vectors.length}`);
});
test('native transport agrees with the same exact BigInt witnesses',async()=>{
 let count=0;
 for(let i=0;i<vectors.length;i+=7){const [op,args,expected]=vectors[i];assert.deepEqual(await native[op](...args),expected,`${op} ${JSON.stringify(args)}`);count++;}
 console.log(`Native BigInt witnesses: ${count}`);
});
test('all nine new operations agree through the optional HTTP transport',async()=>{
 const server=createServer(calendarHandler());await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 try{
  const web=createCalendar(httpTransport(`http://127.0.0.1:${server.address().port}/calendar`));
  for(const [op,args,expected]of vectors.slice(0,36))assert.deepEqual(await web[op](...args),expected,op);
 }finally{await new Promise(resolve=>server.close(resolve));}
});
test('same-day milliseconds, negative epochs, exact large durations and addition round-trip',async()=>{
 const a=await wasm.instant(-1,86399,999),b=await wasm.instant(0,0,0);
 assert.equal(await wasm.compare(a,b),-1);assert.equal(await wasm.compare(b,a),1);
 assert.deepEqual(await wasm.duration(a,b),duration(1n));assert.deepEqual(await wasm.duration(b,a),duration(-1n));
 const full=await wasm.duration(instant(low),instant(high));assert.deepEqual(full,duration(high-low));
 assert.deepEqual(await wasm.shift(instant(low),full),instant(high));
 assert.equal((await wasm.duration(instant(1000n),instant(1001n))).milliseconds,1);
 const split=splitUnixMilliseconds(-1);assert.equal(joinUnixMilliseconds(split),-1);
 assert.deepEqual(await wasm.instant(split.unixDay,split.second,split.millisecond),a);
});
test('half-open intervals retain empty, touching, nested, disjoint and symmetric intersections',async()=>{
 for(const [a,b,c,d]of [[0n,1n,1n,2n],[0n,3n,1n,1n],[0n,0n,0n,0n],[0n,1n,3n,4n],[-5n,5n,-2n,2n]]){
  const first=await wasm.span(instant(a),instant(b)),second=await wasm.span(instant(c),instant(d));
  assert.equal(await wasm.contains(first,instant(a)),a<b?1:0);
  assert.equal(await wasm.contains(first,instant(b)),0);
  assert.equal(await wasm.overlaps(first,second),await wasm.overlaps(second,first));
  assert.deepEqual(await wasm.intersection(first,second),await wasm.intersection(second,first));
 }
});
test('malformed records, units, ranges and overflows fail without changing caller values',async()=>{
 const p=instant(0n),q=instant(1n),window=span(p,q),saved=structuredClone(window);
 const calls=[['instant',[0,0,.1]],['instant',[0,86400,0]],['instant',[0,0,1000]],
  ['compare',[0,p]],['duration',[p,{...q,time_scale:'local'}]],['compare',[p,{...q,millisecond:NaN}]],
  ['shift',[p,{...duration(1n),schema:'janus.instant/1'}]],['shift',[instant(high),duration(1n)]],
  ['shift',[instant(low),duration(-1n)]],['span',[q,p]],['contains',[{...window,end_second:-1},p]],
  ['overlaps',[{...window,end_millisecond:0,start_millisecond:1},window]],
  ['intersection',[window,{...window,schema:'old'}]],['duration_new',[1,0,0,0]],['duration_new',[0,0,0,1]],
  ['duration_new',[-1,0,86400,0]],['duration_new',[1,3652059,0,0]]];
 for(const client of [wasm,native])for(const [op,args]of calls)await assert.rejects(client[op](...args),`${op} must fail`);
 assert.deepEqual(window,saved);
 assert.deepEqual(await wasm.span(p,q),window);
});
test('result snapshots survive later operations and concurrent instances',async()=>{
 const other=await bundledCalendar(),first=await wasm.span(instant(-1n),instant(1n)),saved=structuredClone(first);
 const results=await Promise.all(Array.from({length:64},(_,i)=>wasm.instant(0,0,i)));
 assert.deepEqual(results.map(v=>v.millisecond),Array.from({length:64},(_,i)=>i));
 await other.shift(instant(0n),duration(-1n));assert.deepEqual(first,saved);
});
