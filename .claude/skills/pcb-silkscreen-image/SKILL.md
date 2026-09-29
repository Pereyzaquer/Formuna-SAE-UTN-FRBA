---
name: pcb-silkscreen-image
description: Places a logo, image, silhouette, or icon onto a KiCad PCB's silkscreen layer as real manufacturable vector graphics (not KiCad's "Place Image", which is editor-only reference art that never reaches the Gerbers). Use this whenever the user wants to add a team logo, sponsor logo, QR code, icon, or any picture to a .kicad_pcb silkscreen — trigger on phrases like "put this logo on the PCB", "add this image to the silkscreen", "stampame este QR en la placa", "subir una imagen al PCB", or when they attach an image and ask where/how to place it on a board. Works with PNG, JPG, WEBP, or SVG source images, for any KiCad project.
---

# PCB silkscreen image placement

Turns a raster or vector image into clean vector shapes on a KiCad board's
silkscreen, in a spot that doesn't collide with anything already there.

Read this whole file before starting — steps 5 and 6 exist because of two real
failures this skill's author hit on a live board, and skipping them produces a
board that looks fine in KiCad's own render but is quietly polluted.

## Why not KiCad's "Place Image"?

KiCad's built-in Image tool embeds a bitmap that only ever renders inside
KiCad's own editor — it is not exported to Gerbers and the fab will never
print it. To actually get a logo onto the physical silkscreen you need real
vector shapes (polygons), which is what this skill produces via
`import_svg_logo`.

## Step 0 — locate the target board and back it up

Find the `.kicad_pcb` file the user means (ask if there's more than one in
the repo). Before touching anything:

```bash
cp <board>.kicad_pcb <scratch>/pre_logo_snapshot.kicad_pcb
```

Keep this snapshot around for the whole session — step 6 diffs against it.

## Step 1 — get a clean PNG

If the source is already a PNG, skip to Step 2.

**For SVG**: `import_svg_logo` (step 4) takes SVG directly — skip straight
there, no raster step needed at all.

**For WEBP, JPG, or anything else**: convert to PNG first. Two paths exist;
**prefer the browser one, it's the one proven to actually work**:

### Preferred: convert via the Browser pane (Chromium's own decoder)

`scripts/to_png.ps1` (Windows Imaging Component / WPF) looks like the
obvious tool for this, and it correctly reports width/height for WEBP files —
but on at least one real machine it silently returned solid-black pixel data
for every WEBP tested, while still claiming success (right dimensions, wrong
content). GDI+ (`System.Drawing`) refuses WEBP outright. Don't trust either
one for WEBP without verifying (see the check below) — Chromium decodes WEBP
correctly and is simpler to trust:

1. Copy the source image next to a tiny local HTML page and serve both over
   a local static file server (`node -e "require('http').createServer(...)"`
   on some free port — this repo's earlier sessions used this pattern
   repeatedly, it's ~10 lines, don't overthink it). A `file://` URL will
   likely be refused by the sandboxed browser; serve over `http://localhost`
   instead.
2. Navigate the Browser pane to that page. The page should have an
   `<img src="the-image.webp">` and a script exposing a function that, once
   the image has loaded, draws it to a `<canvas>` at natural size and returns
   `canvas.toDataURL('image/png')`.
