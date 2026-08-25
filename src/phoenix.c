#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
static const Rgb C_CHAR    = { 0.243, 0.078, 0.047 };
static const Rgb C_DEEP    = { 0.486, 0.055, 0.086 };
static const Rgb C_SCARLET = { 0.808, 0.125, 0.063 };
static const Rgb C_EMBER   = { 1.000, 0.353, 0.071 };
static const Rgb C_ORANGE  = { 1.000, 0.549, 0.102 };
static const Rgb C_GOLD    = { 1.000, 0.761, 0.227 };
static const Rgb C_PALE    = { 1.000, 0.902, 0.639 };
static const Rgb C_WHITE   = { 1.000, 0.969, 0.886 };
static const Rgb C_TALON   = { 1.000, 0.804, 0.431 };
static const Rgb C_OUTLINE = { 0.188, 0.055, 0.071 };   /* sprite keyline */

/* ---- anatomy, in body units (+x forward, +y down) --------------------- */
#define SH_X    3.8
#define SH_Y   -4.4
#define WB_X   -6.8
#define WB_Y   -2.6
#define WING_LEN 19.5
#define TAIL_X -11.6
#define TAIL_Y  -0.6
#define HEAD_X  12.4
#define HEAD_Y  -6.5
#define HEAD_R   4.5
#define EYE_X   14.3
#define EYE_Y   -7.5
#define BEAK_X  21.2
#define BEAK_Y  -5.9
#define CREST_X 11.2
#define CREST_Y -10.4
#define LEG_X    1.2
#define LEG_Y    3.4
#define NOSE_X  23.0
#define TAIL_END -16.0

void phoenix_init(Phoenix *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  flyer_init(&b->f, x, y);
  double W = av_world();
  b->f.scale = scale;
  b->f.max_speed = 330 * W;
  b->f.max_force = 1600 * W;
  b->f.wander_gain = 240 * W;
  /* a long streamer is elegant at 150px and an incoherent wire at 28px */
  b->plume_len = av_pixel_mode() ? 14 : 32;
  b->carrying = 1;
  b->wing_range_mul = 1;
  b->tail_spread = 1;
  b->burn_front = TAIL_END - 4;
}

void phoenix_ignite(Phoenix *b) {
  if (b->burning) return;
  b->burning = 1;
  b->burn_t = 0;
  b->f.bounding = 0;
}

Vec phoenix_release_point(Phoenix *b) {
  return flyer_to_world(&b->f, LEG_X - 1, LEG_Y + 4.4);
}

/* ---- the burn --------------------------------------------------------- */

static void phoenix_update_burn(Phoenix *b, double dt) {
  b->burn_t += dt;
  double t = b->burn_t;

  if (t < 0.75) {
    /* 1. charge — wings open wide, the beat slows to a ceremony */
    double k = t / 0.75;
    b->f.hover = 1;
    b->wing_range_mul   = av_lerp(1, 1.32, av_ease_in_out(k));
    b->wing_hz_override = av_lerp(9, 2.9,  av_ease_in_out(k));
    b->heat  = av_ease_in_cubic(k) * 0.55;
    b->f.pitch = av_lerp(0, -0.16, av_ease_in_out(k));
    b->tail_spread = av_lerp(1, 1.5, av_ease_in_out(k));
    b->crackle = k;
  } else if (t < 1.95) {
    /* 2. ignition — the burn front sweeps tail to head */
    double k = (t - 0.75) / 1.2;
    b->wing_range_mul   = av_lerp(1.32, 1.55, av_ease_out_cubic(k));
    b->wing_hz_override = av_lerp(2.9, 1.6, k);
    b->heat = av_lerp(0.55, 1, av_ease_in_out(k));
    b->f.pitch = av_lerp(-0.16, -0.42, av_ease_out_cubic(k));  /* head thrown back */
    b->tail_spread = av_lerp(1.5, 2.1, k);
    b->burn_front = av_lerp(TAIL_END - 3, NOSE_X + 3, av_ease_in_out(k));
    b->crackle = 1;
  } else {
    b->consumed = 1;
    b->heat = 1;
  }

  b->f.vy -= 26 * av_world() * dt;   /* it lifts a little as it goes */
}

