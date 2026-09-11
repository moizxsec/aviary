/* The dove.
 *
 * The pigeon's cousin, and built on the same bones: it lands, it bob-walks, it
 * sets the letter down and waits to be noticed, then leaves. What is different
 * is the register. The pigeon is grey and put-upon and cracks its wings on the
 * way out; the dove is pale, unhurried, and lifts off on a soft whistle. Where
 * the pigeon has an oil-slick throat, the dove has only a faint pink wash on
 * the nape and a scatter of dark spots down the wing.
 *
 * The flight and the walk are the pigeon's, proven, and only retuned slower.
 * It never burns, and it is never in a hurry.
 */
#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
static const Rgb D_WHITE   = { 0.945, 0.929, 0.902 };   /* breast, belly, face */
static const Rgb D_PALE    = { 0.851, 0.831, 0.804 };
static const Rgb D_LIGHT   = { 0.749, 0.729, 0.717 };
static const Rgb D_GREY    = { 0.612, 0.596, 0.604 };
static const Rgb D_SLATE   = { 0.494, 0.482, 0.510 };
static const Rgb D_DARK    = { 0.361, 0.353, 0.396 };
static const Rgb D_DEEP    = { 0.255, 0.251, 0.302 };
static const Rgb D_NAPE    = { 0.741, 0.600, 0.608 };   /* the pink at the neck */
static const Rgb D_BILL    = { 0.176, 0.161, 0.184 };
static const Rgb D_CERE    = { 0.902, 0.886, 0.878 };
static const Rgb D_FOOT    = { 0.855, 0.514, 0.490 };
static const Rgb D_FOOTD   = { 0.655, 0.361, 0.361 };
static const Rgb D_SPOT    = { 0.243, 0.231, 0.259 };   /* the wing spots */
static const Rgb D_EYE     = { 0.106, 0.098, 0.129 };   /* doves have a dark eye */
static const Rgb D_RING    = { 0.702, 0.741, 0.769 };   /* pale orbital ring */
static const Rgb D_OUTLINE = { 0.176, 0.169, 0.212 };

/* ---- anatomy, in body units (+x forward, +y down) ---------------------- */
/* The dove is a touch slimmer than the pigeon and carries a longer tail. */
#define SH_X     2.5
#define SH_Y    -4.8
#define WING_LEN 25.0
#define TAIL_X  -13.0
#define TAIL_Y   -1.0
#define TAIL_LEN 16.0        /* longer than the pigeon's 13 */
#define HEAD_X   13.4
#define HEAD_Y  -10.4
#define HEAD_R    4.2        /* rounder, smaller skull */
#define EYE_X    15.0
#define EYE_Y   -11.0
#define BILL_X   20.8
#define BILL_Y   -9.0
#define HIP_X     1.5
#define HIP_Y     5.4
#define LEG_LEN   7.6
#define STAND_H  (HIP_Y + LEG_LEN)

#define STEP_LEN   7.0       /* body units covered per stride */
#define WALK_SPEED 20.0      /* body units per second — slower than the pigeon */
#define HEAD_THRUST 0.30     /* fraction of the stride spent thrusting */

void dove_init(Dove *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  double W = av_world();
  flyer_init(&b->f, x, y);
  b->f.scale = scale;
  b->f.max_speed = 220 * W;      /* lighter and calmer than the pigeon */
  b->f.max_force = 900 * W;
  b->f.wander_gain = 130 * W;
  b->f.bounding = 1;
  b->f.fore_floor = 0.62;
  b->carrying = 1;
  b->stand_h = STAND_H;
  b->next_idle = av_rand_range(0.7, 2.0);
  b->tail_fan = 0.08;
}

Vec dove_capsule_point(Dove *b) {
  return flyer_to_world(&b->f, HIP_X - 1.0, HIP_Y + LEG_LEN * 0.62);
}

int dove_walking(const Dove *b) {
  return b->grounded && fabs(b->walk_target - b->f.x) > 2.0 * b->f.scale;
}

void dove_walk_to(Dove *b, double world_x) { b->walk_target = world_x; }

void dove_touch_down(Dove *b, double ground_y) {
  b->grounded = 1;
  b->ground_y = ground_y;
  b->f.y = ground_y - b->stand_h * b->f.scale;
  b->f.vx = b->f.vy = 0;
  b->f.heading = 0;
  b->f.pitch = 0;
  b->walk_target = b->f.x;
  b->walk_speed = 0;
  b->flare = 0;
}