3. Call that function via the browser's JS-execution tool and capture the
   returned `data:image/png;base64,...` string (it will likely be saved to a
   tool-result file rather than returned inline if it's large — read that
   file, don't fight the size limit).
4. Decode the base64 payload and write it to a `.png` file with Node
   (`Buffer.from(b64, 'base64')` → `fs.writeFileSync`).

### Fallback: `scripts/to_png.ps1` (WIC/WPF)

```bash
powershell -ExecutionPolicy Bypass -File scripts/to_png.ps1 -InputPath <in> -OutputPath <out.png>
```

Only trust this for non-WEBP formats, or for WEBP after you've verified it
(see below) — WIC's codec support varies by machine and Windows version.

### Verify whichever path you used

Before tracing, sanity-check the PNG isn't degenerate — a broken decode often
comes back as one solid color:

```bash
node -e '
const {decodePNG}=require("scripts/decode_png.js");
const img=decodePNG("out.png");
let black=0,white=0,other=0,n=0;
for(let y=0;y<img.height;y+=7)for(let x=0;x<img.width;x+=7){
  const i=(y*img.width+x)*img.channels;
  const l=0.299*img.data[i]+0.587*img.data[i+1]+0.114*img.data[i+2];
  n++; if(l<50)black++; else if(l>200)white++; else other++;
}
console.log({n,black,white,other});
'
```

If one of `black`/`white` is ~equal to `n` and the others are 0, the decode is
almost certainly broken (a real logo has both light and dark regions) —
switch to the other conversion path before going any further. Don't skip this
— tracing a corrupted all-one-color image doesn't error, it just silently
produces a solid black rectangle that looks like a rendering bug and wastes
time chasing the wrong cause.

## Step 2 — decode

`scripts/decode_png.js` is a dependency-free PNG decoder (handles the RGB/RGBA
8-bit non-interlaced case, which covers essentially every logo/screenshot
export). `scripts/trace_image.js` (next step) uses it internally — you won't
normally call it directly except for the sanity check above.

## Step 3 — trace to SVG

```bash
node scripts/trace_image.js <in.png> <out.svg> [options]
```

Two modes:

- **`--mode contour`** (default). Marching-squares contour tracing with
  Douglas-Peucker simplification, emitted as one `<path fill-rule="evenodd">`.
  This is the right choice for essentially everything: logos, text, icons,
  silhouettes. `evenodd` means letter counters (the hole in an O/A/B/P/R) and
  any other nested hole render correctly automatically — you don't need to
  classify inner vs. outer contours yourself.
- **`--mode grid`**. Per-run-length `<rect>` tiling on the pixel grid. Use
  this *only* for QR codes / data-matrix style images that are already
  literally a grid of flat squares — it gives crisper, smaller output there
  since that IS the shape, whereas contour mode would (correctly, but
  wastefully) trace every module's corners individually.

Useful flags: `--threshold N` (0-255, default 128), `--invert` (trace white
regions instead of black — use for a white-on-transparent logo), `--maxDim N`
(downsample cap before tracing, default 500 — raise it if fine detail like
small text or thin letter counters is getting lost, lower it if the traced
file is unwieldy), `--pad N` (quiet-zone margin), `--simplify N` (Douglas-
Peucker tolerance — 0 disables it and keeps every raw contour point).

**Look at the result before importing it.** Serve the SVG the same way you
served the source image and view it in the Browser pane (Node's http server
needs to map `.svg` to `image/svg+xml` explicitly, or Chromium will render it
as plain text instead of an image — an easy way to think the trace failed
when it didn't). Confirm the shape actually looks like the source and that
holes look like holes, not solid blobs. If loop count is 1 or 2 for something
that obviously has multiple disconnected parts (separate letters, a hole),
that's the same "corrupted source" symptom from Step 1, not a tracer bug —
double check the PNG, don't start debugging the tracer.

## Step 4 — find where it goes

```bash
node scripts/find_free_space.js <board.kicad_pcb> --side front --margin 3
```

Prints the largest free square (JSON: `x0,y0,x1,y1,cx,cy,sizeMM`) that avoids
component courtyards on that side of the board *and* any silkscreen graphics
already placed there — an existing warning triangle, logo, or other artwork
counts as an obstacle too, not just parts. `--side back` searches B.SilkS
instead. If nothing free is big enough, it still reports the best it found;
use your judgment (a smaller logo, or ask the user for a preferred spot)
rather than forcing something into too-tight a space.

If you're placing more than one image, place and verify one fully (through
Step 6) before re-running this search for the next — its own already-placed
graphics become obstacles for the next search automatically.

Sanity-check the chosen spot before importing: render the board's
F.Silkscreen + F.Courtyard + Edge.Cuts layers (`mcp__kicad__get_board_2d_view`)
and eyeball the target area. A rectangle can be geometrically empty of
component bodies and still be a bad choice — e.g. sitting directly on top of
an existing safety marking like a high-voltage warning triangle, which this
script's obstacle scan does account for, but a second look costs nothing on
something as visible and permanent as PCB silkscreen.

Compute the import position from the result: if the image's aspect ratio
isn't square, fit its width to the free square's `sizeMM` (or a bit less) and
center it — `x = cx - width/2`, `y = cy - height/2` (height follows from the
SVG's own aspect ratio, which `import_svg_logo` preserves automatically from
`width` alone).

## Step 5 — import

```
mcp__kicad__import_svg_logo(pcbPath=<board>, svgPath=<out.svg>, layer="F.SilkS" (or "B.SilkS"), x=<x>, y=<y>, width=<width>, filled=true)
```

## Step 6 — verify, every time, no exceptions

This step exists because of two real side effects observed on a genuine large
board from calling this exact tool — not hypothetical caution:

```bash
node scripts/verify_and_fix.js <board.kicad_pcb> --before <scratch>/pre_logo_snapshot.kicad_pcb
```

This does two things:

1. **Normalizes line endings back to LF.** The tool's underlying save path
   silently converts the whole file to CRLF. Harmless to KiCad, but it turns
   the next `git diff` into a multi-hundred-thousand-line wall of noise that
   buries the one real change inside it — exactly the kind of thing that
   makes a teammate's code review assume something much bigger happened than
   actually did.
2. **Diffs the zone list against your pre-op snapshot.** The same tool call
   has been observed to silently fabricate small extra keepout zones (seen
   as rounded shapes named like `NoCu<something>`, brand-new random UUIDs,
   in locations unrelated to the logo just placed) that were never requested.
   Left in the file, one of these is next week's "there's a mystery zone
   somewhere that won't let me route or pour copper" — the exact bug class
   this same board hit earlier from an entirely different cause. Don't ship
   a board with an unexplained keepout in it. If the script reports any, look
   at each (`node scripts/remove_zones.js <board> --uuids <uuid...>` removes
   them) before telling the user you're done. If you split or edited a zone
   yourself as part of this task, your own new UUIDs will show up in this
   diff too — that's expected, only the unattributable ones are the problem.

Finish by re-opening the board (`mcp__kicad__open_project`) and rendering the
placed area once more to confirm the final result looks right, then tell the
user it needs a manual "Fill All Zones" (B in KiCad) if any zone fills might
be stale near where you worked, and that they should visually confirm in
KiCad before sending the board to fab — this skill gets you a correct,
manufacturable placement, but a human should still look at a board before
it's ordered.

## Reference: what "good" looks like

A correctly traced logo, once rendered, should show every visually-separate
part of the source (each letter, each disconnected shape) as its own region,
and every hole (letter counters, a ring, a wheel's rim) as an actual gap, not
a filled blob. If you get a single solid black rectangle or a wildly
oversimplified shape (a complex logo collapsing to a 4-point quadrilateral),
that is the "corrupted source pixels" failure mode from Step 1, essentially
always — go back and verify the PNG decode before suspecting the tracer.