/* embers and fire falling off the bird as it flies, then as it burns */
static void phoenix_shed(Phoenix *b, double dt, Particles *P) {
  b->ember_clock += dt;
  Flyer *f = &b->f;

  if (!b->burning) {
    /* a slow drip of sparks from the tail — it is a bird made of fire */
    if (b->ember_clock > 0.045) {
      b->ember_clock = 0;
      Vec tip = b->trail[b->ntrail ? b->ntrail - 1 : 0];
      p_ember(P, tip.x + av_rand_sym(3), tip.y + av_rand_sym(3),
              f->vx * 0.12, f->vy * 0.1, 18,
              av_rand_range(0.5, 1.2), av_rand_range(0.5, 1.1));
    }
    return;
  }

  double t = b->burn_t;

  if (t < 0.75) {
    int n = 2 + (int)(b->crackle * 6);
    for (int i = 0; i < n; i++) {
      Vec p = flyer_to_world(f, av_rand_range(TAIL_END, NOSE_X - 6), av_rand_range(-7, 5));
      p_ember(P, p.x, p.y, f->vx * 0.2, f->vy * 0.2 - 20, 30,
              av_rand_range(0.7, 1.9), av_rand_range(0.9, 2.1));
    }
    if (av_rand() < 0.35) {
      Vec p = flyer_to_world(f, av_rand_range(-10, 14), av_rand_range(-6, 4));
      p_spark(P, p.x, p.y, 130);
    }
  } else if (t < 1.95) {
    for (int i = 0; i < 5; i++) {
      Vec p = flyer_to_world(f, b->burn_front + av_rand_sym(2.4), av_rand_range(-9, 6));
      p_fire(P, p.x, p.y,
             f->vx * 0.25 + av_rand_sym(58), f->vy * 0.2 - av_rand_range(0, 40),
             av_rand_range(1.5, 3.4), av_rand_range(0.26, 0.52),
             av_rand_range(180, 320));
    }
    for (int i = 0; i < 3; i++) {
      Vec p = flyer_to_world(f, b->burn_front + av_rand_sym(3), av_rand_range(-9, 6));
      p_ember(P, p.x, p.y, av_rand_sym(60), -av_rand_range(20, 90), 40,
              av_rand_range(0.7, 1.9), av_rand_range(0.9, 2.1));
    }
    if (av_rand() < 0.6) {
      Vec p = flyer_to_world(f, b->burn_front, av_rand_range(-8, 5));
      p_spark(P, p.x, p.y, 200);
    }
    if (av_rand() < 0.5) {
      Vec p = flyer_to_world(f, b->burn_front - av_rand_range(4, 12), av_rand_range(-6, 4));
      p_smoke(P, p.x, p.y, av_rand_range(2.0, 4.5), av_rand_range(0.06, 0.14));
    }
  }
}

/* wing override during the ceremony: slow, wide, deliberate */
static void phoenix_update_wings(Phoenix *b, double dt) {
  Flyer *f = &b->f;
  if (b->wing_hz_override > 0) {
    f->effort = av_damp(f->effort, 1.3, 5, dt);
    f->wing_hz = b->wing_hz_override;
    f->wing_phase += f->wing_hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase) * 0.35;
    f->spread = av_damp(f->spread, 1, 8, dt);
    f->bob = av_damp(f->bob, -f->flap * 2.2 * f->scale, 16, dt);
    f->gliding = 0;
    return;
  }
  flyer_update_wings(f, dt);
}

void phoenix_update(Phoenix *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  if (b->burning) phoenix_update_burn(b, dt);

  /* flyer_update would call the base wing model; run the body integration and
   * then let the phoenix decide its own wings */
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
  if (f->wander_gain > 0)
    flyer_force(f,
                av_noise(f->t * 0.55 + f->noise_off, 0) * f->wander_gain,
                av_noise(f->t * 0.73 + f->noise_off, 1) * f->wander_gain * 0.85);

  f->vx += f->ax * dt;
  f->vy += f->ay * dt;
  double sp = hypot(f->vx, f->vy);
  double cap = f->hover ? f->max_speed * 0.45 : f->max_speed;
  if (sp > cap && sp > 0) { f->vx = f->vx / sp * cap; f->vy = f->vy / sp * cap; }
  f->x += f->vx * dt;
  f->y += f->vy * dt;

  flyer_update_pose(f, dt);
  phoenix_update_wings(b, dt);
  f->ax = 0;
  f->ay = 0;

  /* legs drop as it slows to a hover, tuck away at speed */
  b->legs_out = av_damp(b->legs_out,
                        (f->hover || flyer_speed(f) < f->max_speed * 0.3) ? 1 : 0, 6, dt);
  b->crest_sway = av_damp(b->crest_sway,
                          av_clamp(flyer_speed(f) / f->max_speed, 0, 1), 5, dt);

  /* Tail-root history in world space. Capped by arc length rather than by
   * frame count — otherwise a fast bird drags a 250px streamer and a slow one
   * drags a stub, purely as an artefact of frame rate. */
  Vec root = flyer_to_world(f, TAIL_X, TAIL_Y);
  if (b->ntrail < TRAIL_MAX) b->ntrail++;
  for (int i = b->ntrail - 1; i > 0; i--) b->trail[i] = b->trail[i - 1];
  b->trail[0] = root;

  double max_arc = b->plume_len * f->scale * 1.12;
  double acc = 0;
  for (int i = 1; i < b->ntrail; i++) {
    acc += hypot(b->trail[i].x - b->trail[i - 1].x, b->trail[i].y - b->trail[i - 1].y);
    if (acc >= max_arc) { b->ntrail = i + 1; break; }
  }
  b->arc = acc;

  if (P) phoenix_shed(b, dt, P);
}

/* ---- tail plumes ------------------------------------------------------ */

/* Where a plume sits at parameter u (0 root .. 1 tip). Blends between the path
 * the bird actually flew (fast: feathers stream straight behind) and a drooping
 * rest pose (slow: long feathers hang and curl). */
