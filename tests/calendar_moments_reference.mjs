// Independent exact oracle: BigInt POSIX milliseconds, not AML arithmetic.
import {writeFileSync} from 'node:fs';
const DAY=86400000n;
const minDay=Number(BigInt(Date.parse('0001-01-01T00:00:00Z'))/DAY);
const maxDay=Number(BigInt(Date.parse('9999-12-31T00:00:00Z'))/DAY);
export const low=BigInt(minDay)*DAY, high=BigInt(maxDay)*DAY+DAY-1n;
export function instant(t){
 let day=t/DAY,rest=t%DAY;if(rest<0){day--;rest+=DAY;}
 return {schema:'janus.instant/1',time_scale:'posix',unix_day:Number(day),second:Number(rest/1000n),millisecond:Number(rest%1000n)};
}
export function total(i){return BigInt(i.unix_day)*DAY+BigInt(i.second)*1000n+BigInt(i.millisecond);}
export function duration(t){const sign=t<0n?-1:t>0n?1:0;t=t<0n?-t:t;return {schema:'janus.duration/1',time_scale:'posix',sign,days:Number(t/DAY),seconds:Number((t%DAY)/1000n),milliseconds:Number(t%1000n)};}
export function span(a,b){return {schema:'janus.span/1',time_scale:'posix',start_unix_day:a.unix_day,start_second:a.second,start_millisecond:a.millisecond,end_unix_day:b.unix_day,end_second:b.second,end_millisecond:b.millisecond};}
export function bounds(s){return [total({unix_day:s.start_unix_day,second:s.start_second,millisecond:s.start_millisecond}),total({unix_day:s.end_unix_day,second:s.end_second,millisecond:s.end_millisecond})];}
let seed=19999;
function random(n){seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed%n;}
export function cases(count=240){
 const values=[low,low+1n,-86400001n,-86400000n,-1n,0n,1n,86399999n,86400000n,high-1n,high];
 for(let i=0;i<count;i++)values.push(BigInt(minDay+random(maxDay-minDay+1))*DAY+BigInt(random(86400000)));
 const result=[];
 for(let n=0;n<values.length;n++){
  const a=instant(values[n]),b=instant(values[(n*17+3)%values.length]),t=total(a),u=total(b);
  const start=t<u?t:u,end=t<u?u:t;
  const first=span(instant(start),instant(end)),otherStart=start+(end-start)/3n,otherEnd=end-(end-start)/4n;
  const other=span(instant(otherStart),instant(otherEnd));
  const sign=t<u?-1:t>u?1:0,delta=duration(u-t);
  result.push(['instant',[a.unix_day,a.second,a.millisecond],a],['compare',[a,b],sign],['duration',[a,b],delta],
   ['duration_new',[delta.sign,delta.days,delta.seconds,delta.milliseconds],delta],['shift',[a,delta],b],
   ['span',[instant(start),instant(end)],first],['contains',[first,a],t>=start&&t<end?1:0],
   ['overlaps',[first,other],start<end&&otherStart<otherEnd?1:0],
   ['intersection',[first,other],span(instant(otherStart),instant(otherEnd))]);
 }
 // Explicit empty, touching and disjoint sets exercise half-open endpoint semantics.
 for(const [a,b,c,d] of [[0n,1n,1n,2n],[0n,3n,1n,1n],[0n,0n,0n,0n],[0n,1n,3n,4n],[-1n,1n,0n,2n]]){
  const x=span(instant(a),instant(b)),y=span(instant(c),instant(d));
  const from=a>c?a:c,to=b<d?b:d,overlap=from<to;
  result.push(['overlaps',[x,y],overlap?1:0],['intersection',[x,y],span(instant(from),instant(overlap?to:from))]);
 }
 return result;
}
if(process.argv[1]===new URL(import.meta.url).pathname){
 const data=cases(40),source=[`IMPORT "${process.argv[2]}"`];
 function expression(v){
  if(typeof v==='number')return String(v);
  if(v.schema==='janus.instant/1')return `calendar_instant(${v.unix_day},${v.second},${v.millisecond})`;
  if(v.schema==='janus.duration/1')return `calendar_duration_new(${v.sign},${v.days},${v.seconds},${v.milliseconds})`;
  const [a,b]=bounds(v);return `calendar_span(${expression(instant(a))},${expression(instant(b))})`;
 }
 for(const [op,args,expected] of data){
  const names=[];
  for(const [i,arg] of args.entries()){const name=`arg${i}`;source.push(`${name}=${expression(arg)}`);names.push(name);}
  const got='value';
  source.push(`${got}=calendar_${op}(${names.join(',')})`);
  if(typeof expected==='number')source.push(`assert(${got}==${expected}, "BigInt scalar")`);
  else for(const [key,value]of Object.entries(expected))source.push(typeof value==='number'?`assert(record_get(${got},"${key}")==${value},"BigInt field")`:`assert(text_equal(record_get(${got},"${key}"),"${value}"),"record identity")`);
 }
 source.push('event=janus_event(20730,43201,123,20730,43210,456,498)',
  'lag=calendar_duration(janus_event_instant(event),janus_known_instant(event))',
  'assert(record_get(lag,"seconds")==9 and record_get(lag,"milliseconds")==333,"two clocks")',
  'captured=calendar_span(calendar_instant(-1,86399,999),calendar_instant(0,0,0))',
  'SPAWN frozen:\n    return captured',
  'record_set(captured,"end_millisecond",17)',
  'AWAIT frozen INTO restored',
  'assert(record_get(restored,"end_millisecond")==0,"worker owns span")',
  'assert(checkpoint_save(restored,"moments.state")>=1,"checkpoint")',
  'assert(record_equal(restored,checkpoint_load("moments.state")),"exact replay")',
  'PRINT "MOMENTS_OK"');
 writeFileSync(process.argv[3],source.join('\n')+'\n');
 console.log(`BigInt AML fixture: ${data.length} independent operations, ${source.length} statements`);
}
