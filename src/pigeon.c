/* The pigeon.
 *
 * Everything the phoenix does, this bird does differently. It is heavy: the
 * beat is slower and deeper, it glides on raised wings rather than bounding,
 * it does not hover — it lands. Then it walks, which is where the real work
 * is, because a walking pigeon is doing something quite specific with its head.
 *
 * It never burns.
 */
#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
static const Rgb P_PALE    = { 0.808, 0.831, 0.871 };
static const Rgb P_LIGHT   = { 0.659, 0.698, 0.761 };
static const Rgb P_SLATE   = { 0.486, 0.533, 0.612 };
static const Rgb P_MID     = { 0.361, 0.408, 0.494 };
static const Rgb P_DARK    = { 0.251, 0.290, 0.369 };
static const Rgb P_DEEP    = { 0.165, 0.196, 0.259 };
static const Rgb P_GREEN   = { 0.290, 0.620, 0.494 };
static const Rgb P_VIOLET  = { 0.502, 0.376, 0.635 };
static const Rgb P_BILL    = { 0.180, 0.173, 0.204 };
static const Rgb P_CERE    = { 0.894, 0.902, 0.925 };
static const Rgb P_FOOT    = { 0.839, 0.463, 0.478 };
static const Rgb P_FOOTD   = { 0.651, 0.306, 0.337 };
static const Rgb P_EYE     = { 0.925, 0.573, 0.149 };

/* ---- anatomy, in body units (+x forward, +y down) ---------------------- */
#define SH_X     2.5
#define SH_Y    -5.0
#define WING_LEN 26.0
#define TAIL_X  -13.0
#define TAIL_Y   -1.0
#define TAIL_LEN 13.0
#define HEAD_X   13.6
#define HEAD_Y  -10.2
#define HEAD_R    4.6
#define EYE_X    15.4
#define EYE_Y   -10.9
#define BILL_X   21.5
#define BILL_Y   -8.8
#define HIP_X     1.5
#define HIP_Y     5.5
#define LEG_LEN   8.0
#define STAND_H  (HIP_Y + LEG_LEN)
#define NOSE_X   23.0

#define STEP_LEN   7.5      /* body units covered per stride */
#define WALK_SPEED 26.0     /* body units per second */
#define HEAD_THRUST 0.30    /* fraction of the stride spent thrusting */

void pigeon_init(Pigeon *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  double W = av_world();
  flyer_init(&b->f, x, y);
  b->f.scale = scale;
  b->f.max_speed = 260 * W;      /* heavier and less nimble than the phoenix */
  b->f.max_force = 1000 * W;
  b->f.wander_gain = 150 * W;
  b->f.bounding = 1;
  b->f.fore_floor = 0.62;      /* broad wings stay visible through mid-stroke */
  b->carrying = 1;
  b->stand_h = STAND_H;
  b->next_idle = av_rand_range(0.8, 2.2);
  b->tail_fan = 0.10;
}

Vec pigeon_capsule_point(Pigeon *b) {
  return flyer_to_world(&b->f, HIP_X - 1.0, HIP_Y + LEG_LEN * 0.62);
}

int pigeon_walking(const Pigeon *b) {
  return b->grounded && fabs(b->walk_target - b->f.x) > 2.0 * b->f.scale;
}

void pigeon_walk_to(Pigeon *b, double world_x) { b->walk_target = world_x; }