void dove_launch(Dove *b) {
  if (!b->grounded) return;
  b->grounded = 0;
  b->clapped = 0;
  b->clap = 0.7;                      /* a whistle, not the pigeon's hard clack */
  b->crouch = 0;
  b->bow = 0;
  double W = av_world();
  b->f.vx = b->f.facing * 34 * W;
  b->f.vy = -175 * W;                 /* lifts less steeply than the pigeon */
  b->f.hover = 0;
}

/* ---- wings ------------------------------------------------------------- */
/* Same beat as the pigeon, slower, and the upstroke does not crack overhead —
 * a dove's wings whistle rather than clap, so `clap` here only lifts the beat
 * frequency for a moment on take-off. */
static void dove_wings(Dove *b, double dt, Particles *P) {
  Flyer *f = &b->f;

  if (b->grounded) {
    f->flap = av_damp(f->flap, 0.0, 10, dt);
    f->fold = av_damp(f->fold, 1.0, 8, dt);
    f->spread = av_damp(f->spread, 0.0, 9, dt);
    f->effort = av_damp(f->effort, 0.1, 6, dt);
    f->bob = av_damp(f->bob, 0, 10, dt);
    f->gliding = 0;
    return;
  }

  double accel = hypot(f->ax, f->ay);
  double climb = -f->vy / f->max_speed;
  double want = 0.36 + (accel / f->max_force) * 0.85 + fmax(0, climb) * 0.85;
  f->effort = av_damp(f->effort, av_clamp(want, 0.2, 1.5), 5, dt);

  f->bound_timer -= dt;
  if (f->bound_timer <= 0) {
    if (f->gliding) { f->gliding = 0; f->bound_timer = av_rand_range(1.0, 1.9); }
    else if (f->effort < 0.6 && flyer_speed(f) > f->max_speed * 0.5) {
      f->gliding = 1;
      f->bound_timer = av_rand_range(0.6, 1.3);   /* holds a glide a beat longer */
    } else {
      f->bound_timer = av_rand_range(0.4, 0.9);
    }
  }

  f->spread = av_damp(f->spread, f->gliding ? 0.94 : 1.0, 8, dt);

  if (f->gliding) {
    f->flap = av_damp(f->flap, -0.32, 7, dt);
    f->fold = av_damp(f->fold, 0.04, 7, dt);
    f->bob  = av_damp(f->bob, 0.6, 5, dt);
    f->vy += 56 * av_world() * dt;
  } else {
    double hz = av_lerp(3.8, 6.6, av_clamp(f->effort / 1.4, 0, 1));
    if (b->clap > 0.02) hz = av_lerp(hz, 8.2, b->clap);
    f->wing_hz = hz;

    double prev = f->wing_phase;
    f->wing_phase += hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase) * 0.6;

    /* the wing-whistle: a few pale motes off the tips at the top of the beat,
     * where the pigeon throws a clap of dust */
    if (b->clap > 0.25 && P) {
      double a = prev - floor(prev), c = f->wing_phase - floor(f->wing_phase);
      if (a > c) {
        Vec t = flyer_to_world(f, SH_X - 2, SH_Y - WING_LEN * 0.7);
        for (int i = 0; i < 3; i++)
          p_ash(P, t.x + av_rand_sym(4) * av_world(), t.y + av_rand_sym(3) * av_world(),
                av_rand_sym(20) * av_world(), -av_rand_range(3, 18) * av_world(),
                av_rand_range(0.4, 0.9), 0);
      }
    }

    double amp = av_lerp(1.0, 3.0, av_clamp(f->effort, 0, 1.4)) * f->scale;
    f->bob = av_damp(f->bob, -f->flap * amp, 20, dt);
  }

  if (b->clap > 0) b->clap = fmax(0.0, b->clap - dt / 0.85);
}

/* ---- the walk ---------------------------------------------------------- */
/* Doves bob-walk exactly as pigeons do: the head is held still in space while
 * the body advances under it, then snapped forward. Linear during the hold. */
static double head_offset(const Dove *b) {
  double u = b->walk_phase - floor(b->walk_phase);
  double A = STEP_LEN * 0.5;
  if (u < HEAD_THRUST) return av_lerp(-A, A, av_ease_out_cubic(u / HEAD_THRUST));
  return av_lerp(A, -A, (u - HEAD_THRUST) / (1 - HEAD_THRUST));
}

