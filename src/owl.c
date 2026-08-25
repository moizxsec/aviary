/* Errol.
 *
 * An owl's wing area is enormous next to its weight, so nothing about its
 * flight is quick: the beat is slow and deep, it glides for long stretches and
 * barely sinks, and the whole body floats rather than darts. It also holds its
 * head unnervingly still while the body moves underneath.
 *
 * This particular owl is very old and does not judge distances well.
 */
#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
static const Rgb O_CREAM  = { 0.886, 0.847, 0.769 };
static const Rgb O_LIGHT  = { 0.784, 0.745, 0.667 };
static const Rgb O_MID    = { 0.643, 0.596, 0.518 };
static const Rgb O_BASE   = { 0.549, 0.502, 0.439 };
static const Rgb O_DARK   = { 0.384, 0.345, 0.290 };
static const Rgb O_DEEP   = { 0.243, 0.212, 0.180 };
static const Rgb O_EYE    = { 0.910, 0.659, 0.235 };
static const Rgb O_BEAK   = { 0.282, 0.267, 0.298 };

/* ---- anatomy, in body units (+x forward, +y down) --------------------- */
#define SH_X     2.0
#define SH_Y    -5.0
#define WING_LEN 25.0
#define TAIL_X  -10.5
#define TAIL_Y    0.5
#define TAIL_LEN  9.0
/* An owl has no neck to speak of: the head sits straight down into the
 * shoulders and overlaps them, which is most of why the silhouette reads. */
#define HEAD_X    9.6
#define HEAD_Y   -8.2
#define HEAD_R    8.2
#define DISC_X   11.6
#define DISC_Y   -7.8
#define DISC_R    6.4
#define EYE_X    14.0
#define EYE_Y    -8.8
#define BEAK_X   16.4
#define BEAK_Y   -6.2
#define HIP_X     0.5
#define HIP_Y     8.5
#define LEG_LEN   6.0
#define STAND_H  (HIP_Y + LEG_LEN)

void owl_init(Owl *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  double W = av_world();
  flyer_init(&b->f, x, y);
  b->f.scale = scale;
  b->f.max_speed  = 190 * W;      /* slow. He is not in a hurry, ever. */
  b->f.max_force  = 620 * W;
  b->f.wander_gain = 210 * W;     /* and not in a straight line either */
  b->f.bounding   = 1;
  b->f.fore_floor = 0.66;         /* broad wings never go fully edge-on */
  b->f.glide_sink = 34;           /* he floats; he does not drop */
  b->carrying = 1;
  b->stand_h = STAND_H;
  b->tail_fan = 0.55;             /* short broad tail, always half spread */
  b->tufts = 0.5;
}

Vec owl_letter_point(Owl *b) {
  return flyer_to_world(&b->f, HIP_X - 1.0, HIP_Y + LEG_LEN * 0.55);
}

/* ---- wings ------------------------------------------------------------- */

static void owl_wings(Owl *b, double dt) {
  Flyer *f = &b->f;

  if (b->grounded) {
    f->flap = av_damp(f->flap, 0.0, 8, dt);
    f->fold = av_damp(f->fold, 1.0, 7, dt);
    f->spread = av_damp(f->spread, 0.0, 8, dt);
    f->bob = av_damp(f->bob, 0, 9, dt);
    f->gliding = 0;
    return;
  }

  if (b->struck) {
    /* nothing useful is happening with the wings at this point */
    f->spread = av_damp(f->spread, 0.55, 4, dt);
    f->wing_phase += 2.2 * dt;
    f->flap = flap_curve(f->wing_phase) * 0.45;
    f->fold = 0.5;
    return;
  }

  double accel = hypot(f->ax, f->ay);
  double climb = -f->vy / f->max_speed;
  double want = 0.34 + (accel / f->max_force) * 0.8 + fmax(0, climb) * 0.8;
  f->effort = av_damp(f->effort, av_clamp(want, 0.15, 1.5), 4, dt);

  /* long glides between slow bursts of flapping */
  f->bound_timer -= dt;
  if (f->bound_timer <= 0) {
    if (f->gliding) { f->gliding = 0; f->bound_timer = av_rand_range(0.8, 1.5); }
    else if (f->effort < 0.7) { f->gliding = 1; f->bound_timer = av_rand_range(0.9, 1.8); }
    else f->bound_timer = av_rand_range(0.5, 1.0);
  }

  f->spread = av_damp(f->spread, f->gliding ? 0.96 : 1.0, 6, dt);

  if (f->gliding) {
    f->flap = av_damp(f->flap, -0.16, 5, dt);
    f->fold = av_damp(f->fold, 0.02, 5, dt);
    f->bob  = av_damp(f->bob, 0.4, 4, dt);
    f->vy += f->glide_sink * av_world() * dt;
  } else {
    /* 2.6 to 4.2 Hz. A pigeon is nearly twice that, a finch four times. */
    f->wing_hz = av_lerp(2.6, 4.2, av_clamp(f->effort / 1.3, 0, 1));
    f->wing_phase += f->wing_hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase) * 0.45;

    /* a big slow wing lifts the whole body on every downstroke */
    double amp = av_lerp(2.0, 5.0, av_clamp(f->effort, 0, 1.4)) * f->scale;
    f->bob = av_damp(f->bob, -f->flap * amp, 12, dt);
  }
}