void pigeon_touch_down(Pigeon *b, double ground_y) {
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

void pigeon_launch(Pigeon *b) {
  if (!b->grounded) return;
  b->grounded = 0;
  b->clapped = 0;
  b->clap = 1.0;
  b->crouch = 0;
  double W = av_world();
  b->f.vx = b->f.facing * 40 * W;
  b->f.vy = -210 * W;                 /* pigeons leave steeply */
  b->f.hover = 0;
}

/* ---- wings ------------------------------------------------------------- */
/* Slower and deeper than a small bird. On take-off the upstroke goes past
 * vertical so the wingtips meet over the back — that is the crack you hear
 * when a pigeon leaves in a hurry. */
static void pigeon_wings(Pigeon *b, double dt, Particles *P) {
  Flyer *f = &b->f;

  if (b->grounded) {
    f->flap = av_damp(f->flap, 0.0, 10, dt);
    f->fold = av_damp(f->fold, 1.0, 8, dt);
    f->spread = av_damp(f->spread, 0.0, 9, dt);   /* folded against the body */
    f->effort = av_damp(f->effort, 0.1, 6, dt);
    f->bob = av_damp(f->bob, 0, 10, dt);
    f->gliding = 0;
    return;
  }

  double accel = hypot(f->ax, f->ay);
  double climb = -f->vy / f->max_speed;
  double want = 0.40 + (accel / f->max_force) * 0.9 + fmax(0, climb) * 0.9;
  f->effort = av_damp(f->effort, av_clamp(want, 0.2, 1.6), 5, dt);

  /* a pigeon does not bound like a finch; it flaps, then holds a flat glide */
  f->bound_timer -= dt;
  if (f->bound_timer <= 0) {
    if (f->gliding) { f->gliding = 0; f->bound_timer = av_rand_range(0.9, 1.7); }
    else if (f->effort < 0.6 && flyer_speed(f) > f->max_speed * 0.5) {
      f->gliding = 1;
      f->bound_timer = av_rand_range(0.5, 1.1);
    } else {
      f->bound_timer = av_rand_range(0.4, 0.9);
    }
  }

  f->spread = av_damp(f->spread, f->gliding ? 0.94 : 1.0, 8, dt);

  if (f->gliding) {
    /* held in a shallow V, barely losing height */
    f->flap = av_damp(f->flap, -0.30, 7, dt);
    f->fold = av_damp(f->fold, 0.04, 7, dt);
    f->bob  = av_damp(f->bob, 0.6, 5, dt);
    f->vy += 62 * av_world() * dt;
  } else {
    double hz = av_lerp(4.6, 7.8, av_clamp(f->effort / 1.4, 0, 1));
    if (b->clap > 0.02) hz = av_lerp(hz, 9.4, b->clap);
    f->wing_hz = hz;

    double prev = f->wing_phase;
    f->wing_phase += hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase) * 0.6;

    /* the clap happens at the very top of the upstroke */
    if (b->clap > 0.25 && P) {
      double a = prev - floor(prev), c = f->wing_phase - floor(f->wing_phase);
      if (a > c) {                              /* wrapped past the top */
        Vec t = flyer_to_world(f, SH_X - 2, SH_Y - WING_LEN * 0.75);
        for (int i = 0; i < 5; i++)
          p_ash(P, t.x + av_rand_sym(4) * av_world(), t.y + av_rand_sym(3) * av_world(),
                av_rand_sym(30) * av_world(), -av_rand_range(4, 26) * av_world(),
                av_rand_range(0.5, 1.1), 0);
      }
    }

    double amp = av_lerp(1.0, 3.4, av_clamp(f->effort, 0, 1.4)) * f->scale;
    f->bob = av_damp(f->bob, -f->flap * amp, 20, dt);
  }

  if (b->clap > 0) b->clap = fmax(0.0, b->clap - dt / 0.75);
}

/* ---- the walk ---------------------------------------------------------- */
/*
 * The head bob is not a bob.
 *
 * A walking pigeon holds its head STILL in space while its body advances
 * underneath it, then snaps the head forward to a new fixed point and holds
 * again. Roughly 30% of each stride is the thrust, the other 70% is the hold.
 * During the hold the head-to-body offset therefore has to decrease *linearly*
 * at exactly the walking speed — that linearity is what sells it. Draw it as a
 * sine wave instead and you get a chicken.
 */
static double head_offset(const Pigeon *b) {
  double u = b->walk_phase - floor(b->walk_phase);
  double A = STEP_LEN * 0.5;
  if (u < HEAD_THRUST) return av_lerp(-A, A, av_ease_out_cubic(u / HEAD_THRUST));
  return av_lerp(A, -A, (u - HEAD_THRUST) / (1 - HEAD_THRUST));
}

/* Same principle for the feet: planted while the body passes over them, then
 * swung forward. A foot that slides along the ground reads as ice. */
static void foot_offset(const Pigeon *b, int leg, double *ox, double *lift) {
  double u = b->walk_phase + (leg ? 0.5 : 0.0);
  u -= floor(u);
  double S = STEP_LEN * 0.5;
  if (u < 0.55) {                       /* stance: planted, body moves over it */
    *ox = av_lerp(S, -S, u / 0.55);
    *lift = 0;
  } else {                              /* swing: forward and slightly up */
    double t = (u - 0.55) / 0.45;
    *ox = av_lerp(-S, S, av_smooth(t));
    *lift = sin(t * M_PI) * 2.4;
  }
}