static void foot_offset(const Dove *b, int leg, double *ox, double *lift) {
  double u = b->walk_phase + (leg ? 0.5 : 0.0);
  u -= floor(u);
  double S = STEP_LEN * 0.5;
  if (u < 0.55) {
    *ox = av_lerp(S, -S, u / 0.55);
    *lift = 0;
  } else {
    double t = (u - 0.55) / 0.45;
    *ox = av_lerp(-S, S, av_smooth(t));
    *lift = sin(t * M_PI) * 2.2;
  }
}

static void dove_ground(Dove *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  double sc = f->scale;

  f->y = b->ground_y - b->stand_h * sc + (b->crouch * 3.0 + b->bow * 2.2) * sc;
  f->vy = 0;
  f->heading = av_damp(f->heading, 0, 8, dt);

  double dx = b->walk_target - f->x;
  double want = 0;
  if (fabs(dx) > 2.0 * sc) {
    want = WALK_SPEED;
    f->facing = dx > 0 ? 1 : -1;
  }
  b->walk_speed = av_damp(b->walk_speed, want, 6, dt);
  f->facing_blend = av_damp(f->facing_blend, f->facing, 7, dt);

  double moved = b->walk_speed * sc * dt * (f->facing >= 0 ? 1 : -1);
  f->x += moved;
  f->vx = b->walk_speed * sc * (f->facing >= 0 ? 1 : -1);

  if (b->walk_speed > 0.5)
    b->walk_phase += fabs(moved) / (STEP_LEN * sc);
  else
    b->walk_phase = 0;

  double u = b->walk_phase - floor(b->walk_phase);
  double rock = b->walk_speed > 0.5 ? -fabs(sin(u * TAU)) * 0.6 * sc : 0;
  f->bob = av_damp(f->bob, rock, 18, dt);

  b->tail_fan  = av_damp(b->tail_fan, 0.08, 5, dt);
  b->tail_drop = av_damp(b->tail_drop, b->walk_speed > 0.5 ? -0.05 : 0.20, 4, dt);
  b->legs_out  = av_damp(b->legs_out, 1, 8, dt);
  b->flare     = av_damp(b->flare, 0, 6, dt);

  /* idle business: a standing dove favours a slow coo over a peck */
  if (b->walk_speed < 0.5) {
    b->idle_t += dt;
    if (b->act) {
      b->act_t += dt;
      double dur = b->act == 1 ? 0.7 : (b->act == 3 ? 1.4 : 1.0);
      if (b->act_t > dur) { b->act = 0; b->act_t = 0; }
    } else if (b->idle_t > b->next_idle) {
      b->idle_t = 0;
      b->next_idle = av_rand_range(1.3, 3.8);
      double r = av_rand();
      /* weighted towards looking about and cooing, away from pecking */
      b->act = r < 0.16 ? 1 : (r < 0.56 ? 2 : (r < 0.90 ? 3 : 4));
      b->act_t = 0;
      if (b->act == 4) {
        b->walk_target = f->x + av_rand_sym(12) * sc;
        b->act = 0;
      }
    }
  } else {
    b->act = 0;
    b->idle_t = 0;
  }

  double dip = 0, look = 0, puff = 0;
  if (b->act == 1) {                            /* a light peck */
    double t = b->act_t / 0.7;
    dip = sin(av_clamp(t, 0, 1) * M_PI) * 0.9;
  } else if (b->act == 2) {                     /* look about */
    look = sin(b->act_t * 4.2) * 0.8;
  } else if (b->act == 3) {                     /* coo: throat swells, slow */
    puff = sin(av_clamp(b->act_t / 1.4, 0, 1) * M_PI) * 1.0;
    dip  = sin(av_clamp(b->act_t / 1.4, 0, 1) * M_PI) * 0.35;
  }
  if (b->capsule_drop > 0 && b->capsule_drop < 1) dip = 1.0;

  b->head_dip = av_damp(b->head_dip, dip + b->bow, 9, dt);
  b->look = av_damp(b->look, look, 8, dt);
  b->puff = av_damp(b->puff, puff, 6, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) { f->blink = 0.12; f->blink_at = av_rand_range(2.0, 5.4); }
  if (f->blink > 0) f->blink -= dt;

  dove_wings(b, dt, P);
}

/* ---- airborne ---------------------------------------------------------- */

