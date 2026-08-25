'use strict';
window.PX = window.PX || {};

(function (NS) {
  const TAU = Math.PI * 2;

  const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
  const lerp = (a, b, t) => a + (b - a) * t;
  const invLerp = (a, b, v) => (b === a ? 0 : (v - a) / (b - a));
  const smooth = (t) => t * t * (3 - 2 * t);
  const smoother = (t) => t * t * t * (t * (t * 6 - 15) + 10);

  const easeOutCubic = (t) => 1 - Math.pow(1 - t, 3);
  const easeInCubic = (t) => t * t * t;
  const easeOutQuint = (t) => 1 - Math.pow(1 - t, 5);
  const easeInOut = (t) => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);

  const rand = (a = 1, b) => (b === undefined ? Math.random() * a : a + Math.random() * (b - a));
  const randSym = (a = 1) => (Math.random() * 2 - 1) * a;
  const pick = (arr) => arr[(Math.random() * arr.length) | 0];

  // roughly gaussian, mean 0, sd ~0.4
  const gauss = () => (Math.random() + Math.random() + Math.random() - 1.5) * 0.6;

  function wrapAngle(a) {
    while (a > Math.PI) a -= TAU;
    while (a < -Math.PI) a += TAU;
    return a;
  }

  // frame-rate independent angular smoothing
  function angleTowards(cur, target, rate, dt) {
    const d = wrapAngle(target - cur);
    return cur + d * (1 - Math.exp(-rate * dt));
  }

  // frame-rate independent scalar smoothing
  function damp(cur, target, rate, dt) {
    return cur + (target - cur) * (1 - Math.exp(-rate * dt));
  }

  // cheap 1D value noise, seeded and deterministic
  function makeNoise(seed) {
    const p = new Float32Array(512);
    let s = (seed | 0) || 1;
    for (let i = 0; i < 512; i++) {
      s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
      p[i] = s / 4294967296;
    }
    return function (x) {
      const i = Math.floor(x);
      const f = x - i;
      const a = p[i & 511];
      const b = p[(i + 1) & 511];
      return (a + (b - a) * smoother(f)) * 2 - 1; // -1 .. 1
    };
  }

  // hot -> cold fire ramp. t: 0 = white hot, 1 = dead ash
  function fireColor(t, alpha = 1) {
    t = clamp(t, 0, 1);
    let r, g, b;
    if (t < 0.22) {
      const k = t / 0.22;
      r = lerp(255, 255, k); g = lerp(247, 209, k); b = lerp(226, 118, k);
    } else if (t < 0.5) {
      const k = (t - 0.22) / 0.28;
      r = lerp(255, 255, k); g = lerp(209, 132, k); b = lerp(118, 32, k);
    } else if (t < 0.78) {
      const k = (t - 0.5) / 0.28;
      r = lerp(255, 208, k); g = lerp(132, 42, k); b = lerp(32, 16, k);
    } else {
      const k = (t - 0.78) / 0.22;
      r = lerp(208, 74, k); g = lerp(42, 30, k); b = lerp(16, 26, k);
    }
    return `rgba(${r | 0},${g | 0},${b | 0},${alpha})`;
  }

  Object.assign(NS, {
    TAU, clamp, lerp, invLerp, smooth, smoother,
    easeOutCubic, easeInCubic, easeOutQuint, easeInOut,
    rand, randSym, pick, gauss,
    wrapAngle, angleTowards, damp, makeNoise, fireColor
  });
})(window.PX);
