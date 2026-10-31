// WASM transport for createCalendar. Each instance owns its request/result slot.
import { API, createCalendar, validateRequest } from './calendar.mjs';
const operations=Object.freeze({gregorian:0,hebrew:1,date:2,subject:3,local:4,add:5,anniversary:6});
// Match native %.9g, including exact halfway cases (ties to even).
// This converts transport numbers; calendar arithmetic remains in C.
function wireNumber(x) {
  if(Number.isInteger(x)) return x;
  const absolute=Math.abs(x), unit=Math.floor(Math.log10(absolute))-8;
  const view=new DataView(new ArrayBuffer(4));view.setFloat32(0,absolute);
  const bits=view.getUint32(0), exponent=(bits>>>23)&255;
  let numerator=BigInt((bits&0x7fffff)+(exponent?0x800000:0)), denominator=1n;
  const binaryExponent=exponent?exponent-150:-149;
  if(binaryExponent>=0) numerator<<=BigInt(binaryExponent);
  else denominator<<=BigInt(-binaryExponent);
  if(unit>=0) denominator*=10n**BigInt(unit);
  else numerator*=10n**BigInt(-unit);
  let digits=numerator/denominator;
  const twiceRemainder=2n*(numerator%denominator);
  if(twiceRemainder>denominator || (twiceRemainder===denominator && digits%2n)) digits++;
  return Number(`${x<0?'-':''}${digits}e${unit}`);
}
export async function wasmTransport(bytes) {
  const module=bytes instanceof WebAssembly.Module ? bytes : await WebAssembly.compile(bytes);
  if (WebAssembly.Module.imports(module).length) throw new TypeError('calendar WASM must be self-contained');
  const instance=await WebAssembly.instantiate(module,{}), w=instance.exports;
  if (w.janus_abi()!==1) throw new TypeError('JANUS.CALENDAR WASM ABI mismatch');
  const decoder=new TextDecoder('utf-8',{fatal:true});
  function text(pointer) {
    const memory=new Uint8Array(w.memory.buffer);
    let end=pointer;
    while (end<memory.length && memory[end]!==0) end++;
    if (!pointer || end===memory.length) throw new TypeError('invalid calendar WASM text');
    return decoder.decode(memory.subarray(pointer,end));
  }
  return request=>{
    validateRequest(request);
    const args=[...request.args]; while(args.length<5) args.push(0);
    if(w.janus_call(operations[request.op],...args))
      throw new RangeError('calendar date or recurrence outside contract');
    if(w.janus_kind()===1) return {api:API,value:w.janus_scalar()};
    const value={};
    for(let i=0;i<w.janus_count();i++) {
      const key=text(w.janus_key(i)), pointer=w.janus_text(i);
      const number=w.janus_number(i);
      // Same float32 round-trip decimal precision as the native JSON transport.
      value[key]=pointer ? text(pointer) : wireNumber(number);
    }
    return {api:API,value};
  };
}
export async function wasmCalendar(bytes) { return createCalendar(await wasmTransport(bytes)); }
