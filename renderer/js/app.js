'use strict';

(function (NS) {
  const { clamp, lerp, rand, randSym, pick, TAU, easeOutCubic, fireColor, damp } = NS;

  const canvas = document.getElementById('stage');
  const ctx = canvas.getContext('2d');
  const layer = document.getElementById('dom-layer');

  let W = 0, H = 0, DPR = 1;

  function resize() {
    DPR = Math.min(2, window.devicePixelRatio || 1);
    W = window.innerWidth;
    H = window.innerHeight;
    canvas.width = Math.round(W * DPR);
    canvas.height = Math.round(H * DPR);
    canvas.style.width = W + 'px';
    canvas.style.height = H + 'px';
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
  }
  window.addEventListener('resize', resize);
  resize();

  const particles = new NS.Particles(3000);
  const letter = new NS.Letter(layer);

  // ------------------------------------------------------------- scene ----

  const S = {
    ENTER: 'enter',
    SETTLE: 'settle',
    DROP: 'drop',
    WATCH: 'watch',
    BURN: 'burn',
    ASH: 'ash',
    READING: 'reading',
    DONE: 'done'
  };

  class Scene {
    constructor(data) {
      this.data = data;
      this.state = S.ENTER;
      this.clock = 0;
      this.flash = 0;
      this.bloom = 0;
      this.scroll = null;
      this.done = false;

      const target = letter.plannedRect();
      this.landing = { x: target.x + target.w / 2, y: target.y + 18 };
      this.perch = { x: this.landing.x + randSym(26), y: this.landing.y - 112 };

      const entry = this.pickEntry();
      this.bird = new NS.Phoenix({
        x: entry.x,
        y: entry.y,
        scale: 1.35,
        maxSpeed: 340,
        maxForce: 1700
      });
      this.bird.vx = entry.vx;
      this.bird.vy = entry.vy;
      this.bird.heading = Math.atan2(entry.vy, Math.abs(entry.vx));
      this.bird.facing = entry.vx >= 0 ? 1 : -1;
      this.bird.facingBlend = this.bird.facing;

      this.bird.goTo([
        { x: entry.mid.x, y: entry.mid.y, radius: 150 },
        { x: this.perch.x, y: this.perch.y, slow: 210, radius: 26 }
      ]);
    }

    // comes in off any edge, aimed inward but never straight at the target,
    // so the approach reads as a curve
    pickEntry() {
      const pad = 170;
      const edge = pick(['left', 'right', 'top', 'top', 'left', 'right', 'bottom']);
      let x, y;
      if (edge === 'left')  { x = -pad;     y = rand(H * 0.12, H * 0.78); }
      if (edge === 'right') { x = W + pad;  y = rand(H * 0.12, H * 0.78); }
      if (edge === 'top')   { x = rand(W * 0.1, W * 0.9); y = -pad; }
      if (edge === 'bottom'){ x = rand(W * 0.1, W * 0.9); y = H + pad; }

      const dx = this.perch.x - x;
      const dy = this.perch.y - y;
      const d = Math.hypot(dx, dy) || 1;
      const ux = dx / d, uy = dy / d;

      // swing wide: the mid waypoint is pushed off the direct line
      const side = Math.random() < 0.5 ? 1 : -1;
      const swing = rand(0.22, 0.42) * d * side;
      const mid = {
        x: x + ux * d * 0.55 - uy * swing,
        y: y + uy * d * 0.55 + ux * swing
      };

      const sp = 300;
      return {
        x, y, mid,
        vx: (mid.x - x) / Math.hypot(mid.x - x, mid.y - y) * sp,
        vy: (mid.y - y) / Math.hypot(mid.x - x, mid.y - y) * sp
      };
    }

    update(dt) {
      this.clock += dt;
      const b = this.bird;

      switch (this.state) {
        case S.ENTER: {
          const d = Math.hypot(b.x - this.perch.x, b.y - this.perch.y);
          if (d < 46 && b.speed < 150) {
            this.state = S.SETTLE;
            this.clock = 0;
          }
          break;
        }

        case S.SETTLE: {
          // flare: nose up, wings beating hard, bleeding off speed
          b.hover = true;
          b.wanderGain = 90;
          b.pitch = damp(b.pitch, -0.22, 4, dt);
          b.maxSpeed = damp(b.maxSpeed, 150, 3, dt);
          // gentle figure-eight so it never looks pinned in place
          b.force(Math.sin(b.t * 1.7) * 130, Math.cos(b.t * 2.6) * 90);
          if (this.clock > 0.95) {
            this.state = S.DROP;
            this.clock = 0;
            this.release();
          }
          break;
        }

        case S.DROP: {
          b.hover = true;
          b.force(Math.sin(b.t * 1.7) * 110, Math.cos(b.t * 2.6) * 80 - 60);
          if (this.scroll) this.updateScroll(dt);
          if (!this.scroll && this.clock > 0.25) {
            this.state = S.WATCH;
            this.clock = 0;
          }
          break;
        }

        case S.WATCH: {
          b.hover = true;
          b.force(Math.sin(b.t * 1.4) * 90, Math.cos(b.t * 2.1) * 70 - 90);
          if (this.clock > 0.9) {
            this.state = S.BURN;
            this.clock = 0;
            b.ignite();
          }
          break;
        }

        case S.BURN: {
          b.hover = true;
          b.wanderGain = 40;
          if (!b.flashed && b.burnT >= 1.95) {
            b.flashed = true;
            this.ignitionFlash();
          }
          if (b.consumed && this.clock > 2.2) {
            this.state = S.ASH;
            this.clock = 0;
          }
          break;
        }

        case S.ASH: {
          if (this.clock > 2.6 || particles.count < 40) {
            this.state = letter.open ? S.READING : S.DONE;
            this.clock = 0;
          }
          break;
        }

        case S.READING: {
          if (!letter.open) {
            this.state = S.DONE;
            this.clock = 0;
          }
          break;
        }

        case S.DONE: {
          if (this.clock > 0.9 && particles.count === 0) this.done = true;
          break;
        }
      }

      if (!b.consumed) b.update(dt, particles);
      particles.update(dt);

      this.flash = Math.max(0, this.flash - dt * 2.6);
      this.bloom = Math.max(0, this.bloom - dt * 1.1);
    }

    release() {
      const p = this.bird.releasePoint();
      this.bird.carrying = false;
      this.scroll = {
        x: p.x, y: p.y,
        vx: this.bird.vx * 0.3 + randSym(14),
        vy: 40,
        rot: rand(-0.3, 0.3),
        rotV: randSym(2.4),
        t: 0
      };
      for (let i = 0; i < 8; i++) particles.ember(p.x, p.y, { spread: 40, ttl: rand(0.4, 0.9) });
    }

    updateScroll(dt) {
      const s = this.scroll;
      s.t += dt;
      s.vy += 900 * dt;
      s.vx *= Math.exp(-1.2 * dt);
      s.x += s.vx * dt;
      s.y += s.vy * dt;
      s.rot += s.rotV * dt;
      s.rotV *= Math.exp(-1.4 * dt);

      if (s.y >= this.landing.y) {
        this.scroll = null;
        const r = letter.show(this.data);
        letter.onDismiss = () => {};
        // a puff of dust and a couple of sparks as it settles
        for (let i = 0; i < 10; i++) {
          particles.smoke(this.landing.x + randSym(30), r.y + 8, { r: rand(4, 10), alpha: 0.08 });
        }
        for (let i = 0; i < 12; i++) {
          particles.spark(this.landing.x + randSym(20), r.y + 6, { spread: 90 });
        }
      }
    }

    ignitionFlash() {
      const b = this.bird;
      const x = b.x, y = b.y + b.bob;

      this.flash = 1;
      this.bloom = 1;
      particles.ring(x, y, { r: 8, r1: Math.max(W, H) * 0.24, w0: 10, ttl: 0.8, hue: 0.04 });
      particles.ring(x, y, { r: 4, r1: 150, w0: 4, ttl: 0.46, hue: 0.02 });

      for (let i = 0; i < 190; i++) {
        const a = rand(TAU);
        const sp = rand(40, 420) * (0.4 + Math.random());
        particles.ember(x + Math.cos(a) * rand(0, 10), y + Math.sin(a) * rand(0, 8), {
          vx: Math.cos(a) * sp,
          vy: Math.sin(a) * sp - rand(20, 90),
          ttl: rand(1.1, 2.6),
          r: rand(0.6, 2.1),
          spread: 10
        });
      }
      for (let i = 0; i < 60; i++) {
        const a = rand(TAU);
        particles.spark(x, y, { spread: 520 });
      }
      for (let i = 0; i < 34; i++) {
        particles.fire(x + randSym(16), y + randSym(12), { r: rand(4, 11), ttl: rand(0.4, 0.9) });
      }
      for (let i = 0; i < 110; i++) {
        particles.ash(x + randSym(26), y + randSym(20), {
          vx: randSym(90), vy: -rand(30, 200),
          ttl: rand(2.6, 5.4), glow: Math.random() < 0.35 ? rand(0.3, 0.9) : 0
        });
      }
      for (let i = 0; i < 16; i++) {
        particles.smoke(x + randSym(18), y + randSym(14), { r: rand(7, 16), alpha: rand(0.08, 0.18) });
      }

      // the paper takes the heat
      letter.scorch();
      const r = letter.rect();
      if (r) {
        for (let i = 0; i < 22; i++) {
          particles.ash(r.left + rand(0, r.width), r.top - rand(0, 40), {
            vx: randSym(20), vy: rand(4, 26), ttl: rand(2.2, 4.4)
          });
        }
      }
    }

    draw() {
      ctx.clearRect(0, 0, W, H);

      // warm bloom washing the desktop at the moment it goes up
      if (this.bloom > 0.001) {
        const b = this.bird;
        const g = ctx.createRadialGradient(b.x, b.y, 0, b.x, b.y, Math.max(W, H) * 0.75);
        const a = Math.pow(this.bloom, 2.2);
        g.addColorStop(0, fireColor(0.1, 0.42 * a));
        g.addColorStop(0.28, fireColor(0.35, 0.2 * a));
        g.addColorStop(1, fireColor(0.9, 0));
        ctx.save();
        ctx.globalCompositeOperation = 'lighter';
        ctx.fillStyle = g;
        ctx.fillRect(0, 0, W, H);
        ctx.restore();
      }

      this.bird.draw(ctx);
      this.drawScroll();
      particles.draw(ctx);

      if (this.flash > 0.001) {
        const b = this.bird;
        ctx.save();
        ctx.globalCompositeOperation = 'lighter';
        const r = lerp(30, 260, 1 - this.flash);
        const g = ctx.createRadialGradient(b.x, b.y, 0, b.x, b.y, r);
        g.addColorStop(0, `rgba(255,252,240,${this.flash})`);
        g.addColorStop(0.5, `rgba(255,206,120,${this.flash * 0.6})`);
        g.addColorStop(1, 'rgba(255,140,40,0)');
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(b.x, b.y, r, 0, TAU);
        ctx.fill();
        ctx.restore();
      }
    }

    drawScroll() {
      const s = this.scroll;
      if (!s) return;
      ctx.save();
      ctx.translate(s.x, s.y);
      ctx.rotate(s.rot);
      ctx.scale(1.7, 1.7);
      ctx.shadowColor = 'rgba(0,0,0,0.35)';
      ctx.shadowBlur = 6;
      ctx.shadowOffsetY = 2;
      ctx.fillStyle = '#e8d7ad';
      ctx.beginPath();
      ctx.roundRect(-5.2, -1.7, 10.4, 3.4, 1.6);
      ctx.fill();
      ctx.shadowColor = 'transparent';
      ctx.fillStyle = '#cbb488';
      ctx.beginPath();
      ctx.roundRect(-5.8, -2.2, 1.7, 4.4, 0.85);
      ctx.fill();
      ctx.beginPath();
      ctx.roundRect(4.1, -2.2, 1.7, 4.4, 0.85);
      ctx.fill();
      ctx.fillStyle = '#8d1220';
      ctx.beginPath();
      ctx.arc(0, 0.1, 1.3, 0, TAU);
      ctx.fill();
      ctx.restore();
    }
  }

  // -------------------------------------------------------------- loop ----

  let scene = null;
  let raf = 0;
  let last = 0;

  function frame(ts) {
    if (!scene) { raf = 0; return; }
    const dt = Math.min(0.05, last ? (ts - last) / 1000 : 1 / 60);
    last = ts;

    scene.update(dt);
    scene.draw();

    if (scene.done) {
      scene = null;
      raf = 0;
      last = 0;
      ctx.clearRect(0, 0, W, H);
      particles.clear();
      setInteractive(false);
      publishHitRect();
      if (window.aviary) window.aviary.sceneDone();
      return;
    }
    raf = requestAnimationFrame(frame);
  }

  function start(data) {
    letter.hide();
    particles.clear();
    resize();
    scene = new Scene(data);
    last = 0;
    if (!raf) raf = requestAnimationFrame(frame);
  }

  // ------------------------------------------------ mouse pass-through ----
  // The overlay covers the whole screen, so it stays click-through except
  // for the moments the pointer is genuinely over the letter.

  let interactive = false;
  function setInteractive(on) {
    if (on === interactive) return;
    interactive = on;
    if (window.aviary) window.aviary.setInteractive(on);
  }

  // Hand the letter's bounds to the main process, which watches the cursor
  // against them. Linux never forwards mouse events to an ignoring window,
  // so the renderer cannot be the one deciding this.
  let lastRect = '';
  function publishHitRect() {
    const r = letter.rect();
    const payload = r
      ? { x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width), h: Math.round(r.height) }
      : null;
    const key = JSON.stringify(payload);
    if (key === lastRect) return;
    lastRect = key;
    if (window.aviary) window.aviary.setHitRect(payload);
  }
  setInterval(publishHitRect, 200);

  // On Windows/macOS events are forwarded, so react instantly there too.
  window.addEventListener('mousemove', (e) => {
    const r = letter.rect();
    const inside = !!r &&
      e.clientX >= r.left && e.clientX <= r.right &&
      e.clientY >= r.top && e.clientY <= r.bottom;
    setInteractive(inside);
  });

  // ------------------------------------------------------------ wiring ----

  if (window.aviary) {
    window.aviary.onDeliver(start);
  }

  // dev harness: open renderer/index.html in a browser and press space
  window.addEventListener('keydown', (e) => {
    if (e.code === 'Space' && !window.aviary) {
      start({ text: 'test flight', from: 'dev', bird: 'phoenix' });
    }
  });

  window.PX_start = start;

  // Deterministic driver — used by scripts/capture.js to step the scene at a
  // fixed dt and grab frames. Nothing in the app path touches it.
  window.PX_test = {
    begin(data) {
      letter.hide();
      particles.clear();
      resize();
      scene = new Scene(data);
      if (raf) cancelAnimationFrame(raf);
      raf = 0;
      last = 0;
      return true;
    },
    step(dt, times) {
      if (!scene) return null;
      for (let i = 0; i < (times || 1); i++) scene.update(dt);
      scene.draw();
      return scene.state;
    },
    info() {
      if (!scene) return null;
      const b = scene.bird;
      return {
        state: scene.state,
        x: Math.round(b.x), y: Math.round(b.y),
        speed: Math.round(b.speed),
        burnT: +b.burnT.toFixed(2),
        heat: +b.heat.toFixed(2),
        consumed: b.consumed,
        particles: particles.count,
        letterOpen: letter.open,
        done: scene.done
      };
    },
    skipTo(state, maxSteps) {
      const dt = 1 / 60;
      let n = 0;
      while (scene && scene.state !== state && n < (maxSteps || 3000)) {
        scene.update(dt);
        n++;
      }
      if (scene) scene.draw();
      return n;
    }
  };
})(window.PX);