static void dove_air(Dove *b, double dt, Particles *P) {
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
  if (f->wander_gain > 0)
    flyer_force(f,
                av_noise(f->t * 0.5 + f->noise_off, 0) * f->wander_gain,
                av_noise(f->t * 0.66 + f->noise_off, 1) * f->wander_gain * 0.7);

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
  dove_wings(b, dt, P);

  /* the same airbrake landing as the pigeon, a shade softer */
  double want_flare = b->flare;
  b->tail_fan  = av_damp(b->tail_fan,  av_lerp(0.08, 1.0, want_flare), 6, dt);
  b->tail_drop = av_damp(b->tail_drop, av_lerp(0.0, 0.85, want_flare), 6, dt);
  b->legs_out  = av_damp(b->legs_out,  want_flare > 0.25 ? 1 : 0, 7, dt);
  f->pitch     = av_damp(f->pitch, -0.58 * want_flare, 6, dt);

  b->head_dip = av_damp(b->head_dip, 0, 8, dt);
  b->look = av_damp(b->look, 0, 8, dt);
  b->puff = av_damp(b->puff, 0, 8, dt);
  b->crouch = av_damp(b->crouch, 0, 8, dt);

  f->ax = 0;
  f->ay = 0;
}

void dove_update(Dove *b, double dt, Particles *P) {
  if (b->grounded) { b->f.t += dt; dove_ground(b, dt, P); }
  else dove_air(b, dt, P);
}

/* ---- drawing ----------------------------------------------------------- */

static double dip_dy(const Dove *b)  { return b->head_dip * 9.0; }
static double dip_dx(const Dove *b)  { return b->head_dip * 2.4; }

static void draw_tail(Dove *b, cairo_t *cr) {
  double fan = b->tail_fan;
  double th = (180.0 - b->tail_drop * 34.0) * D2R;
  double cx = cos(th), sy = sin(th);
  double tipx = TAIL_X + cx * TAIL_LEN;
  double tipy = TAIL_Y + sy * TAIL_LEN;
  double nx = -sy, ny = cx;

  /* Longer and slimmer than the pigeon's, and it tapers to a point rather than
   * squaring off — a mourning dove's tail. */
  double w0 = 1.4;
  double w1 = av_lerp(2.0, 6.6, fan);
  double shoulder = 0.66;
  double sxp = av_lerp(TAIL_X, tipx, shoulder);
  double syp = av_lerp(TAIL_Y, tipy, shoulder);
  double cut = av_lerp(0.28, 0.62, fan);   /* narrow point at the end */

  cairo_new_path(cr);
  cairo_move_to(cr, TAIL_X + nx * w0, TAIL_Y + ny * w0);
  cairo_line_to(cr, sxp + nx * w1, syp + ny * w1);
  cairo_line_to(cr, tipx + nx * w1 * cut, tipy + ny * w1 * cut);
  cairo_line_to(cr, tipx - nx * w1 * cut, tipy - ny * w1 * cut);
  cairo_line_to(cr, sxp - nx * w1, syp - ny * w1);
  cairo_line_to(cr, TAIL_X - nx * w0, TAIL_Y - ny * w0);
  cairo_close_path(cr);
  av_set_rgba(cr, D_GREY, 1);
  cairo_fill_preserve(cr);
  if (av_pixel_mode()) {
    av_set_rgba(cr, D_OUTLINE, 1);
    cairo_set_line_width(cr, 0.6 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
  }

  /* The dove has no dark terminal band. Instead the outer feathers go pale —
   * the white tail-corners that flash when it takes off. */
  double bt = 0.86;
  double bx = av_lerp(TAIL_X, tipx, bt), by = av_lerp(TAIL_Y, tipy, bt);
  double bw = w1 * av_lerp(1.0, cut, (bt - shoulder) / (1 - shoulder));
  cairo_new_path(cr);
  cairo_move_to(cr, bx + nx * bw, by + ny * bw);
  cairo_line_to(cr, tipx + nx * w1 * cut, tipy + ny * w1 * cut);
  cairo_line_to(cr, tipx - nx * w1 * cut, tipy - ny * w1 * cut);
  cairo_line_to(cr, bx - nx * bw, by - ny * bw);
  cairo_close_path(cr);
  av_set_rgba(cr, D_PALE, 1);
  cairo_fill(cr);

  /* a thin dark line just inside the pale tip, the way the real feathers band */
  if (fan > 0.3) {
    double lt = 0.74;
    double lx = av_lerp(TAIL_X, tipx, lt), ly = av_lerp(TAIL_Y, tipy, lt);
    double lw = w1 * av_lerp(1.0, cut, (lt - shoulder) / (1 - shoulder));
    av_set_rgba(cr, D_DARK, 0.7);
    cairo_set_line_width(cr, 0.7);
    cairo_move_to(cr, lx + nx * lw, ly + ny * lw);
    cairo_line_to(cr, lx - nx * lw, ly - ny * lw);
    cairo_stroke(cr);
  }

  if (fan > 0.45) {
    av_set_rgba(cr, D_SLATE, 0.7);
    cairo_set_line_width(cr, 0.5);
    for (int i = -2; i <= 2; i++) {
      if (!i) continue;
      double k = i / 2.0;
      cairo_move_to(cr, TAIL_X + nx * w0 * k, TAIL_Y + ny * w0 * k);
      cairo_line_to(cr, tipx + nx * w1 * cut * k, tipy + ny * w1 * cut * k);
      cairo_stroke(cr);
    }
  }
}

static void draw_leg(Dove *b, cairo_t *cr, int leg, int near) {
  double sc_out = b->legs_out;
  if (sc_out < 0.02 && !b->grounded) return;

  double ox = 0, lift = 0;
  if (b->grounded && b->walk_speed > 0.5) foot_offset(b, leg, &ox, &lift);
  else ox = leg ? 1.3 : -1.3;

  double reach = LEG_LEN * av_lerp(0.35, 1.0, sc_out);
  double fx = HIP_X + ox * (b->grounded ? 1 : 0.4);
  double fy = HIP_Y + reach - lift;
  if (!b->grounded) fx += b->flare * 6.0;

  Rgb c = near ? D_FOOT : D_FOOTD;
  av_set_rgba(cr, c, 1);
  cairo_set_line_width(cr, near ? 1.4 : 1.1);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, HIP_X + (leg ? 1.0 : -1.0), HIP_Y);
  av_quad_to(cr, fx - 1.5, HIP_Y + reach * 0.55, fx, fy);
  cairo_stroke(cr);

  if (sc_out > 0.5) {
    cairo_set_line_width(cr, near ? 0.9 : 0.7);
    for (int t = 0; t < 3; t++) {
      cairo_move_to(cr, fx, fy);
      cairo_line_to(cr, fx + 1.3 + t * 1.0, fy + 0.9 - t * 0.25);
      cairo_stroke(cr);
    }
    cairo_move_to(cr, fx, fy);
    cairo_line_to(cr, fx - 1.8, fy + 0.7);
    cairo_stroke(cr);
  }
}

