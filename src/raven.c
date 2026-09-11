/* The raven.
 *
 * The largest of the letter-birds and the gravest. It shares the pigeon's
 * build — it lands, it walks, it sets the message down and waits — but nothing
 * about it is quick. The beat is slow and heavy, the walk is a deliberate
 * stride, and where the pigeon leaves once you have read the thing, the raven
 * stays on, longer than any of them, as though reluctant to have brought it.
 *
 * It is black, which is a problem against a dark desktop, so it is not painted
 * black: it is a very dark blue-grey with a cool edge of light down one side
 * and an oil-on-water sheen where the gloss catches — which is what a raven
 * actually looks like. Heavy bill, a shag of throat feathers that lifts when it
 * croaks, and a wedge of a tail. It never burns.
 */
#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
/* Never pure black: the body is dark, the outline is *lighter* than the body
 * so the silhouette survives against a dark background, and the sheen is the
 * blue-and-purple an actual raven throws. */
/* A raven is BLACK. The body stays near-black; the sheen is a cool steel used
 * only as thin highlight lines and small glints, never as a fill, or the bird
 * turns blue and stops being a raven. The outline is a touch lighter than the
 * body so the silhouette survives a dark desktop. */
/* A raven at this size reads as dark slate, not pure black — kept clearly above
 * the near-black desktop so the silhouette is solid, with black shadows and a
 * neutral (never blue) sheen. */
static const Rgb R_BODY    = { 0.161, 0.173, 0.204 };
static const Rgb R_BODY2   = { 0.098, 0.106, 0.133 };   /* shadow */
static const Rgb R_SHEEN   = { 0.322, 0.357, 0.427 };   /* cool steel, desaturated */
static const Rgb R_SHEEN2  = { 0.212, 0.227, 0.278 };   /* the faintest cool wash */
static const Rgb R_BILL    = { 0.086, 0.090, 0.114 };
static const Rgb R_BILLHI  = { 0.200, 0.216, 0.259 };
static const Rgb R_FOOT    = { 0.075, 0.078, 0.094 };
static const Rgb R_FOOTHI  = { 0.169, 0.176, 0.208 };
static const Rgb R_EYE     = { 0.043, 0.043, 0.063 };
static const Rgb R_CATCH   = { 0.612, 0.663, 0.733 };
static const Rgb R_OUTLINE = { 0.278, 0.306, 0.373 };

/* ---- anatomy, in body units (+x forward, +y down) --------------------- */
/* Bigger than the pigeon in every dimension, and the bill much heavier. */
#define SH_X     2.5
#define SH_Y    -5.6
#define WING_LEN 29.0
#define TAIL_X  -13.5
#define TAIL_Y   -1.2
#define TAIL_LEN 15.0        /* a wedge: the middle feathers run longest */
#define HEAD_X   14.2
#define HEAD_Y  -10.6
#define HEAD_R    4.6
#define EYE_X    15.9
#define EYE_Y   -11.2
#define BILL_X   22.6
#define BILL_Y   -9.4
#define BILL_END 28.0        /* heavy and deep, faintly hooked */
#define HIP_X     1.5
#define HIP_Y     6.0
#define LEG_LEN   8.6
#define STAND_H  (HIP_Y + LEG_LEN)

#define STEP_LEN   8.6      /* a long, deliberate stride */
#define WALK_SPEED 22.0     /* body units per second */
#define HEAD_THRUST 0.32

void raven_init(Raven *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  double W = av_world();
  flyer_init(&b->f, x, y);
  b->f.scale = scale;
  b->f.max_speed = 210 * W;      /* heavy and unhurried */
  b->f.max_force = 900 * W;
  b->f.wander_gain = 120 * W;
  b->f.bounding = 1;
  b->f.fore_floor = 0.60;
  b->carrying = 1;
  b->stand_h = STAND_H;
  b->next_idle = av_rand_range(1.2, 3.0);
  b->tail_fan = 0.10;
}

Vec raven_capsule_point(Raven *b) {
  return flyer_to_world(&b->f, HIP_X - 1.2, HIP_Y + LEG_LEN * 0.62);
}

int raven_walking(const Raven *b) {
  return b->grounded && fabs(b->walk_target - b->f.x) > 2.0 * b->f.scale;
}

void raven_walk_to(Raven *b, double world_x) { b->walk_target = world_x; }