/* ---- airborne ---------------------------------------------------------- */

static void owl_air(Owl *b, double dt, Particles *P) {
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

  /* He wanders, and he sags. Both on purpose. */
  if (f->wander_gain > 0)
    flyer_force(f,
                av_noise(f->t * 0.42 + f->noise_off, 0) * f->wander_gain,
                av_noise(f->t * 0.55 + f->noise_off, 1) * f->wander_gain * 1.15);
  b->wobble = av_damp(b->wobble, sin(f->t * 1.35) * 0.5 + 0.5, 2, dt);
  flyer_force(f, 0, b->wobble * 40 * av_world());

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
  owl_wings(b, dt);

  b->tail_fan = av_damp(b->tail_fan, av_lerp(0.55, 1.0, b->flare), 5, dt);
  b->legs_out = av_damp(b->legs_out, b->flare > 0.3 ? 1 : 0.15, 6, dt);
  f->pitch    = av_damp(f->pitch, -0.55 * b->flare, 5, dt);
  b->fluff    = av_damp(b->fluff, 0.25, 3, dt);
  b->sprawl   = av_damp(b->sprawl, 0, 8, dt);
  f->roll     = av_damp(f->roll, 0, 8, dt);

  /* an old bird moults in flight */
  if (P && av_rand() < 0.006) {
    Vec q = flyer_to_world(f, av_rand_range(-10, 4), av_rand_range(-4, 6));
    p_feather(P, q.x, q.y, f->vx * 0.2, f->vy * 0.2, av_rand_range(0.2, 0.8));
  }

  f->ax = 0;
  f->ay = 0;
}

/* ---- the collision ----------------------------------------------------- */

void owl_strike(Owl *b, Particles *P, double dir_x) {
  if (b->struck) return;
  Flyer *f = &b->f;
  double W = av_world();

  b->struck = 1;
  b->dazed = 1;
  b->tufts = 1;
  b->fluff = 1;

  /* everything reverses and drops */
  /* bounce him well clear of the wall, or he lands jammed against the edge
   * with no room for the letter */
  f->vx = -dir_x * (fabs(f->vx) * 0.55 + 60 * W);
  f->vy = 40 * W;
  b->roll_v = (av_rand() < 0.5 ? -1 : 1) * av_rand_range(4.5, 7.5);
  b->wing_limp = 1;

  if (!P) return;
  for (int i = 0; i < 14; i++) {
    Vec q = flyer_to_world(f, av_rand_range(-8, 12), av_rand_range(-8, 6));
    p_feather(P, q.x, q.y,
              -dir_x * av_rand_range(20, 130) * W, av_rand_sym(90) * W,
              av_rand_range(0.0, 0.9));
  }
  for (int i = 0; i < 6; i++) {
    Vec q = flyer_to_world(f, 10, av_rand_range(-8, 6));
    p_ash(P, q.x, q.y, -dir_x * av_rand_range(30, 150) * W,
          av_rand_sym(70) * W, av_rand_range(0.4, 0.9), 0);
  }
}

