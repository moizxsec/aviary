/* The swallow.
 *
 * Long narrow wings and a deeply forked tail: high aspect ratio, very low
 * drag, and a rudder on the back end. That combination is why a swallow can
 * turn inside its own body length and why it is still out flying when every
 * other bird has gone to shelter.
 *
 * This one has been out in it, and arrives soaked.
 */
#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
static const Rgb S_BLUE    = { 0.125, 0.149, 0.243 };   /* glossy blue-black */
static const Rgb S_BLUE2   = { 0.204, 0.243, 0.384 };
static const Rgb S_SHEEN   = { 0.329, 0.384, 0.557 };
static const Rgb S_RUST    = { 0.659, 0.282, 0.188 };
static const Rgb S_RUST2   = { 0.808, 0.408, 0.259 };
static const Rgb S_CREAM   = { 0.925, 0.886, 0.800 };
static const Rgb S_WATER   = { 0.596, 0.678, 0.760 };
static const Rgb S_OUTLINE = { 0.078, 0.086, 0.141 };

/* ---- anatomy, in body units (+x forward, +y down) --------------------- */
#define SH_X     1.8
#define SH_Y    -3.6
#define WING_LEN 30.0        /* long. This is the whole point of a swallow. */
#define TAIL_X   -9.5
#define TAIL_Y   -0.6
#define TAIL_IN   9.0        /* the short inner web */
#define TAIL_OUT 21.0        /* the outer streamers */
#define HEAD_X   10.8
#define HEAD_Y   -5.4
#define HEAD_R    4.2
#define EYE_X    12.6
#define EYE_Y    -6.2
#define BILL_X   15.4
#define BILL_Y   -4.6
#define HIP_X     0.6
#define HIP_Y     4.4
#define LEG_LEN   3.4        /* swallows have almost no legs */
#define STAND_H  (HIP_Y + LEG_LEN)

void swallow_init(Swallow *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  double W = av_world();
  flyer_init(&b->f, x, y);
  b->f.scale = scale;
  b->f.max_speed  = 380 * W;      /* fast */
  b->f.max_force  = 2300 * W;     /* and it can change its mind instantly */
  b->f.wander_gain = 320 * W;
  b->f.bounding   = 1;
  b->f.fore_floor = 0.40;         /* a narrow wing really does vanish edge-on */
  b->f.glide_sink = 55;
  b->carrying = 1;
  b->stand_h = STAND_H;
  b->wet = 1.0;
  b->tail_spread = 0.25;
}

Vec swallow_letter_point(Swallow *b) {
  return flyer_to_world(&b->f, HIP_X - 0.6, HIP_Y + LEG_LEN * 0.6);
}

void swallow_touch_down(Swallow *b, double ground_y) {
  Flyer *f = &b->f;
  b->grounded = 1;
  b->ground_y = ground_y;
  f->y = ground_y - b->stand_h * f->scale;
  f->vx = f->vy = 0;
  f->heading = 0;
  f->pitch = 0;
  f->roll = 0;
  b->flare = 0;
}

void swallow_launch(Swallow *b) {
  if (!b->grounded) return;
  double W = av_world();
  b->grounded = 0;
  b->crouch = 0;
  b->f.vx = b->f.facing * 150 * W;
  b->f.vy = -190 * W;
}

/* ---- wings ------------------------------------------------------------- */