static Vec plume_point(Phoenix *b, int j, double u) {
  Flyer *f = &b->f;
  int n = b->ntrail;
  Vec root = b->trail[0];

  double arc_k   = av_clamp(b->arc / (b->plume_len * f->scale * 0.85), 0, 1);
  double speed_k = av_clamp(flyer_speed(f) / (f->max_speed * 0.5), 0, 1) * arc_k;

  /* sample the flown path by distance travelled, not by frame index */
  double want = u * b->plume_len * f->scale;
  double tx = b->trail[n - 1].x, ty = b->trail[n - 1].y;
  double acc = 0;
  for (int i = 1; i < n; i++) {
    double dx = b->trail[i].x - b->trail[i - 1].x;
    double dy = b->trail[i].y - b->trail[i - 1].y;
    double seg = hypot(dx, dy);
    if (acc + seg >= want) {
      double fr = seg > 1e-6 ? (want - acc) / seg : 0;
      tx = b->trail[i - 1].x + dx * fr;
      ty = b->trail[i - 1].y + dy * fr;
      break;
    }
    acc += seg;
  }

  /* rest pose: straight back from the body, sagging under its own weight */
  double sx = flyer_xscale(f);
  double sgn = sx < 0 ? -1 : 1;
  double a = f->heading * sgn;
  double bx = (TAIL_X - u * b->plume_len) * f->scale * sx;
  double by = (TAIL_Y + u * u * 7.5) * f->scale;
  double rx = f->x + bx * cos(a) - by * sin(a);
  double ry = f->y + f->bob + bx * sin(a) + by * cos(a);

  double px = av_lerp(rx, tx, speed_k);
  double py = av_lerp(ry, ty, speed_k);

  /* sway: a travelling wave down the feather, plus a fan between plumes */
  int pix = av_pixel_mode();
  double phase = f->t * 5.2 - u * 3.4 + j * 2.2;
  /* At sprite size two plumes swaying independently bow apart far enough that
   * the gap between them reads as a hole. Keep them close and near-parallel. */
  double amp = (pix ? (0.7 + 1.5 * u) : (2.4 + 5.6 * u)) * b->tail_spread;
  double dx = px - root.x, dy = py - root.y;
  double dl = hypot(dx, dy);
  if (dl < 1e-6) dl = 1;
  double nx = -dy / dl, ny = dx / dl;
  double fan = (j - (PLUMES - 1) / 2.0) * 7.4 * pow(u, 0.75) * b->tail_spread;
  double s = sin(phase) * amp * u + fan;

  Vec v = { px + nx * s, py + ny * s };
  return v;
}

#define PLUME_SEG 16

static void draw_tail(Phoenix *b, cairo_t *cr) {
  if (b->ntrail < 4) return;
  double h = b->heat * 0.5;
  int pix = av_pixel_mode();
  int nplumes = pix ? 2 : PLUMES;

  for (int j = 0; j < nplumes; j++) {
    Vec  pt[PLUME_SEG + 1];
    Vec  nrm[PLUME_SEG + 1];

    for (int i = 0; i <= PLUME_SEG; i++) pt[i] = plume_point(b, j, (double)i / PLUME_SEG);

    for (int i = 0; i <= PLUME_SEG; i++) {
      double u = (double)i / PLUME_SEG;
      Vec q = pt[i < PLUME_SEG ? i + 1 : PLUME_SEG];
      Vec r = pt[i > 0 ? i - 1 : 0];
      double dx = q.x - r.x, dy = q.y - r.y;
      double dl = hypot(dx, dy);
      if (dl < 1e-6) dl = 1;
      /* thin at the root, a slight swell two thirds out, whisker-fine at the tip */
      double w = 0.32 + sin(pow(u, 0.7) * M_PI) * 1.05 * (1 - u * 0.55);
      nrm[i].x = (-dy / dl) * w * b->f.scale;
      nrm[i].y = ( dx / dl) * w * b->f.scale;
    }

    cairo_new_path(cr);
    cairo_move_to(cr, pt[0].x + nrm[0].x, pt[0].y + nrm[0].y);
    for (int i = 1; i <= PLUME_SEG; i++) cairo_line_to(cr, pt[i].x + nrm[i].x, pt[i].y + nrm[i].y);
    for (int i = PLUME_SEG; i >= 0; i--) cairo_line_to(cr, pt[i].x - nrm[i].x, pt[i].y - nrm[i].y);
    cairo_close_path(cr);

    cairo_pattern_t *g = cairo_pattern_create_linear(pt[0].x, pt[0].y,
                                                     pt[PLUME_SEG].x, pt[PLUME_SEG].y);
    Rgb c0 = av_mix(pix ? C_SCARLET : C_DEEP,   C_PALE,  h);
    Rgb c1 = av_mix(pix ? C_EMBER   : C_SCARLET, C_PALE,  h);
    Rgb c2 = av_mix(pix ? C_EMBER   : C_EMBER,   C_WHITE, h);
    Rgb c3 = av_mix(C_GOLD, C_WHITE, h);
    Rgb c4 = av_mix(C_PALE,    C_WHITE, h);
    /* fading a feather out with alpha is what turns it into dither confetti;
     * in pixel mode it stays solid and lets the shape do the tapering */
    double a0 = pix ? 1.00 : 0.92, a1 = pix ? 1.00 : 0.85;
    double a2 = pix ? 1.00 : 0.66, a3 = pix ? 1.00 : 0.34;
    double a4 = pix ? 1.00 : 0.00;
    cairo_pattern_add_color_stop_rgba(g, 0.00, c0.r, c0.g, c0.b, a0);
    cairo_pattern_add_color_stop_rgba(g, 0.30, c1.r, c1.g, c1.b, a1);
    cairo_pattern_add_color_stop_rgba(g, 0.66, c2.r, c2.g, c2.b, a2);
    cairo_pattern_add_color_stop_rgba(g, 0.88, c3.r, c3.g, c3.b, a3);
    cairo_pattern_add_color_stop_rgba(g, 1.00, c4.r, c4.g, c4.b, a4);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);

    /* No keyline on the plumes: a ribbon doubles back on itself, so an
     * outline traced around it closes into a wire loop rather than reading
     * as a feather. The solid fill carries it. */
    if (pix) continue;

    /* hot core down the shaft */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    av_set_rgba(cr, C_GOLD, 0.16 + h * 0.3);
    cairo_set_line_width(cr, 0.5 * b->f.scale);
    cairo_move_to(cr, pt[0].x, pt[0].y);
    for (int i = 1; i <= PLUME_SEG; i++) cairo_line_to(cr, pt[i].x, pt[i].y);
    cairo_stroke(cr);
    cairo_restore(cr);
  }
}