static void draw_carried_letter(Dove *b, cairo_t *cr) {
  if (!b->carrying) return;
  double ox = 0, lift = 0;
  if (b->grounded && b->walk_speed > 0.5) foot_offset(b, 1, &ox, &lift);
  else ox = 1.3;
  double reach = LEG_LEN * av_lerp(0.35, 1.0, b->legs_out);
  double x = HIP_X + ox * (b->grounded ? 1 : 0.4) - 0.4;
  double y = HIP_Y + reach * 0.58 - lift * 0.6;
  if (!b->grounded) x += b->flare * 6.0;
  av_draw_tied_letter(cr, x, y, 0.30 + b->f.bob * 0.02, 0.6);
}

static void draw_body(Dove *b, cairo_t *cr) {
  cairo_new_path(cr);
  cairo_move_to(cr, 11.0, -3.0);
  cairo_curve_to(cr,   6.0, -7.2,  -4.0, -7.4, -11.5, -4.6);
  cairo_curve_to(cr, -13.6, -3.4, -14.0, -0.6, -12.4,  1.2);
  cairo_curve_to(cr,  -8.0,  6.4,   0.0,  7.8,   7.2,  5.6);
  cairo_curve_to(cr,  10.6,  4.2,  12.0,  0.6,  11.0, -3.0);
  cairo_close_path(cr);

  /* pale all through: the back a warm grey, the breast almost white */
  cairo_pattern_t *g = cairo_pattern_create_linear(-12, 4, 10, -6);
  cairo_pattern_add_color_stop_rgb(g, 0.00, D_LIGHT.r, D_LIGHT.g, D_LIGHT.b);
  cairo_pattern_add_color_stop_rgb(g, 0.50, D_PALE.r,  D_PALE.g,  D_PALE.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, D_WHITE.r, D_WHITE.g, D_WHITE.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, D_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);
}