static void pigeon_ground(Pigeon *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  double sc = f->scale;

  f->y = b->ground_y - b->stand_h * sc + b->crouch * 3.0 * sc;
  f->vy = 0;
  f->heading = av_damp(f->heading, 0, 8, dt);

  double dx = b->walk_target - f->x;
  double want = 0;
  if (fabs(dx) > 2.0 * sc) {
    want = WALK_SPEED;
    f->facing = dx > 0 ? 1 : -1;
  }
  b->walk_speed = av_damp(b->walk_speed, want, 7, dt);
  f->facing_blend = av_damp(f->facing_blend, f->facing, 7, dt);

  double moved = b->walk_speed * sc * dt * (f->facing >= 0 ? 1 : -1);
  f->x += moved;
  f->vx = b->walk_speed * sc * (f->facing >= 0 ? 1 : -1);

  /* the stride is driven by ground covered, never by a clock */
  if (b->walk_speed > 0.5)
    b->walk_phase += fabs(moved) / (STEP_LEN * sc);
  else
    b->walk_phase = 0;                 /* stand with the feet together */

  /* body rocks twice per stride */
  double u = b->walk_phase - floor(b->walk_phase);
  double rock = b->walk_speed > 0.5 ? -fabs(sin(u * TAU)) * 0.7 * sc : 0;
  f->bob = av_damp(f->bob, rock, 18, dt);

  b->tail_fan  = av_damp(b->tail_fan, 0.10, 5, dt);
  b->tail_drop = av_damp(b->tail_drop, b->walk_speed > 0.5 ? -0.06 : 0.16, 4, dt);
  b->legs_out  = av_damp(b->legs_out, 1, 8, dt);
  b->flare     = av_damp(b->flare, 0, 6, dt);

  /* idle business: a standing pigeon is never quite still */
  if (b->walk_speed < 0.5) {
    b->idle_t += dt;
    if (b->act) {
      b->act_t += dt;
      double dur = b->act == 1 ? 0.65 : (b->act == 3 ? 1.1 : 0.9);
      if (b->act_t > dur) { b->act = 0; b->act_t = 0; }
    } else if (b->idle_t > b->next_idle) {
      b->idle_t = 0;
      b->next_idle = av_rand_range(1.1, 3.4);
      double r = av_rand();
      b->act = r < 0.34 ? 1 : (r < 0.68 ? 2 : (r < 0.88 ? 3 : 4));
      b->act_t = 0;
      if (b->act == 4) {                       /* a small shuffling turn */
        b->walk_target = f->x + av_rand_sym(14) * sc;
        b->act = 0;
      }
      if (b->act == 1 && P) {
        Vec t = flyer_to_world(f, BILL_X, BILL_Y + 8);
        for (int i = 0; i < 3; i++)
          p_ash(P, t.x + av_rand_sym(3) * av_world(), t.y,
                av_rand_sym(16) * av_world(), -av_rand_range(4, 18) * av_world(),
                av_rand_range(0.4, 0.9), 0);
      }
    }
  } else {
    b->act = 0;
    b->idle_t = 0;
  }

  double dip = 0, look = 0, puff = 0;
  if (b->act == 1) {                            /* peck */
    double t = b->act_t / 0.65;
    dip = sin(av_clamp(t, 0, 1) * M_PI) * 1.0;
  } else if (b->act == 2) {                     /* look about */
    look = sin(b->act_t * 5.0) * 0.8;
  } else if (b->act == 3) {                     /* coo */
    puff = sin(av_clamp(b->act_t / 1.1, 0, 1) * M_PI) * 1.0;
  }
  if (b->capsule_drop > 0 && b->capsule_drop < 1) dip = 1.0;

  b->head_dip = av_damp(b->head_dip, dip, 10, dt);
  b->look = av_damp(b->look, look, 9, dt);
  b->puff = av_damp(b->puff, puff, 7, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) { f->blink = 0.11; f->blink_at = av_rand_range(1.8, 5.0); }
  if (f->blink > 0) f->blink -= dt;

  pigeon_wings(b, dt, P);
}

/* ---- airborne ---------------------------------------------------------- */