void raven_touch_down(Raven *b, double ground_y) {
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

void raven_launch(Raven *b) {
  if (!b->grounded) return;
  b->grounded = 0;
  b->clapped = 0;
  b->clap = 0.5;                      /* heavy wings, a dull thud not a crack */
  b->crouch = 0;
  double W = av_world();
  b->f.vx = b->f.facing * 46 * W;
  b->f.vy = -190 * W;                 /* it labours up */
  b->f.hover = 0;
}

/* ---- wings ------------------------------------------------------------- */
/* Big wings, slow and deep. A raven's wingbeat is unmistakable — you can hear
 * it — so this is the slowest beat in the aviary. */
static void raven_wings(Raven *b, double dt, Particles *P) {
  Flyer *f = &b->f;

  if (b->grounded) {
    f->flap = av_damp(f->flap, 0.0, 9, dt);
    f->fold = av_damp(f->fold, 1.0, 7, dt);
    f->spread = av_damp(f->spread, 0.0, 8, dt);
    f->effort = av_damp(f->effort, 0.1, 6, dt);
    f->bob = av_damp(f->bob, 0, 9, dt);
    f->gliding = 0;
    return;
  }

  double accel = hypot(f->ax, f->ay);
  double climb = -f->vy / f->max_speed;
  double want = 0.42 + (accel / f->max_force) * 0.9 + fmax(0, climb) * 0.9;
  f->effort = av_damp(f->effort, av_clamp(want, 0.2, 1.6), 5, dt);

  f->bound_timer -= dt;
  if (f->bound_timer <= 0) {
    if (f->gliding) { f->gliding = 0; f->bound_timer = av_rand_range(1.1, 2.0); }
    else if (f->effort < 0.6 && flyer_speed(f) > f->max_speed * 0.5) {
      f->gliding = 1;
      f->bound_timer = av_rand_range(0.7, 1.5);
    } else {
      f->bound_timer = av_rand_range(0.5, 1.0);
    }
  }

  f->spread = av_damp(f->spread, f->gliding ? 0.96 : 1.0, 7, dt);

  if (f->gliding) {
    f->flap = av_damp(f->flap, -0.26, 6, dt);
    f->fold = av_damp(f->fold, 0.05, 6, dt);
    f->bob  = av_damp(f->bob, 0.7, 5, dt);
    f->vy += 60 * av_world() * dt;
  } else {
    double hz = av_lerp(3.4, 5.8, av_clamp(f->effort / 1.4, 0, 1));   /* slow */
    if (b->clap > 0.02) hz = av_lerp(hz, 7.0, b->clap);
    f->wing_hz = hz;

    double prev = f->wing_phase;
    f->wing_phase += hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase) * 0.6;

    /* a few dark motes off the tips on the heavy downbeat of a launch */
    if (b->clap > 0.25 && P) {
      double a = prev - floor(prev), c = f->wing_phase - floor(f->wing_phase);
      if (a > c) {
        Vec t = flyer_to_world(f, SH_X - 2, SH_Y - WING_LEN * 0.72);
        for (int i = 0; i < 4; i++)
          p_ash(P, t.x + av_rand_sym(5) * av_world(), t.y + av_rand_sym(3) * av_world(),
                av_rand_sym(26) * av_world(), -av_rand_range(4, 22) * av_world(),
                av_rand_range(0.5, 1.2), 0);
      }
    }

    double amp = av_lerp(1.2, 3.8, av_clamp(f->effort, 0, 1.4)) * f->scale;
    f->bob = av_damp(f->bob, -f->flap * amp, 18, dt);
  }

  if (b->clap > 0) b->clap = fmax(0.0, b->clap - dt / 0.9);
}

/* ---- the walk ---------------------------------------------------------- */
static double head_offset(const Raven *b) {
  double u = b->walk_phase - floor(b->walk_phase);
  double A = STEP_LEN * 0.5;
  if (u < HEAD_THRUST) return av_lerp(-A, A, av_ease_out_cubic(u / HEAD_THRUST));
  return av_lerp(A, -A, (u - HEAD_THRUST) / (1 - HEAD_THRUST));
}

static void foot_offset(const Raven *b, int leg, double *ox, double *lift) {
  double u = b->walk_phase + (leg ? 0.5 : 0.0);
  u -= floor(u);
  double S = STEP_LEN * 0.5;
  if (u < 0.58) {
    *ox = av_lerp(S, -S, u / 0.58);
    *lift = 0;
  } else {
    double t = (u - 0.58) / 0.42;
    *ox = av_lerp(-S, S, av_smooth(t));
    *lift = sin(t * M_PI) * 2.6;
  }
}