/* wing folded against the flank, carrying the dove's scatter of dark spots */
static void draw_folded_wing(Dove *b, cairo_t *cr) {
  (void)b;
  cairo_new_path(cr);
  cairo_move_to(cr, 4.5, -5.4);
  av_quad_to(cr, -2.0, -6.4, -9.5, -3.4);
  av_quad_to(cr, -12.4, -2.0, -11.6, 0.6);
  av_quad_to(cr, -6.0, 3.0, 1.5, 1.2);
  av_quad_to(cr, 4.8, -0.4, 4.5, -5.4);
  cairo_close_path(cr);
  av_set_rgba(cr, D_LIGHT, 1);
  cairo_fill_preserve(cr);
  cairo_clip(cr);

  /* the spots — a mourning dove's are a loose row down the coverts */
  av_set_rgba(cr, D_SPOT, 1);
  double spot[4][2] = { { -1.4, -3.6 }, { -4.6, -2.2 }, { -7.4, -1.0 }, { -3.0, 0.4 } };
  for (int i = 0; i < 4; i++) {
    cairo_new_path(cr);
    cairo_arc(cr, spot[i][0], spot[i][1], i == 3 ? 0.7 : 0.95, 0, TAU);
    cairo_fill(cr);
  }

  /* the dark primaries showing past the folded arm */
  av_set_rgba(cr, D_SLATE, 1);
  cairo_set_line_width(cr, 1.5);
  for (int i = 0; i < 2; i++) {
    double x = -8.6 - i * 1.8;
    cairo_move_to(cr, x + 2.0, -3.0);
    av_quad_to(cr, x, -0.4, x - 0.6, 2.0);
    cairo_stroke(cr);
  }
  cairo_reset_clip(cr);

  /* the pale trailing edge */
  av_set_rgba(cr, D_WHITE, av_pixel_mode() ? 0.95 : 0.6);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, -9.5, -3.2);
  av_quad_to(cr, -12.2, -1.8, -11.4, 0.6);
  cairo_stroke(cr);

  if (!av_pixel_mode()) {
    av_set_rgba(cr, D_DARK, 0.4);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 4.5, -5.4);
    av_quad_to(cr, -2.0, -6.4, -9.5, -3.4);
    cairo_stroke(cr);
  }
}