static void swallow_wings(Swallow *b, double dt) {
  Flyer *f = &b->f;

  if (b->grounded) {
    f->flap = av_damp(f->flap, 0.0, 12, dt);
    f->fold = av_damp(f->fold, 1.0, 10, dt);
    f->spread = av_damp(f->spread, 0.0, 11, dt);
    f->bob = av_damp(f->bob, 0, 12, dt);
    f->gliding = 0;
    return;
  }

  double accel = hypot(f->ax, f->ay);
  double climb = -f->vy / f->max_speed;
  double want = 0.30 + (accel / f->max_force) * 1.1 + fmax(0, climb) * 0.9;
  f->effort = av_damp(f->effort, av_clamp(want, 0.1, 1.6), 8, dt);

  /* short bursts of very fast flapping, then long flat swoops */
  f->bound_timer -= dt;
  if (f->bound_timer <= 0) {
    if (f->gliding) { f->gliding = 0; f->bound_timer = av_rand_range(0.28, 0.55); }
    else if (f->effort < 0.75) { f->gliding = 1; f->bound_timer = av_rand_range(0.5, 1.2); }
    else f->bound_timer = av_rand_range(0.25, 0.5);
  }

  f->spread = av_damp(f->spread, f->gliding ? 0.98 : 1.0, 12, dt);

  if (f->gliding) {
    f->flap = av_damp(f->flap, -0.10, 9, dt);
    f->fold = av_damp(f->fold, 0.0, 9, dt);
    f->bob  = av_damp(f->bob, 0.2, 7, dt);
    f->vy += f->glide_sink * av_world() * dt;
  } else {
    f->wing_hz = av_lerp(6.5, 9.5, av_clamp(f->effort / 1.4, 0, 1));
    f->wing_phase += f->wing_hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase) * 0.7;
    double amp = av_lerp(0.6, 2.0, av_clamp(f->effort, 0, 1.4)) * f->scale;
    f->bob = av_damp(f->bob, -f->flap * amp, 26, dt);
  }
}

/* ---- airborne ---------------------------------------------------------- */

static void swallow_air(Swallow *b, double dt, Particles *P) {
  Flyer *f = &b->f;
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

  /* higher-frequency noise than the others: this is darting, not drifting */
  if (f->wander_gain > 0)
    flyer_force(f,
                av_noise(f->t * 1.5 + f->noise_off, 0) * f->wander_gain,
                av_noise(f->t * 1.9 + f->noise_off, 1) * f->wander_gain);

  f->vx += f->ax * dt;
  f->vy += f->ay * dt;
  double sp = hypot(f->vx, f->vy);
  if (sp > f->max_speed && sp > 0) {
    f->vx = f->vx / sp * f->max_speed;
    f->vy = f->vy / sp * f->max_speed;
  }
  f->x += f->vx * dt;
  f->y += f->vy * dt;

  flyer_update_pose(f, dt);
  swallow_wings(b, dt);

  /* the fork opens as a rudder when it turns hard, and as a brake to land */
  double want_spread = av_clamp(0.2 + fabs(f->bank) * 0.9, 0, 1);
  b->tail_spread = av_damp(b->tail_spread, fmax(want_spread, b->flare), 9, dt);
  b->legs_out = av_damp(b->legs_out, b->flare > 0.3 ? 1 : 0, 8, dt);
  f->pitch = av_damp(f->pitch, -0.5 * b->flare, 7, dt);

  /* water comes off it the whole way in */
  /* There is no rain falling here — the bird is the evidence that it rained
   * where he came from. So he trails water the whole way in, and it comes off
   * the wingtips hardest, the way it does off a real bird in flight. */
  b->drip_clock += dt;
  double W2 = av_world();
  if (P && b->wet > 0.12 && b->drip_clock > 0.024) {
    b->drip_clock = 0;
    for (int i = 0; i < 2; i++) {
      Vec q = flyer_to_world(f, av_rand_range(-9, 8), av_rand_range(-3, 5));
      /* water carries most of the bird's speed before gravity takes it, so it
       * streams out behind him instead of dropping in a column */
      p_water(P, q.x, q.y,
              f->vx * 0.62 + av_rand_sym(40) * W2,
              f->vy * 0.5 + av_rand_range(5, 40) * W2, 1);
    }
    if (av_rand() < 0.5) {
      /* flung off the trailing wingtip */
      Vec q = flyer_to_world(f, -6, -4 - f->flap * 9);
      p_water(P, q.x, q.y, f->vx * 0.7 - av_rand_range(20, 90) * W2,
              f->vy * 0.5 + av_rand_sym(60) * W2, 1);
    }
  }

  f->ax = 0;
  f->ay = 0;
}

/* ---- on the ground ----------------------------------------------------- */