static void raven_ground(Raven *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  double sc = f->scale;

  f->y = b->ground_y - b->stand_h * sc + b->crouch * 3.4 * sc;
  f->vy = 0;
  f->heading = av_damp(f->heading, 0, 8, dt);

  double dx = b->walk_target - f->x;
  double want = 0;
  if (fabs(dx) > 2.0 * sc) {
    want = WALK_SPEED;
    f->facing = dx > 0 ? 1 : -1;
  }
  b->walk_speed = av_damp(b->walk_speed, want, 6, dt);
  f->facing_blend = av_damp(f->facing_blend, f->facing, 6, dt);

  double moved = b->walk_speed * sc * dt * (f->facing >= 0 ? 1 : -1);
  f->x += moved;
  f->vx = b->walk_speed * sc * (f->facing >= 0 ? 1 : -1);

  if (b->walk_speed > 0.5)
    b->walk_phase += fabs(moved) / (STEP_LEN * sc);
  else
    b->walk_phase = 0;

  double u = b->walk_phase - floor(b->walk_phase);
  double rock = b->walk_speed > 0.5 ? -fabs(sin(u * TAU)) * 0.8 * sc : 0;
  f->bob = av_damp(f->bob, rock, 16, dt);

  b->tail_fan  = av_damp(b->tail_fan, 0.10, 5, dt);
  b->tail_drop = av_damp(b->tail_drop, b->walk_speed > 0.5 ? -0.05 : 0.14, 4, dt);
  b->legs_out  = av_damp(b->legs_out, 1, 7, dt);
  b->flare     = av_damp(b->flare, 0, 6, dt);

  /* idle business: a standing raven looks around, and now and then throws its
   * head and gives a croak, throat hackles standing out */
  if (b->walk_speed < 0.5) {
    b->idle_t += dt;
    if (b->act) {
      b->act_t += dt;
      double dur = b->act == 1 ? 0.8 : (b->act == 3 ? 1.5 : 1.1);
      if (b->act_t > dur) { b->act = 0; b->act_t = 0; }
    } else if (b->idle_t > b->next_idle) {
      b->idle_t = 0;
      b->next_idle = av_rand_range(1.8, 4.4);
      double r = av_rand();
      b->act = r < 0.22 ? 1 : (r < 0.56 ? 2 : (r < 0.90 ? 3 : 4));
      b->act_t = 0;
      if (b->act == 4) {
        b->walk_target = f->x + av_rand_sym(16) * sc;
        b->act = 0;
      }
    }
  } else {
    b->act = 0;
    b->idle_t = 0;
  }

  double dip = 0, look = 0, throat = 0;
  if (b->act == 1) {                            /* a heavy peck */
    double t = b->act_t / 0.8;
    dip = sin(av_clamp(t, 0, 1) * M_PI) * 1.1;
  } else if (b->act == 2) {                     /* look about, slow */
    look = sin(b->act_t * 3.4) * 0.9;
  } else if (b->act == 3) {                     /* croak: head forward, hackles up */
    double t = av_clamp(b->act_t / 1.5, 0, 1);
    throat = sin(t * M_PI);
    dip = -sin(t * M_PI) * 0.5;                 /* head thrown a little forward/up */
  }
  if (b->capsule_drop > 0 && b->capsule_drop < 1) dip = 1.0;

  b->head_dip = av_damp(b->head_dip, dip, 9, dt);
  b->look = av_damp(b->look, look, 8, dt);
  b->throat = av_damp(b->throat, throat, 7, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) { f->blink = 0.13; f->blink_at = av_rand_range(2.2, 5.6); }
  if (f->blink > 0) f->blink -= dt;

  raven_wings(b, dt, P);
}

/* ---- airborne ---------------------------------------------------------- */