static void owl_tumble(Owl *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  double W = av_world();
  f->t += dt;

  f->vy += 600 * W * dt;              /* now he is just falling */
  f->vx *= exp(-0.8 * dt);
  f->x += f->vx * dt;
  f->y += f->vy * dt;

  f->roll += b->roll_v * dt;
  b->roll_v *= exp(-0.55 * dt);
  f->heading = av_damp(f->heading, 0, 5, dt);
  f->pitch = 0;
  f->bob = 0;

  b->wing_limp = av_damp(b->wing_limp, 1, 6, dt);
  b->legs_out = av_damp(b->legs_out, 0.7, 4, dt);
  b->tail_fan = av_damp(b->tail_fan, 0.8, 4, dt);
  owl_wings(b, dt);

  if (P && av_rand() < 0.12) {
    Vec q = flyer_to_world(f, av_rand_range(-10, 8), av_rand_range(-6, 6));
    p_feather(P, q.x, q.y, f->vx * 0.3, f->vy * 0.1, av_rand_range(0.0, 0.9));
  }
}

void owl_touch_down(Owl *b, double ground_y) {
  Flyer *f = &b->f;
  b->grounded = 1;
  b->struck = 0;
  b->ground_y = ground_y;
  f->y = ground_y - b->stand_h * f->scale;
  f->vx = f->vy = 0;
  f->heading = 0;
  f->pitch = 0;
  b->roll_v = 0;
  b->sprawl = 1;               /* he arrives on his back */
  b->wing_limp = 1;
}

void owl_launch(Owl *b) {
  if (!b->grounded) return;
  double W = av_world();
  b->grounded = 0;
  b->struck = 0;
  b->sprawl = 0;
  b->crouch = 0;
  b->wing_limp = 0;
  b->f.roll = 0;
  b->f.vx = b->f.facing * 60 * W;
  b->f.vy = -150 * W;
}

/* ---- on the ground ----------------------------------------------------- */

static void owl_ground(Owl *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  f->t += dt;
  f->vx = f->vy = 0;
  f->y = b->ground_y - b->stand_h * f->scale + b->crouch * 3.0 * f->scale;

  /* he lands on his back and has to get the right way up first */
  f->roll = av_damp(f->roll, b->sprawl * -1.9, 7, dt);
  b->legs_out = av_damp(b->legs_out, 1, 6, dt);
  b->wing_limp = av_damp(b->wing_limp, b->sprawl > 0.3 ? 1 : 0, 5, dt);
  b->tail_fan = av_damp(b->tail_fan, 0.55, 4, dt);

  /* The rouse: fluff everything up, shake it out hard, then settle. Every
   * bird does this after a fright; on an owl it is most of the personality. */
  if (b->shake > 0) {
    b->shake_t += dt;
    double t = b->shake_t;
    if (t < 0.35) {
      b->fluff = av_damp(b->fluff, 1.0, 9, dt);
    } else if (t < 1.15) {
      double k = 1.0 - (t - 0.35) / 0.80;
      f->roll = sin(t * 46.0) * 0.30 * k;
      b->head_turn = sin(t * 38.0) * 0.9 * k;
      b->fluff = 1.0;
      if (P && av_rand() < 0.22) {
        Vec q = flyer_to_world(f, av_rand_range(-10, 8), av_rand_range(-8, 6));
        p_feather(P, q.x, q.y, av_rand_sym(60) * av_world(),
                  -av_rand_range(0, 40) * av_world(), av_rand_range(0.0, 0.9));
      }
    } else if (t < 1.7) {
      b->fluff = av_damp(b->fluff, 0.2, 6, dt);
      b->head_turn = av_damp(b->head_turn, 0, 8, dt);
    } else {
      b->shake = 0;
      b->shake_t = 0;
    }
  } else {
    b->fluff = av_damp(b->fluff, 0.3, 3, dt);
    /* owls swivel; a dazed one swivels slowly and stops in odd places */
    b->head_turn = av_damp(b->head_turn, sin(f->t * 0.9) * 0.7, 2.5, dt);
  }

  b->dazed = av_damp(b->dazed, 0, 0.7, dt);
  b->tufts = av_damp(b->tufts, 0.55, 2, dt);
  owl_wings(b, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) { f->blink = 0.16; f->blink_at = av_rand_range(1.4, 4.0); }
  if (f->blink > 0) f->blink -= dt;
}

