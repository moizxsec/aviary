'use strict';
window.PX = window.PX || {};

(function (NS) {
  const {
    TAU, clamp, lerp, damp, rand, randSym,
    easeInOut, easeOutCubic, easeInCubic, fireColor, flapCurve, foldCurve
  } = NS;

  const D2R = Math.PI / 180;

  // ---- palette --------------------------------------------------------
  const COL = {
    char:    [ 62,  20,  12],
    deep:    [124,  14,  22],
    scarlet: [206,  32,  16],
    ember:   [255,  90,  18],
    orange:  [255, 140,  26],
    gold:    [255, 194,  58],
    pale:    [255, 230, 163],
    white:   [255, 247, 226],
    talon:   [255, 205, 110]
  };

  function mix(a, b, t) {
    return [
      Math.round(lerp(a[0], b[0], t)),
      Math.round(lerp(a[1], b[1], t)),
      Math.round(lerp(a[2], b[2], t))
    ];
  }
  function rgba(c, a = 1) { return `rgba(${c[0]},${c[1]},${c[2]},${a})`; }

  // ---- anatomy, in body units (+x forward, +y down) --------------------
  const A = {
    shoulder:  { x:  3.8, y: -4.4 },
    wingBack:  { x: -6.8, y: -2.6 },   // where the secondaries meet the back
    wingLen:   19.5,
    tailRoot:  { x: -11.6, y: -0.6 },
    headC:     { x: 12.4, y: -6.5 },
    headR:     4.5,
    eye:       { x: 14.3, y: -7.5 },
    beakTip:   { x: 21.2, y: -5.9 },
    crest:     { x: 11.2, y: -10.4 },
    legRoot:   { x:  1.2, y:  3.4 },
    nose:      23,
    tail:      -16
  };

  const TRAIL_MAX = 46;
  const PLUMES = 3;

  class Phoenix extends NS.Flyer {
    constructor(o = {}) {
      super(Object.assign({ maxSpeed: 330, maxForce: 1600, scale: 1, wanderGain: 240 }, o));

      this.trail = [];
      this.plumeLen = 32;
      this.arc = 0;
      this.carrying = true;
      this.legsOut = 0;

      this.crestSway = 0;
      this.wingRangeMul = 1;
      this.tailSpread = 1;

      this.heat = 0;          // 0 normal .. 1 white hot
      this.burning = false;
      this.burnT = 0;
      this.burnFront = A.tail - 4;
      this.consumed = false;  // body fully eaten by the fire
      this.flashed = false;
      this.emberClock = 0;
      this.crackle = 0;
    }

    // ------------------------------------------------------------------
    ignite() {
      if (this.burning) return;
      this.burning = true;
      this.burnT = 0;
      this.bounding = false;
    }

    releasePoint() {
      return this.toWorld(A.legRoot.x - 1, A.legRoot.y + 4.4);
    }

    update(dt, particles) {
      if (this.burning) this.updateBurn(dt);

      super.update(dt);

      // legs drop when the bird slows to hover, tuck away at speed
      const wantLegs = this.hover || this.speed < this.maxSpeed * 0.3 ? 1 : 0;
      this.legsOut = damp(this.legsOut, wantLegs, 6, dt);

      // crest streams back harder the faster it flies
      this.crestSway = damp(this.crestSway, clamp(this.speed / this.maxSpeed, 0, 1), 5, dt);

      // Tail-root history in world space. Capped by arc length rather than by
      // frame count — otherwise a fast bird drags a 250px streamer and a slow
      // one drags a stub, purely as an artefact of frame rate.
      const root = this.toWorld(A.tailRoot.x, A.tailRoot.y);
      this.trail.unshift({ x: root.x, y: root.y });
      const maxArc = this.plumeLen * this.scale * 1.12;
      let acc = 0;
      for (let i = 1; i < this.trail.length; i++) {
        acc += Math.hypot(this.trail[i].x - this.trail[i - 1].x,
                          this.trail[i].y - this.trail[i - 1].y);
        if (acc >= maxArc) { this.trail.length = i + 1; break; }
      }
      this.arc = acc;
      if (this.trail.length > TRAIL_MAX) this.trail.length = TRAIL_MAX;

      if (particles) this.shed(dt, particles);
    }

    updateBurn(dt) {
      this.burnT += dt;
      const t = this.burnT;

      // 1. charge — wings open wide, beat slows to a ceremony, body whitens
      if (t < 0.75) {
        const k = t / 0.75;
        this.hover = true;
        this.wingRangeMul = lerp(1, 1.32, easeInOut(k));
        this.wingHzOverride = lerp(9, 2.9, easeInOut(k));
        this.heat = easeInCubic(k) * 0.55;
        this.pitch = lerp(0, -0.16, easeInOut(k));
        this.tailSpread = lerp(1, 1.5, easeInOut(k));
        this.crackle = k;
      }
      // 2. ignition — the burn front sweeps tail to head
      else if (t < 1.95) {
        const k = (t - 0.75) / 1.2;
        this.wingRangeMul = lerp(1.32, 1.55, easeOutCubic(k));
        this.wingHzOverride = lerp(2.9, 1.6, k);
        this.heat = lerp(0.55, 1, easeInOut(k));
        this.pitch = lerp(-0.16, -0.42, easeOutCubic(k));   // head thrown back
        this.tailSpread = lerp(1.5, 2.1, k);
        this.burnFront = lerp(A.tail - 3, A.nose + 3, easeInOut(k));
        this.crackle = 1;
      }
      // 3. gone
      else {
        this.consumed = true;
        this.heat = 1;
      }

      this.vy -= 26 * dt;         // it lifts a little as it goes
    }

    updateWings(dt) {
      if (this.wingHzOverride) {
        this.effort = damp(this.effort, 1.3, 5, dt);
        this.wingHz = this.wingHzOverride;
        this.wingPhase += this.wingHz * dt;
        this.flap = flapCurve(this.wingPhase);
        this.fold = foldCurve(this.wingPhase) * 0.35;
        this.spread = damp(this.spread, 1, 8, dt);
        this.bob = damp(this.bob, -this.flap * 2.2 * this.scale, 16, dt);
        this.gliding = false;
        return;
      }
      super.updateWings(dt);
    }

    // embers and fire falling off the bird as it flies / burns
    shed(dt, P) {
      this.emberClock += dt;

      if (!this.burning) {
        // a slow drip of sparks from the tail tips — it is a bird made of fire
        if (this.emberClock > 0.045) {
          this.emberClock = 0;
          const tip = this.plumeTip(1);
          P.ember(tip.x + randSym(3), tip.y + randSym(3), {
            vx: this.vx * 0.12, vy: this.vy * 0.1,
            r: rand(0.5, 1.2), ttl: rand(0.5, 1.1), spread: 18
          });
        }
        return;
      }

      const t = this.burnT;

      if (t < 0.75) {
        // charge: embers peel off every feather and rise
        const n = 2 + ((this.crackle * 6) | 0);
        for (let i = 0; i < n; i++) {
          const p = this.toWorld(rand(A.tail, A.nose - 6), rand(-7, 5));
          P.ember(p.x, p.y, { vx: this.vx * 0.2, vy: this.vy * 0.2 - 20, spread: 30 });
        }
        if (Math.random() < 0.35) {
          const p = this.toWorld(rand(-10, 14), rand(-6, 4));
          P.spark(p.x, p.y, { spread: 130 });
        }
      } else if (t < 1.95) {
        // ignition: fire pours off the burn line itself
        for (let i = 0; i < 5; i++) {
          const ly = rand(-9, 6);
          const p = this.toWorld(this.burnFront + randSym(2.4), ly);
          P.fire(p.x, p.y, {
            vx: this.vx * 0.25 + randSym(58),
            vy: this.vy * 0.2 - rand(0, 40),
            r: rand(1.5, 3.4), ttl: rand(0.26, 0.52),
            buoy: rand(180, 320)
          });
        }
        for (let i = 0; i < 3; i++) {
          const p = this.toWorld(this.burnFront + randSym(3), rand(-9, 6));
          P.ember(p.x, p.y, { vx: randSym(60), vy: -rand(20, 90), spread: 40 });
        }
        if (Math.random() < 0.6) {
          const p = this.toWorld(this.burnFront, rand(-8, 5));
          P.spark(p.x, p.y, { spread: 200 });
        }
        // smoke starts trailing off the eaten end
        if (Math.random() < 0.5) {
          const p = this.toWorld(this.burnFront - rand(4, 12), rand(-6, 4));
          P.smoke(p.x, p.y, { r: rand(4, 9), alpha: rand(0.05, 0.12) });
        }
      }
    }

    // ---- tail plumes ---------------------------------------------------

    // Where a plume sits at parameter u (0 root .. 1 tip). Blends between the
    // path the bird actually flew (fast: feathers stream straight behind) and
    // a drooping rest pose (slow: long feathers hang and curl).
    plumePoint(j, u, out) {
      const trail = this.trail;
      const n = trail.length;
      const root = trail[0];

      const arcK = clamp(this.arc / (this.plumeLen * this.scale * 0.85), 0, 1);
      const speedK = clamp(this.speed / (this.maxSpeed * 0.5), 0, 1) * arcK;

      // sample the flown path by distance travelled, not by frame index
      const want = u * this.plumeLen * this.scale;
      let tx = trail[n - 1].x;
      let ty = trail[n - 1].y;
      let acc = 0;
      for (let i = 1; i < n; i++) {
        const dx = trail[i].x - trail[i - 1].x;
        const dy = trail[i].y - trail[i - 1].y;
        const seg = Math.hypot(dx, dy);
        if (acc + seg >= want) {
          const f = seg > 1e-6 ? (want - acc) / seg : 0;
          tx = trail[i - 1].x + dx * f;
          ty = trail[i - 1].y + dy * f;
          break;
        }
        acc += seg;
      }

      // rest pose: straight back from the body, sagging under its own weight
      const sx = this.xScale();
      const sgn = sx < 0 ? -1 : 1;
      const a = this.heading * sgn;
      const bx = (A.tailRoot.x - u * this.plumeLen) * this.scale * sx;
      const by = (A.tailRoot.y + u * u * 7.5) * this.scale;
      const rx = this.x + bx * Math.cos(a) - by * Math.sin(a);
      const ry = this.y + this.bob + bx * Math.sin(a) + by * Math.cos(a);

      let px = lerp(rx, tx, speedK);
      let py = lerp(ry, ty, speedK);

      // sway: a travelling wave down the feather, plus a fan between plumes
      const phase = this.t * 5.2 - u * 3.4 + j * 2.2;
      const amp = (2.4 + 5.6 * u) * this.tailSpread;
      const dx = px - root.x;
      const dy = py - root.y;
      const dl = Math.hypot(dx, dy) || 1;
      const nx = -dy / dl;
      const ny = dx / dl;
      const fan = (j - (PLUMES - 1) / 2) * 7.4 * Math.pow(u, 0.75) * this.tailSpread;
      const s = Math.sin(phase) * amp * u + fan;

      out.x = px + nx * s;
      out.y = py + ny * s;
      return out;
    }

    plumeTip(j) {
      return this.plumePoint(j, 1, { x: 0, y: 0 });
    }

    drawTail(ctx) {
      if (this.trail.length < 4) return;
      const SEG = 16;
      const a = { x: 0, y: 0 };
      const b = { x: 0, y: 0 };

      for (let j = 0; j < PLUMES; j++) {
        const pts = [];
        for (let i = 0; i <= SEG; i++) {
          const u = i / SEG;
          this.plumePoint(j, u, a);
          pts.push({ x: a.x, y: a.y, u });
        }

        // build a tapered ribbon
        ctx.beginPath();
        for (let i = 0; i < pts.length; i++) {
          const p = pts[i];
          const q = pts[Math.min(pts.length - 1, i + 1)];
          const r = pts[Math.max(0, i - 1)];
          const dx = q.x - r.x;
          const dy = q.y - r.y;
          const dl = Math.hypot(dx, dy) || 1;
          // thin at the root, a slight swell two-thirds out, whisker-fine at the tip
          const w = 0.32 + Math.sin(Math.pow(p.u, 0.7) * Math.PI) * 1.05 * (1 - p.u * 0.55);
          p.nx = (-dy / dl) * w * this.scale;
          p.ny = (dx / dl) * w * this.scale;
        }
        ctx.moveTo(pts[0].x + pts[0].nx, pts[0].y + pts[0].ny);
        for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x + pts[i].nx, pts[i].y + pts[i].ny);
        for (let i = pts.length - 1; i >= 0; i--) ctx.lineTo(pts[i].x - pts[i].nx, pts[i].y - pts[i].ny);
        ctx.closePath();

        const tip = pts[pts.length - 1];
        const g = ctx.createLinearGradient(pts[0].x, pts[0].y, tip.x, tip.y);
        const h = this.heat * 0.34;
        g.addColorStop(0, rgba(mix(COL.deep, COL.pale, h), 0.92));
        g.addColorStop(0.3, rgba(mix(COL.scarlet, COL.pale, h), 0.85));
        g.addColorStop(0.66, rgba(mix(COL.ember, COL.white, h), 0.66));
        g.addColorStop(0.88, rgba(mix(COL.gold, COL.white, h), 0.34));
        g.addColorStop(1, rgba(mix(COL.pale, COL.white, h), 0));
        ctx.fillStyle = g;
        ctx.fill();

        // hot core down the shaft
        ctx.save();
        ctx.globalCompositeOperation = 'lighter';
        ctx.strokeStyle = rgba(COL.gold, 0.16 + h * 0.3);
        ctx.lineWidth = 0.5 * this.scale;
        ctx.beginPath();
        ctx.moveTo(pts[0].x, pts[0].y);
        for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y);
        ctx.stroke();
        ctx.restore();
      }
    }

    // ---- wings ---------------------------------------------------------

    // One wing, drawn in body-local space.
    //
    // The trick for a side view: a wing is a flat plane sweeping up and down,
    // so at mid-stroke you see it edge-on and it appears SHORT, while at the
    // top and bottom of the stroke it is broadside and appears long. Skipping
    // that foreshortening is what makes a 2D bird look like a paper cutout.
    drawWing(ctx, depth) {
      const h = this.heat * 0.34;              // colour only warms; the glow does the rest
      const near = depth === 'near';
      const S = A.shoulder;
      const B = A.wingBack;

      // banking shows more of one wing and less of the other
      const bankK = clamp(1 + this.bank * (near ? 0.16 : -0.24), 0.6, 1.28);
      const yOff = near ? 0 : 2.2;
      const shade = near ? 0 : 0.3;
      const alpha = near ? 1 : 0.82;

      // the far wing is seen at a shallower angle, so its arc reads smaller
      const range = 66 * this.wingRangeMul * (near ? 1 : 0.88);
      const f = this.flap;
      const phi = (180 - f * range) * D2R;
      // Edge-on at mid-stroke, broadside at the extremes. A glide holds the
      // wing spread, so it never collapses all the way to a line.
      let foreshorten = 0.42 + 0.58 * Math.min(1, Math.abs(f) * 1.12);
      if (this.gliding) foreshorten = Math.max(foreshorten, 0.68);
      const L = A.wingLen * (near ? 1 : 0.92) * bankK * this.spread * foreshorten;

      const cs = Math.cos(phi), sn = Math.sin(phi);
      const armLen = L * 0.52;
      const wx = S.x + cs * armLen;
      const wy = S.y + sn * armLen + yOff;

      // hand trails behind the arm, more so at the extremes of the stroke
      const handPhi = (180 - f * (range + 22)) * D2R;
      const handLen = L * 0.50 * (1 - 0.34 * this.fold);
      const tx = wx + Math.cos(handPhi) * handLen;
      const ty = wy + Math.sin(handPhi) * handLen;

      // leading edge bows forward of the shoulder-wrist line
      const bow = 2.1 * foreshorten;
      const bx = wx + Math.cos(phi - Math.PI / 2) * bow;
      const by = wy + Math.sin(phi - Math.PI / 2) * bow;

      ctx.save();

      // ---- membrane: coverts + secondaries -------------------------
      ctx.beginPath();
      ctx.moveTo(S.x + 1.6, S.y + yOff);
      ctx.quadraticCurveTo(
        lerp(S.x, bx, 0.5) + Math.cos(phi - Math.PI / 2) * bow * 0.7,
        lerp(S.y + yOff, by, 0.5) + Math.sin(phi - Math.PI / 2) * bow * 0.7,
        bx, by
      );
      // secondaries: a short fan just past the wrist, then the trailing edge
      // sweeps back down to the body. The primaries live beyond this.
      const sx2 = wx + Math.cos(handPhi) * handLen * 0.30;
      const sy2 = wy + Math.sin(handPhi) * handLen * 0.30;
      ctx.lineTo(sx2, sy2);
      ctx.quadraticCurveTo(
        lerp(sx2, B.x, 0.42) - Math.cos(phi - Math.PI / 2) * 3.0,
        lerp(sy2, B.y + yOff, 0.42) - Math.sin(phi - Math.PI / 2) * 3.0,
        B.x, B.y + yOff
      );
      ctx.quadraticCurveTo(-1.5, S.y + 0.6 + yOff, S.x + 1.6, S.y + yOff);
      ctx.closePath();

      const g = ctx.createLinearGradient(S.x, S.y, tx, ty);
      g.addColorStop(0, rgba(mix(mix(COL.deep, COL.char, shade), COL.pale, h), alpha));
      g.addColorStop(0.5, rgba(mix(mix(COL.scarlet, COL.char, shade), COL.pale, h), alpha));
      g.addColorStop(1, rgba(mix(mix(COL.ember, COL.char, shade * 0.8), COL.pale, h), alpha));
      ctx.fillStyle = g;
      ctx.fill();

      // covert row: a soft second layer near the shoulder
      ctx.beginPath();
      ctx.moveTo(S.x + 1.2, S.y + yOff);
      ctx.quadraticCurveTo(lerp(S.x, wx, 0.45), lerp(S.y, wy, 0.45), wx * 0.62, wy * 0.62 + yOff);
      ctx.quadraticCurveTo(-2.4, S.y + 1.6 + yOff, S.x + 1.2, S.y + yOff);
      ctx.closePath();
      ctx.fillStyle = rgba(mix(mix(COL.ember, COL.char, shade), COL.gold, h * 0.6), alpha * 0.55);
      ctx.fill();

      // leading-edge light
      ctx.strokeStyle = rgba(mix(COL.gold, COL.white, h), alpha * 0.55);
      ctx.lineWidth = 0.7;
      ctx.beginPath();
      ctx.moveTo(S.x + 1.6, S.y + yOff);
      ctx.quadraticCurveTo(lerp(S.x, bx, 0.5), lerp(S.y + yOff, by, 0.5), bx, by);
      ctx.stroke();

      // ---- primaries ------------------------------------------------
      // Each primary lags the one inboard of it, so the stroke travels out
      // along the wing as a ripple instead of the whole plank moving at once.
      const N = 6;
      const splay = (1 - this.fold * 0.6) * this.spread;
      for (let i = 0; i < N; i++) {
        const k = i / (N - 1);
        const lag = this.wingPhase - (i + 1) * 0.05;
        const lf = this.gliding && !this.wingHzOverride ? f : flapCurve(lag);
        const lphi = (180 - lf * (range + 22)) * D2R;
        const pAngle = lphi + lerp(-6, 30, k) * splay * D2R;
        const pLen = handLen * lerp(1.15, 0.62, Math.pow(k, 1.35));

        const rx = lerp(wx, tx, 0.02 + k * 0.22);
        const ry = lerp(wy, ty, 0.02 + k * 0.22);
        const px = rx + Math.cos(pAngle) * pLen;
        const py = ry + Math.sin(pAngle) * pLen;

        const w = lerp(1.5, 0.85, k) * (0.55 + 0.45 * foreshorten);
        const nx = -Math.sin(pAngle) * w;
        const ny = Math.cos(pAngle) * w;

        ctx.beginPath();
        ctx.moveTo(rx + nx * 0.7, ry + ny * 0.7);
        ctx.quadraticCurveTo(lerp(rx, px, 0.6) + nx, lerp(ry, py, 0.6) + ny, px, py);
        ctx.quadraticCurveTo(lerp(rx, px, 0.6) - nx * 0.75, lerp(ry, py, 0.6) - ny * 0.75, rx - nx * 0.7, ry - ny * 0.7);
        ctx.closePath();

        const fg = ctx.createLinearGradient(rx, ry, px, py);
        fg.addColorStop(0, rgba(mix(mix(COL.scarlet, COL.char, shade), COL.pale, h), alpha));
        fg.addColorStop(0.6, rgba(mix(mix(COL.ember, COL.char, shade), COL.pale, h), alpha));
        fg.addColorStop(1, rgba(mix(mix(COL.gold, COL.char, shade * 0.5), COL.white, h), alpha * 0.92));
        ctx.fillStyle = fg;
        ctx.fill();
      }
      ctx.restore();
    }

    // ---- body ----------------------------------------------------------

    drawBody(ctx) {
      const h = this.heat * 0.34;

      ctx.beginPath();
      ctx.moveTo(9.6, -4.0);
      ctx.bezierCurveTo(5.2, -7.8, -3.2, -7.4, -9.6, -3.4);
      ctx.bezierCurveTo(-12.4, -2.0, -12.8, 0.2, -10.8, 1.4);
      ctx.bezierCurveTo(-6.6, 5.6, 0.4, 6.6, 5.4, 4.4);
      ctx.bezierCurveTo(8.6, 3.0, 10.4, 0.2, 9.6, -4.0);
      ctx.closePath();

      const g = ctx.createLinearGradient(-12, 2, 10, -5);
      g.addColorStop(0, rgba(mix(COL.deep, COL.white, h)));
      g.addColorStop(0.42, rgba(mix(COL.scarlet, COL.pale, h)));
      g.addColorStop(0.78, rgba(mix(COL.ember, COL.white, h)));
      g.addColorStop(1, rgba(mix(COL.orange, COL.white, h)));
      ctx.fillStyle = g;
      ctx.fill();

      // breast highlight
      ctx.save();
      ctx.globalCompositeOperation = 'lighter';
      const bg = ctx.createRadialGradient(6.5, 0.6, 0.5, 6.5, 0.6, 7);
      bg.addColorStop(0, rgba(COL.gold, 0.42 + h * 0.4));
      bg.addColorStop(1, rgba(COL.gold, 0));
      ctx.fillStyle = bg;
      ctx.beginPath();
      ctx.arc(6.5, 0.6, 7, 0, TAU);
      ctx.fill();
      ctx.restore();

      // a few scapular feather ticks along the back
      ctx.strokeStyle = rgba(mix(COL.char, COL.pale, h * 0.9), 0.28);
      ctx.lineWidth = 0.55;
      for (let i = 0; i < 4; i++) {
        const x = lerp(-7.5, 3.5, i / 3);
        ctx.beginPath();
        ctx.moveTo(x, -5.2 + i * 0.24);
        ctx.quadraticCurveTo(x - 2.2, -3.9, x - 3.4, -3.2);
        ctx.stroke();
      }
    }

    drawHead(ctx) {
      const h = this.heat * 0.34;
      const bobY = this.headBob * 0.5;
      const C = A.headC;

      ctx.save();
      ctx.translate(0, bobY);

      // neck wedge
      ctx.beginPath();
      ctx.moveTo(7.2, -5.4);
      ctx.quadraticCurveTo(10.0, -8.8, 13.4, -8.4);
      ctx.lineTo(13.0, -2.6);
      ctx.quadraticCurveTo(9.6, -1.4, 7.6, -1.6);
      ctx.closePath();
      const ng = ctx.createLinearGradient(7, -4, 13, -6);
      ng.addColorStop(0, rgba(mix(COL.scarlet, COL.pale, h)));
      ng.addColorStop(1, rgba(mix(COL.ember, COL.white, h)));
      ctx.fillStyle = ng;
      ctx.fill();

      // crest — three swept plumes, they lie back with airspeed
      const sweep = 14 * this.crestSway;
      for (let i = 0; i < 3; i++) {
        const k = i / 2;
        const base = 212 + k * 26 + sweep;
        const wob = Math.sin(this.t * 6.4 + i * 1.7) * 5 * (0.3 + this.crestSway * 0.7);
        const ang = (base + wob) * D2R;
        const len = lerp(11.5, 7.5, Math.abs(k - 0.4) * 1.5) * (1 + this.heat * 0.25);
        const rx = A.crest.x - k * 1.8;
        const ry = A.crest.y + k * 1.1;
        const tx = rx + Math.cos(ang) * len;
        const ty = ry + Math.sin(ang) * len;
        const cx = lerp(rx, tx, 0.5) + Math.cos(ang - Math.PI / 2) * 2.4;
        const cy = lerp(ry, ty, 0.5) + Math.sin(ang - Math.PI / 2) * 2.4;

        ctx.beginPath();
        ctx.moveTo(rx + 1.1, ry + 0.5);
        ctx.quadraticCurveTo(cx, cy, tx, ty);
        ctx.quadraticCurveTo(cx - 1.0, cy + 1.4, rx - 1.1, ry + 0.9);
        ctx.closePath();
        const cg = ctx.createLinearGradient(rx, ry, tx, ty);
        cg.addColorStop(0, rgba(mix(COL.scarlet, COL.white, h), 0.95));
        cg.addColorStop(1, rgba(mix(COL.gold, COL.white, h), 0.6));
        ctx.fillStyle = cg;
        ctx.fill();
      }

      // skull
      ctx.beginPath();
      ctx.arc(C.x, C.y, A.headR, 0, TAU);
      const hg = ctx.createRadialGradient(C.x + 1.4, C.y - 1.6, 0.4, C.x, C.y, A.headR * 1.5);
      hg.addColorStop(0, rgba(mix(COL.orange, COL.white, h)));
      hg.addColorStop(0.6, rgba(mix(COL.scarlet, COL.pale, h)));
      hg.addColorStop(1, rgba(mix(COL.deep, COL.pale, h)));
      ctx.fillStyle = hg;
      ctx.fill();

      // beak — gold, with a visible gape line
      ctx.beginPath();
      ctx.moveTo(16.0, -8.0);
      ctx.quadraticCurveTo(19.8, -7.2, A.beakTip.x, A.beakTip.y);
      ctx.quadraticCurveTo(18.6, -4.9, 15.8, -4.4);
      ctx.closePath();
      const bg2 = ctx.createLinearGradient(15.8, -6, A.beakTip.x, A.beakTip.y);
      bg2.addColorStop(0, rgba(mix(COL.gold, COL.white, h)));
      bg2.addColorStop(1, rgba(mix([255, 170, 60], COL.white, h)));
      ctx.fillStyle = bg2;
      ctx.fill();
      ctx.strokeStyle = rgba(mix(COL.char, COL.pale, h), 0.4);
      ctx.lineWidth = 0.5;
      ctx.beginPath();
      ctx.moveTo(15.9, -6.1);
      ctx.quadraticCurveTo(18.8, -6.0, A.beakTip.x, A.beakTip.y);
      ctx.stroke();

      // eye
      const E = A.eye;
      if (this.blink > 0) {
        ctx.strokeStyle = rgba(mix(COL.char, COL.pale, h), 0.85);
        ctx.lineWidth = 0.9;
        ctx.beginPath();
        ctx.arc(E.x, E.y, 1.5, 0.15, Math.PI - 0.15);
        ctx.stroke();
      } else {
        ctx.fillStyle = rgba(mix(COL.gold, COL.white, h));
        ctx.beginPath();
        ctx.arc(E.x, E.y, 1.75, 0, TAU);
        ctx.fill();
        ctx.fillStyle = this.heat > 0.8 ? rgba(COL.white, 0.9) : 'rgba(26,10,6,0.95)';
        ctx.beginPath();
        ctx.arc(E.x + 0.25, E.y, 1.0, 0, TAU);
        ctx.fill();
        ctx.fillStyle = 'rgba(255,255,255,0.95)';
        ctx.beginPath();
        ctx.arc(E.x + 0.65, E.y - 0.55, 0.38, 0, TAU);
        ctx.fill();
      }
      ctx.restore();
    }

    drawLegs(ctx) {
      const k = this.legsOut;
      if (k < 0.02) return;
      const h = this.heat * 0.34;
      const L = A.legRoot;
      const drop = lerp(0.8, 4.2, k);
      const swing = Math.sin(this.t * 3.1) * 0.5 * k;

      ctx.save();
      ctx.strokeStyle = rgba(mix(COL.talon, COL.white, h), 0.95);
      ctx.lineWidth = 1.15;
      ctx.lineCap = 'round';
      for (let s = -1; s <= 1; s += 2) {
        const x = L.x + s * 1.4;
        ctx.beginPath();
        ctx.moveTo(x, L.y);
        ctx.quadraticCurveTo(x - 1.2, L.y + drop * 0.6, x - 2.0 + swing, L.y + drop);
        ctx.stroke();
        // talons
        ctx.lineWidth = 0.8;
        for (let f = -1; f <= 1; f++) {
          ctx.beginPath();
          ctx.moveTo(x - 2.0 + swing, L.y + drop);
          ctx.lineTo(x - 2.0 + swing + f * 1.3, L.y + drop + 1.5);
          ctx.stroke();
        }
        ctx.lineWidth = 1.15;
      }
      ctx.restore();
    }

    drawScroll(ctx) {
      if (!this.carrying) return;
      const L = A.legRoot;
      const y = L.y + lerp(2.6, 5.0, this.legsOut);
      ctx.save();
      ctx.translate(L.x - 1.2, y);
      ctx.rotate(Math.sin(this.t * 2.2) * 0.12 - 0.06);
      ctx.fillStyle = '#d9c69c';
      ctx.beginPath();
      ctx.roundRect(-3.8, -1.15, 7.6, 2.3, 1.1);
      ctx.fill();
      ctx.fillStyle = '#b89f74';
      ctx.beginPath();
      ctx.roundRect(-4.2, -1.5, 1.2, 3.0, 0.6);
      ctx.fill();
      ctx.beginPath();
      ctx.roundRect(3.0, -1.5, 1.2, 3.0, 0.6);
      ctx.fill();
      ctx.fillStyle = '#7e1019';
      ctx.beginPath();
      ctx.arc(0, 0.05, 0.85, 0, TAU);
      ctx.fill();
      ctx.restore();
    }

    // the wavy edge the fire has eaten up to
    clipUnburnt(ctx) {
      if (!this.burning || this.burnT < 0.75) return false;
      const x = this.burnFront;
      ctx.beginPath();
      ctx.moveTo(60, -40);
      ctx.lineTo(60, 40);
      for (let y = 40; y >= -40; y -= 3) {
        const w = Math.sin(y * 0.62 + this.t * 11) * 1.5 + Math.sin(y * 1.7 - this.t * 17) * 0.7;
        ctx.lineTo(x + w, y);
      }
      ctx.closePath();
      ctx.clip();
      return true;
    }

    draw(ctx) {
      if (this.consumed) return;

      // heat haze / halo, in world space so it isn't squashed by the flip
      ctx.save();
      ctx.globalCompositeOperation = 'lighter';
      const gr = (16 + this.heat * 30) * this.scale;
      const pulse = 0.86 + 0.14 * Math.sin(this.t * 7.3);
      const gg = ctx.createRadialGradient(this.x, this.y + this.bob, 0, this.x, this.y + this.bob, gr);
      gg.addColorStop(0, fireColor(0.12, (0.13 + this.heat * 0.34) * pulse));
      gg.addColorStop(0.28, fireColor(0.35, (0.07 + this.heat * 0.2) * pulse));
      gg.addColorStop(0.6, fireColor(0.6, (0.02 + this.heat * 0.07) * pulse));
      gg.addColorStop(1, fireColor(0.9, 0));
      ctx.fillStyle = gg;
      ctx.beginPath();
      ctx.arc(this.x, this.y + this.bob, gr, 0, TAU);
      ctx.fill();
      ctx.restore();

      // tail streams behind everything
      ctx.save();
      if (this.burning && this.burnT >= 0.75) {
        // the tail goes first — it is behind the burn line
        const gone = clamp((this.burnFront - (A.tail - 3)) / 10, 0, 1);
        ctx.globalAlpha = 1 - gone;
      }
      this.drawTail(ctx);
      ctx.restore();

      ctx.save();
      this.applyTransform(ctx);

      ctx.save();
      const clipped = this.clipUnburnt(ctx);

      this.drawWing(ctx, 'far');
      this.drawLegs(ctx);
      this.drawScroll(ctx);
      this.drawBody(ctx);
      this.drawHead(ctx);
      this.drawWing(ctx, 'near');

      if (clipped) {
        // white-hot rim right where the fire is eating
        ctx.save();
        ctx.globalCompositeOperation = 'lighter';
        const fade = clamp((A.nose + 3 - this.burnFront) / 8, 0, 1);
        const rg = ctx.createLinearGradient(this.burnFront - 0.5, 0, this.burnFront + 3.2, 0);
        rg.addColorStop(0, fireColor(0.06, 0.62 * fade));
        rg.addColorStop(0.4, fireColor(0.3, 0.24 * fade));
        rg.addColorStop(1, fireColor(0.7, 0));
        ctx.fillStyle = rg;
        ctx.fillRect(this.burnFront - 1, -16, 4.6, 28);
        ctx.restore();
      }
      ctx.restore();

      ctx.restore();
    }
  }

  NS.Phoenix = Phoenix;
  NS.PHOENIX_ANATOMY = A;
})(window.PX);
