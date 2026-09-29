const fs = require('fs');
const zlib = require('zlib');

function decodePNG(path) {
  const buf = fs.readFileSync(path);
  if (!buf.slice(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) throw new Error('not png');
  let off = 8;
  let w, h, bitDepth, colorType;
  let idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.slice(off + 4, off + 8).toString('ascii');
    const data = buf.slice(off + 8, off + 8 + len);
    if (type === 'IHDR') {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4);
      bitDepth = data[8]; colorType = data[9];
      if (data[12] !== 0) throw new Error('interlaced not supported');
    } else if (type === 'IDAT') {
      idat.push(data);
    } else if (type === 'IEND') break;
    off += 8 + len + 4;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const channels = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colorType];
  const bpp = Math.ceil(channels * bitDepth / 8);
  const rowBytes = Math.ceil(w * channels * bitDepth / 8);
  const out = Buffer.alloc(h * rowBytes);
  let rawOff = 0;
  for (let y = 0; y < h; y++) {
    const filterType = raw[rawOff]; rawOff++;
    const rowStart = y * rowBytes;
    const prevRowStart = (y - 1) * rowBytes;
    for (let x = 0; x < rowBytes; x++) {
      const rawByte = raw[rawOff + x];
      const a = x >= bpp ? out[rowStart + x - bpp] : 0;
      const b = y > 0 ? out[prevRowStart + x] : 0;
      const c = (y > 0 && x >= bpp) ? out[prevRowStart + x - bpp] : 0;
      let val;
      switch (filterType) {
        case 0: val = rawByte; break;
        case 1: val = rawByte + a; break;
        case 2: val = rawByte + b; break;
        case 3: val = rawByte + ((a + b) >> 1); break;
        case 4: {
          const p = a + b - c;
          const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
          const pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
          val = rawByte + pr;
          break;
        }
        default: throw new Error('bad filter ' + filterType);
      }
      out[rowStart + x] = val & 0xff;
    }
    rawOff += rowBytes;
  }
  return { width: w, height: h, channels, bitDepth, data: out };
}

module.exports = { decodePNG };

if (require.main === module) {
  const img = decodePNG(process.argv[2]);
  console.log(JSON.stringify({ width: img.width, height: img.height, channels: img.channels, bitDepth: img.bitDepth }));
}