/* ---- wings ------------------------------------------------------------ */

/* One wing, drawn in body-local space.
 *
 * The trick for a side view: a wing is a flat plane sweeping up and down, so at
 * mid-stroke you see it edge-on and it appears SHORT, while at the top and
 * bottom of the stroke it is broadside and appears long. Skipping that
 * foreshortening is what makes a 2D bird look like a paper cutout. */
static void draw_wing(Phoenix *b, cairo_t *cr, int near) {
  /* one wing only at sprite size: the far wing is just extra dark mass and it
   * muddies a silhouette that is only a couple of dozen pixels wide */
  if (av_pixel_mode() && !near) return;
  Flyer *f = &b->f;
  double h = b->heat * 0.34;         /* heat warms; the glow does the rest */

  double bank_k = av_clamp(1 + f->bank * (near ? 0.16 : -0.24), 0.6, 1.28);
  double y_off  = near ? 0 : 2.2;
  double shade  = near ? 0 : 0.3;
  double alpha  = near ? 1 : 0.82;

  /* the far wing is seen at a shallower angle, so its arc reads smaller */
  double range = 66 * b->wing_range_mul * (near ? 1 : 0.88);
  double fl = f->flap;
  double phi = (180 - fl * range) * D2R;

  double fore = 0.42 + 0.58 * fmin(1.0, fabs(fl) * 1.12);
  if (f->gliding) fore = fmax(fore, 0.68);

  double L = WING_LEN * (near ? 1 : 0.92) * bank_k * f->spread * fore;
  double arm = L * 0.52;
  double wx = SH_X + cos(phi) * arm;
  double wy = SH_Y + sin(phi) * arm + y_off;

  /* the hand trails past the arm at both extremes: the wingtip traces a
   * figure-eight, travelling further than the wrist */
  double hand_phi = (180 - fl * (range + 22)) * D2R;
  double hand_len = L * 0.50 * (1 - 0.34 * f->fold);
  double tx = wx + cos(hand_phi) * hand_len;
  double ty = wy + sin(hand_phi) * hand_len;

  double bow = 2.1 * fore;
  double bx = wx + cos(phi - M_PI / 2) * bow;
  double by = wy + sin(phi - M_PI / 2) * bow;

  cairo_save(cr);

  /* membrane: coverts + secondaries, ending just past the wrist so the
   * primaries beyond it read as separate feathers */
  double sx2 = wx + cos(hand_phi) * hand_len * 0.30;
  double sy2 = wy + sin(hand_phi) * hand_len * 0.30;

  cairo_new_path(cr);
  cairo_move_to(cr, SH_X + 1.6, SH_Y + y_off);
  av_quad_to(cr,
             av_lerp(SH_X, bx, 0.5) + cos(phi - M_PI / 2) * bow * 0.7,
             av_lerp(SH_Y + y_off, by, 0.5) + sin(phi - M_PI / 2) * bow * 0.7,
             bx, by);
  cairo_line_to(cr, sx2, sy2);
  av_quad_to(cr,
             av_lerp(sx2, WB_X, 0.42) - cos(phi - M_PI / 2) * 3.0,
             av_lerp(sy2, WB_Y + y_off, 0.42) - sin(phi - M_PI / 2) * 3.0,
             WB_X, WB_Y + y_off);
  av_quad_to(cr, -1.5, SH_Y + 0.6 + y_off, SH_X + 1.6, SH_Y + y_off);
  cairo_close_path(cr);

  {
    cairo_pattern_t *g = cairo_pattern_create_linear(SH_X, SH_Y, tx, ty);
    int pix = av_pixel_mode();
    Rgb a0 = av_mix(av_mix(pix ? C_SCARLET : C_DEEP,  C_CHAR, shade),       C_PALE, h);
    Rgb a1 = av_mix(av_mix(pix ? C_EMBER   : C_SCARLET, C_CHAR, shade),     C_PALE, h);
    Rgb a2 = av_mix(av_mix(pix ? C_ORANGE  : C_EMBER, C_CHAR, shade * 0.8), C_PALE, h);
    cairo_pattern_add_color_stop_rgba(g, 0.0, a0.r, a0.g, a0.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 0.5, a1.r, a1.g, a1.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 1.0, a2.r, a2.g, a2.b, alpha);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }

  /* covert row: a soft second layer near the shoulder */
  cairo_new_path(cr);
  cairo_move_to(cr, SH_X + 1.2, SH_Y + y_off);
  av_quad_to(cr, av_lerp(SH_X, wx, 0.45), av_lerp(SH_Y, wy, 0.45),
             wx * 0.62, wy * 0.62 + y_off);
  av_quad_to(cr, -2.4, SH_Y + 1.6 + y_off, SH_X + 1.2, SH_Y + y_off);
  cairo_close_path(cr);
  if (!av_pixel_mode()) {
    av_set_rgba(cr, av_mix(av_mix(C_EMBER, C_CHAR, shade), C_GOLD, h * 0.6), alpha * 0.55);
    cairo_fill(cr);
  } else {
    cairo_new_path(cr);
  }

  /* leading-edge light */
  av_set_rgba(cr, av_mix(C_GOLD, C_WHITE, h), alpha * 0.55);
  cairo_set_line_width(cr, 0.7);
  cairo_move_to(cr, SH_X + 1.6, SH_Y + y_off);
  av_quad_to(cr, av_lerp(SH_X, bx, 0.5), av_lerp(SH_Y + y_off, by, 0.5), bx, by);
  cairo_stroke(cr);

  /* primaries. Each lags the one inboard of it, so the stroke travels out
   * along the wing as a ripple instead of the whole plank moving at once. */
  const int N = av_pixel_mode() ? 4 : 6;
  double splay = (1 - f->fold * 0.6) * f->spread;
  for (int i = 0; i < N; i++) {
    double k = (double)i / (N - 1);
    double lag = f->wing_phase - (i + 1) * 0.05;
    double lf = (f->gliding && b->wing_hz_override <= 0) ? fl : flap_curve(lag);
    double lphi = (180 - lf * (range + 22)) * D2R;
    double p_ang = lphi + av_lerp(-6, 30, k) * splay * D2R;
    double p_len = hand_len * av_lerp(1.15, 0.62, pow(k, 1.35));

    double rx = av_lerp(wx, tx, 0.02 + k * 0.22);
    double ry = av_lerp(wy, ty, 0.02 + k * 0.22);
    double px = rx + cos(p_ang) * p_len;
    double py = ry + sin(p_ang) * p_len;

    double w = av_lerp(1.5, 0.85, k) * (0.55 + 0.45 * fore);
    double nx = -sin(p_ang) * w;
    double ny =  cos(p_ang) * w;

    cairo_new_path(cr);
    cairo_move_to(cr, rx + nx * 0.7, ry + ny * 0.7);
    av_quad_to(cr, av_lerp(rx, px, 0.6) + nx, av_lerp(ry, py, 0.6) + ny, px, py);
    av_quad_to(cr, av_lerp(rx, px, 0.6) - nx * 0.75, av_lerp(ry, py, 0.6) - ny * 0.75,
               rx - nx * 0.7, ry - ny * 0.7);
    cairo_close_path(cr);

    cairo_pattern_t *g = cairo_pattern_create_linear(rx, ry, px, py);
    Rgb b0 = av_mix(av_mix(av_pixel_mode() ? C_EMBER : C_SCARLET, C_CHAR, shade), C_PALE, h);
    Rgb b1 = av_mix(av_mix(av_pixel_mode() ? C_ORANGE : C_EMBER,   C_CHAR, shade), C_PALE, h);
    Rgb b2 = av_mix(av_mix(C_GOLD,    C_CHAR, shade * 0.5), C_WHITE, h);
    cairo_pattern_add_color_stop_rgba(g, 0.0, b0.r, b0.g, b0.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 0.6, b1.r, b1.g, b1.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 1.0, b2.r, b2.g, b2.b, alpha * 0.92);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }
  cairo_restore(cr);
}

