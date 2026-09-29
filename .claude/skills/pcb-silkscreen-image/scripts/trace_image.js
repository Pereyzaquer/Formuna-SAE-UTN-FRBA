/* Trace a PNG into a clean SVG for PCB silkscreen import.
 *
 * Two modes:
 *   --mode contour   Marching-squares contour tracing + polygon simplification,
 *                     emitted as ONE <path> with fill-rule="evenodd" so nested
 *                     holes (the inside of an "O", "A", "B", ...) render correctly.
 *                     Use this for logos, text, silhouettes -- anything with
 *                     curves or holes. This is the default and right for almost
 *                     everything.
 *   --mode grid       Per-run-length <rect> tiling on a regular pixel grid.
 *                     Use this ONLY for QR codes / data matrices / other images
 *                     that are already a grid of flat squares -- contour tracing
 *                     also works there but grid mode gives crisper, more
 *                     compact, more obviously-correct output for that specific
 *                     case since the "true" shape already IS a pixel grid.
 *
 * Usage:
 *   node trace_image.js <in.png> <out.svg> [options]
 * Options:
 *   --mode contour|grid       (default contour)
 *   --threshold N             luminance 0-255 cutoff, default 128
 *   --invert                  trace the WHITE regions instead of black
 *                             (use when the logo is white-on-transparent/dark)
 *   --maxDim N                downsample so the longer side is <= N working
 *                             pixels before tracing (default 500). Keeps output
 *                             size sane; final physical resolution is set later
 *                             by the width you pass to import_svg_logo, not by
 *                             this number.
 *   --pad N                   quiet-zone padding in working pixels added around
 *                             the auto-detected content bbox (default 4)
 *   --simplify N              Douglas-Peucker tolerance in working pixels for
 *                             contour mode (default 0.8). Higher = fewer nodes,
 *                             coarser shape. 0 disables simplification.
 */
const fs = require('fs');
const path = require('path');
const { decodePNG } = require(path.join(__dirname, 'decode_png.js'));

function parseArgs(argv) {
  const pos = [];
  const opt = { mode: 'contour', threshold: 128, invert: false, maxDim: 500, pad: 4, simplify: 0.8 };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--mode') opt.mode = argv[++i];
    else if (a === '--threshold') opt.threshold = +argv[++i];
    else if (a === '--invert') opt.invert = true;
    else if (a === '--maxDim') opt.maxDim = +argv[++i];
    else if (a === '--pad') opt.pad = +argv[++i];
    else if (a === '--simplify') opt.simplify = +argv[++i];
    else pos.push(a);
  }
  return { pos, opt };
}

const { pos, opt } = parseArgs(process.argv.slice(2));
const [inPng, outSvg] = pos;
if (!inPng || !outSvg) {
  console.error('Usage: node trace_image.js <in.png> <out.svg> [--mode contour|grid] [--threshold N] [--invert] [--maxDim N] [--pad N] [--simplify N]');
  process.exit(1);
}

const img = decodePNG(inPng);
const { width: W0, height: H0, data, channels } = img;
function rawLum(x, y) {
  const i = (y * W0 + x) * channels;
  const r = data[i], g = data[i + 1], b = data[i + 2];
  const a = channels === 4 ? data[i + 3] : 255;
  // Composite onto white so a transparent background reads as background, not black.
  const l = 0.299 * r + 0.587 * g + 0.114 * b;
  return (l * a + 255 * (255 - a)) / 255;
}

// ---- downsample (box filter) so tracing stays fast and the node count stays sane ----
const longSide = Math.max(W0, H0);
const factor = Math.max(1, Math.ceil(longSide / opt.maxDim));
const W = Math.ceil(W0 / factor), H = Math.ceil(H0 / factor);
const lum = new Float32Array(W * H);
for (let y = 0; y < H; y++) {
  for (let x = 0; x < W; x++) {
    let sum = 0, n = 0;
    const x0 = x * factor, y0 = y * factor;
    for (let dy = 0; dy < factor && y0 + dy < H0; dy++)
      for (let dx = 0; dx < factor && x0 + dx < W0; dx++) { sum += rawLum(x0 + dx, y0 + dy); n++; }
    lum[y * W + x] = sum / n;
  }
}
function isFg(x, y) {
  if (x < 0 || y < 0 || x >= W || y >= H) return false;
  const dark = lum[y * W + x] < opt.threshold;
  return opt.invert ? !dark : dark;
}

