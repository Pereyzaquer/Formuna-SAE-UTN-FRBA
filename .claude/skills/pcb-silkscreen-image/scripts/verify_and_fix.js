/* Post-import integrity check for a KiCad board mutated by the kicad MCP's
 * import_svg_logo tool (or any other tool call that goes through its internal
 * KiCad-engine save path).
 *
 * That save path has TWO known side effects on at least one real, large board
 * (observed directly, twice, in a working session -- not theoretical):
 *   1. It silently rewrites line endings from LF to CRLF. Harmless to KiCad
 *      itself, but it turns every future `git diff` on the file into a
 *      multi-hundred-thousand-line noise wall that hides the real change.
 *   2. It can silently fabricate extra zone/keepout objects that were never
 *      requested and have nothing to do with the logo just imported (seen as
 *      small rounded "NoCu<ref>"-style keepouts with brand new random UUIDs,
 *      unrelated in location to the import). Left in place, these become the
 *      next "mystery zone that won't let me route or pour copper" support
 *      request -- exactly the failure mode this script exists to catch before
 *      it ships.
 *
 * This script:
 *   - normalizes line endings back to LF (byte-for-byte preserving everything
 *     else)
 *   - diffs the zone list against a pre-op snapshot and reports any zone UUID
 *     that is new AND not attributable to the logo import (i.e. not one of
 *     the gr_poly/fp_poly graphics you just added) -- these should be reviewed
 *     and almost always removed
 *
 * Usage:
 *   node verify_and_fix.js <board.kicad_pcb> --before <pre_op_snapshot.kicad_pcb>
 *
 * Exit code 0 = clean (only line-ending fix applied, if needed).
 * Exit code 1 = unexplained new zones found; their UUIDs are printed so you
 * can inspect and remove them (see remove_zones.js).
 */
const fs = require('fs');

function parseArgs(argv) {
  const pos = []; const opt = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--before') opt.before = argv[++i];
    else pos.push(argv[i]);
  }
  return { pos, opt };
}
const { pos, opt } = parseArgs(process.argv.slice(2));
const PCB = pos[0];
if (!PCB || !opt.before) {
  console.error('Usage: node verify_and_fix.js <board.kicad_pcb> --before <pre_op_snapshot.kicad_pcb>');
  process.exit(2);
}

// ---- 1. normalize line endings back to LF ----
let cur = fs.readFileSync(PCB, 'utf8');
const hadCRLF = /\r/.test(cur);
if (hadCRLF) {
  const before = cur.length;
  cur = cur.replace(/\r\n/g, '\n').replace(/\r/g, '');
  fs.writeFileSync(PCB, cur);
  console.error(`fixed line endings: stripped ${before - cur.length} CR bytes`);
} else {
  console.error('line endings already LF, nothing to fix');
}

// ---- 2. diff zone UUIDs against the pre-op snapshot ----
function matchBlock(str, i) { let d = 0; for (; i < str.length; i++) { const c = str[i]; if (c === '(') d++; else if (c === ')') { d--; if (d === 0) return i + 1; } } return str.length; }
function zoneUuids(text) {
  const out = new Map();
  let i = 0;
  while ((i = text.indexOf('(zone', i)) !== -1) {
    const end = matchBlock(text, i);
    const blk = text.slice(i, end);
    const u = (blk.match(/\(uuid "([^"]+)"\)/) || [])[1];
    const name = (blk.match(/\(name "([^"]*)"\)/) || [, '(unnamed)'])[1];
    const bbox = [...blk.matchAll(/\(xy (-?[\d.]+) (-?[\d.]+)\)/g)].reduce((b, m) => {
      const x = +m[1], y = +m[2];
      return { xmin: Math.min(b.xmin, x), xmax: Math.max(b.xmax, x), ymin: Math.min(b.ymin, y), ymax: Math.max(b.ymax, y) };
    }, { xmin: Infinity, xmax: -Infinity, ymin: Infinity, ymax: -Infinity });
    if (u) out.set(u, { name, bbox });
    i = end;
  }
  return out;
}

const before = fs.readFileSync(opt.before, 'utf8').replace(/\r\n/g, '\n').replace(/\r/g, '');
const beforeZones = zoneUuids(before);
const afterZones = zoneUuids(cur);

const newOnes = [];
for (const [uuid, info] of afterZones) if (!beforeZones.has(uuid)) newOnes.push({ uuid, ...info });

if (newOnes.length === 0) {
  console.error('zone check OK: no unexplained new zones');
  process.exit(0);
}

console.error(`\n*** ${newOnes.length} zone(s) appeared that were NOT in the pre-op snapshot ***`);
console.error('If you just split/notched a zone yourself (e.g. via a separate script), your own');
console.error('new UUIDs will show up here too -- that\'s expected. What is NOT expected is a small');
console.error('rounded keepout named like "NoCu<something>" you never asked for. Review each one:\n');
for (const z of newOnes) {
  const w = (z.bbox.xmax - z.bbox.xmin).toFixed(1), h = (z.bbox.ymax - z.bbox.ymin).toFixed(1);
  console.error(`  ${z.uuid}  name="${z.name}"  bbox ~${w}x${h}mm at (${z.bbox.xmin.toFixed(1)},${z.bbox.ymin.toFixed(1)})`);
}
console.error('\nTo remove one, use: node remove_zones.js <board.kicad_pcb> --uuids <uuid...>');
process.exit(1);