static void raven_air(Raven *b, double dt, Particles *P) {
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
                av_noise(f->t * 0.45 + f->noise_off, 0) * f->wander_gain,
                av_noise(f->t * 0.6 + f->noise_off, 1) * f->wander_gain * 0.7);

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
  raven_wings(b, dt, P);

  double want_flare = b->flare;
  b->tail_fan  = av_damp(b->tail_fan,  av_lerp(0.10, 1.0, want_flare), 6, dt);
  b->tail_drop = av_damp(b->tail_drop, av_lerp(0.0, 0.9, want_flare), 6, dt);
  b->legs_out  = av_damp(b->legs_out,  want_flare > 0.25 ? 1 : 0, 6, dt);
  f->pitch     = av_damp(f->pitch, -0.6 * want_flare, 6, dt);

  b->head_dip = av_damp(b->head_dip, 0, 8, dt);
  b->look = av_damp(b->look, 0, 8, dt);
  b->throat = av_damp(b->throat, 0, 8, dt);
  b->crouch = av_damp(b->crouch, 0, 8, dt);

  f->ax = 0;
  f->ay = 0;
}

void raven_update(Raven *b, double dt, Particles *P) {
  if (b->grounded) { b->f.t += dt; raven_ground(b, dt, P); }
  else raven_air(b, dt, P);
}

/* ---- drawing ----------------------------------------------------------- */

static double dip_dy(const Raven *b)  { return b->head_dip * 10.0; }
static double dip_dx(const Raven *b)  { return b->head_dip * 3.2; }

/* a rim of cool light down the top/back edge of any filled shape, so a black
 * bird still has a silhouette on a black desktop */
static void rim_and_fill(Raven *b, cairo_t *cr, Rgb fill) {
  av_set_rgba(cr, fill, 1);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, R_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
}

static void draw_tail(Raven *b, cairo_t *cr) {
  double fan = b->tail_fan;
  double th = (180.0 - b->tail_drop * 32.0) * D2R;
  double cx = cos(th), sy = sin(th);
  double tipx = TAIL_X + cx * TAIL_LEN;
  double tipy = TAIL_Y + sy * TAIL_LEN;
  double nx = -sy, ny = cx;

  /* The wedge: broad at the root, and instead of squaring off it comes to a
   * blunt central point — the middle rectrices longest. */
  double w0 = 1.8;
  double w1 = av_lerp(3.2, 7.6, fan);
  double shoulder = 0.60;
  double sxp = av_lerp(TAIL_X, tipx, shoulder);
  double syp = av_lerp(TAIL_Y, tipy, shoulder);
  double point = av_lerp(1.10, 1.30, fan);   /* central feathers overrun the rest */
  double px2 = TAIL_X + cx * TAIL_LEN * point;
  double py2 = TAIL_Y + sy * TAIL_LEN * point;

  cairo_new_path(cr);
  cairo_move_to(cr, TAIL_X + nx * w0, TAIL_Y + ny * w0);
  cairo_line_to(cr, sxp + nx * w1, syp + ny * w1);
  cairo_line_to(cr, tipx + nx * w1 * 0.35, tipy + ny * w1 * 0.35);
  cairo_line_to(cr, px2, py2);                       /* the wedge point */
  cairo_line_to(cr, tipx - nx * w1 * 0.35, tipy - ny * w1 * 0.35);
  cairo_line_to(cr, sxp - nx * w1, syp - ny * w1);
  cairo_line_to(cr, TAIL_X - nx * w0, TAIL_Y - ny * w0);
  cairo_close_path(cr);
  rim_and_fill(b, cr, R_BODY);

  /* a blue sheen laid along the upper half of the fan */
  if (!av_pixel_mode()) {
    av_set_rgba(cr, R_SHEEN, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, TAIL_X + nx * w0 * 0.4, TAIL_Y + ny * w0 * 0.4);
    cairo_line_to(cr, px2, py2);
    cairo_stroke(cr);
  }

  if (fan > 0.4) {
    av_set_rgba(cr, R_BODY2, 0.9);
    cairo_set_line_width(cr, 0.5);
    for (int i = -2; i <= 2; i++) {
      if (!i) continue;
      double k = i / 2.0;
      cairo_move_to(cr, TAIL_X + nx * w0 * k, TAIL_Y + ny * w0 * k);
      cairo_line_to(cr, tipx + nx * w1 * 0.35 * k, tipy + ny * w1 * 0.35 * k);
      cairo_stroke(cr);
    }
  }
}