static void pigeon_air(Pigeon *b, double dt, Particles *P) {
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
  pigeon_wings(b, dt, P);

  /* Landing: the whole body becomes an airbrake. Nose comes up, the wings
   * cup forward and beat against the direction of travel, the tail fans wide
   * and drops, and the legs swing out in front. */
  double want_flare = b->flare;
  b->tail_fan  = av_damp(b->tail_fan,  av_lerp(0.10, 1.0, want_flare), 6, dt);
  b->tail_drop = av_damp(b->tail_drop, av_lerp(0.0, 0.85, want_flare), 6, dt);
  b->legs_out  = av_damp(b->legs_out,  want_flare > 0.25 ? 1 : 0, 7, dt);
  f->pitch     = av_damp(f->pitch, -0.62 * want_flare, 6, dt);

  b->head_dip = av_damp(b->head_dip, 0, 8, dt);
  b->look = av_damp(b->look, 0, 8, dt);
  b->puff = av_damp(b->puff, 0, 8, dt);
  b->crouch = av_damp(b->crouch, 0, 8, dt);

  f->ax = 0;
  f->ay = 0;
}

void pigeon_update(Pigeon *b, double dt, Particles *P) {
  if (b->grounded) { b->f.t += dt; pigeon_ground(b, dt, P); }
  else pigeon_air(b, dt, P);
}

/* ---- drawing ----------------------------------------------------------- */

static const Rgb P_OUTLINE = { 0.129, 0.153, 0.208 };

static double dip_dy(const Pigeon *b)  { return b->head_dip * 9.0; }
static double dip_dx(const Pigeon *b)  { return b->head_dip * 2.5; }

