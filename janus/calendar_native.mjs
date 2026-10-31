// Optional Node transport to libaml, with no model or database dependency.
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:http';
import { createCalendar, validateRequest } from './calendar.mjs';
const execute = promisify(execFile);
export function nativeTransport(binary = fileURLToPath(new URL('./calendar-bridge', import.meta.url))) {
  return async request => {
    validateRequest(request);
    const { stdout } = await execute(binary, [request.op, ...request.args.map(String)], {
      timeout:5000, maxBuffer:65536, encoding:'utf8',
    });
    return JSON.parse(stdout);
  };
}
export function nativeCalendar(binary) { return createCalendar(nativeTransport(binary)); }
// Mount this handler at the consumer's same-origin /calendar endpoint.
export function calendarHandler(transport = nativeTransport()) {
  return async (request, response) => {
    if (request.method !== 'POST' || request.url !== '/calendar') {
      response.writeHead(404).end(); return;
    }
    const origin = request.headers.origin;
    if (origin && origin !== `http://${request.headers.host}` && origin !== `https://${request.headers.host}`) {
      response.writeHead(403).end(); return;
    }
    try {
      const chunks=[]; let bytes=0;
      for await (const chunk of request) {
        bytes+=chunk.length;
        if (bytes>1024) { response.writeHead(413).end(); return; }
        chunks.push(chunk);
      }
      const value=await transport(validateRequest(JSON.parse(Buffer.concat(chunks).toString('utf8'))));
      response.writeHead(200, { 'Content-Type':'application/json', 'Cache-Control':'no-store' });
      response.end(JSON.stringify(value));
    } catch {
      response.writeHead(400, { 'Content-Type':'application/json' });
      response.end(JSON.stringify({ error:'JANUS.CALENDAR request failed' }));
    }
  };
}
if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const port=Number(process.argv[2] ?? 8765);
  createServer(calendarHandler()).listen(port, '127.0.0.1', function () {
    process.stdout.write(`JANUS.CALENDAR http://127.0.0.1:${this.address().port}/calendar\n`);
  });
}