static void draw_leg(Raven *b, cairo_t *cr, int leg, int near) {
  double sc_out = b->legs_out;
  if (sc_out < 0.02 && !b->grounded) return;

  double ox = 0, lift = 0;
  if (b->grounded && b->walk_speed > 0.5) foot_offset(b, leg, &ox, &lift);
  else ox = leg ? 1.6 : -1.6;

  double reach = LEG_LEN * av_lerp(0.35, 1.0, sc_out);
  double fx = HIP_X + ox * (b->grounded ? 1 : 0.4);
  double fy = HIP_Y + reach - lift;
  if (!b->grounded) fx += b->flare * 6.0;

  Rgb c = near ? R_FOOTHI : R_FOOT;
  av_set_rgba(cr, c, 1);
  cairo_set_line_width(cr, near ? 1.8 : 1.4);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, HIP_X + (leg ? 1.2 : -1.2), HIP_Y);
  av_quad_to(cr, fx - 1.8, HIP_Y + reach * 0.55, fx, fy);
  cairo_stroke(cr);

  if (sc_out > 0.5) {                          /* big scaly toes */
    cairo_set_line_width(cr, near ? 1.2 : 1.0);
    for (int t = 0; t < 3; t++) {
      cairo_move_to(cr, fx, fy);
      cairo_line_to(cr, fx + 1.6 + t * 1.3, fy + 1.0 - t * 0.3);
      cairo_stroke(cr);
    }
    cairo_move_to(cr, fx, fy);
    cairo_line_to(cr, fx - 2.2, fy + 0.8);
    cairo_stroke(cr);
  }
}

static void draw_carried_letter(Raven *b, cairo_t *cr) {
  if (!b->carrying) return;
  double ox = 0, lift = 0;
  if (b->grounded && b->walk_speed > 0.5) foot_offset(b, 1, &ox, &lift);
  else ox = 1.6;
  double reach = LEG_LEN * av_lerp(0.35, 1.0, b->legs_out);
  double x = HIP_X + ox * (b->grounded ? 1 : 0.4) - 0.4;
  double y = HIP_Y + reach * 0.58 - lift * 0.6;
  if (!b->grounded) x += b->flare * 6.0;
  av_draw_tied_letter(cr, x, y, 0.30 + b->f.bob * 0.02, 0.7);
}

static void draw_body(Raven *b, cairo_t *cr) {
  cairo_new_path(cr);
  cairo_move_to(cr, 12.0, -3.4);
  cairo_curve_to(cr,   6.4, -8.0,  -4.6, -8.4, -12.6, -5.0);
  cairo_curve_to(cr, -14.8, -3.6, -15.2, -0.6, -13.4,  1.4);
  cairo_curve_to(cr,  -8.6,  7.0,   0.0,  8.4,   7.8,  6.0);
  cairo_curve_to(cr,  11.4,  4.4,  13.2,  0.6,  12.0, -3.4);
  cairo_close_path(cr);

  /* Dark all through — the underside near-black, the back only barely lifted,
   * never a blue fill. */
  cairo_pattern_t *g = cairo_pattern_create_linear(-12, 5, 10, -7);
  cairo_pattern_add_color_stop_rgb(g, 0.00, R_BODY2.r, R_BODY2.g, R_BODY2.b);
  cairo_pattern_add_color_stop_rgb(g, 0.60, R_BODY.r,  R_BODY.g,  R_BODY.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, R_SHEEN2.r, R_SHEEN2.g, R_SHEEN2.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, R_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);

  /* the one sheen: a short, dim cool highlight over the shoulder, the way light
   * just catches the gloss of a black bird — a hint, never a bright saddle */
  av_set_rgba(cr, R_SHEEN, av_pixel_mode() ? 0.5 : 0.4);
  cairo_set_line_width(cr, 0.85);
  cairo_move_to(cr, 5.0, -5.6);
  av_quad_to(cr, 0.0, -6.8, -5.0, -5.6);
  cairo_stroke(cr);
}

/* the folded wing: long primaries that reach right back over the tail, the
 * corvid tell */