static void swallow_ground(Swallow *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  f->t += dt;
  f->vx = f->vy = 0;
  f->y = b->ground_y - b->stand_h * f->scale + b->crouch * 2.0 * f->scale;
  f->heading = av_damp(f->heading, 0, 9, dt);
  b->legs_out = av_damp(b->legs_out, 1, 9, dt);
  b->flare = av_damp(b->flare, 0, 7, dt);

  /* The dry-off. A soaked bird does not fluff and settle the way a dry one
   * rouses — it whips the whole body back and forth and throws the water off
   * sideways. It is violent, and it is the only way it gets dry. */
  if (b->shake > 0) {
    b->shake_t += dt;
    double t = b->shake_t;
    if (t < 0.18) {
      b->tail_spread = av_damp(b->tail_spread, 0.7, 10, dt);
    } else if (t < 1.30) {
      double k = 1.0 - (t - 0.18) / 1.12;
      f->roll = sin(t * 62.0) * 0.34 * k;
      b->look = sin(t * 54.0) * 1.1 * k;
      b->wet = av_damp(b->wet, 0.06, 2.6, dt);
      if (P) {
        int n = 1 + (int)(k * 3);
        for (int i = 0; i < n; i++) {
          Vec q = flyer_to_world(f, av_rand_range(-9, 9), av_rand_range(-4, 4));
          double a = av_rand_range(0, TAU);
          double sp = av_rand_range(60, 230) * av_world() * (0.4 + k);
          p_water(P, q.x, q.y, cos(a) * sp, sin(a) * sp - 40 * av_world(), 1);
        }
      }
    } else if (t < 1.75) {
      f->roll = av_damp(f->roll, 0, 10, dt);
      b->look = av_damp(b->look, 0, 10, dt);
      b->tail_spread = av_damp(b->tail_spread, 0.25, 6, dt);
    } else {
      b->shake = 0;
      b->shake_t = 0;
    }
  } else {
    f->roll = av_damp(f->roll, 0, 8, dt);
    b->look = av_damp(b->look, sin(f->t * 2.1) * 0.5, 3, dt);
    b->tail_spread = av_damp(b->tail_spread, 0.25, 5, dt);
    /* still dripping, if it has not shaken yet */
    b->drip_clock += dt;
    if (P && b->wet > 0.4 && b->drip_clock > 0.22) {
      b->drip_clock = 0;
      Vec q = flyer_to_world(f, av_rand_range(-7, 6), 4.5);
      p_water(P, q.x, q.y, av_rand_sym(8) * av_world(),
              av_rand_range(30, 70) * av_world(), 1);
    }
  }

  swallow_wings(b, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) { f->blink = 0.10; f->blink_at = av_rand_range(1.6, 4.2); }
  if (f->blink > 0) f->blink -= dt;
}

void swallow_update(Swallow *b, double dt, Particles *P) {
  if (b->grounded) swallow_ground(b, dt, P);
  else             swallow_air(b, dt, P);
}

/* ---- drawing ----------------------------------------------------------- */

/* Wet feathers are darker and they clump. Both are handled here so every part
 * of the bird gets soaked by the same amount. */
static Rgb wetten(Rgb c, double wet) {
  Rgb dark = { c.r * 0.62, c.g * 0.64, c.b * 0.72 };
  return av_mix(c, dark, wet);
}

static void draw_tail(Swallow *b, cairo_t *cr) {
  double sp = b->tail_spread;
  Rgb dk = wetten(S_BLUE, b->wet);
  Rgb md = wetten(S_BLUE2, b->wet);

  /* the short inner web */
  double iw = av_lerp(1.6, 3.4, sp);
  cairo_new_path(cr);
  cairo_move_to(cr, TAIL_X, TAIL_Y - 1.4);
  cairo_line_to(cr, TAIL_X - TAIL_IN, TAIL_Y - iw);
  cairo_line_to(cr, TAIL_X - TAIL_IN, TAIL_Y + iw);
  cairo_line_to(cr, TAIL_X, TAIL_Y + 1.4);
  cairo_close_path(cr);
  av_set_rgba(cr, md, 1);
  cairo_fill(cr);

  /* The two outer streamers. This deep fork is the swallow's rudder: it opens
   * when the bird turns and it is why it can corner the way it does. */
  for (int i = 0; i < 2; i++) {
    double dir = i ? 1.0 : -1.0;
    double ang = (180.0 + dir * av_lerp(3.0, 19.0, sp)) * D2R;
    double len = TAIL_OUT * av_lerp(1.0, 0.86, sp);
    double tx = TAIL_X + cos(ang) * len;
    double ty = TAIL_Y + sin(ang) * len;
    double nx = -sin(ang), ny = cos(ang);
    double w0 = 2.0, w1 = 0.5;

    cairo_new_path(cr);
    cairo_move_to(cr, TAIL_X + nx * w0, TAIL_Y + ny * w0);
    av_quad_to(cr, av_lerp(TAIL_X, tx, 0.6) + nx * 1.2,
               av_lerp(TAIL_Y, ty, 0.6) + ny * 1.2, tx + nx * w1, ty + ny * w1);
    cairo_line_to(cr, tx - nx * w1, ty - ny * w1);
    av_quad_to(cr, av_lerp(TAIL_X, tx, 0.6) - nx * 0.8,
               av_lerp(TAIL_Y, ty, 0.6) - ny * 0.8, TAIL_X - nx * w0, TAIL_Y - ny * w0);
    cairo_close_path(cr);
    av_set_rgba(cr, i ? dk : md, 1);
    cairo_fill(cr);
  }
}

