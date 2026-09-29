/* Find the largest free square area on a PCB's front or back for placing a
 * logo/image on silkscreen, avoiding: component courtyards on that side, AND
 * existing board-level silkscreen graphics already placed there (warning
 * triangles, other logos, text) -- not just courtyards, since silk-on-silk
 * overlap is exactly the kind of mistake that's easy to miss otherwise.
 *
 * Usage: node find_free_space.js <board.kicad_pcb> [--side front|back] [--margin N]
 * Prints the best square as JSON on stdout; diagnostics go to stderr.
 */
const fs = require('fs');
function parseArgs(argv) {
  const pos = []; const opt = { side: 'front', margin: 3.0 };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--side') opt.side = argv[++i];
    else if (a === '--margin') opt.margin = +argv[++i];
    else if (a === '--aspect') opt.aspect = argv[++i]; // "W:H", e.g. "5:1" for a wide logo
    else pos.push(a);
  }
  return { pos, opt };
}
const { pos, opt } = parseArgs(process.argv.slice(2));
const IN = pos[0];
const WANT_BACK = opt.side === 'back';
const src = fs.readFileSync(IN, 'utf8');

function matchBlock(str, i) { let d = 0; for (; i < str.length; i++) { const c = str[i]; if (c === '(') d++; else if (c === ')') { d--; if (d === 0) return i + 1; } } return str.length; }
function* iterBlocks(str, token) { let i = 0; while ((i = str.indexOf(token, i)) !== -1) { const end = matchBlock(str, i); yield { start: i, end, text: str.slice(i, end) }; i = end; } }
const rad = d => d * Math.PI / 180;
function fwd(x, y, A, back) { if (back) x = -x; const c = Math.cos(rad(A)), s = Math.sin(rad(A)); return [x * c + y * s, -x * s + y * c]; }

