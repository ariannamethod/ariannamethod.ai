// Independent oracle: host ICU Hebrew calendar + ECMAScript UTC Gregorian dates.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';
const epoch = Date.parse('2024-10-03T00:00:00Z');
const dayOf = text => (Date.parse(`${text}T00:00:00Z`) - epoch) / 86400000;
const formatter = new Intl.DateTimeFormat('en-u-ca-hebrew', {
  timeZone:'UTC', year:'numeric', month:'long', day:'numeric',
});
const month = { Nisan:1, Iyar:2, Sivan:3, Tamuz:4, Av:5, Elul:6,
  Tishri:7, Heshvan:8, Kislev:9, Tevet:10, Shevat:11, Adar:12, 'Adar I':12, 'Adar II':13 };
function heb(day) {
  const parts = Object.fromEntries(formatter.formatToParts(new Date(epoch + day*86400000)).map(p=>[p.type,p.value]));
  assert.ok(Object.hasOwn(month,parts.month),parts.month);
  return [+parts.year,month[parts.month],+parts.day];
}
const native = process.argv[2];
const lines=execFileSync(native,['--dates'], { encoding:'utf8', maxBuffer:4_000_000 }).trim().split('\n');
const samples=[];
for (const [i,line] of lines.entries()) {
  const [day,gy,gm,gd,hy,hm,hd]=line.split(',').map(Number);
  const greg = new Date(epoch+day*86400000);
  assert.deepEqual([gy,gm,gd], [greg.getUTCFullYear(),greg.getUTCMonth()+1,greg.getUTCDate()],`Gregorian day ${day}`);
  assert.deepEqual([hy,hm,hd],heb(day),`Hebrew day ${day}`);
  if (i%101===0 || [0,477,498,730,731].includes(day)) samples.push([day,gy,gm,gd,hy,hm,hd]);
}
assert.equal(dayOf('2026-01-23'),477);
assert.equal(dayOf('2026-02-13'),498);
assert.equal(dayOf('2026-10-03'),730);
assert.equal(dayOf('2026-10-04'),731);
const importPath=process.argv[3];
const source=[`IMPORT "${importPath}"`];
for (const [day,gy,gm,gd,hy,hm,hd] of samples) {
  source.push(`assert(janus_gregorian(${gy}, ${gm}, ${gd}) == ${day}, "UTC Gregorian oracle")`);
  source.push(`assert(janus_hebrew(${hy}, ${hm}, ${hd}) == ${day}, "ICU Hebrew oracle")`);
  source.push(`d = calendar_date(${day})`);
  source.push(`assert(record_get(d, "hebrew_year") == ${hy} and record_get(d, "hebrew_month") == ${hm} and record_get(d, "hebrew_day") == ${hd}, "ICU coordinates")`);
}
// Independently find the 2027 Hebrew anniversary by searching ICU dates.
const [,birthMonth,birthDay]=heb(498);
let next=731;
while (heb(next)[1]!==birthMonth || heb(next)[2]!==birthDay) next++;
source.push(`view = janus_subject(498, 731)`);
source.push(`assert(record_get(view, "next_hebrew_day") == ${next}, "ICU anniversary search")`);
const gregNext=dayOf('2027-02-13');
source.push(`assert(record_get(view, "next_gregorian_day") == ${gregNext}, "Gregorian next birthday")`);
source.push(`assert(record_get(view, "anniversary_gap_days") == ${next-gregNext}, "independent calendar gap")`);
source.push('assert(record_get(view, "birthquake") == 1, "model-time quake")');
source.push(`assert(janus_add(janus_gregorian(2024, 1, 31), 0, 1, 0, 0) == ${dayOf('2024-02-29')}, "month end")`);
source.push(`assert(janus_anniversary(janus_gregorian(2024, 2, 29), 2025, 0) == ${dayOf('2025-03-01')}, "leap anniversary")`);
source.push('assert(record_get(janus_local(19999, 43199, 0, -1), "epoch_day") == -1, "signed noon floor")');
source.push('assert(record_get(janus_local(19999, 43200, 0, -1), "epoch_day") == 0, "noon epoch")');
source.push('assert(record_get(janus_local(19999, 57600, 120, 64800), "hebrew_day") == 2, "supplied sunset")');
source.push('assert(record_get(janus_local(19998, 64800, 0, 64800), "hebrew_year") == 5785, "sunset new year")');

source.push('assert(janus_distance(-100, 100) == 200, "signed interval")');
source.push('assert(janus_compare(-1, 0) == -1 and janus_compare(0, -1) == 1 and janus_compare(0, 0) == 0, "ordering")');
source.push('assert(record_get(janus_interval(-100, 100), "duration_days") == 200, "interval")');
source.push('event = janus_event(20730, 43200, 123, 20730, 43205, 456, 498)');
source.push('assert(record_get(event, "known_second") == 43205, "two clocks")');
source.push('assert(text_equal(record_get(event, "schema"), "janus.event/1"), "event identity")');
source.push('SPAWN captured:\n    return view');
source.push('record_set(view, "birth_day", 0)');
source.push('AWAIT captured INTO received');
source.push('assert(record_get(received, "birth_day") == 498, "worker snapshot")');
source.push('assert(checkpoint_save(received, "calendar-replay.state") >= 1, "checkpoint")');
source.push('assert(record_equal(received, checkpoint_load("calendar-replay.state")), "deterministic replay")');
source.push('PRINT "JANUS_CALENDAR_OK"');
writeFileSync(process.argv[4],source.join('\n')+'\n');
console.log(`PASS independent Gregorian/ICU oracle: ${lines.length} dates, ${samples.length} AML samples; Hebrew anniversary day ${next}`);
