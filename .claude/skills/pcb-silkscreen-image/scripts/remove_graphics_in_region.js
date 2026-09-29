/* Remove board-level (gr_poly/gr_line/gr_rect/gr_circle/gr_arc) graphics on a
 * given layer whose points fall within a bounding region -- used to undo a
 * logo placement (move it, or take it back) without touching anything else
 * on that layer (e.g. an unrelated warning triangle or a different logo).
 * Usage: node remove_graphics_in_region.js <board.kicad_pcb> --layer B.SilkS --x0 .. --y0 .. --x1 .. --y1 .. [--margin 1] --out <out>
 */
const fs = require('fs');
function parseArgs(argv) {
  const pos = []; const opt = { margin: 1 };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--layer') opt.layer = argv[++i];
    else if (a === '--x0') opt.x0 = +argv[++i];
    else if (a === '--y0') opt.y0 = +argv[++i];
    else if (a === '--x1') opt.x1 = +argv[++i];
    else if (a === '--y1') opt.y1 = +argv[++i];
    else if (a === '--margin') opt.margin = +argv[++i];
    else if (a === '--out') opt.out = argv[++i];
    else pos.push(a);
  }
  return { pos, opt };
}
const { pos, opt } = parseArgs(process.argv.slice(2));
const IN = pos[0];
const OUT = opt.out || IN;
let s = fs.readFileSync(IN, 'utf8');
function matchBlock(str, i) { let d = 0; for (; i < str.length; i++) { const c = str[i]; if (c === '(') d++; else if (c === ')') { d--; if (d === 0) return i + 1; } } return str.length; }

// only scan top-level (skip footprint interiors)
function* iterBlocks(str, token) { let i = 0; while ((i = str.indexOf(token, i)) !== -1) { const end = matchBlock(str, i); yield { start: i, end, text: str.slice(i, end) }; i = end; } }
let boardOnly = s;
for (const blk of iterBlocks(s, '(footprint ')) boardOnly = boardOnly.slice(0, blk.start) + ' '.repeat(blk.end - blk.start) + boardOnly.slice(blk.end);

const toRemove = [];
const gfxRe = /\((gr_line|gr_rect|gr_poly|gr_circle|gr_arc|gr_curve)\b/g; let gm;
while ((gm = gfxRe.exec(boardOnly))) {
  const gEnd = matchBlock(boardOnly, gm.index);
  const g = boardOnly.slice(gm.index, gEnd);
  const lm = g.match(/\(layer "([^"]+)"\)/); if (!lm || lm[1] !== opt.layer) continue;
  const coords = [...g.matchAll(/\((?:start|end|center|mid|xy)\s+(-?[\d.]+)\s+(-?[\d.]+)\)/g)].map(m => [+m[1], +m[2]]);
  if (!coords.length) continue;
  const inside = coords.every(([x, y]) => x >= opt.x0 - opt.margin && x <= opt.x1 + opt.margin && y >= opt.y0 - opt.margin && y <= opt.y1 + opt.margin);
  if (inside) toRemove.push({ start: gm.index, end: gEnd });
}
console.error(`found ${toRemove.length} graphics on ${opt.layer} within region (${opt.x0},${opt.y0})-(${opt.x1},${opt.y1})`);

// remove from the end backwards, including a leading whitespace/newline run like other scripts in this repo do
toRemove.sort((a, b) => b.start - a.start);
for (const { start, end } of toRemove) {
  let realStart = start;
  while (realStart > 0 && (s[realStart - 1] === '\t' || s[realStart - 1] === ' ')) realStart--;
  if (s[realStart - 1] === '\n') realStart--;
  s = s.slice(0, realStart) + s.slice(end);
}
fs.writeFileSync(OUT, s);
console.error('removed', toRemove.length, '-> wrote', OUT);