static void draw_wing(Swallow *b, cairo_t *cr, int near) {
  Flyer *f = &b->f;
  if (av_pixel_mode() && !near) return;

  double y_off = near ? 0 : 1.8;
  double shade = near ? 0.0 : 0.30;
  double alpha = near ? 1.0 : 0.85;
  Rgb base = wetten(av_mix(S_BLUE2, S_BLUE, shade), b->wet);
  Rgb tipc = wetten(av_mix(S_BLUE, S_OUTLINE, shade), b->wet);

  /* folded: the wingtips of a perched swallow reach past the end of its tail */
  if (f->spread < 0.15) {
    cairo_new_path(cr);
    cairo_move_to(cr, 3.2, -4.2 + y_off);
    av_quad_to(cr, -3.0, -5.0 + y_off, -12.0, -2.2 + y_off);
    av_quad_to(cr, -18.0, -0.6 + y_off, -20.5, 0.8 + y_off);
    av_quad_to(cr, -14.0, 1.6 + y_off, -4.0, 1.4 + y_off);
    av_quad_to(cr, 2.0, 0.6 + y_off, 3.2, -4.2 + y_off);
    cairo_close_path(cr);
    av_set_rgba(cr, base, alpha);
    cairo_fill_preserve(cr);
    if (av_pixel_mode()) {
      av_set_rgba(cr, S_OUTLINE, alpha);
      cairo_set_line_width(cr, 0.6 / f->scale);
      cairo_stroke(cr);
    } else {
      cairo_new_path(cr);
    }
    return;
  }

  WingPose w;
  wing_pose(f, 70.0, 26.0, WING_LEN, near ? 1.0 : 0.9, SH_X, SH_Y, y_off, &w);

  /* A long, narrow, sharply swept blade — high aspect ratio. Where the owl's
   * wing is a paddle, this is a scythe. */
  double ca = cos(w.hand_phi), sa = sin(w.hand_phi);
  double tipx = w.wx + ca * w.hand * 1.30;
  double tipy = w.wy + sa * w.hand * 1.30;
  double ta = w.hand_phi + 20 * D2R;
  double trx = w.wx + cos(ta) * w.hand * 0.52;
  double try_ = w.wy + sin(ta) * w.hand * 0.52;

  cairo_new_path(cr);
  cairo_move_to(cr, SH_X + 1.0, SH_Y + y_off);
  av_quad_to(cr, w.bx, w.by, tipx, tipy);
  av_quad_to(cr, av_lerp(tipx, trx, 0.55), av_lerp(tipy, try_, 0.55) + 0.8, trx, try_);
  av_quad_to(cr, av_lerp(trx, -8.0, 0.5) - 1.0, av_lerp(try_, -0.5 + y_off, 0.5) + 1.0,
             -8.0, -0.5 + y_off);
  av_quad_to(cr, -1.5, SH_Y + 1.2 + y_off, SH_X + 1.0, SH_Y + y_off);
  cairo_close_path(cr);
  av_set_rgba(cr, base, alpha);
  cairo_fill_preserve(cr);
  if (av_pixel_mode()) {
    av_set_rgba(cr, S_OUTLINE, alpha);
    cairo_set_line_width(cr, 0.6 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
  }

  /* darker outer third */
  cairo_save(cr);
  cairo_new_path(cr);
  cairo_move_to(cr, av_lerp(w.wx, tipx, 0.55), av_lerp(w.wy, tipy, 0.55));
  cairo_line_to(cr, tipx, tipy);
  cairo_line_to(cr, trx, try_);
  cairo_close_path(cr);
  av_set_rgba(cr, tipc, alpha);
  cairo_fill(cr);
  cairo_restore(cr);
}

static void draw_body(Swallow *b, cairo_t *cr) {
  double wet = b->wet;
  /* soaked feathers lie flat, so the whole bird is slimmer wet than dry */
  double slim = 1.0 - wet * 0.10;

  cairo_save(cr);
  cairo_scale(cr, 1.0, slim);

  cairo_new_path(cr);
  cairo_move_to(cr, 9.0, -2.6);
  cairo_curve_to(cr,  5.0, -5.6, -2.5, -5.4, -8.5, -2.6);
  cairo_curve_to(cr, -10.4, -1.6, -10.6, 0.4, -9.0, 1.4);
  cairo_curve_to(cr, -5.0,  4.4,  1.5,  5.0,  6.0, 3.4);
  cairo_curve_to(cr,  8.6,  2.4,  9.8,  0.2,  9.0, -2.6);
  cairo_close_path(cr);

  cairo_pattern_t *g = cairo_pattern_create_linear(0, -5, 0, 5);
  Rgb back  = wetten(S_BLUE, wet);
  Rgb mid   = wetten(S_BLUE2, wet);
  Rgb belly = wetten(S_CREAM, wet);
  cairo_pattern_add_color_stop_rgb(g, 0.00, back.r, back.g, back.b);
  cairo_pattern_add_color_stop_rgb(g, 0.40, mid.r, mid.g, mid.b);
  cairo_pattern_add_color_stop_rgb(g, 0.62, belly.r, belly.g, belly.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, belly.r, belly.g, belly.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, S_OUTLINE, 1);
    cairo_set_line_width(cr, 0.65 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);
  cairo_restore(cr);

  /* Wet feathers separate into points instead of lying as one smooth sheet.
   * The spikes are the clearest single signal that the bird is soaked. */
  if (wet > 0.12) {
    av_set_rgba(cr, wetten(S_BLUE, 1.0), 1);
    cairo_set_line_width(cr, 1.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double spike[6][3] = {
      { -7.0, -3.2, 2.55 }, { -3.0, -4.8, 1.95 }, {  1.5, -4.8, 1.35 },
      { -8.5,  1.0, 2.95 }, { -3.5,  4.2, 1.55 }, {  2.5,  4.0, 1.15 }
    };
    for (int i = 0; i < 6; i++) {
      double len = (1.4 + wet * 2.4) * (0.75 + 0.25 * sin(b->f.t * 3.0 + i));
      cairo_move_to(cr, spike[i][0], spike[i][1]);
      cairo_line_to(cr, spike[i][0] + cos(spike[i][2]) * len,
                        spike[i][1] + sin(spike[i][2]) * len);
      cairo_stroke(cr);
    }

    /* water still hanging off the underside */
    av_set_rgba(cr, S_WATER, 0.75 * wet);
    for (int i = 0; i < 3; i++) {
      double x = -5.0 + i * 4.5;
      double yy = 4.2 + sin(b->f.t * 4.0 + i * 1.7) * 0.4;
      cairo_arc(cr, x, yy, 0.9, 0, TAU);
      cairo_fill(cr);
    }
  }
}

static void draw_head(Swallow *b, cairo_t *cr) {
  Flyer *f = &b->f;
  double wet = b->wet;
  double dx = b->look * 0.8;

  /* skull */
  cairo_new_path(cr);
  cairo_arc(cr, HEAD_X + dx, HEAD_Y, HEAD_R, 0, TAU);
  av_set_rgba(cr, wetten(S_BLUE, wet), 1);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, S_OUTLINE, 1);
    cairo_set_line_width(cr, 0.65 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }

  /* the rust face and throat — the only warm colour on the bird */
  cairo_new_path(cr);
  cairo_move_to(cr, HEAD_X + dx + 1.0, HEAD_Y - 2.6);
  av_quad_to(cr, HEAD_X + dx + 4.4, HEAD_Y - 1.0, HEAD_X + dx + 3.6, HEAD_Y + 2.2);
  av_quad_to(cr, HEAD_X + dx - 0.6, HEAD_Y + 4.2, 6.4, 2.0);
  av_quad_to(cr, 8.0, -0.6, HEAD_X + dx + 1.0, HEAD_Y - 2.6);
  cairo_close_path(cr);
  av_set_rgba(cr, wetten(av_pixel_mode() ? S_RUST : S_RUST2, wet), 1);
  cairo_fill(cr);

  /* short wide bill, built for catching things mid-air */
  av_set_rgba(cr, S_OUTLINE, 1);
  cairo_new_path(cr);
  cairo_move_to(cr, HEAD_X + dx + 3.2, BILL_Y - 0.7);
  cairo_line_to(cr, BILL_X + dx, BILL_Y);
  cairo_line_to(cr, HEAD_X + dx + 3.2, BILL_Y + 1.1);
  cairo_close_path(cr);
  cairo_fill(cr);

  /* eye */
  double ex = EYE_X + dx, ey = EYE_Y;
  if (f->blink > 0) {
    av_set_rgba(cr, S_SHEEN, 0.9);
    cairo_set_line_width(cr, 0.7);
    cairo_new_path(cr);
    cairo_arc(cr, ex, ey, 1.2, 0.2, M_PI - 0.2);
    cairo_stroke(cr);
  } else {
    cairo_set_source_rgb(cr, 0.04, 0.04, 0.07);
    cairo_arc(cr, ex, ey, 1.4, 0, TAU);
    cairo_fill(cr);
    if (!av_pixel_mode()) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
      cairo_arc(cr, ex + 0.5, ey - 0.4, 0.35, 0, TAU);
      cairo_fill(cr);
    }
  }
}

