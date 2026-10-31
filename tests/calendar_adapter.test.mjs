import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createCalendar, httpTransport, splitUnixMilliseconds, joinUnixMilliseconds } from '../janus/calendar.mjs';
import { nativeCalendar, calendarHandler } from '../janus/calendar_native.mjs';
const cal=nativeCalendar();
const epoch=Date.parse('2024-10-03T00:00:00Z');
const ord=text=>(Date.parse(`${text}T00:00:00Z`)-epoch)/86400000;
const fmt=new Intl.DateTimeFormat('en-u-ca-hebrew',{year:'numeric',month:'long',day:'numeric',timeZone:'UTC'});
const months={Nisan:1,Iyar:2,Sivan:3,Tamuz:4,Av:5,Elul:6,Tishri:7,Heshvan:8,Kislev:9,Tevet:10,Shevat:11,Adar:12,'Adar I':12,'Adar II':13};
const yearMaps=new Map();
function yearMap(y) {
  if(!yearMaps.has(y)) {
    const result=new Map(),date=new Date(0); date.setUTCFullYear(y-3761,0,1);
    const start=(date.getTime()-epoch)/86400000-366;
    for(let day=start;day<start+1462;day++) {
      const p=Object.fromEntries(fmt.formatToParts(new Date(epoch+day*86400000)).map(p=>[p.type,p.value]));
      if(+p.year===y) result.set(`${months[p.month]}:${+p.day}`,day);
    }
    yearMaps.set(y,result);
  }
  return yearMaps.get(y);
}
const hd=(y,m,d)=>yearMap(y).get(`${m}:${d}`);
function anniversary(y,m,d,target) {
  let result;
  if(m===8 && d===30 && hd(y+1,8,30)===undefined) result=hd(target,9,1)-1;
  else if(m===9 && d===30 && hd(y+1,9,30)===undefined) result=hd(target,10,1)-1;
  else if(m===13) result=hd(target,hd(target,13,1)===undefined?12:13,d);
  else if(m===12 && d===30 && hd(target,13,1)===undefined) result=hd(target,11,30);
  else result=hd(target,m,d)??hd(target,m+1,1);
  return result;
}
test('native adapter preserves C values and both origins', async()=>{
  assert.equal(await cal.gregorian(2026,1,23),ord('2026-01-23'));
  assert.equal(await cal.gregorian(2026,2,13),ord('2026-02-13'));
  const [a,b]=await Promise.all([cal.subject(477,731),cal.subject(498,731)]);
  assert.equal(a.age_days,254); assert.equal(b.age_days,233); assert.equal(b.birthquake,1);
  const binary=fileURLToPath(new URL('../janus/calendar-bridge',import.meta.url));
  const direct=JSON.parse(execFileSync(binary,['subject','498','731'],{encoding:'utf8'}));
  assert.deepEqual(b,direct.value);
  assert.deepEqual(await cal.subject(498,731),b);
  assert.equal((await cal.date(ord('1800-05-02'))).gregorian_year,1800);
});
test('ICU witnesses all Hebrew anniversary rules, including missing day 30',async()=>{
  let facets=new Set();
  for(let y=5780;y<=5787;y++) {
    for(const [m,d] of [[8,30],[9,30],[12,30],[13,20],[11,26]]) {
      const birth=hd(y,m,d); if(birth===undefined) continue;
      for(const target of [y+1,y+2,y+3]) {
        const expected=anniversary(y,m,d,target);
        assert.ok(Number.isInteger(expected));
        assert.equal(await cal.anniversary(birth,target,1),expected,`${y}/${m}/${d} -> ${target}`);
        if(m===8&&hd(y+1,m,d)===undefined) facets.add('cheshvan');
        if(m===9&&hd(y+1,m,d)===undefined) facets.add('kislev');
        if(m===12&&hd(target,13,1)===undefined) facets.add('adar-I-30');
        if(m===13) facets.add('adar-II');
      }
    }
  }
  assert.deepEqual([...facets].sort(),['adar-I-30','adar-II','cheshvan','kislev']);
});
test('month/year shifts cross leap months and retain documented clamping',async()=>{
  const before=hd(5784,13,29);
  assert.equal(await cal.add(before,0,1,0,1),hd(5784,1,29));
  assert.equal(await cal.add(before,0,7,0,1),hd(5785,7,29));
  assert.equal(await cal.add(before,1,0,0,1),hd(5785,12,29));
  assert.equal(await cal.add(hd(5785,7,29),0,-7,0,1),before);
  assert.equal(await cal.add(ord('2024-01-31'),0,1,0,0),ord('2024-02-29'));
  assert.equal(await cal.anniversary(ord('2024-02-29'),2025,0),ord('2025-03-01'));
});
test('UTC midnight, Janus noon, offset and supplied sunset have separate boundaries',async()=>{
  assert.equal((await cal.local(19999,43199,0,-1)).epoch_day,-1);
  assert.equal((await cal.local(19999,43200,0,-1)).epoch_day,0);
  assert.equal((await cal.local(19998,86399,0,-1)).utc_civil_day,-1);
  assert.equal((await cal.local(19999,0,-60,-1)).day,-1);
  assert.equal((await cal.local(19999,57599,120,64800)).hebrew_day,1);
  assert.equal((await cal.local(19999,57600,120,64800)).hebrew_day,2);
  const newYear=await cal.local(19998,64800,0,64800);
  assert.equal(newYear.hebrew_year,5785); assert.equal(newYear.hebrew_month,7); assert.equal(newYear.hebrew_day,1);
  assert.equal(newYear.hebrew_leap,0); assert.equal(newYear.metonic_year,9);
});
test('LALO timestamps round-trip including negative milliseconds',()=>{
  for(const ms of [-Number.MAX_SAFE_INTEGER,-86400001,-1,0,epoch,epoch+43200123,Number.MAX_SAFE_INTEGER])
    assert.equal(joinUnixMilliseconds(splitUnixMilliseconds(ms)),ms);
  assert.deepEqual(splitUnixMilliseconds(-1),{unixDay:-1,second:86399,millisecond:999});
  assert.throws(()=>splitUnixMilliseconds(.5)); assert.throws(()=>joinUnixMilliseconds({unixDay:0,second:86400,millisecond:0}));
});
test('HTTP browser transport returns the same native result and refuses malformed input',async()=>{
  const server=createServer(calendarHandler());
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  try {
    const endpoint=`http://127.0.0.1:${server.address().port}/calendar`;
    const web=createCalendar(httpTransport(endpoint));
    assert.deepEqual(await web.subject(498,731),await cal.subject(498,731));
    const invalid=await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({op:'date',args:[.1]})});
    assert.equal(invalid.status,400);
    const foreign=await fetch(endpoint,{method:'POST',headers:{Origin:'https://elsewhere.invalid'},body:'{}'});
    assert.equal(foreign.status,403);
    const tooLarge=await fetch(endpoint,{method:'POST',body:' '.repeat(1025)}); assert.equal(tooLarge.status,413);
  } finally { await new Promise(resolve=>server.close(resolve)); }
});
test('bad operations, arity, range and calendar values fail visibly',async()=>{
  await assert.rejects(cal.date(.5)); await assert.rejects(cal.date(0,1)); await assert.rejects(cal.date('0'));
  await assert.rejects(cal.gregorian(2023,2,29)); await assert.rejects(cal.hebrew(5786,13,1));
  await assert.rejects(createCalendar(async()=>({api:'other',value:0})).date(0));
});