static void draw_folded_wing(Raven *b, cairo_t *cr) {
  cairo_new_path(cr);
  cairo_move_to(cr, 5.0, -6.2);
  av_quad_to(cr, -3.0, -7.4, -12.0, -3.8);
  av_quad_to(cr, -16.5, -2.0, -15.0, 1.0);       /* primaries overrun the body */
  av_quad_to(cr, -7.0, 3.4, 2.0, 1.4);
  av_quad_to(cr, 5.4, -0.6, 5.0, -6.2);
  cairo_close_path(cr);
  rim_and_fill(b, cr, R_BODY);
  cairo_new_path(cr);
  /* re-fill for clip */
  cairo_move_to(cr, 5.0, -6.2);
  av_quad_to(cr, -3.0, -7.4, -12.0, -3.8);
  av_quad_to(cr, -16.5, -2.0, -15.0, 1.0);
  av_quad_to(cr, -7.0, 3.4, 2.0, 1.4);
  av_quad_to(cr, 5.4, -0.6, 5.0, -6.2);
  cairo_close_path(cr);
  cairo_clip(cr);

  /* primary separations */
  av_set_rgba(cr, R_BODY2, 1);
  cairo_set_line_width(cr, 1.4);
  for (int i = 0; i < 3; i++) {
    double x = -6.0 - i * 3.2;
    cairo_move_to(cr, x + 2.4, -6.0);
    av_quad_to(cr, x, -1.0, x - 1.2, 3.0);
    cairo_stroke(cr);
  }
  /* one cool sheen streak across the coverts — thin, so it reads as gloss on
   * black, not as a blue wing */
  av_set_rgba(cr, R_SHEEN, av_pixel_mode() ? 0.55 : 0.35);
  cairo_set_line_width(cr, 0.9);
  cairo_move_to(cr, 1.0, -4.4);
  av_quad_to(cr, -6.0, -4.2, -11.5, -1.6);
  cairo_stroke(cr);
  cairo_reset_clip(cr);

  /* a dim cool edge along the leading edge — just enough to lift the wing off
   * the dark, not a bright stripe down the bird */
  av_set_rgba(cr, R_SHEEN, av_pixel_mode() ? 0.5 : 0.35);
  cairo_set_line_width(cr, 0.7);
  cairo_move_to(cr, 4.6, -6.0);
  av_quad_to(cr, -3.0, -6.8, -11.0, -3.8);
  cairo_stroke(cr);
}

