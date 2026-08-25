'use strict';

// Generates the tray icon at runtime so the repo carries no binary assets.
// Tiny RGBA -> PNG encoder + a hand-plotted flame glyph.

const zlib = require('zlib');
const { nativeImage } = require('electron');

const CRC_TABLE = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c;
  }
  return t;
})();

function crc32(buf) {
  let c = -1;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body), 0);
  return Buffer.concat([len, body, crc]);
}

function encodePNG(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0; // filter: none
    rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0);
  ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 6;  // colour type: RGBA
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0))
  ]);
}

// Signed distance-ish flame field, evaluated per pixel in [-1,1] space.
function flame(u, v) {
  // v: -1 top .. 1 bottom
  const t = (v + 1) / 2;                       // 0 tip .. 1 base
  const width = Math.pow(t, 0.62) * 0.62;      // narrow tip, round base
  const lean = Math.sin(t * 2.4) * 0.10 * (1 - t);
  const d = Math.abs(u - lean) / Math.max(width, 1e-3);
  if (t < 0.02 || t > 1) return -1;
  const notch = t > 0.78 ? 0 : Math.max(0, 0.34 - Math.abs(u - lean) * 3.2) * (1 - t) * 0.9;
  return 1 - d - notch;
}

function makeIcon(size = 22) {
  const px = Buffer.alloc(size * size * 4);
  const SS = 3; // supersample

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      let cov = 0;
      let heat = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const u = ((x + (sx + 0.5) / SS) / size) * 2 - 1;
          const v = ((y + (sy + 0.5) / SS) / size) * 2 - 1;
          const f = flame(u * 1.25, v * 1.06);
          if (f > 0) {
            cov++;
            heat += Math.min(1, f * 1.5);
          }
        }
      }
      const n = SS * SS;
      if (!cov) continue;
      const a = cov / n;
      const h = heat / cov; // 0 edge .. 1 core
      // ramp: scarlet edge -> orange -> gold core
      const r = 210 + 45 * h;
      const g = 34 + 165 * Math.pow(h, 1.5);
      const b = 12 + 70 * Math.pow(h, 3.4);
      const i = (y * size + x) * 4;
      px[i] = Math.round(r);
      px[i + 1] = Math.round(g);
      px[i + 2] = Math.round(b);
      px[i + 3] = Math.round(a * 255);
    }
  }

  return nativeImage.createFromBuffer(encodePNG(size, size, px));
}

module.exports = { makeIcon, encodePNG };