static void draw_open_wing(Dove *b, cairo_t *cr, int near) {
  Flyer *f = &b->f;
  if (av_pixel_mode() && !near) return;
  if (f->spread < 0.05) return;

  double y_off = near ? 0 : 2.2;
  double len_k = near ? 1.0 : 0.90;
  double shade = near ? 0.0 : 0.30;
  double alpha = near ? 1.0 : 0.85;
  double range = av_lerp(76.0, 104.0, b->clap);

  WingPose w;
  wing_pose(f, range, 20.0, WING_LEN, len_k, SH_X, SH_Y, y_off, &w);

  /* pale coverts, a grey trailing arm, dark primaries at the tip */
  Rgb base = av_mix(D_PALE,  D_DEEP, shade);
  Rgb edge = av_mix(D_LIGHT, D_DEEP, shade);
  Rgb tipc = av_mix(D_SLATE, D_DEEP, shade);

  if (av_pixel_mode()) {
    double ca = cos(w.hand_phi), sa = sin(w.hand_phi);
    double tipx = w.wx + ca * w.hand * 1.18;
    double tipy = w.wy + sa * w.hand * 1.18;
    double ta = w.hand_phi + 34 * D2R;
    double trx = w.wx + cos(ta) * w.hand * 0.86;
    double try_ = w.wy + sin(ta) * w.hand * 0.86;

    cairo_new_path(cr);
    cairo_move_to(cr, SH_X + 1.4, SH_Y + y_off);
    av_quad_to(cr, w.bx, w.by, tipx, tipy);
    cairo_line_to(cr, trx, try_);
    av_quad_to(cr, av_lerp(trx, -10.0, 0.5) - 2.0,
               av_lerp(try_, -1.0 + y_off, 0.5) + 1.5, -10.0, -1.0 + y_off);
    av_quad_to(cr, -2.0, SH_Y + 1.6 + y_off, SH_X + 1.4, SH_Y + y_off);
    cairo_close_path(cr);
    av_set_rgba(cr, base, alpha);
    cairo_fill_preserve(cr);
    av_set_rgba(cr, D_OUTLINE, alpha);
    cairo_set_line_width(cr, 0.65 / f->scale);
    cairo_stroke(cr);

    /* the dark outer third: the primaries */
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_move_to(cr, av_lerp(w.wx, tipx, 0.55), av_lerp(w.wy, tipy, 0.55));
    cairo_line_to(cr, tipx, tipy);
    cairo_line_to(cr, trx, try_);
    cairo_line_to(cr, av_lerp(w.wx, trx, 0.60), av_lerp(w.wy, try_, 0.60));
    cairo_close_path(cr);
    av_set_rgba(cr, tipc, alpha);
    cairo_fill(cr);
    cairo_restore(cr);
    return;
  }

  double sx2 = w.wx + cos(w.hand_phi) * w.hand * 0.34;
  double sy2 = w.wy + sin(w.hand_phi) * w.hand * 0.34;
  cairo_new_path(cr);
  cairo_move_to(cr, SH_X + 1.4, SH_Y + y_off);
  av_quad_to(cr, av_lerp(SH_X, w.bx, 0.5), av_lerp(SH_Y + y_off, w.by, 0.5), w.bx, w.by);
  cairo_line_to(cr, sx2, sy2);
  av_quad_to(cr, av_lerp(sx2, -10.0, 0.45), av_lerp(sy2, -1.0 + y_off, 0.45),
             -10.0, -1.0 + y_off);
  av_quad_to(cr, -2.0, SH_Y + 1.4 + y_off, SH_X + 1.4, SH_Y + y_off);
  cairo_close_path(cr);
  av_set_rgba(cr, base, alpha);
  cairo_fill(cr);

  int N = 6;
  double splay = (1 - f->fold * 0.5) * f->spread;
  for (int i = 0; i < N; i++) {
    double k = (double)i / (N - 1);
    double lag = f->wing_phase - (i + 1) * 0.045;
    double lf = f->gliding ? f->flap : flap_curve(lag);
    double lphi = (180 - lf * (range + 20.0)) * D2R;
    double ang = lphi + av_lerp(-4, 26, k) * splay * D2R;
    double len = w.hand * av_lerp(1.10, 0.66, pow(k, 1.3));

    double rx = av_lerp(w.wx, w.tx, 0.02 + k * 0.20);
    double ry = av_lerp(w.wy, w.ty, 0.02 + k * 0.20);
    double px = rx + cos(ang) * len;
    double py = ry + sin(ang) * len;
    double ww = av_lerp(1.7, 1.0, k) * (0.55 + 0.45 * w.fore);
    double nx = -sin(ang) * ww, ny = cos(ang) * ww;

    cairo_new_path(cr);
    cairo_move_to(cr, rx + nx * 0.7, ry + ny * 0.7);
    av_quad_to(cr, av_lerp(rx, px, 0.6) + nx, av_lerp(ry, py, 0.6) + ny, px, py);
    av_quad_to(cr, av_lerp(rx, px, 0.6) - nx * 0.75, av_lerp(ry, py, 0.6) - ny * 0.75,
               rx - nx * 0.7, ry - ny * 0.7);
    cairo_close_path(cr);

    cairo_pattern_t *g = cairo_pattern_create_linear(rx, ry, px, py);
    cairo_pattern_add_color_stop_rgba(g, 0.0, base.r, base.g, base.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 0.55, edge.r, edge.g, edge.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 1.0, tipc.r, tipc.g, tipc.b, alpha);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }
}

