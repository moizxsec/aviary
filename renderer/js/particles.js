'use strict';
window.PX = window.PX || {};

(function (NS) {
  const { clamp, lerp, rand, randSym, fireColor, TAU } = NS;

  // One pooled system for every effect the birds throw off.
  // kinds: fire | ember | spark | ash | smoke | ring | dust
  class Particles {
    constructor(max = 2600) {
      this.max = max;
      this.list = [];
    }

    get count() { return this.list.length; }

    clear() { this.list.length = 0; }

    spawn(p) {
      if (this.list.length >= this.max) return null;
      p.life = 0;
      p.dead = false;
      this.list.push(p);
      return p;
    }

    fire(x, y, opts = {}) {
      return this.spawn({
        kind: 'fire', x, y,
        vx: (opts.vx || 0) + randSym(34),
        vy: (opts.vy || 0) - rand(22, 78),
        r: opts.r || rand(1.6, 4.4),
        ttl: opts.ttl || rand(0.34, 0.78),
        buoy: opts.buoy != null ? opts.buoy : rand(120, 260),
        drag: 2.2,
        wob: rand(TAU),
        wobRate: rand(5, 11),
        heat0: opts.heat0 != null ? opts.heat0 : rand(0, 0.16)
      });
    }

    ember(x, y, opts = {}) {
      return this.spawn({
        kind: 'ember', x, y,
        vx: (opts.vx || 0) + randSym(opts.spread || 46),
        vy: (opts.vy || 0) - rand(30, 130),
        r: opts.r || rand(0.7, 1.9),
        ttl: opts.ttl || rand(0.9, 2.1),
        buoy: rand(30, 95),
        grav: rand(60, 150),
        drag: 0.9,
        wob: rand(TAU),
        wobRate: rand(3, 8),
        wobAmp: rand(14, 46),
        heat0: rand(0, 0.2)
      });
    }

    spark(x, y, opts = {}) {
      return this.spawn({
        kind: 'spark', x, y,
        vx: (opts.vx || 0) + randSym(opts.spread || 240),
        vy: (opts.vy || 0) + randSym(opts.spread || 240) - 40,
        r: rand(0.5, 1.2),
        ttl: rand(0.18, 0.5),
        drag: 3.4,
        grav: 240,
        len: rand(3, 9)
      });
    }

    ash(x, y, opts = {}) {
      return this.spawn({
        kind: 'ash', x, y,
        vx: (opts.vx || 0) + randSym(26),
        vy: (opts.vy || 0) - rand(10, 60),
        r: opts.r || rand(0.9, 2.6),
        ttl: opts.ttl || rand(2.4, 5.2),
        grav: rand(9, 26),
        drag: 1.6,
        wob: rand(TAU),
        wobRate: rand(0.7, 2.1),
        wobAmp: rand(16, 52),
        spin: randSym(3),
        rot: rand(TAU),
        glow: opts.glow || 0
      });
    }

    smoke(x, y, opts = {}) {
      return this.spawn({
        kind: 'smoke', x, y,
        vx: (opts.vx || 0) + randSym(14),
        vy: (opts.vy || 0) - rand(14, 44),
        r: opts.r || rand(6, 15),
        grow: rand(16, 38),
        ttl: opts.ttl || rand(1.2, 2.6),
        drag: 1.1,
        wob: rand(TAU),
        wobRate: rand(0.5, 1.4),
        wobAmp: rand(8, 24),
        alpha: opts.alpha || rand(0.07, 0.17)
      });
    }

    ring(x, y, opts = {}) {
      return this.spawn({
        kind: 'ring', x, y,
        r: opts.r || 6,
        r1: opts.r1 || 320,
        w0: opts.w0 || 9,
        ttl: opts.ttl || 0.7,
        hue: opts.hue || 0.08
      });
    }

    update(dt) {
      const list = this.list;
      let w = 0;
      for (let i = 0; i < list.length; i++) {
        const p = list[i];
        p.life += dt;
        if (p.life >= p.ttl) continue;

        if (p.kind !== 'ring') {
          if (p.drag) {
            const k = Math.exp(-p.drag * dt);
            p.vx *= k;
            p.vy *= k;
          }
          if (p.buoy) p.vy -= p.buoy * dt;
          if (p.grav) p.vy += p.grav * dt;
          if (p.wobAmp) {
            p.wob += p.wobRate * dt;
            p.x += Math.sin(p.wob) * p.wobAmp * dt;
          } else if (p.wobRate) {
            p.wob += p.wobRate * dt;
          }
          if (p.spin) p.rot += p.spin * dt;
          p.x += p.vx * dt;
          p.y += p.vy * dt;
          if (p.grow) p.r += p.grow * dt;
        }

        list[w++] = p;
      }
      list.length = w;
    }

    draw(ctx) {
      const list = this.list;
      if (!list.length) return;

      // pass 1 — normal blend: smoke behind, then ash
      ctx.save();
      ctx.globalCompositeOperation = 'source-over';
      for (let i = 0; i < list.length; i++) {
        const p = list[i];
        if (p.kind !== 'smoke') continue;
        const t = p.life / p.ttl;
        const a = p.alpha * Math.sin(Math.min(1, t * 1.4) * Math.PI) ;
        if (a <= 0.002) continue;
        const g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, p.r);
        g.addColorStop(0, `rgba(48,40,36,${a})`);
        g.addColorStop(0.55, `rgba(34,28,26,${a * 0.6})`);
        g.addColorStop(1, 'rgba(24,20,19,0)');
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.r, 0, TAU);
        ctx.fill();
      }

      for (let i = 0; i < list.length; i++) {
        const p = list[i];
        if (p.kind !== 'ash') continue;
        const t = p.life / p.ttl;
        const a = (1 - t) * (t < 0.08 ? t / 0.08 : 1);
        ctx.save();
        ctx.translate(p.x, p.y);
        ctx.rotate(p.rot);
        // flake: a squashed, slightly irregular chip of soot
        ctx.globalAlpha = a * 0.85;
        ctx.fillStyle = '#332f2c';
        ctx.beginPath();
        ctx.ellipse(0, 0, p.r, p.r * 0.52, 0, 0, TAU);
        ctx.fill();
        ctx.globalAlpha = a * 0.5;
        ctx.fillStyle = '#5b534c';
        ctx.beginPath();
        ctx.ellipse(-p.r * 0.2, -p.r * 0.16, p.r * 0.5, p.r * 0.24, 0, 0, TAU);
        ctx.fill();
        ctx.restore();

        // a dying coal still trapped in the flake
        if (p.glow > 0) {
          const heat = clamp(p.glow * (1 - t) * (0.6 + 0.4 * Math.sin(p.life * 9 + p.wob)), 0, 1);
          if (heat > 0.02) {
            ctx.save();
            ctx.globalCompositeOperation = 'lighter';
            ctx.globalAlpha = heat;
            ctx.fillStyle = fireColor(0.55, 1);
            ctx.beginPath();
            ctx.arc(p.x, p.y, p.r * 0.45, 0, TAU);
            ctx.fill();
            ctx.restore();
          }
        }
      }
      ctx.restore();

      // pass 2 — additive: fire, embers, sparks, shockwaves
      ctx.save();
      ctx.globalCompositeOperation = 'lighter';

      for (let i = 0; i < list.length; i++) {
        const p = list[i];
        const t = p.life / p.ttl;

        if (p.kind === 'fire') {
          const heat = clamp(p.heat0 + t * 0.95, 0, 1);
          const a = (1 - t) * (1 - t) * 0.9;
          const r = p.r * (1 + t * 0.9) * (1 - t * 0.35);
          const wob = Math.sin(p.wob + p.life * p.wobRate) * r * 0.25;
          const g = ctx.createRadialGradient(p.x + wob, p.y, 0, p.x + wob, p.y, r * 2.1);
          g.addColorStop(0, fireColor(heat * 0.4, a));
          g.addColorStop(0.4, fireColor(heat, a * 0.55));
          g.addColorStop(1, fireColor(1, 0));
          ctx.fillStyle = g;
          ctx.beginPath();
          ctx.arc(p.x + wob, p.y, r * 2.1, 0, TAU);
          ctx.fill();
        } else if (p.kind === 'ember') {
          const heat = clamp(p.heat0 + t * 1.05, 0, 1);
          const flick = 0.65 + 0.35 * Math.sin(p.life * (9 + p.wobRate * 2) + p.wob);
          const a = (1 - t) * flick;
          const r = p.r * (1 - t * 0.4);
          const g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, r * 4);
          g.addColorStop(0, fireColor(heat * 0.35, a));
          g.addColorStop(0.3, fireColor(heat, a * 0.5));
          g.addColorStop(1, fireColor(0.95, 0));
          ctx.fillStyle = g;
          ctx.beginPath();
          ctx.arc(p.x, p.y, r * 4, 0, TAU);
          ctx.fill();
        } else if (p.kind === 'spark') {
          const a = (1 - t);
          const sp = Math.hypot(p.vx, p.vy);
          const ux = sp > 1 ? p.vx / sp : 1;
          const uy = sp > 1 ? p.vy / sp : 0;
          const len = p.len * clamp(sp / 260, 0.2, 1.6);
          ctx.strokeStyle = fireColor(0.12 + t * 0.5, a);
          ctx.lineWidth = p.r * 1.4;
          ctx.lineCap = 'round';
          ctx.beginPath();
          ctx.moveTo(p.x, p.y);
          ctx.lineTo(p.x - ux * len, p.y - uy * len);
          ctx.stroke();
        } else if (p.kind === 'ring') {
          // Three feathered passes: a single hard stroke reads as a geometry
          // artefact, not as heat moving through air.
          const e = 1 - Math.pow(1 - t, 3);
          const r = lerp(p.r, p.r1, e);
          const a = Math.pow(1 - t, 2.4) * 0.34;
          const w = Math.max(0.5, p.w0 * (1 - e));
          for (let k = -1; k <= 1; k++) {
            ctx.strokeStyle = fireColor(p.hue + t * 0.5, a * (k === 0 ? 1 : 0.4));
            ctx.lineWidth = w * (k === 0 ? 1 : 1.9);
            ctx.beginPath();
            ctx.arc(p.x, p.y, Math.max(0.5, r + k * w * 1.7), 0, TAU);
            ctx.stroke();
          }
        }
      }
      ctx.restore();
    }
  }

  NS.Particles = Particles;
})(window.PX);
