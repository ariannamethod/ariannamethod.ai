// Browser-safe client. The supplied transport calls the canonical AML C boundary.
export const API = 'janus.calendar/1';
// Record inputs carry exact split coordinates. Calendrical arithmetic stays in C.
const definitions=Object.freeze([
  ['gregorian',['int','int','int']],['hebrew',['int','int','int']],['date',['int']],
  ['subject',['int','int']],['local',['int','int','int','int']],['add',['int','int','int','int','int']],
  ['anniversary',['int','int','int']],['instant',['int','int','int']],
  ['compare',['instant','instant']],['duration',['instant','instant']],
  ['duration_new',['int','int','int','int']],['shift',['instant','duration']],
  ['span',['instant','instant']],['contains',['span','instant']],
  ['overlaps',['span','span']],['intersection',['span','span']],
]);
const counts=Object.freeze(Object.fromEntries(definitions.map(([name,types])=>[name,types.length])));
const recordFields=Object.freeze({
  instant:['unix_day','second','millisecond'],duration:['sign','days','seconds','milliseconds'],
  span:['start_unix_day','start_second','start_millisecond','end_unix_day','end_second','end_millisecond'],
});
function exactInteger(x) {
  if(!Number.isInteger(x) || Math.abs(x)>16777216) throw new TypeError('calendar arguments must be exact scalar integers');
  return x;
}
export function encodeRequest(request) {
  if(!request || !Object.hasOwn(counts,request.op) || !Array.isArray(request.args) || request.args.length!==counts[request.op])
    throw new TypeError('invalid JANUS.CALENDAR request');
  const operation=definitions.findIndex(([name])=>name===request.op),types=definitions[operation][1],args=[];
  for(let i=0;i<types.length;i++) {
    const type=types[i],value=request.args[i];
    if(type==='int') args.push(exactInteger(value));
    else {
      if(!value || typeof value!=='object' || Array.isArray(value) || value.schema!==`janus.${type}/1` || value.time_scale!=='posix')
        throw new TypeError('calendar record violates its schema or time scale');
      for(const field of recordFields[type]) args.push(exactInteger(value[field]));
    }
  }
  return {operation,args};
}
export function validateRequest(request) { encodeRequest(request);return request; }
export function createCalendar(transport) {
  async function call(op, args) {
    const response = await transport(validateRequest({ op, args }));
    if (response?.api !== API || !Object.hasOwn(response, 'value'))
      throw new TypeError('JANUS.CALENDAR transport version mismatch');
    return response.value;
  }
  return Object.freeze(Object.fromEntries(Object.keys(counts).map(op => [op, (...args) => call(op, args)])));
}
export function httpTransport(endpoint, fetcher = globalThis.fetch) {
  return async request => {
    validateRequest(request);
    const response = await fetcher(endpoint, {
      method:'POST', headers:{ 'Content-Type':'application/json' }, body:JSON.stringify(request),
    });
    if (!response.ok) throw new Error(`JANUS.CALENDAR HTTP ${response.status}`);
    return response.json();
  };
}
// Exact transport conversion for LALO's integer Unix milliseconds; no calendar arithmetic.
export function splitUnixMilliseconds(ms) {
  if (!Number.isSafeInteger(ms)) throw new TypeError('timestamp must be safe integer Unix milliseconds');
  const unixDay = Math.floor(ms / 86400000);
  const remainder = ms - unixDay * 86400000;
  return { unixDay, second:Math.floor(remainder / 1000), millisecond:remainder % 1000 };
}
export function joinUnixMilliseconds({ unixDay, second, millisecond }) {
  if (!Number.isInteger(unixDay) || !Number.isInteger(second) || second < 0 || second >= 86400 ||
      !Number.isInteger(millisecond) || millisecond < 0 || millisecond >= 1000)
    throw new TypeError('invalid split timestamp');
  const ms = unixDay * 86400000 + second * 1000 + millisecond;
  if (!Number.isSafeInteger(ms)) throw new RangeError('timestamp outside safe integer range');
  return ms;
}