/* ---- body ------------------------------------------------------------- */

static void draw_body(Phoenix *b, cairo_t *cr) {
  double h = b->heat * 0.34;

  cairo_new_path(cr);
  cairo_move_to(cr, 9.6, -4.0);
  cairo_curve_to(cr,  5.2,  -7.8,  -3.2, -7.4,  -9.6, -3.4);
  cairo_curve_to(cr, -12.4, -2.0, -12.8,  0.2, -10.8,  1.4);
  cairo_curve_to(cr, -6.6,   5.6,   0.4,  6.6,   5.4,  4.4);
  cairo_curve_to(cr,  8.6,   3.0,  10.4,  0.2,   9.6, -4.0);
  cairo_close_path(cr);

  cairo_pattern_t *g = cairo_pattern_create_linear(-12, 2, 10, -5);
  int pix = av_pixel_mode();
  Rgb c0 = av_mix(pix ? C_SCARLET : C_DEEP,   C_WHITE, h);
  Rgb c1 = av_mix(pix ? C_EMBER   : C_SCARLET, C_PALE, h);
  Rgb c2 = av_mix(pix ? C_ORANGE  : C_EMBER,  C_WHITE, h);
  Rgb c3 = av_mix(pix ? C_GOLD    : C_ORANGE, C_WHITE, h);
  cairo_pattern_add_color_stop_rgb(g, 0.00, c0.r, c0.g, c0.b);
  cairo_pattern_add_color_stop_rgb(g, 0.42, c1.r, c1.g, c1.b);
  cairo_pattern_add_color_stop_rgb(g, 0.78, c2.r, c2.g, c2.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, c3.r, c3.g, c3.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, C_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / b->f.scale);
    cairo_stroke(cr);
    cairo_pattern_destroy(g);
    return;                       /* highlights and ticks are sub-pixel noise */
  }
  cairo_fill(cr);
  cairo_pattern_destroy(g);

  /* breast highlight */
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
  cairo_pattern_t *bg = cairo_pattern_create_radial(6.5, 0.6, 0.5, 6.5, 0.6, 7);
  cairo_pattern_add_color_stop_rgba(bg, 0, C_GOLD.r, C_GOLD.g, C_GOLD.b, 0.30 + h * 0.35);
  cairo_pattern_add_color_stop_rgba(bg, 1, C_GOLD.r, C_GOLD.g, C_GOLD.b, 0);
  cairo_set_source(cr, bg);
  cairo_arc(cr, 6.5, 0.6, 7, 0, TAU);
  cairo_fill(cr);
  cairo_pattern_destroy(bg);
  cairo_restore(cr);

  /* a few scapular feather ticks along the back */
  av_set_rgba(cr, av_mix(C_CHAR, C_PALE, h * 0.9), 0.28);
  cairo_set_line_width(cr, 0.55);
  for (int i = 0; i < 4; i++) {
    double x = av_lerp(-7.5, 3.5, i / 3.0);
    cairo_move_to(cr, x, -5.2 + i * 0.24);
    av_quad_to(cr, x - 2.2, -3.9, x - 3.4, -3.2);
    cairo_stroke(cr);
  }
}