// board bbox from Edge.Cuts
function computeBoardBBox(s) {
  const items = [];
  const re = /\((gr_line|gr_rect|gr_arc|gr_poly|gr_circle|gr_curve)\b/g; let m;
  while ((m = re.exec(s))) {
    const e = matchBlock(s, m.index);
    const blk = s.slice(m.index, e);
    if (blk.includes('"Edge.Cuts"')) for (const n of blk.matchAll(/\((?:start|end|center|mid|xy)\s+(-?[\d.]+)\s+(-?[\d.]+)\)/g)) items.push([+n[1], +n[2]]);
  }
  const xs = items.map(p => p[0]), ys = items.map(p => p[1]);
  return { minx: Math.min(...xs), maxx: Math.max(...xs), miny: Math.min(...ys), maxy: Math.max(...ys) };
}
const BOARD = computeBoardBBox(src);

const obstacles = []; // {xmin,xmax,ymin,ymax}
for (const blk of iterBlocks(src, '(footprint ')) {
  const t = blk.text;
  const layerM = t.match(/\(layer "([^"]+)"\)/);
  const back = layerM && layerM[1].startsWith('B.');
  if (back !== WANT_BACK) continue; // only obstacles on the side we're placing on
  const propPos = t.indexOf('(property');
  const head = propPos > -1 ? t.slice(0, propPos) : t;
  const atM = head.match(/\(at\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)(?:\s+(-?\d+(?:\.\d+)?))?\s*\)/);
  if (!atM) continue;
  const fx = +atM[1], fy = +atM[2], fa = atM[3] !== undefined ? +atM[3] : 0;

  const pts = [];
  const gfxRe = /\((fp_line|fp_rect|fp_poly|fp_circle|fp_arc|fp_curve)\b/g; let gm; let haveCourt = false; const courtPts = [];
  while ((gm = gfxRe.exec(t))) {
    const gEnd = matchBlock(t, gm.index); const g = t.slice(gm.index, gEnd);
    const lm = g.match(/\(layer "([^"]+)"\)/); if (!lm) continue;
    const ly = lm[1]; const isCourt = ly.endsWith('.CrtYd'); const isBody = ly.endsWith('.Fab') || ly.endsWith('.SilkS');
    if (!isCourt && !isBody) continue;
    for (const cM of g.matchAll(/\((?:start|end|center|mid|xy)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\)/g)) {
      const p = [+cM[1], +cM[2]];
      if (isCourt) { courtPts.push(p); haveCourt = true; }
      pts.push(p);
    }
  }
  const padRe = /\(pad\s+"[^"]*"\s+\S+\s+\S+/g; let pm;
  while ((pm = padRe.exec(t))) {
    const pEnd = matchBlock(t, pm.index); const p = t.slice(pm.index, pEnd);
    const pAt = p.match(/\(at\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)(?:\s+(-?\d+(?:\.\d+)?))?\s*\)/);
    const pSz = p.match(/\(size\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*\)/);
    if (!pAt || !pSz) continue;
    let px = +pAt[1], py = +pAt[2]; let pw = +pSz[1], ph = +pSz[2];
    const pr = pAt[3] !== undefined ? ((+pAt[3] % 360) + 360) % 360 : 0;
    if (pr === 90 || pr === 270) { const tmp = pw; pw = ph; ph = tmp; }
    for (const sx of [-1, 1]) for (const sy of [-1, 1]) { pts.push([px + sx * pw / 2, py + sy * ph / 2]); courtPts.push([px + sx * pw / 2, py + sy * ph / 2]); }
  }
  const useLocal = haveCourt ? courtPts : pts;
  if (useLocal.length === 0) continue;
  let bxmin = Infinity, bxmax = -Infinity, bymin = Infinity, bymax = -Infinity;
  for (const [lx, ly] of useLocal) { const [ox, oy] = fwd(lx, ly, fa, back); const X = fx + ox, Y = fy + oy; if (X < bxmin) bxmin = X; if (X > bxmax) bxmax = X; if (Y < bymin) bymin = Y; if (Y > bymax) bymax = Y; }
  obstacles.push({ xmin: bxmin, xmax: bxmax, ymin: bymin, ymax: bymax });
}
// board-level (non-footprint) graphics already on F.Silkscreen: warning triangles, logos, text, etc.
// strip out footprint blocks first so we only scan top-level board items
let boardOnly = src;
for (const blk of iterBlocks(src, '(footprint ')) boardOnly = boardOnly.slice(0, blk.start) + ' '.repeat(blk.end - blk.start) + boardOnly.slice(blk.end);
{
  const gfxRe = /\((gr_line|gr_rect|gr_poly|gr_circle|gr_arc|gr_curve|gr_text)\b/g; let gm;
  while ((gm = gfxRe.exec(boardOnly))) {
    const gEnd = matchBlock(boardOnly, gm.index); const g = boardOnly.slice(gm.index, gEnd);
    const lm = g.match(/\(layer "([^"]+)"\)/); if (!lm || lm[1] !== (WANT_BACK ? 'B.SilkS' : 'F.SilkS')) continue;
    const coords = [...g.matchAll(/\((?:start|end|center|mid|xy)\s+(-?[\d.]+)\s+(-?[\d.]+)\)/g)].map(m => [+m[1], +m[2]]);
    if (!coords.length) continue;
    const xs = coords.map(p => p[0]), ys = coords.map(p => p[1]);
    obstacles.push({ xmin: Math.min(...xs), xmax: Math.max(...xs), ymin: Math.min(...ys), ymax: Math.max(...ys) });
  }
}
console.error('board bbox', BOARD, 'front obstacles (incl. board graphics):', obstacles.length);

// grid scan
const RES = 1.0; // mm per cell
const MARGIN = opt.margin; // keep this far from board edge
const gx0 = BOARD.minx + MARGIN, gy0 = BOARD.miny + MARGIN;
const gx1 = BOARD.maxx - MARGIN, gy1 = BOARD.maxy - MARGIN;
const W = Math.floor((gx1 - gx0) / RES), H = Math.floor((gy1 - gy0) / RES);
const occ = new Uint8Array(W * H);
function markOcc(o) {
  const cx0 = Math.max(0, Math.floor((o.xmin - 0.3 - gx0) / RES));
  const cx1 = Math.min(W - 1, Math.ceil((o.xmax + 0.3 - gx0) / RES));
  const cy0 = Math.max(0, Math.floor((o.ymin - 0.3 - gy0) / RES));
  const cy1 = Math.min(H - 1, Math.ceil((o.ymax + 0.3 - gy0) / RES));
  for (let y = cy0; y <= cy1; y++) for (let x = cx0; x <= cx1; x++) occ[y * W + x] = 1;
}
for (const o of obstacles) markOcc(o);

// maximal square via DP
const dp = new Int32Array(W * H);
let best = { size: 0, x: 0, y: 0 };
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  if (occ[y * W + x]) { dp[y * W + x] = 0; continue; }
  if (x === 0 || y === 0) dp[y * W + x] = 1;
  else dp[y * W + x] = 1 + Math.min(dp[y * W + x - 1], dp[(y - 1) * W + x], dp[(y - 1) * W + x - 1]);
  if (dp[y * W + x] > best.size) best = { size: dp[y * W + x], x: x - dp[y * W + x] + 1, y: y - dp[y * W + x] + 1 };
}
const bx0 = gx0 + best.x * RES, by0 = gy0 + best.y * RES;
const bx1 = bx0 + best.size * RES, by1 = by0 + best.size * RES;
console.log(JSON.stringify({ sizeMM: best.size * RES, x0: +bx0.toFixed(2), y0: +by0.toFixed(2), x1: +bx1.toFixed(2), y1: +by1.toFixed(2), cx: +((bx0 + bx1) / 2).toFixed(2), cy: +((by0 + by1) / 2).toFixed(2) }));

