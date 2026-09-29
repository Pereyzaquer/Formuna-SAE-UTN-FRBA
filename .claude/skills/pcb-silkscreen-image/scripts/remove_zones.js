/* Delete specific (zone ...) blocks from a kicad_pcb by UUID. */
const fs = require('fs');
const IN = process.argv[2];
const OUT_IDX = process.argv.indexOf('--out');
const OUT = OUT_IDX > -1 ? process.argv[OUT_IDX + 1] : IN;
const uuids = process.argv.slice(process.argv.indexOf('--uuids') + 1).filter(a => a !== '--out' && a !== OUT);

let s = fs.readFileSync(IN, 'utf8');
function matchBlock(str, i) { let d = 0; for (; i < str.length; i++) { const c = str[i]; if (c === '(') d++; else if (c === ')') { d--; if (d === 0) return i + 1; } } return str.length; }

let removed = 0;
for (const uuid of uuids) {
  const idx = s.indexOf('"' + uuid + '"');
  if (idx < 0) { console.error('NOT FOUND', uuid); continue; }
  const zStart = s.lastIndexOf('(zone', idx);
  const zEnd = matchBlock(s, zStart);
  // sanity: uuid must be inside this block, and block must start exactly at "(zone" (a token boundary)
  if (idx < zStart || idx > zEnd) { console.error('MISMATCH', uuid); continue; }
  const before = s.slice(Math.max(0, zStart - 1), zStart);
  if (!/[\s)]/.test(before) && zStart !== 0) { console.error('BOUNDARY SUSPECT', uuid); continue; }
  // remove the block plus one trailing newline/tab run if present, and leading whitespace/tab run
  let realStart = zStart;
  while (realStart > 0 && (s[realStart - 1] === '\t' || s[realStart - 1] === ' ')) realStart--;
  if (s[realStart - 1] === '\n') realStart--;
  s = s.slice(0, realStart) + s.slice(zEnd);
  removed++;
  console.error('removed', uuid);
}
fs.writeFileSync(OUT, s);
console.error(`removed ${removed}/${uuids.length} zones -> ${OUT}`);