static void draw_legs(Swallow *b, cairo_t *cr) {
  if (b->legs_out < 0.05) return;
  double reach = LEG_LEN * av_lerp(0.3, 1.0, b->legs_out);
  av_set_rgba(cr, wetten(S_RUST, b->wet * 0.5), 1);
  cairo_set_line_width(cr, 0.9);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  for (int leg = 0; leg < 2; leg++) {
    double x = HIP_X + (leg ? 1.1 : -1.1) + b->flare * 3.0;
    cairo_move_to(cr, x, HIP_Y - 0.5);
    cairo_line_to(cr, x - 0.6, HIP_Y + reach);
    cairo_stroke(cr);
    for (int t = -1; t <= 1; t += 2) {
      cairo_move_to(cr, x - 0.6, HIP_Y + reach);
      cairo_line_to(cr, x - 0.6 + t * 1.1, HIP_Y + reach + 0.8);
      cairo_stroke(cr);
    }
  }
}

void swallow_draw(Swallow *b, cairo_t *cr) {
  Flyer *f = &b->f;
  cairo_save(cr);
  flyer_transform(f, cr);

  draw_tail(b, cr);
  if (!av_pixel_mode() && f->spread >= 0.15) draw_wing(b, cr, 0);
  draw_legs(b, cr);
  draw_body(b, cr);
  draw_head(b, cr);
  draw_wing(b, cr, 1);
  if (b->carrying) {
    double reach = LEG_LEN * av_lerp(0.3, 1.0, b->legs_out);
    av_draw_tied_letter(cr, HIP_X + 1.0 + b->flare * 3.0,
                        HIP_Y + reach * 0.7, 0.24, 0.4);
  }
  cairo_restore(cr);
}

void swallow_bbox(Swallow *b, double *x0, double *y0, double *x1, double *y1) {
  double r = 44 * b->f.scale;
  double cx = b->f.x, cy = b->f.y + b->f.bob;
  if (cx - r < *x0) *x0 = cx - r;
  if (cy - r < *y0) *y0 = cy - r;
  if (cx + r > *x1) *x1 = cx + r;
  if (cy + r > *y1) *y1 = cy + r;
}