static void draw_open_wing(Raven *b, cairo_t *cr, int near) {
  Flyer *f = &b->f;
  if (av_pixel_mode() && !near) return;
  if (f->spread < 0.05) return;

  double y_off = near ? 0 : 2.6;
  double len_k = near ? 1.0 : 0.90;
  double shade = near ? 0.0 : 0.35;
  double alpha = near ? 1.0 : 0.85;
  double range = av_lerp(80.0, 114.0, b->clap);

  WingPose w;
  wing_pose(f, range, 24.0, WING_LEN, len_k, SH_X, SH_Y, y_off, &w);

  Rgb base = av_mix(R_BODY,  R_BODY2, shade);
  Rgb edge = av_mix(R_SHEEN, R_BODY2, shade);
  Rgb tipc = av_mix(R_BODY2, R_BODY2, shade);

  if (av_pixel_mode()) {
    double ca = cos(w.hand_phi), sa = sin(w.hand_phi);
    double tipx = w.wx + ca * w.hand * 1.20;
    double tipy = w.wy + sa * w.hand * 1.20;
    double ta = w.hand_phi + 36 * D2R;
    double trx = w.wx + cos(ta) * w.hand * 0.88;
    double try_ = w.wy + sin(ta) * w.hand * 0.88;

    cairo_new_path(cr);
    cairo_move_to(cr, SH_X + 1.6, SH_Y + y_off);
    av_quad_to(cr, w.bx, w.by, tipx, tipy);
    cairo_line_to(cr, trx, try_);
    av_quad_to(cr, av_lerp(trx, -11.0, 0.5) - 2.0,
               av_lerp(try_, -1.0 + y_off, 0.5) + 1.6, -11.0, -1.0 + y_off);
    av_quad_to(cr, -2.0, SH_Y + 1.8 + y_off, SH_X + 1.6, SH_Y + y_off);
    cairo_close_path(cr);
    av_set_rgba(cr, base, alpha);
    cairo_fill_preserve(cr);
    av_set_rgba(cr, R_OUTLINE, alpha);
    cairo_set_line_width(cr, 0.65 / f->scale);
    cairo_stroke(cr);

    /* splayed primary "fingers": three notches at the wingtip */
    av_set_rgba(cr, R_BODY2, alpha);
    cairo_set_line_width(cr, 0.8 / f->scale);
    for (int i = 0; i < 3; i++) {
      double k = 0.62 + i * 0.14;
      double fxp = av_lerp(w.wx, tipx, k), fyp = av_lerp(w.wy, tipy, k);
      cairo_move_to(cr, av_lerp(SH_X, w.wx, 0.7), av_lerp(SH_Y, w.wy, 0.7));
      cairo_line_to(cr, fxp, fyp);
      cairo_stroke(cr);
    }
    /* blue sheen on the near wing */
    if (near) {
      av_set_rgba(cr, R_SHEEN, 0.55);
      cairo_set_line_width(cr, 1.2);
      cairo_move_to(cr, SH_X + 1.0, SH_Y + 1.0);
      av_quad_to(cr, av_lerp(SH_X, w.wx, 0.5), av_lerp(SH_Y, w.wy, 0.5) + 1.0,
                 w.wx, w.wy);
      cairo_stroke(cr);
    }
    return;
  }

  double sx2 = w.wx + cos(w.hand_phi) * w.hand * 0.34;
  double sy2 = w.wy + sin(w.hand_phi) * w.hand * 0.34;
  cairo_new_path(cr);
  cairo_move_to(cr, SH_X + 1.6, SH_Y + y_off);
  av_quad_to(cr, av_lerp(SH_X, w.bx, 0.5), av_lerp(SH_Y + y_off, w.by, 0.5), w.bx, w.by);
  cairo_line_to(cr, sx2, sy2);
  av_quad_to(cr, av_lerp(sx2, -11.0, 0.45), av_lerp(sy2, -1.0 + y_off, 0.45),
             -11.0, -1.0 + y_off);
  av_quad_to(cr, -2.0, SH_Y + 1.6 + y_off, SH_X + 1.6, SH_Y + y_off);
  cairo_close_path(cr);
  av_set_rgba(cr, base, alpha);
  cairo_fill(cr);

  int N = 7;
  double splay = (1 - f->fold * 0.5) * f->spread;
  for (int i = 0; i < N; i++) {
    double k = (double)i / (N - 1);
    double lag = f->wing_phase - (i + 1) * 0.05;
    double lf = f->gliding ? f->flap : flap_curve(lag);
    double lphi = (180 - lf * (range + 22.0)) * D2R;
    double ang = lphi + av_lerp(-4, 30, k) * splay * D2R;   /* wide finger splay */
    double len = w.hand * av_lerp(1.14, 0.66, pow(k, 1.3));

    double rx = av_lerp(w.wx, w.tx, 0.02 + k * 0.20);
    double ry = av_lerp(w.wy, w.ty, 0.02 + k * 0.20);
    double px = rx + cos(ang) * len;
    double py = ry + sin(ang) * len;
    double ww = av_lerp(2.0, 1.1, k) * (0.55 + 0.45 * w.fore);
    double nx = -sin(ang) * ww, ny = cos(ang) * ww;

    cairo_new_path(cr);
    cairo_move_to(cr, rx + nx * 0.7, ry + ny * 0.7);
    av_quad_to(cr, av_lerp(rx, px, 0.6) + nx, av_lerp(ry, py, 0.6) + ny, px, py);
    av_quad_to(cr, av_lerp(rx, px, 0.6) - nx * 0.75, av_lerp(ry, py, 0.6) - ny * 0.75,
               rx - nx * 0.7, ry - ny * 0.7);
    cairo_close_path(cr);

    cairo_pattern_t *g = cairo_pattern_create_linear(rx, ry, px, py);
    cairo_pattern_add_color_stop_rgba(g, 0.0, base.r, base.g, base.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 0.5, edge.r, edge.g, edge.b, alpha);
    cairo_pattern_add_color_stop_rgba(g, 1.0, tipc.r, tipc.g, tipc.b, alpha);
    cairo_set_source(cr, g);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }
}

