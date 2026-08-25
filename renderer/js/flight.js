'use strict';
window.PX = window.PX || {};

(function (NS) {
  const { TAU, clamp, lerp, damp, angleTowards, wrapAngle, rand, randSym, makeNoise } = NS;

  // ---------------------------------------------------------------------
  // Wingbeat shape.
  //
  // Real birds are not sinusoidal. The downstroke is the power stroke:
  // short, fast, wing fully spread. The upstroke is longer and the wrist
  // flexes so the wing folds and slices back up with less drag.
  //
  // returns -1 (tip at top of upstroke) .. +1 (tip at bottom of downstroke)
  // ---------------------------------------------------------------------
  const DOWN_FRACTION = 0.40;

  function flapCurve(p) {
    p = p - Math.floor(p);
    if (p < DOWN_FRACTION) {
      return -Math.cos(Math.PI * (p / DOWN_FRACTION));         // -1 -> +1, quick
    }
    return Math.cos(Math.PI * ((p - DOWN_FRACTION) / (1 - DOWN_FRACTION))); // +1 -> -1, slower
  }

  // How folded the wrist is. Peaks mid-upstroke, zero through the downstroke.
  function foldCurve(p) {
    p = p - Math.floor(p);
    if (p < DOWN_FRACTION) return 0;
    const t = (p - DOWN_FRACTION) / (1 - DOWN_FRACTION);
    return Math.sin(t * Math.PI) * 0.92;
  }

  class Flyer {
    constructor(o = {}) {
      this.x = o.x || 0;
      this.y = o.y || 0;
      this.vx = o.vx || 0;
      this.vy = o.vy || 0;
      this.ax = 0;
      this.ay = 0;

      this.maxSpeed = o.maxSpeed || 340;
      this.maxForce = o.maxForce || 1500;
      this.scale = o.scale || 1;

      this.heading = o.heading || 0;   // body angle, radians
      this.facing = 1;                 // +1 right, -1 left
      this.facingBlend = 1;            // smoothed mirror for the flip
      this.bank = 0;                   // roll, read as wing foreshortening
      this.pitch = 0;                  // nose-up, added to heading
      this.bob = 0;                    // body rise/fall driven by the wings

      this.wingPhase = rand(1);
      this.wingHz = 7.4;
      this.flap = 0;
      this.fold = 0;
      this.spread = 1;                 // 0 folded to the body, 1 fully out
      this.effort = 1;                 // 0 gliding .. ~1.5 climbing hard

      this.hover = false;
      this.gliding = false;
      this.boundTimer = rand(0.4);
      this.bounding = true;

      this.noise = makeNoise(o.seed || ((Math.random() * 1e9) | 0));
      this.noiseOff = rand(1000);
      this.t = 0;

      this.waypoints = [];
      this.arriveRadius = 150;
      this.wanderGain = o.wanderGain != null ? o.wanderGain : 260;

      this.headBob = 0;
      this.headPhase = rand(TAU);
      this.blinkAt = rand(2, 5);
      this.blink = 0;

      this._m = { c: 1, s: 0 };
    }

    // ---- steering -----------------------------------------------------

    force(fx, fy) { this.ax += fx; this.ay += fy; }

    get speed() { return Math.hypot(this.vx, this.vy); }

    goTo(list) {
      this.waypoints = list.slice();
      return this;
    }

    get target() { return this.waypoints[0] || null; }

    seek(tx, ty, slowRadius) {
      const dx = tx - this.x;
      const dy = ty - this.y;
      const d = Math.hypot(dx, dy) || 1e-6;
      let want = this.maxSpeed;
      if (slowRadius && d < slowRadius) want = this.maxSpeed * (d / slowRadius);
      const dvx = (dx / d) * want - this.vx;
      const dvy = (dy / d) * want - this.vy;
      const m = Math.hypot(dvx, dvy) || 1e-6;
      const k = Math.min(this.maxForce, m * 5.5) / m;
      this.force(dvx * k, dvy * k);
      return d;
    }

    // Birds never track a straight line. Low-frequency noise on both axes
    // gives the drifting, air-pushed-around quality.
    wander(gain = this.wanderGain) {
      const n = this.noise;
      this.force(
        n(this.t * 0.55 + this.noiseOff) * gain,
        n(this.t * 0.73 + this.noiseOff + 77) * gain * 0.85
      );
    }

    // ---- integration --------------------------------------------------

    update(dt) {
      this.t += dt;

      // waypoint following with an arrival slow-down on the last one
      const wp = this.target;
      if (wp) {
        const last = this.waypoints.length === 1;
        const d = this.seek(wp.x, wp.y, last ? (wp.slow || this.arriveRadius) : 0);
        const hit = wp.radius || (last ? 26 : 120);
        if (d < hit && !last) this.waypoints.shift();
      }

      if (this.wanderGain) this.wander();

      // integrate
      this.vx += this.ax * dt;
      this.vy += this.ay * dt;

      const sp = Math.hypot(this.vx, this.vy);
      const cap = this.hover ? this.maxSpeed * 0.45 : this.maxSpeed;
      if (sp > cap) {
        this.vx = (this.vx / sp) * cap;
        this.vy = (this.vy / sp) * cap;
      }

      this.x += this.vx * dt;
      this.y += this.vy * dt;

      this.updatePose(dt);
      this.updateWings(dt);

      this.ax = 0;
      this.ay = 0;
    }

    updatePose(dt) {
      const sp = this.speed;

      // facing flips with hysteresis so a hovering bird doesn't strobe
      if (this.vx > 22) this.facing = 1;
      else if (this.vx < -22) this.facing = -1;
      this.facingBlend = damp(this.facingBlend, this.facing, 9, dt);

      // heading follows velocity, but clamped so the bird stays readable
      // rather than flying nose-down like a dart
      let want = 0;
      if (sp > 26) {
        want = clamp(Math.atan2(this.vy, Math.abs(this.vx)), -0.85, 0.85) * 0.72;
      }
      this.heading = angleTowards(this.heading, want + this.pitch, 7, dt);

      // bank: lateral acceleration relative to the flight direction
      if (sp > 30) {
        const ux = this.vx / sp;
        const uy = this.vy / sp;
        const lateral = this.ax * -uy + this.ay * ux;   // cross product z
        this.bank = damp(this.bank, clamp(lateral / this.maxForce, -1, 1), 5, dt);
      } else {
        this.bank = damp(this.bank, 0, 4, dt);
      }

      // head lags the body and adds its own tiny bob — birds stabilise the head
      this.headPhase += dt * (3.2 + this.effort * 2.6);
      this.headBob = damp(
        this.headBob,
        Math.sin(this.headPhase) * 0.5 - this.bob * 0.35,
        14, dt
      );

      // blink
      this.blinkAt -= dt;
      if (this.blinkAt <= 0) {
        this.blink = 0.13;
        this.blinkAt = rand(2.2, 6.5);
      }
      if (this.blink > 0) this.blink -= dt;
    }

    updateWings(dt) {
      const sp = this.speed;
      const accel = Math.hypot(this.ax, this.ay);
      const climb = -this.vy / this.maxSpeed;      // positive = going up

      let want = 0.34 + (accel / this.maxForce) * 0.95 + Math.max(0, climb) * 0.75;
      if (this.hover) want = 1.1;
      this.effort = damp(this.effort, clamp(want, 0.12, 1.5), 6, dt);

      // Bounding flight: at cruise a small bird flaps in bursts, then folds
      // up and coasts on the momentum it just bought. Cheap for the bird,
      // and it's what makes the flight path undulate.
      if (this.bounding && !this.hover) {
        this.boundTimer -= dt;
        if (this.boundTimer <= 0) {
          if (this.gliding) {
            this.gliding = false;
            this.boundTimer = rand(0.34, 0.62);
          } else if (this.effort < 0.62 && sp > this.maxSpeed * 0.34) {
            this.gliding = true;
            this.boundTimer = rand(0.20, 0.42);
          } else {
            this.boundTimer = rand(0.25, 0.5);
          }
        }
      } else {
        this.gliding = false;
      }

      const targetSpread = this.gliding ? 0.88 : 1;
      this.spread = damp(this.spread, targetSpread, 9, dt);

      if (this.gliding) {
        // wings held just past level, body sags a touch under gravity
        this.flap = damp(this.flap, 0.18, 8, dt);
        this.fold = damp(this.fold, 0.06, 8, dt);
        this.bob = damp(this.bob, 0.9, 6, dt);
        this.vy += 130 * dt;                       // the sink half of a bound
      } else {
        this.wingHz = this.hover
          ? lerp(9.2, 11.4, clamp(this.effort - 0.9, 0, 1))
          : lerp(5.4, 9.6, clamp(this.effort / 1.3, 0, 1));

        this.wingPhase += this.wingHz * dt;
        this.flap = flapCurve(this.wingPhase);
        this.fold = foldCurve(this.wingPhase);

        // Body rises on the downstroke. Amplitude scales with effort, so a
        // hard-climbing bird visibly porpoises and a cruising one barely does.
        const amp = lerp(0.7, 2.6, clamp(this.effort, 0, 1.3)) * this.scale;
        this.bob = damp(this.bob, -this.flap * amp, 22, dt);
      }
    }

    // ---- transforms ---------------------------------------------------

    // The bird art is drawn facing +x. Flying left mirrors on X (never on Y —
    // that would turn it upside down) and negates the heading, so "nose down"
    // still means nose down. facingBlend runs smoothly through zero, which
    // squashes the bird edge-on for a frame or two: that is the turn.
    xScale() {
      const fb = clamp(this.facingBlend, -1, 1);
      const sgn = fb < 0 ? -1 : 1;
      return sgn * Math.max(0.2, Math.abs(fb));
    }

    applyTransform(ctx) {
      const sx = this.xScale();
      const sgn = sx < 0 ? -1 : 1;
      ctx.translate(this.x, this.y + this.bob);
      ctx.rotate(this.heading * sgn);
      ctx.scale(this.scale * sx, this.scale);
      this._m.sgn = sgn;
      this._m.sx = sx;
      return sgn;
    }

    // local point -> world point, matching applyTransform exactly
    toWorld(lx, ly) {
      const sx = this.xScale();
      const sgn = sx < 0 ? -1 : 1;
      const px = lx * this.scale * sx;
      const py = ly * this.scale;
      const a = this.heading * sgn;
      const c = Math.cos(a);
      const s = Math.sin(a);
      return {
        x: this.x + px * c - py * s,
        y: this.y + this.bob + px * s + py * c
      };
    }
  }

  NS.Flyer = Flyer;
  NS.flapCurve = flapCurve;
  NS.foldCurve = foldCurve;
})(window.PX);
