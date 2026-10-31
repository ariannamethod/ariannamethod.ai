// Browser-safe client. The supplied transport calls the canonical AML C boundary.
export const API = 'janus.calendar/1';
const counts = Object.freeze({ gregorian:3, hebrew:3, date:1, subject:2, local:4, add:5, anniversary:3 });
export function validateRequest(request) {
  if (!request || !Object.hasOwn(counts, request.op) || !Array.isArray(request.args) ||
      request.args.length !== counts[request.op] ||
      !request.args.every(x => Number.isInteger(x) && Math.abs(x) <= 16777216))
    throw new TypeError('invalid JANUS.CALENDAR request');
  return request;
}
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