// ---- content bbox (for cropping + quiet-zone padding) ----
let minX = W, maxX = -1, minY = H, maxY = -1;
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (isFg(x, y)) {
  if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y;
}
if (maxX < 0) { console.error('No foreground pixels found at this threshold -- try --threshold or --invert'); process.exit(1); }
const cx0 = Math.max(0, minX - opt.pad), cy0 = Math.max(0, minY - opt.pad);
const cx1 = Math.min(W, maxX + 1 + opt.pad), cy1 = Math.min(H, maxY + 1 + opt.pad);
const CW = cx1 - cx0, CH = cy1 - cy0;
console.error(`source ${W0}x${H0} -> working ${W}x${H} (factor ${factor}) -> content crop ${CW}x${CH}`);

let svgBody;

if (opt.mode === 'grid') {
  // ---- row run-length rectangles (good for QR / module grids) ----
  const rowRuns = [];
  for (let y = cy0; y < cy1; y++) {
    const runs = []; let inRun = false, start = 0;
    for (let x = cx0; x < cx1; x++) {
      const b = isFg(x, y);
      if (b && !inRun) { inRun = true; start = x; }
      else if (!b && inRun) { inRun = false; runs.push([start - cx0, x - cx0]); }
    }
    if (inRun) runs.push([start - cx0, cx1 - cx0]);
    rowRuns.push(runs);
  }
  function sameRuns(a, b) { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (a[i][0] !== b[i][0] || a[i][1] !== b[i][1]) return false; return true; }
  const rects = [];
  let i = 0;
  while (i < rowRuns.length) {
    let j = i + 1;
    while (j < rowRuns.length && sameRuns(rowRuns[i], rowRuns[j])) j++;
    for (const [rs, re] of rowRuns[i]) rects.push([rs, i, re, j]);
    i = j;
  }
  console.error('grid mode: rects =', rects.length);
  svgBody = '<g fill="#000000" stroke="none">\n' + rects.map(([x0, y0, x1, y1]) => `<rect x="${x0}" y="${y0}" width="${x1 - x0}" height="${y1 - y0}"/>`).join('\n') + '\n</g>';
} else {
  // ---- marching squares contour tracing ----
  // Grid of samples at integer points (x,y) for x in [cx0-1, cx1], y in [cy0-1, cy1]
  // (1-cell padding of "background" all around so every shape closes into loops).
  function s(x, y) { return isFg(x, y) ? 1 : 0; }
  const segs = []; // each: [ [x1,y1], [x2,y2] ]
  for (let cy = cy0 - 1; cy < cy1; cy++) {
    for (let cx = cx0 - 1; cx < cx1; cx++) {
      const tl = s(cx, cy), tr = s(cx + 1, cy), br = s(cx + 1, cy + 1), bl = s(cx, cy + 1);
      const c = tl * 8 + tr * 4 + br * 2 + bl * 1;
      if (c === 0 || c === 15) continue;
      const T = [cx + 0.5, cy], B = [cx + 0.5, cy + 1], L = [cx, cy + 0.5], R = [cx + 1, cy + 0.5];
      switch (c) {
        case 1: segs.push([L, B]); break;
        case 2: segs.push([B, R]); break;
        case 3: segs.push([L, R]); break;
        case 4: segs.push([T, R]); break;
        case 5: segs.push([L, T]); segs.push([B, R]); break; // saddle, fixed resolution
        case 6: segs.push([T, B]); break;
        case 7: segs.push([T, L]); break;
        case 8: segs.push([T, L]); break;
        case 9: segs.push([T, B]); break;
        case 10: segs.push([T, R]); segs.push([L, B]); break; // saddle, fixed resolution
        case 11: segs.push([T, R]); break;
        case 12: segs.push([L, R]); break;
        case 13: segs.push([B, R]); break;
        case 14: segs.push([L, B]); break;
      }
    }
  }
  console.error('contour mode: raw segments =', segs.length);

  // link segments into closed loops
  const key = p => p[0] + ',' + p[1];
  const adj = new Map(); // key -> [ [otherPoint, segIndex used-flag holder] ... ]
  segs.forEach((seg, idx) => {
    for (const [a, b] of [[seg[0], seg[1]], [seg[1], seg[0]]]) {
      const k = key(a);
      if (!adj.has(k)) adj.set(k, []);
      adj.get(k).push({ to: b, idx });
    }
  });
  const usedSeg = new Uint8Array(segs.length);
  const loops = [];
  for (let idx = 0; idx < segs.length; idx++) {
    if (usedSeg[idx]) continue;
    const loop = [segs[idx][0], segs[idx][1]];
    usedSeg[idx] = 1;
    let guard = 0;
    while (guard++ < segs.length * 2) {
      const cur = loop[loop.length - 1];
      if (key(cur) === key(loop[0]) && loop.length > 2) break; // closed
      const options = adj.get(key(cur)) || [];
      let next = null;
      for (const o of options) { if (!usedSeg[o.idx]) { next = o; break; } }
      if (!next) break;
      usedSeg[next.idx] = 1;
      loop.push(next.to);
    }
    if (loop.length >= 3) loops.push(loop);
  }
  console.error('contour mode: closed loops =', loops.length);

  // Douglas-Peucker simplification (closed polyline: split at 2 far-apart points, simplify each half)
  function dp(points, eps) {
    if (eps <= 0 || points.length <= 3) return points;
    function rec(pts) {
      if (pts.length <= 2) return pts;
      let maxD = -1, maxI = 0;
      const [x1, y1] = pts[0], [x2, y2] = pts[pts.length - 1];
      const dx = x2 - x1, dy = y2 - y1, len = Math.hypot(dx, dy) || 1e-9;
      for (let i = 1; i < pts.length - 1; i++) {
        const [x, y] = pts[i];
        const d = Math.abs(dy * x - dx * y + x2 * y1 - y2 * x1) / len;
        if (d > maxD) { maxD = d; maxI = i; }
      }
      if (maxD > eps) {
        const left = rec(pts.slice(0, maxI + 1));
        const right = rec(pts.slice(maxI));
        return left.slice(0, -1).concat(right);
      }
      return [pts[0], pts[pts.length - 1]];
    }
    // pick two roughly-opposite points to split the closed loop into two open chains
    let iA = 0, best = -1, iB = 0;
    for (let i = 1; i < points.length; i++) {
      const d = Math.hypot(points[i][0] - points[0][0], points[i][1] - points[0][1]);
      if (d > best) { best = d; iB = i; }
    }
    const chain1 = points.slice(iA, iB + 1);
    const chain2 = points.slice(iB).concat(points.slice(0, iA + 1));
    const s1 = rec(chain1), s2 = rec(chain2);
    return s1.slice(0, -1).concat(s2.slice(0, -1));
  }

  let totalIn = 0, totalOut = 0;
  const pathParts = [];
  for (const loop of loops) {
    totalIn += loop.length;
    const simp = dp(loop, opt.simplify);
    totalOut += simp.length;
    const pts = simp.map(([x, y]) => [x - cx0, y - cy0]);
    pathParts.push('M ' + pts.map(p => p[0].toFixed(2) + ',' + p[1].toFixed(2)).join(' L ') + ' Z');
  }
  console.error(`contour mode: nodes ${totalIn} -> ${totalOut} after simplify (eps=${opt.simplify})`);
  svgBody = `<path fill="#000000" fill-rule="evenodd" stroke="none" d="${pathParts.join(' ')}"/>`;
}

const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${CW} ${CH}" width="${CW}" height="${CH}">\n${svgBody}\n</svg>\n`;
fs.writeFileSync(outSvg, svg);
console.error('wrote', outSvg, `(${CW}x${CH} working px, ${(svg.length / 1024).toFixed(0)} KB)`);