static void draw_head(Phoenix *b, cairo_t *cr) {
  Flyer *f = &b->f;
  double h = b->heat * 0.34;

  cairo_save(cr);
  cairo_translate(cr, 0, f->head_bob * 0.5);

  /* neck wedge */
  cairo_new_path(cr);
  cairo_move_to(cr, 7.2, -5.4);
  av_quad_to(cr, 10.0, -8.8, 13.4, -8.4);
  cairo_line_to(cr, 13.0, -2.6);
  av_quad_to(cr, 9.6, -1.4, 7.6, -1.6);
  cairo_close_path(cr);
  {
    cairo_pattern_t *g = cairo_pattern_create_linear(7, -4, 13, -6);
    Rgb n0 = av_mix(C_SCARLET, C_PALE, h);
    Rgb n1 = av_mix(C_EMBER, C_WHITE, h);
    cairo_pattern_add_color_stop_rgb(g, 0, n0.r, n0.g, n0.b);
    cairo_pattern_add_color_stop_rgb(g, 1, n1.r, n1.g, n1.b);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }

  /* crest — three swept plumes, they lie back with airspeed */
  double sweep = 14 * b->crest_sway;
  int ncrest = av_pixel_mode() ? 2 : 3;
  for (int i = 0; i < ncrest; i++) {
    double k = ncrest > 1 ? i / (double)(ncrest - 1) : 0;
    double base = 212 + k * 26 + sweep;
    double wob = sin(f->t * 6.4 + i * 1.7) * 5 * (0.3 + b->crest_sway * 0.7);
    double ang = (base + wob) * D2R;
    double len = av_lerp(11.5, 7.5, fabs(k - 0.4) * 1.5) * (1 + b->heat * 0.25);
    double rx = CREST_X - k * 1.8;
    double ry = CREST_Y + k * 1.1;
    double tx = rx + cos(ang) * len;
    double ty = ry + sin(ang) * len;
    double cx = av_lerp(rx, tx, 0.5) + cos(ang - M_PI / 2) * 2.4;
    double cy = av_lerp(ry, ty, 0.5) + sin(ang - M_PI / 2) * 2.4;

    cairo_new_path(cr);
    cairo_move_to(cr, rx + 1.1, ry + 0.5);
    av_quad_to(cr, cx, cy, tx, ty);
    av_quad_to(cr, cx - 1.0, cy + 1.4, rx - 1.1, ry + 0.9);
    cairo_close_path(cr);
    cairo_pattern_t *g = cairo_pattern_create_linear(rx, ry, tx, ty);
    Rgb k0 = av_mix(C_SCARLET, C_WHITE, h);
    Rgb k1 = av_mix(C_GOLD, C_WHITE, h);
    cairo_pattern_add_color_stop_rgba(g, 0, k0.r, k0.g, k0.b, 0.95);
    cairo_pattern_add_color_stop_rgba(g, 1, k1.r, k1.g, k1.b, 0.60);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }

  /* skull */
  cairo_new_path(cr);
  cairo_arc(cr, HEAD_X, HEAD_Y, HEAD_R, 0, TAU);
  {
    cairo_pattern_t *g = cairo_pattern_create_radial(HEAD_X + 1.4, HEAD_Y - 1.6, 0.4,
                                                     HEAD_X, HEAD_Y, HEAD_R * 1.5);
    Rgb h0 = av_mix(C_ORANGE,  C_WHITE, h);
    Rgb h1 = av_mix(C_SCARLET, C_PALE,  h);
    Rgb h2 = av_mix(C_DEEP,    C_PALE,  h);
    cairo_pattern_add_color_stop_rgb(g, 0.0, h0.r, h0.g, h0.b);
    cairo_pattern_add_color_stop_rgb(g, 0.6, h1.r, h1.g, h1.b);
    cairo_pattern_add_color_stop_rgb(g, 1.0, h2.r, h2.g, h2.b);
    cairo_set_source(cr, g);
    if (av_pixel_mode()) {
      cairo_fill_preserve(cr);
      av_set_rgba(cr, C_OUTLINE, 1);
      cairo_set_line_width(cr, 0.7 / b->f.scale);
      cairo_stroke(cr);
    } else {
      cairo_fill(cr);
    }
    cairo_pattern_destroy(g);
  }

  /* beak, with a visible gape line */
  cairo_new_path(cr);
  cairo_move_to(cr, 16.0, -8.0);
  av_quad_to(cr, 19.8, -7.2, BEAK_X, BEAK_Y);
  av_quad_to(cr, 18.6, -4.9, 15.8, -4.4);
  cairo_close_path(cr);
  {
    cairo_pattern_t *g = cairo_pattern_create_linear(15.8, -6, BEAK_X, BEAK_Y);
    Rgb g0 = av_mix(C_GOLD, C_WHITE, h);
    Rgb amber = { 1.0, 0.667, 0.235 };
    Rgb g1 = av_mix(amber, C_WHITE, h);
    cairo_pattern_add_color_stop_rgb(g, 0, g0.r, g0.g, g0.b);
    cairo_pattern_add_color_stop_rgb(g, 1, g1.r, g1.g, g1.b);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }
  if (!av_pixel_mode()) {
    av_set_rgba(cr, av_mix(C_CHAR, C_PALE, h), 0.4);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 15.9, -6.1);
    av_quad_to(cr, 18.8, -6.0, BEAK_X, BEAK_Y);
    cairo_stroke(cr);
  }

  /* eye */
  if (f->blink > 0) {
    av_set_rgba(cr, av_mix(C_CHAR, C_PALE, h), 0.85);
    cairo_set_line_width(cr, 0.9);
    cairo_new_path(cr);
    cairo_arc(cr, EYE_X, EYE_Y, 1.5, 0.15, M_PI - 0.15);
    cairo_stroke(cr);
  } else {
    av_set_rgba(cr, av_mix(C_GOLD, C_WHITE, h), 1);
    cairo_arc(cr, EYE_X, EYE_Y, 1.75, 0, TAU);
    cairo_fill(cr);
    if (b->heat > 0.8) av_set_rgba(cr, C_WHITE, 0.9);
    else cairo_set_source_rgba(cr, 0.102, 0.039, 0.024, 0.95);
    cairo_arc(cr, EYE_X + 0.25, EYE_Y, 1.0, 0, TAU);
    cairo_fill(cr);
    if (!av_pixel_mode()) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
      cairo_arc(cr, EYE_X + 0.65, EYE_Y - 0.55, 0.38, 0, TAU);
      cairo_fill(cr);
    }
  }
  cairo_restore(cr);
}