void owl_update(Owl *b, double dt, Particles *P) {
  if (b->grounded)      owl_ground(b, dt, P);
  else if (b->struck)   owl_tumble(b, dt, P);
  else                  owl_air(b, dt, P);
}

/* ---- drawing ----------------------------------------------------------- */

static const Rgb O_OUTLINE = { 0.161, 0.137, 0.114 };

static void draw_tail(Owl *b, cairo_t *cr) {
  double fan = b->tail_fan;
  double th = 180.0 * D2R;
  double cx = cos(th), sy = sin(th);
  double tipx = TAIL_X + cx * TAIL_LEN;
  double tipy = TAIL_Y + sy * TAIL_LEN;
  double nx = -sy, ny = cx;
  double w0 = 2.4, w1 = av_lerp(4.0, 8.0, fan);

  cairo_new_path(cr);
  cairo_move_to(cr, TAIL_X + nx * w0, TAIL_Y + ny * w0);
  cairo_line_to(cr, tipx + nx * w1, tipy + ny * w1);
  cairo_line_to(cr, tipx - nx * w1, tipy - ny * w1);
  cairo_line_to(cr, TAIL_X - nx * w0, TAIL_Y - ny * w0);
  cairo_close_path(cr);
  av_set_rgba(cr, O_BASE, 1);
  cairo_fill_preserve(cr);
  if (av_pixel_mode()) {
    av_set_rgba(cr, O_OUTLINE, 1);
    cairo_set_line_width(cr, 0.6 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
  }

  /* owls are barred, not banded */
  av_set_rgba(cr, O_DARK, 0.9);
  cairo_set_line_width(cr, 1.0);
  for (int i = 1; i <= 2; i++) {
    double t = i / 3.0;
    double bx = av_lerp(TAIL_X, tipx, t), by = av_lerp(TAIL_Y, tipy, t);
    double bw = av_lerp(w0, w1, t);
    cairo_move_to(cr, bx + nx * bw, by + ny * bw);
    cairo_line_to(cr, bx - nx * bw, by - ny * bw);
    cairo_stroke(cr);
  }
}

static void draw_legs(Owl *b, cairo_t *cr) {
  double k = b->legs_out;
  if (k < 0.05) return;
  double reach = LEG_LEN * av_lerp(0.3, 1.0, k);

  for (int leg = 0; leg < 2; leg++) {
    double ox = leg ? 1.6 : -1.6;
    double fx = HIP_X + ox + b->flare * 5.0;
    double fy = HIP_Y + reach;

    /* an owl's legs are feathered right down to the toes: thick, not wiry */
    cairo_new_path(cr);
    cairo_move_to(cr, fx - 2.0, HIP_Y - 1.4);
    av_quad_to(cr, fx - 1.7, HIP_Y + reach * 0.6, fx - 1.2, fy);
    cairo_line_to(cr, fx + 1.2, fy);
    av_quad_to(cr, fx + 1.7, HIP_Y + reach * 0.6, fx + 2.0, HIP_Y - 1.4);
    cairo_close_path(cr);
    av_set_rgba(cr, leg ? O_MID : O_DARK, 1);
    cairo_fill(cr);

    /* talons */
    av_set_rgba(cr, O_BEAK, 1);
    cairo_set_line_width(cr, 0.9);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int t = -1; t <= 1; t++) {
      cairo_move_to(cr, fx, fy);
      cairo_line_to(cr, fx + 1.2 + t * 1.2, fy + 1.6 - (t < 0 ? -t : t) * 0.4);
      cairo_stroke(cr);
    }
  }
}