// also report top 5 distinct largest squares (non-overlapping-ish) for options
const candidates = [];
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  if (dp[y * W + x] >= best.size * 0.6) candidates.push({ size: dp[y * W + x], x: x - dp[y * W + x] + 1, y: y - dp[y * W + x] + 1 });
}
candidates.sort((a, b) => b.size - a.size);

// ---- best free rectangle at a given W:H aspect ratio (for wide/short logos, where
// the constraining dimension is height, and a square search undersells how much
// width is actually available) ----
if (opt.aspect) {
  const [arW, arH] = opt.aspect.split(':').map(Number);
  const ratio = arW / arH; // width per unit height
  // rowFree[y*W+x] = number of consecutive free cells starting at x, going right, on row y
  const rowFree = new Int32Array(W * H);
  for (let y = 0; y < H; y++) {
    let run = 0;
    for (let x = W - 1; x >= 0; x--) { run = occ[y * W + x] ? 0 : run + 1; rowFree[y * W + x] = run; }
  }
  let bestRect = { area: 0 };
  for (let x = 0; x < W; x++) {
    for (let y = 0; y < H; y++) {
      if (occ[y * W + x]) continue;
      // grow height downward from (x,y); track the min available width seen so far
      let minW = Infinity;
      for (let h = 1; y + h - 1 < H; h++) {
        const avail = rowFree[(y + h - 1) * W + x];
        if (avail === 0) break;
        if (avail < minW) minW = avail;
        const wCells = Math.min(minW, Math.round(h * ratio));
        if (wCells < 1) continue;
        const area = wCells * h;
        if (area > bestRect.area) bestRect = { area, x, y, w: wCells, h };
      }
    }
  }
  if (bestRect.area > 0) {
    const rx0 = gx0 + bestRect.x * RES, ry0 = gy0 + bestRect.y * RES;
    const rx1 = rx0 + bestRect.w * RES, ry1 = ry0 + bestRect.h * RES;
    console.error('best rectangle @ aspect', opt.aspect, ':', JSON.stringify({
      widthMM: bestRect.w * RES, heightMM: bestRect.h * RES,
      x0: +rx0.toFixed(2), y0: +ry0.toFixed(2), x1: +rx1.toFixed(2), y1: +ry1.toFixed(2),
      cx: +((rx0 + rx1) / 2).toFixed(2), cy: +((ry0 + ry1) / 2).toFixed(2),
    }));
  } else {
    console.error('no free rectangle found at aspect', opt.aspect);
  }
}

const picked = [];
for (const c of candidates) {
  const cx = gx0 + (c.x + c.size / 2) * RES, cy = gy0 + (c.y + c.size / 2) * RES;
  if (picked.some(p => Math.hypot(p.cx - cx, p.cy - cy) < c.size * RES * 0.7)) continue;
  picked.push({ sizeMM: c.size * RES, cx: +cx.toFixed(2), cy: +cy.toFixed(2) });
  if (picked.length >= 6) break;
}
console.error('top candidates:', JSON.stringify(picked, null, 1));