static void draw_head(Dove *b, cairo_t *cr) {
  Flyer *f = &b->f;
  double hx = head_offset(b) * (b->grounded && b->walk_speed > 0.5 ? 1 : 0);
  double dx = hx + dip_dx(b) + b->look * 1.2;
  double dy = dip_dy(b);

  /* neck: swells on a coo. The pink nape wash sits here, not an oil slick. */
  double puff = b->puff * 1.6;
  cairo_new_path(cr);
  cairo_move_to(cr, 6.4, -4.0);
  av_quad_to(cr, 9.0 + dx * 0.4, -8.2 + dy * 0.4, 11.4 + dx * 0.8, -8.6 + dy * 0.8);
  cairo_line_to(cr, 13.8 + dx, -5.6 + dy);
  av_quad_to(cr, 10.6 + puff, -1.2 + puff * 0.4, 7.2, -0.8);
  cairo_close_path(cr);
  av_set_rgba(cr, D_PALE, 1);
  cairo_fill(cr);

  if (!av_pixel_mode()) {
    cairo_pattern_t *g = cairo_pattern_create_linear(8, -6, 13, -2);
    cairo_pattern_add_color_stop_rgba(g, 0.0, D_NAPE.r, D_NAPE.g, D_NAPE.b, 0.55);
    cairo_pattern_add_color_stop_rgba(g, 1.0, D_PALE.r, D_PALE.g, D_PALE.b, 0.0);
    cairo_set_source(cr, g);
    cairo_new_path(cr);
    cairo_move_to(cr, 7.4, -3.2);
    av_quad_to(cr, 10.6 + dx * 0.5, -6.2 + dy * 0.5, 12.6 + dx * 0.8, -5.8 + dy * 0.8);
    av_quad_to(cr, 10.8 + puff, -1.6, 7.8, -1.2);
    cairo_close_path(cr);
    cairo_fill(cr);
  } else {
    av_set_rgba(cr, D_NAPE, 0.85);
    cairo_new_path(cr);
    cairo_arc(cr, 10.4 + dx * 0.7, -5.2 + dy * 0.7, 1.7, 0, TAU);
    cairo_fill(cr);
  }

  /* skull: rounder and paler than the pigeon's */
  double cx = HEAD_X + dx, cy = HEAD_Y + dy;
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, HEAD_R, 0, TAU);
  cairo_pattern_t *hg = cairo_pattern_create_radial(cx + 1.1, cy - 1.3, 0.4, cx, cy, HEAD_R * 1.5);
  cairo_pattern_add_color_stop_rgb(hg, 0.0, D_WHITE.r, D_WHITE.g, D_WHITE.b);
  cairo_pattern_add_color_stop_rgb(hg, 1.0, D_PALE.r,  D_PALE.g,  D_PALE.b);
  cairo_set_source(cr, hg);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, D_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(hg);

  /* bill: slim, dark, with the pale cere at its base */
  cairo_new_path(cr);
  cairo_move_to(cr, 16.9 + dx, -10.0 + dy);
  av_quad_to(cr, 19.6 + dx, -9.6 + dy, BILL_X + dx, BILL_Y + dy);
  av_quad_to(cr, 18.9 + dx, -7.9 + dy, 16.8 + dx, -7.7 + dy);
  cairo_close_path(cr);
  av_set_rgba(cr, D_BILL, 1);
  cairo_fill(cr);

  av_set_rgba(cr, D_CERE, 1);
  cairo_new_path(cr);
  cairo_arc(cr, 16.6 + dx, -10.4 + dy, 1.3, 0, TAU);
  cairo_fill(cr);

  /* eye: dark, ringed with pale skin — the opposite of the pigeon's orange */
  double ex = EYE_X + dx, ey = EYE_Y + dy;
  if (f->blink > 0) {
    av_set_rgba(cr, D_DARK, 0.9);
    cairo_set_line_width(cr, 0.8);
    cairo_new_path(cr);
    cairo_arc(cr, ex, ey, 1.4, 0.2, M_PI - 0.2);
    cairo_stroke(cr);
  } else {
    av_set_rgba(cr, D_RING, 1);
    cairo_arc(cr, ex, ey, 1.9, 0, TAU);
    cairo_fill(cr);
    av_set_rgba(cr, D_EYE, 1);
    cairo_arc(cr, ex + 0.15, ey, 1.3, 0, TAU);
    cairo_fill(cr);
    if (!av_pixel_mode()) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
      cairo_arc(cr, ex + 0.55, ey - 0.5, 0.4, 0, TAU);
      cairo_fill(cr);
    }
  }
}

void dove_draw(Dove *b, cairo_t *cr) {
  Flyer *f = &b->f;
  cairo_save(cr);
  flyer_transform(f, cr);

  draw_tail(b, cr);
  if (!b->grounded) draw_open_wing(b, cr, 0);
  draw_leg(b, cr, 0, 0);
  draw_body(b, cr);
  draw_head(b, cr);
  if (b->grounded || f->spread < 0.2) draw_folded_wing(b, cr);
  else draw_open_wing(b, cr, 1);
  draw_leg(b, cr, 1, 1);
  draw_carried_letter(b, cr);

  cairo_restore(cr);
}

void dove_bbox(Dove *b, double *x0, double *y0, double *x1, double *y1) {
  double r = 40 * b->f.scale;
  double cx = b->f.x, cy = b->f.y + b->f.bob;
  if (cx - r < *x0) *x0 = cx - r;
  if (cy - r < *y0) *y0 = cy - r;
  if (cx + r > *x1) *x1 = cx + r;
  if (cy + r > *y1) *y1 = cy + r;
}
