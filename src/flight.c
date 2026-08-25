#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------------
 * Wingbeat shape.
 *
 * Real birds are not sinusoidal. The downstroke is the power stroke: short,
 * fast, wing fully spread. The upstroke is longer and the wrist flexes so the
 * wing folds and slices back up with less drag.
 *
 * returns -1 (tip at top of upstroke) .. +1 (tip at bottom of downstroke)
 * ------------------------------------------------------------------- */
#define DOWN_FRACTION 0.40

double flap_curve(double p) {
  p = p - floor(p);
  if (p < DOWN_FRACTION) return -cos(M_PI * (p / DOWN_FRACTION));
  return cos(M_PI * ((p - DOWN_FRACTION) / (1 - DOWN_FRACTION)));
}

/* How folded the wrist is. Peaks mid-upstroke, zero through the downstroke. */
double fold_curve(double p) {
  p = p - floor(p);
  if (p < DOWN_FRACTION) return 0;
  return sin(((p - DOWN_FRACTION) / (1 - DOWN_FRACTION)) * M_PI) * 0.92;
}

/* ---------------------------------------------------------------------
 * Wing geometry.
 *
 * The trick for a side view: a wing is a flat plane sweeping up and down, so
 * at mid-stroke you see it edge-on and it appears SHORT, while at the top and
 * bottom of the stroke it is broadside and appears long. Skipping that
 * foreshortening is what makes a 2D bird look like a paper cutout.
 *
 * The hand trails past the arm at both extremes, because the wingtip traces a
 * figure-eight and travels further than the wrist.
 * ------------------------------------------------------------------- */
void wing_pose(const Flyer *f, double range_deg, double sweep_extra,
               double wing_len, double len_k,
               double sh_x, double sh_y, double y_off, WingPose *w) {
  double fl = f->flap;

  w->fore = 0.42 + 0.58 * fmin(1.0, fabs(fl) * 1.12);
  if (f->gliding) w->fore = fmax(w->fore, 0.68);   /* a glide holds it spread */
  if (f->fore_floor > 0) w->fore = fmax(w->fore, f->fore_floor);

  w->L   = wing_len * len_k * f->spread * w->fore;
  w->phi = (180 - fl * range_deg) * (M_PI / 180.0);
  w->arm = w->L * 0.52;
  w->wx  = sh_x + cos(w->phi) * w->arm;
  w->wy  = sh_y + sin(w->phi) * w->arm + y_off;

  w->hand_phi = (180 - fl * (range_deg + sweep_extra)) * (M_PI / 180.0);
  w->hand = w->L * 0.50 * (1 - 0.34 * f->fold);
  w->tx = w->wx + cos(w->hand_phi) * w->hand;
  w->ty = w->wy + sin(w->hand_phi) * w->hand;

  double bow = 2.1 * w->fore;
  w->bx = w->wx + cos(w->phi - M_PI / 2) * bow;
  w->by = w->wy + sin(w->phi - M_PI / 2) * bow;
}

/* ------------------------------------------------------------------------ */

void flyer_init(Flyer *f, double x, double y) {
  memset(f, 0, sizeof(*f));
  f->x = x; f->y = y;
  f->max_speed = 340;
  f->max_force = 1500;
  f->scale = 1;
  f->facing = 1;
  f->facing_blend = 1;
  f->wing_phase = av_rand();
  f->wing_hz = 7.4;
  f->spread = 1;
  f->effort = 1;
  f->bounding = 1;
  f->bound_timer = av_rand_range(0, 0.4);
  f->noise_off = av_rand_range(0, 1000);
  f->wander_gain = 240;
  f->arrive_radius = 150;
  f->head_phase = av_rand_range(0, TAU);
  f->blink_at = av_rand_range(2, 5);
}

void   flyer_force(Flyer *f, double fx, double fy) { f->ax += fx; f->ay += fy; }
double flyer_speed(const Flyer *f) { return hypot(f->vx, f->vy); }

double flyer_seek(Flyer *f, double tx, double ty, double slow_radius) {
  double dx = tx - f->x, dy = ty - f->y;
  double d = hypot(dx, dy);
  if (d < 1e-6) d = 1e-6;
  double want = f->max_speed;
  if (slow_radius > 0 && d < slow_radius) want = f->max_speed * (d / slow_radius);
  double dvx = (dx / d) * want - f->vx;
  double dvy = (dy / d) * want - f->vy;
  double m = hypot(dvx, dvy);
  if (m < 1e-6) m = 1e-6;
  double k = fmin(f->max_force, m * 5.5) / m;
  flyer_force(f, dvx * k, dvy * k);
  return d;
}

/* Birds never track a straight line. Low-frequency noise on both axes gives
 * the drifting, pushed-around-by-air quality. */
static void flyer_wander(Flyer *f) {
  double g = f->wander_gain;
  flyer_force(f,
              av_noise(f->t * 0.55 + f->noise_off, 0) * g,
              av_noise(f->t * 0.73 + f->noise_off, 1) * g * 0.85);
}

void flyer_update(Flyer *f, double dt) {
  f->t += dt;

  if (f->nwp > 0) {
    int last = (f->nwp == 1);
    Waypoint *w = &f->wp[0];
    double d = flyer_seek(f, w->p.x, w->p.y,
                          last ? (w->slow > 0 ? w->slow : f->arrive_radius) : 0);
    double hit = w->radius > 0 ? w->radius : (last ? 26 : 120);
    if (d < hit && !last) {
      memmove(&f->wp[0], &f->wp[1], sizeof(Waypoint) * (size_t)(f->nwp - 1));
      f->nwp--;
    }
  }

  if (f->wander_gain > 0) flyer_wander(f);

  f->vx += f->ax * dt;
  f->vy += f->ay * dt;

  double sp = hypot(f->vx, f->vy);
  double cap = f->hover ? f->max_speed * 0.45 : f->max_speed;
  if (sp > cap && sp > 0) {
    f->vx = f->vx / sp * cap;
    f->vy = f->vy / sp * cap;
  }

  f->x += f->vx * dt;
  f->y += f->vy * dt;

  flyer_update_pose(f, dt);
  flyer_update_wings(f, dt);

  f->ax = 0;
  f->ay = 0;
}