static void draw_tail(Pigeon *b, cairo_t *cr) {
  double fan = b->tail_fan;
  double th = (180.0 - b->tail_drop * 34.0) * D2R;
  double cx = cos(th), sy = sin(th);
  double tipx = TAIL_X + cx * TAIL_LEN;
  double tipy = TAIL_Y + sy * TAIL_LEN;
  double nx = -sy, ny = cx;

  /* Closed, a pigeon's tail is a narrow wedge seen edge-on; spread, it is a
   * broad fan. Parallel sides read as a plank, so the root stays thin and the
   * corners of the tip are cut back. */
  double w0 = 1.5;
  double w1 = av_lerp(2.6, 8.2, fan);
  double shoulder = 0.72;                 /* where the tail reaches full width */
  double sxp = av_lerp(TAIL_X, tipx, shoulder);
  double syp = av_lerp(TAIL_Y, tipy, shoulder);
  double cut = av_lerp(0.55, 0.86, fan);  /* corner cut at the very end */

  cairo_new_path(cr);
  cairo_move_to(cr, TAIL_X + nx * w0, TAIL_Y + ny * w0);
  cairo_line_to(cr, sxp + nx * w1, syp + ny * w1);
  cairo_line_to(cr, tipx + nx * w1 * cut, tipy + ny * w1 * cut);
  cairo_line_to(cr, tipx - nx * w1 * cut, tipy - ny * w1 * cut);
  cairo_line_to(cr, sxp - nx * w1, syp - ny * w1);
  cairo_line_to(cr, TAIL_X - nx * w0, TAIL_Y - ny * w0);
  cairo_close_path(cr);
  av_set_rgba(cr, P_SLATE, 1);
  cairo_fill_preserve(cr);
  if (av_pixel_mode()) {
    av_set_rgba(cr, P_OUTLINE, 1);
    cairo_set_line_width(cr, 0.6 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
  }

  /* every feral pigeon has a dark band right at the end of the tail */
  double bt = 0.80;
  double bx = av_lerp(TAIL_X, tipx, bt), by = av_lerp(TAIL_Y, tipy, bt);
  double bw = w1 * av_lerp(1.0, cut, (bt - shoulder) / (1 - shoulder));
  cairo_new_path(cr);
  cairo_move_to(cr, bx + nx * bw, by + ny * bw);
  cairo_line_to(cr, tipx + nx * w1 * cut, tipy + ny * w1 * cut);
  cairo_line_to(cr, tipx - nx * w1 * cut, tipy - ny * w1 * cut);
  cairo_line_to(cr, bx - nx * bw, by - ny * bw);
  cairo_close_path(cr);
  av_set_rgba(cr, P_DEEP, 1);
  cairo_fill(cr);

  /* the separations between the feathers, once they are spread */
  if (fan > 0.45) {
    av_set_rgba(cr, P_DARK, 0.8);
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

static void draw_leg(Pigeon *b, cairo_t *cr, int leg, int near) {
  double sc_out = b->legs_out;
  if (sc_out < 0.02 && !b->grounded) return;

  double ox = 0, lift = 0;
  if (b->grounded && b->walk_speed > 0.5) foot_offset(b, leg, &ox, &lift);
  else ox = leg ? 1.4 : -1.4;

  double reach = LEG_LEN * av_lerp(0.35, 1.0, sc_out);
  double fx = HIP_X + ox * (b->grounded ? 1 : 0.4);
  double fy = HIP_Y + reach - lift;
  /* on the way in, the feet swing out in front of the bird */
  if (!b->grounded) fx += b->flare * 6.0;

  Rgb c = near ? P_FOOT : P_FOOTD;
  av_set_rgba(cr, c, 1);
  cairo_set_line_width(cr, near ? 1.5 : 1.2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, HIP_X + (leg ? 1.0 : -1.0), HIP_Y);
  av_quad_to(cr, fx - 1.6, HIP_Y + reach * 0.55, fx, fy);
  cairo_stroke(cr);

  if (sc_out > 0.5) {                          /* toes */
    cairo_set_line_width(cr, near ? 1.0 : 0.8);
    for (int t = 0; t < 3; t++) {
      cairo_move_to(cr, fx, fy);
      cairo_line_to(cr, fx + 1.4 + t * 1.1, fy + 0.9 - t * 0.25);
      cairo_stroke(cr);
    }
    cairo_move_to(cr, fx, fy);
    cairo_line_to(cr, fx - 1.9, fy + 0.7);
    cairo_stroke(cr);
  }
}

/* The letter itself, rolled and tied to the leg with thread — the way a
 * message actually travelled by pigeon. Not clutched in the beak. */
static void draw_carried_letter(Pigeon *b, cairo_t *cr) {
  if (!b->carrying) return;
  double ox = 0, lift = 0;
  if (b->grounded && b->walk_speed > 0.5) foot_offset(b, 1, &ox, &lift);
  else ox = 1.4;
  double reach = LEG_LEN * av_lerp(0.35, 1.0, b->legs_out);
  double x = HIP_X + ox * (b->grounded ? 1 : 0.4) - 0.4;
  double y = HIP_Y + reach * 0.58 - lift * 0.6;
  if (!b->grounded) x += b->flare * 6.0;
  av_draw_tied_letter(cr, x, y, 0.30 + b->f.bob * 0.02, 0.6);
}

static void draw_body(Pigeon *b, cairo_t *cr) {
  cairo_new_path(cr);
  cairo_move_to(cr, 11.0, -3.0);
  cairo_curve_to(cr,   6.0, -7.4,  -4.0, -7.6, -11.5, -4.6);
  cairo_curve_to(cr, -13.6, -3.4, -14.0, -0.6, -12.4,  1.2);
  cairo_curve_to(cr,  -8.0,  6.4,   0.0,  7.6,   7.0,  5.4);
  cairo_curve_to(cr,  10.4,  4.0,  12.0,  0.6,  11.0, -3.0);
  cairo_close_path(cr);

  cairo_pattern_t *g = cairo_pattern_create_linear(-12, 4, 10, -6);
  cairo_pattern_add_color_stop_rgb(g, 0.00, P_MID.r,   P_MID.g,   P_MID.b);
  cairo_pattern_add_color_stop_rgb(g, 0.45, P_SLATE.r, P_SLATE.g, P_SLATE.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, P_LIGHT.r, P_LIGHT.g, P_LIGHT.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, P_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);
}

/* wing folded against the flank, with the two bars that make it a pigeon */
static void draw_folded_wing(Pigeon *b, cairo_t *cr) {
  (void)b;
  cairo_new_path(cr);
  cairo_move_to(cr, 4.5, -5.6);
  av_quad_to(cr, -2.0, -6.6, -9.5, -3.4);
  av_quad_to(cr, -12.4, -2.0, -11.6, 0.6);
  av_quad_to(cr, -6.0, 3.0, 1.5, 1.2);
  av_quad_to(cr, 4.8, -0.4, 4.5, -5.6);
  cairo_close_path(cr);
  av_set_rgba(cr, P_SLATE, 1);
  cairo_fill_preserve(cr);
  cairo_clip(cr);

  av_set_rgba(cr, P_DEEP, 1);
  cairo_set_line_width(cr, 1.7);
  for (int i = 0; i < 2; i++) {
    double x = -3.4 - i * 3.6;
    cairo_move_to(cr, x + 2.2, -6.5);
    av_quad_to(cr, x, -2.0, x - 0.8, 3.2);
    cairo_stroke(cr);
  }
  cairo_reset_clip(cr);

  /* pale edge along the folded primaries */
  av_set_rgba(cr, P_PALE, av_pixel_mode() ? 0.9 : 0.5);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, -9.5, -3.2);
  av_quad_to(cr, -12.2, -1.8, -11.4, 0.6);
  cairo_stroke(cr);

  if (!av_pixel_mode()) {
    av_set_rgba(cr, P_DARK, 0.5);
    cairo_set_line_width(cr, 0.5);
    cairo_move_to(cr, 4.5, -5.6);
    av_quad_to(cr, -2.0, -6.6, -9.5, -3.4);
    cairo_stroke(cr);
  }
}

static void draw_open_wing(Pigeon *b, cairo_t *cr, int near) {
  Flyer *f = &b->f;
  if (av_pixel_mode() && !near) return;
  if (f->spread < 0.05) return;

  double y_off = near ? 0 : 2.2;
  double len_k = near ? 1.0 : 0.90;
  double shade = near ? 0.0 : 0.32;
  double alpha = near ? 1.0 : 0.85;
  double range = av_lerp(78.0, 112.0, b->clap);   /* the clap goes past vertical */

  WingPose w;
  wing_pose(f, range, 20.0, WING_LEN, len_k, SH_X, SH_Y, y_off, &w);

  Rgb base = av_mix(P_SLATE, P_DEEP, shade);
  Rgb edge = av_mix(P_LIGHT, P_DEEP, shade);
  Rgb tipc = av_mix(P_DARK,  P_DEEP, shade);

  /* A fan of individually drawn feathers is sub-pixel at sprite scale and
   * collapses into a wire. In pixel mode the whole wing is one solid
   * silhouette, with the primaries implied by a darker outer third. */
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
    av_set_rgba(cr, P_OUTLINE, alpha);
    cairo_set_line_width(cr, 0.65 / f->scale);
    cairo_stroke(cr);

    /* the dark outer third: the primaries, without drawing each one */
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_move_to(cr, av_lerp(w.wx, tipx, 0.52), av_lerp(w.wy, tipy, 0.52));
    cairo_line_to(cr, tipx, tipy);
    cairo_line_to(cr, trx, try_);
    cairo_line_to(cr, av_lerp(w.wx, trx, 0.58), av_lerp(w.wy, try_, 0.58));
    cairo_close_path(cr);
    av_set_rgba(cr, tipc, alpha);
    cairo_fill(cr);
    cairo_restore(cr);

    /* one wing bar across the arm */
    av_set_rgba(cr, av_mix(P_DEEP, P_DARK, shade), alpha * 0.9);
    cairo_set_line_width(cr, 1.4);
    cairo_move_to(cr, av_lerp(SH_X, w.wx, 0.42) - 2.4, av_lerp(SH_Y, w.wy, 0.42) + y_off);
    cairo_line_to(cr, av_lerp(SH_X, w.wx, 0.42) + 3.0, av_lerp(SH_Y, w.wy, 0.42) + 2.6 + y_off);
    cairo_stroke(cr);
    return;
  }

  /* arm: secondaries and coverts */
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

static void draw_head(Pigeon *b, cairo_t *cr) {
  Flyer *f = &b->f;
  double hx = head_offset(b) * (b->grounded && b->walk_speed > 0.5 ? 1 : 0);
  double dx = hx + dip_dx(b) + b->look * 1.2;
  double dy = dip_dy(b);

  /* neck: swells when it coos */
  double puff = b->puff * 1.6;
  cairo_new_path(cr);
  cairo_move_to(cr, 6.4, -4.2);
  av_quad_to(cr, 9.0 + dx * 0.4, -8.2 + dy * 0.4, 11.6 + dx * 0.8, -8.6 + dy * 0.8);
  cairo_line_to(cr, 14.0 + dx, -5.6 + dy);
  av_quad_to(cr, 10.6 + puff, -1.4 + puff * 0.4, 7.2, -1.0);
  cairo_close_path(cr);
  av_set_rgba(cr, P_MID, 1);
  cairo_fill(cr);

  /* the iridescent patch — the only part of a pigeon that is not grey */
  if (!av_pixel_mode()) {
    cairo_pattern_t *g = cairo_pattern_create_linear(8, -6, 13, -2);
    cairo_pattern_add_color_stop_rgba(g, 0.0, P_GREEN.r, P_GREEN.g, P_GREEN.b, 0.85);
    cairo_pattern_add_color_stop_rgba(g, 1.0, P_VIOLET.r, P_VIOLET.g, P_VIOLET.b, 0.8);
    cairo_set_source(cr, g);
  } else {
    av_set_rgba(cr, sin(f->t * 0.9) > 0 ? P_GREEN : P_VIOLET, 1);
  }
  cairo_new_path(cr);
  cairo_move_to(cr, 7.4, -3.4);
  av_quad_to(cr, 10.6 + dx * 0.5, -6.4 + dy * 0.5, 12.8 + dx * 0.8, -6.0 + dy * 0.8);
  av_quad_to(cr, 10.8 + puff, -1.8, 7.8, -1.4);
  cairo_close_path(cr);
  cairo_fill(cr);

  /* skull */
  double cx = HEAD_X + dx, cy = HEAD_Y + dy;
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, HEAD_R, 0, TAU);
  cairo_pattern_t *hg = cairo_pattern_create_radial(cx + 1.2, cy - 1.4, 0.4, cx, cy, HEAD_R * 1.5);
  cairo_pattern_add_color_stop_rgb(hg, 0.0, P_LIGHT.r, P_LIGHT.g, P_LIGHT.b);
  cairo_pattern_add_color_stop_rgb(hg, 1.0, P_SLATE.r, P_SLATE.g, P_SLATE.b);
  cairo_set_source(cr, hg);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, P_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(hg);

  /* bill: short, dark, with the pale cere at its base */
  cairo_new_path(cr);
  cairo_move_to(cr, 17.4 + dx, -9.9 + dy);
  av_quad_to(cr, 20.2 + dx, -9.5 + dy, BILL_X + dx, BILL_Y + dy);
  av_quad_to(cr, 19.4 + dx, -7.6 + dy, 17.2 + dx, -7.4 + dy);
  cairo_close_path(cr);
  av_set_rgba(cr, P_BILL, 1);
  cairo_fill(cr);

  av_set_rgba(cr, P_CERE, 1);
  cairo_new_path(cr);
  cairo_arc(cr, 17.1 + dx, -10.3 + dy, 1.5, 0, TAU);
  cairo_fill(cr);

  /* eye */
  double ex = EYE_X + dx, ey = EYE_Y + dy;
  if (f->blink > 0) {
    av_set_rgba(cr, P_DARK, 0.9);
    cairo_set_line_width(cr, 0.8);
    cairo_new_path(cr);
    cairo_arc(cr, ex, ey, 1.4, 0.2, M_PI - 0.2);
    cairo_stroke(cr);
  } else {
    av_set_rgba(cr, P_EYE, 1);
    cairo_arc(cr, ex, ey, 1.6, 0, TAU);
    cairo_fill(cr);
    av_set_rgba(cr, P_OUTLINE, 1);
    cairo_arc(cr, ex + 0.2, ey, 0.85, 0, TAU);
    cairo_fill(cr);
    if (!av_pixel_mode()) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
      cairo_arc(cr, ex + 0.6, ey - 0.5, 0.35, 0, TAU);
      cairo_fill(cr);
    }
  }
}

void pigeon_draw(Pigeon *b, cairo_t *cr) {
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

void pigeon_bbox(Pigeon *b, double *x0, double *y0, double *x1, double *y1) {
  double r = 40 * b->f.scale;
  double cx = b->f.x, cy = b->f.y + b->f.bob;
  if (cx - r < *x0) *x0 = cx - r;
  if (cy - r < *y0) *y0 = cy - r;
  if (cx + r > *x1) *x1 = cx + r;
  if (cy + r > *y1) *y1 = cy + r;
}