static void draw_legs(Phoenix *b, cairo_t *cr) {
  double k = b->legs_out;
  if (k < 0.02) return;
  double h = b->heat * 0.34;
  double drop = av_lerp(0.8, 4.2, k);
  double swing = sin(b->f.t * 3.1) * 0.5 * k;

  cairo_save(cr);
  av_set_rgba(cr, av_mix(C_TALON, C_WHITE, h), 0.95);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  for (int s = -1; s <= 1; s += 2) {
    double x = LEG_X + s * 1.4;
    cairo_set_line_width(cr, 1.15);
    cairo_move_to(cr, x, LEG_Y);
    av_quad_to(cr, x - 1.2, LEG_Y + drop * 0.6, x - 2.0 + swing, LEG_Y + drop);
    cairo_stroke(cr);
    if (av_pixel_mode()) continue;
    cairo_set_line_width(cr, 0.8);
    for (int t = -1; t <= 1; t++) {
      cairo_move_to(cr, x - 2.0 + swing, LEG_Y + drop);
      cairo_line_to(cr, x - 2.0 + swing + t * 1.3, LEG_Y + drop + 1.5);
      cairo_stroke(cr);
    }
  }
  cairo_restore(cr);
}

/* Same prop as every other bird carries: the letter rolled and tied to the
 * leg with thread, rather than clutched in the talons. */
static void draw_scroll(Phoenix *b, cairo_t *cr) {
  if (!b->carrying) return;
  double y = LEG_Y + av_lerp(2.2, 4.2, b->legs_out);
  av_draw_tied_letter(cr, LEG_X - 1.4, y, sin(b->f.t * 2.2) * 0.10 + 0.24, 0.4);
}