void flyer_update_pose(Flyer *f, double dt) {
  double sp = flyer_speed(f);

  /* facing flips with hysteresis so a hovering bird doesn't strobe */
  if (f->vx >  22) f->facing =  1;
  else if (f->vx < -22) f->facing = -1;
  f->facing_blend = av_damp(f->facing_blend, f->facing, 9, dt);

  /* heading follows velocity, clamped so the bird stays readable rather than
   * flying nose-down like a dart */
  double want = 0;
  if (sp > 26) want = av_clamp(atan2(f->vy, fabs(f->vx)), -0.85, 0.85) * 0.72;
  f->heading = av_angle_towards(f->heading, want + f->pitch, 7, dt);

  /* bank: lateral acceleration relative to the flight direction */
  if (sp > 30) {
    double ux = f->vx / sp, uy = f->vy / sp;
    double lateral = f->ax * -uy + f->ay * ux;
    f->bank = av_damp(f->bank, av_clamp(lateral / f->max_force, -1, 1), 5, dt);
  } else {
    f->bank = av_damp(f->bank, 0, 4, dt);
  }

  /* the head lags the body and adds its own bob — birds stabilise the head */
  f->head_phase += dt * (3.2 + f->effort * 2.6);
  f->head_bob = av_damp(f->head_bob, sin(f->head_phase) * 0.5 - f->bob * 0.35, 14, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) {
    f->blink = 0.13;
    f->blink_at = av_rand_range(2.2, 6.5);
  }
  if (f->blink > 0) f->blink -= dt;
}

void flyer_update_wings(Flyer *f, double dt) {
  double sp = flyer_speed(f);
  double accel = hypot(f->ax, f->ay);
  double climb = -f->vy / f->max_speed;

  double want = 0.34 + (accel / f->max_force) * 0.95 + fmax(0, climb) * 0.75;
  if (f->hover) want = 1.1;
  f->effort = av_damp(f->effort, av_clamp(want, 0.12, 1.5), 6, dt);

  /* Bounding flight: at cruise a small bird flaps in bursts, then folds up and
   * coasts on the momentum it just bought. Cheap for the bird, and it is what
   * makes the flight path undulate. */
  if (f->bounding && !f->hover) {
    f->bound_timer -= dt;
    if (f->bound_timer <= 0) {
      if (f->gliding) {
        f->gliding = 0;
        f->bound_timer = av_rand_range(0.34, 0.62);
      } else if (f->effort < 0.62 && sp > f->max_speed * 0.34) {
        f->gliding = 1;
        f->bound_timer = av_rand_range(0.20, 0.42);
      } else {
        f->bound_timer = av_rand_range(0.25, 0.5);
      }
    }
  } else {
    f->gliding = 0;
  }

  f->spread = av_damp(f->spread, f->gliding ? 0.88 : 1.0, 9, dt);

  if (f->gliding) {
    f->flap = av_damp(f->flap, 0.18, 8, dt);
    f->fold = av_damp(f->fold, 0.06, 8, dt);
    f->bob  = av_damp(f->bob,  0.9,  6, dt);
    f->vy += (f->glide_sink > 0 ? f->glide_sink : 130) * av_world() * dt;
  } else {
    f->wing_hz = f->hover
      ? av_lerp(9.2, 11.4, av_clamp(f->effort - 0.9, 0, 1))
      : av_lerp(5.4,  9.6, av_clamp(f->effort / 1.3, 0, 1));

    f->wing_phase += f->wing_hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase);

    /* Body rises on the downstroke. Amplitude scales with effort, so a bird
     * climbing hard visibly porpoises and a cruising one barely does. */
    double amp = av_lerp(0.7, 2.6, av_clamp(f->effort, 0, 1.3)) * f->scale;
    f->bob = av_damp(f->bob, -f->flap * amp, 22, dt);
  }
}

/* The bird art is drawn facing +x. Flying left mirrors on X (never on Y — that
 * would turn it upside down) and negates the heading, so "nose down" still
 * means nose down. facing_blend runs smoothly through zero, which squashes the
 * bird edge-on for a frame or two: that is the turn. */
double flyer_xscale(const Flyer *f) {
  double fb = av_clamp(f->facing_blend, -1, 1);
  double sgn = fb < 0 ? -1 : 1;
  return sgn * fmax(0.2, fabs(fb));
}

void flyer_transform(const Flyer *f, cairo_t *cr) {
  double sx = flyer_xscale(f);
  double sgn = sx < 0 ? -1 : 1;
  cairo_translate(cr, f->x, f->y + f->bob);
  cairo_rotate(cr, f->heading * sgn + f->roll);
  cairo_scale(cr, f->scale * sx, f->scale);
}

Vec flyer_to_world(const Flyer *f, double lx, double ly) {
  double sx = flyer_xscale(f);
  double sgn = sx < 0 ? -1 : 1;
  double px = lx * f->scale * sx;
  double py = ly * f->scale;
  double a = f->heading * sgn;
  double c = cos(a), s = sin(a);
  Vec v = { f->x + px * c - py * s, f->y + f->bob + px * s + py * c };
  return v;
}