static void draw_body(Owl *b, cairo_t *cr) {
  double puff = 1.0 + b->fluff * 0.12;

  cairo_save(cr);
  cairo_translate(cr, -2.0, 1.0);
  cairo_scale(cr, puff, puff);
  cairo_translate(cr, 2.0, -1.0);

  /* a rounded lump, not a sausage: owls are mostly feather */
  cairo_new_path(cr);
  cairo_move_to(cr, 8.0, -5.0);
  cairo_curve_to(cr,   4.0, -9.6,  -4.0, -9.8,  -9.5, -5.0);
  cairo_curve_to(cr, -12.0, -3.0, -12.4,  1.2, -10.4,  3.6);
  cairo_curve_to(cr,  -6.0,  9.8,   2.0, 11.2,   6.4,  7.6);
  cairo_curve_to(cr,   9.0,  5.4,   9.4,  0.0,   8.0, -5.0);
  cairo_close_path(cr);

  cairo_pattern_t *g = cairo_pattern_create_linear(-10, 8, 7, -8);
  cairo_pattern_add_color_stop_rgb(g, 0.00, O_DARK.r, O_DARK.g, O_DARK.b);
  cairo_pattern_add_color_stop_rgb(g, 0.45, O_BASE.r, O_BASE.g, O_BASE.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, O_LIGHT.r, O_LIGHT.g, O_LIGHT.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, O_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);

  /* mottling down the breast */
  av_set_rgba(cr, O_DARK, 0.55);
  cairo_set_line_width(cr, 0.9);
  for (int i = 0; i < 3; i++) {
    double y = 0.5 + i * 2.6;
    cairo_move_to(cr, 2.0, y);
    av_quad_to(cr, -1.5, y + 0.6, -4.5, y + 0.2);
    cairo_stroke(cr);
  }
  cairo_restore(cr);

  /* Errol has seen better decades: feathers out of place all over him */
  av_set_rgba(cr, O_MID, 1);
  cairo_set_line_width(cr, 1.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  const double spike[4][3] = {
    { -7.0, -8.0, -2.30 }, { -2.0, -9.4, -1.75 },
    { -11.4, 1.4,  3.05 }, {  2.0, 10.4,  1.35 }
  };
  for (int i = 0; i < 4; i++) {
    double len = (2.2 + b->fluff * 2.6) * (0.7 + 0.3 * sin(b->f.t * 2.1 + i));
    cairo_move_to(cr, spike[i][0], spike[i][1]);
    cairo_line_to(cr, spike[i][0] + cos(spike[i][2]) * len,
                      spike[i][1] + sin(spike[i][2]) * len);
    cairo_stroke(cr);
  }
}

static void draw_wing(Owl *b, cairo_t *cr, int near) {
  Flyer *f = &b->f;
  if (av_pixel_mode() && !near) return;

  double y_off = near ? 0 : 2.4;
  double shade = near ? 0.0 : 0.34;
  double alpha = near ? 1.0 : 0.85;
  Rgb base = av_mix(O_BASE, O_DEEP, shade);
  Rgb tipc = av_mix(O_DARK, O_DEEP, shade);

  /* folded, or hanging useless after the impact */
  if (f->spread < 0.15 || b->wing_limp > 0.55) {
    double droop = b->wing_limp * 5.0;
    cairo_new_path(cr);
    cairo_move_to(cr, 4.0, -5.4 + y_off);
    av_quad_to(cr, -2.0, -6.6 + y_off, -9.0, -2.6 + droop + y_off);
    av_quad_to(cr, -11.6, -1.0 + droop + y_off, -10.6, 1.8 + droop + y_off);
    av_quad_to(cr, -4.0, 4.6 + droop * 0.5 + y_off, 2.0, 2.0 + y_off);
    av_quad_to(cr, 4.6, 0.0 + y_off, 4.0, -5.4 + y_off);
    cairo_close_path(cr);
    av_set_rgba(cr, base, alpha);
    cairo_fill_preserve(cr);
    if (av_pixel_mode()) {
      av_set_rgba(cr, O_OUTLINE, alpha);
      cairo_set_line_width(cr, 0.6 / f->scale);
      cairo_stroke(cr);
    } else {
      cairo_new_path(cr);
    }
    av_set_rgba(cr, O_DARK, alpha * 0.8);
    cairo_set_line_width(cr, 1.2);
    for (int i = 0; i < 2; i++) {
      double x = -2.5 - i * 3.4;
      cairo_move_to(cr, x + 1.6, -6.0 + y_off);
      av_quad_to(cr, x, -2.0 + droop * 0.5 + y_off, x - 0.6, 2.4 + droop * 0.5 + y_off);
      cairo_stroke(cr);
    }
    return;
  }

  WingPose w;
  wing_pose(f, 85.0, 14.0, WING_LEN, near ? 1.0 : 0.9, SH_X, SH_Y, y_off, &w);

  /* An owl wing is a broad rounded paddle, not a pointed blade. The tip is
   * blunt and the trailing edge sweeps far back — that huge area is the whole
   * reason the flight is slow and silent. */
  double ca = cos(w.hand_phi), sa = sin(w.hand_phi);
  double tipx = w.wx + ca * w.hand * 1.10;
  double tipy = w.wy + sa * w.hand * 1.10;
  double ta = w.hand_phi + 30 * D2R;
  double t2 = w.hand_phi + 62 * D2R;
  double trx = w.wx + cos(ta) * w.hand * 1.02;
  double try_ = w.wy + sin(ta) * w.hand * 1.02;
  double tbx = w.wx + cos(t2) * w.hand * 0.80;
  double tby = w.wy + sin(t2) * w.hand * 0.80;

  cairo_new_path(cr);
  cairo_move_to(cr, SH_X + 1.4, SH_Y + y_off);
  av_quad_to(cr, w.bx, w.by, tipx, tipy);
  av_quad_to(cr, av_lerp(tipx, trx, 0.5) + 1.2, av_lerp(tipy, try_, 0.5) + 1.2, trx, try_);
  cairo_line_to(cr, tbx, tby);
  av_quad_to(cr, av_lerp(tbx, -10.0, 0.55) - 2.0,
             av_lerp(tby, -1.0 + y_off, 0.55) + 2.0, -10.0, -1.0 + y_off);
  av_quad_to(cr, -2.0, SH_Y + 1.8 + y_off, SH_X + 1.4, SH_Y + y_off);
  cairo_close_path(cr);
  av_set_rgba(cr, base, alpha);
  cairo_fill_preserve(cr);
  if (av_pixel_mode()) {
    av_set_rgba(cr, O_OUTLINE, alpha);
    cairo_set_line_width(cr, 0.65 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
  }

  /* the barred outer half */
  cairo_save(cr);
  cairo_new_path(cr);
  cairo_move_to(cr, av_lerp(w.wx, tipx, 0.35), av_lerp(w.wy, tipy, 0.35));
  cairo_line_to(cr, tipx, tipy);
  cairo_line_to(cr, trx, try_);
  cairo_line_to(cr, tbx, tby);
  cairo_close_path(cr);
  av_set_rgba(cr, tipc, alpha);
  cairo_fill(cr);
  cairo_restore(cr);
}

static void draw_head(Owl *b, cairo_t *cr) {
  Flyer *f = &b->f;
  /* Owls stabilise the head hard: it stays put while the body heaves. */
  double hy = -f->bob * 0.7 / (f->scale > 0 ? 1 : 1) + b->head_dip * 7.0;
  double turn = b->head_turn;
  double hx = HEAD_X - turn * 1.2;

  cairo_save(cr);
  cairo_translate(cr, hx - HEAD_X, hy);

  /* ear tufts, ragged and never quite symmetrical */
  av_set_rgba(cr, O_DARK, 1);
  for (int i = 0; i < 2; i++) {
    double bx = HEAD_X - 4.2 + i * 6.8;
    double h = (4.6 + i * 1.2) * (0.4 + b->tufts * 0.9);
    cairo_new_path(cr);
    cairo_move_to(cr, bx - 2.2, HEAD_Y - HEAD_R * 0.70);
    cairo_line_to(cr, bx - 0.6 - i * 0.8, HEAD_Y - HEAD_R * 0.70 - h);
    cairo_line_to(cr, bx + 2.2, HEAD_Y - HEAD_R * 0.58);
    cairo_close_path(cr);
    cairo_fill(cr);
  }

  /* skull: enormous, and joined straight to the body with no neck at all */
  cairo_new_path(cr);
  cairo_arc(cr, HEAD_X, HEAD_Y, HEAD_R * (1 + b->fluff * 0.06), 0, TAU);
  cairo_pattern_t *g = cairo_pattern_create_radial(HEAD_X + 2, HEAD_Y - 2, 1,
                                                   HEAD_X, HEAD_Y, HEAD_R * 1.4);
  cairo_pattern_add_color_stop_rgb(g, 0.0, O_LIGHT.r, O_LIGHT.g, O_LIGHT.b);
  cairo_pattern_add_color_stop_rgb(g, 1.0, O_BASE.r, O_BASE.g, O_BASE.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, O_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);

  /* the facial disc: the flat dish that aims sound at his ears */
  double dx = DISC_X - turn * 2.6;
  cairo_new_path(cr);
  cairo_arc(cr, dx, DISC_Y, DISC_R, 0, TAU);
  av_set_rgba(cr, O_CREAM, 1);
  cairo_fill_preserve(cr);
  av_set_rgba(cr, O_DARK, 0.75);
  cairo_set_line_width(cr, 0.8);
  cairo_stroke(cr);

  /* eyes: forward-facing, which is why an owl looks at you and not past you */
  double near_x = EYE_X - turn * 2.2, far_x = dx - DISC_R * 0.62 - turn * 1.4;
  double far_vis = av_clamp(0.30 + turn * 0.9, 0, 1);
  double lid = b->dazed * 0.62 + (f->blink > 0 ? 1.0 : 0.0);
  lid = av_clamp(lid, 0, 1);

  for (int e = 0; e < 2; e++) {
    double ex = e ? near_x : far_x;
    double er = e ? 2.9 : 2.3 * far_vis;
    if (er < 0.5) continue;
    av_set_rgba(cr, O_EYE, e ? 1.0 : 0.85);
    cairo_arc(cr, ex, EYE_Y, er, 0, TAU);
    cairo_fill(cr);
    av_set_rgba(cr, O_OUTLINE, 1);
    cairo_arc(cr, ex, EYE_Y, er * 0.52, 0, TAU);
    cairo_fill(cr);
    if (lid > 0.02) {                 /* heavy lids: blinking, or concussed */
      av_set_rgba(cr, O_LIGHT, 1);
      cairo_save(cr);
      cairo_rectangle(cr, ex - er - 1, EYE_Y - er - 1, (er + 1) * 2, (er + 1) * 2 * lid * 0.9);
      cairo_clip(cr);
      cairo_arc(cr, ex, EYE_Y, er + 0.3, 0, TAU);
      cairo_fill(cr);
      cairo_restore(cr);
    }
  }

  /* small hooked beak, mostly buried in the disc */
  av_set_rgba(cr, O_BEAK, 1);
  cairo_new_path(cr);
  cairo_move_to(cr, dx + 1.2, EYE_Y + 1.6);
  cairo_line_to(cr, dx + 3.4 - turn * 0.8, EYE_Y + 2.2);
  cairo_line_to(cr, dx + 1.4, EYE_Y + 4.6);
  cairo_close_path(cr);
  cairo_fill(cr);

  cairo_restore(cr);
}

void owl_draw(Owl *b, cairo_t *cr) {
  Flyer *f = &b->f;
  cairo_save(cr);
  flyer_transform(f, cr);

  draw_tail(b, cr);
  if (!av_pixel_mode()) draw_wing(b, cr, 0);
  draw_legs(b, cr);
  draw_body(b, cr);
  draw_wing(b, cr, 1);
  draw_head(b, cr);          /* the head is so big it sits over everything */
  if (b->carrying) {
    double reach = LEG_LEN * av_lerp(0.3, 1.0, b->legs_out);
    av_draw_tied_letter(cr, HIP_X + 1.6 + b->flare * 5.0,
                        HIP_Y + reach * 0.55, 0.26, 0.5);
  }

  cairo_restore(cr);
}

void owl_bbox(Owl *b, double *x0, double *y0, double *x1, double *y1) {
  double r = 46 * b->f.scale;
  double cx = b->f.x, cy = b->f.y + b->f.bob;
  if (cx - r < *x0) *x0 = cx - r;
  if (cy - r < *y0) *y0 = cy - r;
  if (cx + r > *x1) *x1 = cx + r;
  if (cy + r > *y1) *y1 = cy + r;
}