static void draw_head(Raven *b, cairo_t *cr) {
  Flyer *f = &b->f;
  double hx = head_offset(b) * (b->grounded && b->walk_speed > 0.5 ? 1 : 0);
  double dx = hx + dip_dx(b) + b->look * 1.3;
  double dy = dip_dy(b);
  double shag = b->throat;

  /* neck and the shaggy throat: the hackles hang as a ragged edge under the
   * jaw, and lift outward on a croak */
  cairo_new_path(cr);
  cairo_move_to(cr, 6.8, -4.6);
  av_quad_to(cr, 9.6 + dx * 0.4, -8.8 + dy * 0.4, 12.2 + dx * 0.8, -9.2 + dy * 0.8);
  cairo_line_to(cr, 15.0 + dx, -6.0 + dy);
  /* the throat shag: a couple of jagged points that stand out when shag>0 */
  double s = 1.0 + shag * 3.2;
  av_quad_to(cr, 12.0 + dx * 0.6, -1.4 + dy * 0.4, 11.4 + s, -0.2);
  cairo_line_to(cr, 9.6 + s * 0.7, 1.2);
  cairo_line_to(cr, 8.0 + s * 0.4, -0.2);
  av_quad_to(cr, 7.4, -1.0, 7.0, -1.6);
  cairo_close_path(cr);
  rim_and_fill(b, cr, R_BODY);

  /* skull */
  double cx = HEAD_X + dx, cy = HEAD_Y + dy;
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, HEAD_R, 0, TAU);
  cairo_pattern_t *hg = cairo_pattern_create_radial(cx + 1.2, cy - 1.4, 0.4, cx, cy, HEAD_R * 1.5);
  cairo_pattern_add_color_stop_rgb(hg, 0.0, R_BODY.r,  R_BODY.g,  R_BODY.b);
  cairo_pattern_add_color_stop_rgb(hg, 0.65, R_BODY.r, R_BODY.g,  R_BODY.b);
  cairo_pattern_add_color_stop_rgb(hg, 1.0, R_BODY2.r, R_BODY2.g, R_BODY2.b);
  cairo_set_source(cr, hg);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, R_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(hg);

  /* a single dim cool glint high on the crown — the only bright the head gets */
  av_set_rgba(cr, R_SHEEN, av_pixel_mode() ? 0.5 : 0.4);
  cairo_arc(cr, cx - 0.4, cy - 1.9, 0.75, 0, TAU);
  cairo_fill(cr);

  /* the bill: heavy and deep at the base where it meets the forehead, tapering
   * to a faintly hooked tip. The culmen carries on the line of the crown, the
   * corvid profile, rather than being stuck on as a spike. */
  cairo_new_path(cr);
  cairo_move_to(cr, 15.6 + dx, -12.0 + dy);                  /* up on the forehead */
  av_quad_to(cr, 23.0 + dx, -11.2 + dy, BILL_END + dx, BILL_Y - 0.2 + dy);
  av_quad_to(cr, BILL_END - 0.8 + dx, BILL_Y + 1.4 + dy, 26.0 + dx, -7.8 + dy);
  cairo_line_to(cr, 16.4 + dx, -7.0 + dy);                   /* deep gape at the base */
  cairo_close_path(cr);
  rim_and_fill(b, cr, R_BILL);

  /* a top highlight along the culmen so the heavy bill has form */
  av_set_rgba(cr, R_BILLHI, av_pixel_mode() ? 0.9 : 0.6);
  cairo_set_line_width(cr, 0.9);
  cairo_move_to(cr, 16.4 + dx, -11.4 + dy);
  av_quad_to(cr, 23.0 + dx, -10.8 + dy, BILL_END - 1.0 + dx, BILL_Y - 0.2 + dy);
  cairo_stroke(cr);

  /* eye: dark, with a small cool catch — not the pigeon's bright iris */
  double ex = EYE_X + dx, ey = EYE_Y + dy;
  if (f->blink > 0) {
    av_set_rgba(cr, R_BODY2, 0.9);
    cairo_set_line_width(cr, 0.9);
    cairo_new_path(cr);
    cairo_arc(cr, ex, ey, 1.6, 0.2, M_PI - 0.2);
    cairo_stroke(cr);
  } else {
    av_set_rgba(cr, R_EYE, 1);
    cairo_arc(cr, ex, ey, 1.8, 0, TAU);
    cairo_fill(cr);
    av_set_rgba(cr, R_CATCH, 1);
    cairo_arc(cr, ex + 0.6, ey - 0.6, 0.55, 0, TAU);
    cairo_fill(cr);
  }
}

void raven_draw(Raven *b, cairo_t *cr) {
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

void raven_bbox(Raven *b, double *x0, double *y0, double *x1, double *y1) {
  double r = 46 * b->f.scale;
  double cx = b->f.x, cy = b->f.y + b->f.bob;
  if (cx - r < *x0) *x0 = cx - r;
  if (cy - r < *y0) *y0 = cy - r;
  if (cx + r > *x1) *x1 = cx + r;
  if (cy + r > *y1) *y1 = cy + r;
}