/* the wavy edge the fire has eaten up to */
static int clip_unburnt(Phoenix *b, cairo_t *cr) {
  if (!b->burning || b->burn_t < 0.75) return 0;
  double x = b->burn_front;
  cairo_new_path(cr);
  cairo_move_to(cr, 60, -40);
  cairo_line_to(cr, 60, 40);
  for (double y = 40; y >= -40; y -= 3) {
    double w = sin(y * 0.62 + b->f.t * 11) * 1.5 + sin(y * 1.7 - b->f.t * 17) * 0.7;
    cairo_line_to(cr, x + w, y);
  }
  cairo_close_path(cr);
  cairo_clip(cr);
  return 1;
}

void phoenix_draw(Phoenix *b, cairo_t *cr) {
  if (b->consumed) return;
  Flyer *f = &b->f;

  /* heat haze / halo, in world space so the flip does not squash it.
   * A soft halo around a 28px sprite is just a cloud of stray dither pixels,
   * so pixel mode does without it entirely. */
  if (!av_pixel_mode()) {
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
  {
    double gr = (16 + b->heat * 30) * f->scale;
    double pulse = 0.86 + 0.14 * sin(f->t * 7.3);
    double cx = f->x, cy = f->y + f->bob;
    cairo_pattern_t *g = cairo_pattern_create_radial(cx, cy, 0, cx, cy, gr);
    Rgb q0 = av_fire_color(0.12), q1 = av_fire_color(0.35);
    Rgb q2 = av_fire_color(0.60),  q3 = av_fire_color(0.90);
    cairo_pattern_add_color_stop_rgba(g, 0.00, q0.r, q0.g, q0.b, (0.13 + b->heat * 0.34) * pulse);
    cairo_pattern_add_color_stop_rgba(g, 0.28, q1.r, q1.g, q1.b, (0.07 + b->heat * 0.20) * pulse);
    cairo_pattern_add_color_stop_rgba(g, 0.60, q2.r, q2.g, q2.b, (0.02 + b->heat * 0.07) * pulse);
    cairo_pattern_add_color_stop_rgba(g, 1.00, q3.r, q3.g, q3.b, 0);
    cairo_set_source(cr, g);
    cairo_arc(cr, cx, cy, gr, 0, TAU);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }
  cairo_restore(cr);
  }

  /* the tail streams behind everything, and goes first when the fire starts */
  cairo_save(cr);
  if (b->burning && b->burn_t >= 0.75) {
    double gone = av_clamp((b->burn_front - (TAIL_END - 3)) / 10, 0, 1);
    cairo_push_group(cr);
    draw_tail(b, cr);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 1 - gone);
  } else {
    draw_tail(b, cr);
  }
  cairo_restore(cr);

  cairo_save(cr);
  flyer_transform(f, cr);

  cairo_save(cr);
  int clipped = clip_unburnt(b, cr);

  draw_wing(b, cr, 0);
  draw_legs(b, cr);
  draw_scroll(b, cr);
  draw_body(b, cr);
  draw_head(b, cr);
  draw_wing(b, cr, 1);

  if (clipped) {
    /* white-hot rim right where the fire is eating */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    double fade = av_clamp((NOSE_X + 3 - b->burn_front) / 8, 0, 1);
    cairo_pattern_t *g = cairo_pattern_create_linear(b->burn_front - 0.5, 0,
                                                     b->burn_front + 3.2, 0);
    Rgb r0 = av_fire_color(0.06), r1 = av_fire_color(0.30), r2 = av_fire_color(0.70);
    cairo_pattern_add_color_stop_rgba(g, 0.0, r0.r, r0.g, r0.b, 0.62 * fade);
    cairo_pattern_add_color_stop_rgba(g, 0.4, r1.r, r1.g, r1.b, 0.24 * fade);
    cairo_pattern_add_color_stop_rgba(g, 1.0, r2.r, r2.g, r2.b, 0);
    cairo_set_source(cr, g);
    cairo_rectangle(cr, b->burn_front - 1, -16, 4.6, 28);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
    cairo_restore(cr);
  }
  cairo_restore(cr);
  cairo_restore(cr);
}

void phoenix_bbox(Phoenix *b, double *x0, double *y0, double *x1, double *y1) {
  Flyer *f = &b->f;
  double r = (46 + b->heat * 40) * f->scale;
  if (f->x - r < *x0) *x0 = f->x - r;
  if (f->y - r < *y0) *y0 = f->y - r;
  if (f->x + r > *x1) *x1 = f->x + r;
  if (f->y + r > *y1) *y1 = f->y + r;
  for (int i = 0; i < b->ntrail; i++) {
    double m = 14 * f->scale * b->tail_spread;
    if (b->trail[i].x - m < *x0) *x0 = b->trail[i].x - m;
    if (b->trail[i].y - m < *y0) *y0 = b->trail[i].y - m;
    if (b->trail[i].x + m > *x1) *x1 = b->trail[i].x + m;
    if (b->trail[i].y + m > *y1) *y1 = b->trail[i].y + m;
  }
}
