/* The hawk.
 *
 * A falcon, and the coldest thing in the aviary. Where the others wander in on
 * a curve, it comes on a straight line and fast — a stoop — and it does not
 * slow until the last moment, when it throws its wings up, swings its talons
 * forward and brakes hard on the exact spot: the strike. It sets the letter
 * down, holds one beat with the flat stare a raptor has, and powers away. It
 * lands but it does not walk, it does not linger, and it does not circle.
 *
 * Slate above, pale and finely barred below, a dark hood with the peregrine's
 * black moustache, a hooked beak and a yellow eye-ring. It never burns.
 */
#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
static const Rgb K_BACK    = { 0.325, 0.353, 0.427 };   /* slate blue-grey back */
static const Rgb K_BACK2   = { 0.224, 0.247, 0.310 };   /* the shadowed slate */
static const Rgb K_HOOD    = { 0.169, 0.180, 0.227 };   /* dark head and moustache */
static const Rgb K_PALE    = { 0.878, 0.855, 0.804 };   /* barred underparts */
static const Rgb K_PALE2   = { 0.749, 0.729, 0.690 };
static const Rgb K_BAR     = { 0.310, 0.290, 0.290 };   /* the fine dark barring */
static const Rgb K_CERE    = { 0.902, 0.741, 0.278 };   /* yellow cere / eye-ring */
static const Rgb K_BEAK    = { 0.129, 0.129, 0.153 };
static const Rgb K_FOOT    = { 0.898, 0.729, 0.263 };
static const Rgb K_TALON   = { 0.153, 0.153, 0.176 };
static const Rgb K_EYE     = { 0.055, 0.051, 0.063 };
static const Rgb K_OUTLINE = { 0.145, 0.157, 0.196 };

/* ---- anatomy, in body units (+x forward, +y down) --------------------- */
#define SH_X     3.0
#define SH_Y    -5.0
#define WING_LEN 27.0
#define TAIL_X  -12.5
#define TAIL_Y   -0.8
#define TAIL_LEN 13.5
#define HEAD_X   13.2
#define HEAD_Y   -9.8
#define HEAD_R    4.3
#define EYE_X    15.0
#define EYE_Y   -10.3
#define BEAK_X   19.6
#define BEAK_Y   -8.6
#define HIP_X     1.5
#define HIP_Y     5.4
#define LEG_LEN   8.2
#define STAND_H  (HIP_Y + LEG_LEN)

void hawk_init(Hawk *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  double W = av_world();
  flyer_init(&b->f, x, y);
  b->f.scale = scale;
  b->f.max_speed = 360 * W;      /* fast, and it commits to the line */
  b->f.max_force = 1700 * W;
  b->f.wander_gain = 70 * W;     /* little wander — it does not circle */
  b->f.bounding = 1;
  b->f.fore_floor = 0.5;         /* pointed wings can go nearly edge-on */
  b->carrying = 1;
  b->stand_h = STAND_H;
  b->tail_fan = 0.12;
}

Vec hawk_capsule_point(Hawk *b) {
  return flyer_to_world(&b->f, HIP_X - 0.8, HIP_Y + LEG_LEN * 0.6);
}

int  hawk_walking(const Hawk *b) { (void)b; return 0; }   /* never walks */
void hawk_walk_to(Hawk *b, double world_x) { b->walk_target = world_x; }

void hawk_touch_down(Hawk *b, double ground_y) {
  b->grounded = 1;
  b->ground_y = ground_y;
  b->f.y = ground_y - b->stand_h * b->f.scale;
  b->f.vx = b->f.vy = 0;
  b->f.heading = 0;
  b->f.pitch = 0;
  b->walk_target = b->f.x;
  b->flare = 0;
}

void hawk_launch(Hawk *b) {
  if (!b->grounded) return;
  b->grounded = 0;
  b->clapped = 0;
  b->clap = 0.6;
  b->crouch = 0;
  b->mantle = 0;
  b->talons = 0;
  double W = av_world();
  b->f.vx = b->f.facing * 90 * W;     /* it leaves fast and low, then climbs */
  b->f.vy = -200 * W;
  b->f.hover = 0;
}

/* ---- wings ------------------------------------------------------------- */
/* Long pointed wings, a fast shallow beat, and a hard glide between — a
 * falcon's flight, nothing like the pigeon's laboured flap. */
static void hawk_wings(Hawk *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  (void)P;

  if (b->grounded) {
    f->flap = av_damp(f->flap, 0.0, 11, dt);
    f->fold = av_damp(f->fold, 1.0, 8, dt);
    f->spread = av_damp(f->spread, b->mantle > 0.2 ? 0.9 : 0.0, 9, dt);
    f->effort = av_damp(f->effort, 0.1, 6, dt);
    f->bob = av_damp(f->bob, 0, 10, dt);
    f->gliding = 0;
    return;
  }

  double accel = hypot(f->ax, f->ay);
  double climb = -f->vy / f->max_speed;
  double want = 0.4 + (accel / f->max_force) * 0.9 + fmax(0, climb) * 1.0;
  f->effort = av_damp(f->effort, av_clamp(want, 0.2, 1.6), 6, dt);

  f->bound_timer -= dt;
  if (f->bound_timer <= 0) {
    if (f->gliding) { f->gliding = 0; f->bound_timer = av_rand_range(0.5, 1.0); }
    else if (f->effort < 0.7 && flyer_speed(f) > f->max_speed * 0.45) {
      f->gliding = 1;                          /* long fast glides on the stoop */
      f->bound_timer = av_rand_range(0.6, 1.3);
    } else {
      f->bound_timer = av_rand_range(0.3, 0.7);
    }
  }

  f->spread = av_damp(f->spread, f->gliding ? 0.9 : 1.0, 9, dt);

  if (f->gliding) {
    f->flap = av_damp(f->flap, -0.1, 8, dt);   /* held out flat, swept back */
    f->fold = av_damp(f->fold, 0.12, 8, dt);
    f->bob  = av_damp(f->bob, 0.3, 6, dt);
    f->vy += 40 * av_world() * dt;
  } else {
    double hz = av_lerp(5.2, 8.4, av_clamp(f->effort / 1.4, 0, 1));
    if (b->clap > 0.02) hz = av_lerp(hz, 9.0, b->clap);
    f->wing_hz = hz;
    f->wing_phase += hz * dt;
    f->flap = flap_curve(f->wing_phase);
    f->fold = fold_curve(f->wing_phase) * 0.5;

    double amp = av_lerp(1.0, 2.8, av_clamp(f->effort, 0, 1.4)) * f->scale;
    f->bob = av_damp(f->bob, -f->flap * amp, 20, dt);
  }

  if (b->clap > 0) b->clap = fmax(0.0, b->clap - dt / 0.7);
}

/* ---- on the ground (a brief, still stand — no walk) -------------------- */

static void hawk_ground(Hawk *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  double sc = f->scale;

  f->y = b->ground_y - b->stand_h * sc + b->crouch * 3.0 * sc;
  f->vx = f->vy = 0;
  f->heading = av_damp(f->heading, 0, 10, dt);

  /* the strike relaxes: wings come down off the mantle, talons settle */
  b->mantle = av_damp(b->mantle, 0, 5, dt);
  b->talons = av_damp(b->talons, 0, 6, dt);
  b->tail_fan = av_damp(b->tail_fan, 0.14, 5, dt);
  b->tail_drop = av_damp(b->tail_drop, 0.12, 4, dt);
  b->legs_out = av_damp(b->legs_out, 1, 8, dt);
  b->flare = av_damp(b->flare, 0, 6, dt);

  double dip = 0;
  if (b->capsule_drop > 0 && b->capsule_drop < 1) dip = 1.0;
  b->head_dip = av_damp(b->head_dip, dip, 10, dt);
  /* a slow, flat side-to-side of the head — the raptor stare */
  b->look = av_damp(b->look, sin(f->t * 1.6) * 0.5, 3, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) { f->blink = 0.09; f->blink_at = av_rand_range(2.6, 6.0); }
  if (f->blink > 0) f->blink -= dt;

  hawk_wings(b, dt, P);
}

/* ---- airborne ---------------------------------------------------------- */

static void hawk_air(Hawk *b, double dt, Particles *P) {
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
                av_noise(f->t * 0.4 + f->noise_off, 0) * f->wander_gain,
                av_noise(f->t * 0.5 + f->noise_off, 1) * f->wander_gain * 0.6);

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
  hawk_wings(b, dt, P);

  /* the brake: as the flare comes on, the wings throw up (mantle), the tail
   * fans and drops, the talons swing forward, the nose comes up hard */
  double want_flare = b->flare;
  b->mantle    = av_damp(b->mantle, want_flare, 9, dt);
  b->talons    = av_damp(b->talons, want_flare > 0.3 ? 1 : 0, 8, dt);
  b->tail_fan  = av_damp(b->tail_fan,  av_lerp(0.12, 1.0, want_flare), 7, dt);
  b->tail_drop = av_damp(b->tail_drop, av_lerp(0.0, 0.7, want_flare), 6, dt);
  b->legs_out  = av_damp(b->legs_out,  want_flare > 0.25 ? 1 : 0, 8, dt);
  f->pitch     = av_damp(f->pitch, -0.75 * want_flare, 7, dt);

  b->head_dip = av_damp(b->head_dip, 0, 8, dt);
  b->look = av_damp(b->look, 0, 8, dt);
  b->crouch = av_damp(b->crouch, 0, 8, dt);

  f->ax = 0;
  f->ay = 0;
}

void hawk_update(Hawk *b, double dt, Particles *P) {
  if (b->grounded) { b->f.t += dt; hawk_ground(b, dt, P); }
  else hawk_air(b, dt, P);
}

/* ---- drawing ----------------------------------------------------------- */

static double dip_dy(const Hawk *b)  { return b->head_dip * 8.0; }
static double dip_dx(const Hawk *b)  { return b->head_dip * 2.2; }

static void draw_tail(Hawk *b, cairo_t *cr) {
  double fan = b->tail_fan;
  double th = (180.0 - b->tail_drop * 30.0) * D2R;
  double cx = cos(th), sy = sin(th);
  double tipx = TAIL_X + cx * TAIL_LEN;
  double tipy = TAIL_Y + sy * TAIL_LEN;
  double nx = -sy, ny = cx;

  double w0 = 1.6;
  double w1 = av_lerp(2.8, 7.0, fan);        /* a squared tail, fans wide on the brake */
  double sxp = av_lerp(TAIL_X, tipx, 0.72);
  double syp = av_lerp(TAIL_Y, tipy, 0.72);

  cairo_new_path(cr);
  cairo_move_to(cr, TAIL_X + nx * w0, TAIL_Y + ny * w0);
  cairo_line_to(cr, sxp + nx * w1, syp + ny * w1);
  cairo_line_to(cr, tipx + nx * w1, tipy + ny * w1);
  cairo_line_to(cr, tipx - nx * w1, tipy - ny * w1);
  cairo_line_to(cr, sxp - nx * w1, syp - ny * w1);
  cairo_line_to(cr, TAIL_X - nx * w0, TAIL_Y - ny * w0);
  cairo_close_path(cr);
  av_set_rgba(cr, K_BACK2, 1);
  cairo_fill_preserve(cr);
  if (av_pixel_mode()) {
    av_set_rgba(cr, K_OUTLINE, 1);
    cairo_set_line_width(cr, 0.6 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
  }

  /* the dark subterminal band and pale tip a falcon's tail carries */
  double bt = 0.82;
  double bx = av_lerp(TAIL_X, tipx, bt), by = av_lerp(TAIL_Y, tipy, bt);
  av_set_rgba(cr, K_HOOD, 1);
  cairo_set_line_width(cr, 1.6);
  cairo_move_to(cr, bx + nx * w1 * 0.96, by + ny * w1 * 0.96);
  cairo_line_to(cr, bx - nx * w1 * 0.96, by - ny * w1 * 0.96);
  cairo_stroke(cr);
  av_set_rgba(cr, K_PALE, 0.9);
  cairo_set_line_width(cr, 0.7);
  cairo_move_to(cr, tipx + nx * w1 * 0.9, tipy + ny * w1 * 0.9);
  cairo_line_to(cr, tipx - nx * w1 * 0.9, tipy - ny * w1 * 0.9);
  cairo_stroke(cr);

  /* fine cross-bars when the tail is spread */
  if (fan > 0.4 && !av_pixel_mode()) {
    av_set_rgba(cr, K_HOOD, 0.55);
    cairo_set_line_width(cr, 0.4);
    for (int i = 1; i <= 3; i++) {
      double k = 0.3 + i * 0.15;
      double mx = av_lerp(TAIL_X, tipx, k), my = av_lerp(TAIL_Y, tipy, k);
      cairo_move_to(cr, mx + nx * w1 * 0.9, my + ny * w1 * 0.9);
      cairo_line_to(cr, mx - nx * w1 * 0.9, my - ny * w1 * 0.9);
      cairo_stroke(cr);
    }
  }
}

static void draw_leg(Hawk *b, cairo_t *cr, int leg, int near) {
  double sc_out = b->legs_out;
  if (sc_out < 0.02 && !b->grounded && b->talons < 0.05) return;

  /* on the strike the feet swing forward and the talons spread */
  double fwd = b->talons * 5.0;
  double ox = leg ? 1.4 : -1.4;
  double reach = LEG_LEN * av_lerp(0.4, 1.0, av_clamp(sc_out + b->talons, 0, 1));
  double fx = HIP_X + ox + fwd + (b->grounded ? 0 : b->flare * 4.0);
  double fy = HIP_Y + reach - b->talons * 3.0;

  Rgb c = near ? K_FOOT : av_mix(K_FOOT, K_HOOD, 0.4);
  av_set_rgba(cr, c, 1);
  cairo_set_line_width(cr, near ? 1.9 : 1.5);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, HIP_X + (leg ? 1.0 : -1.0), HIP_Y);
  av_quad_to(cr, fx - 1.4, HIP_Y + reach * 0.5, fx, fy);
  cairo_stroke(cr);

  /* talons: dark, hooked, and spread when swung forward for the strike */
  if (sc_out > 0.4 || b->talons > 0.2) {
    av_set_rgba(cr, K_TALON, 1);
    cairo_set_line_width(cr, near ? 1.1 : 0.9);
    double spread = 0.6 + b->talons * 1.4;
    for (int t = 0; t < 3; t++) {
      double a = (t - 1) * spread;
      cairo_move_to(cr, fx, fy);
      av_quad_to(cr, fx + 1.6 + a, fy + 1.4, fx + 2.2 + a, fy + 2.6 + b->talons);
      cairo_stroke(cr);
    }
    cairo_move_to(cr, fx, fy);
    av_quad_to(cr, fx - 1.8, fy + 1.2, fx - 2.4, fy + 2.2);
    cairo_stroke(cr);
  }
}

static void draw_carried_letter(Hawk *b, cairo_t *cr) {
  if (!b->carrying) return;
  double x = HIP_X - 0.8 + (b->grounded ? 0 : b->flare * 4.0);
  double y = HIP_Y + LEG_LEN * 0.6 - b->talons * 2.4;
  av_draw_tied_letter(cr, x, y, 0.28 + b->f.bob * 0.02, 0.6);
}

static void draw_body(Hawk *b, cairo_t *cr) {
  cairo_new_path(cr);
  cairo_move_to(cr, 11.5, -3.2);
  cairo_curve_to(cr,   6.0, -7.0,  -4.0, -7.0, -11.0, -3.8);
  cairo_curve_to(cr, -12.8, -2.8, -13.0, -0.4, -11.6,  1.2);
  cairo_curve_to(cr,  -7.0,  5.6,   0.5,  6.6,   7.2,  4.8);
  cairo_curve_to(cr,  10.6,  3.6,  12.4,  0.4,  11.5, -3.2);
  cairo_close_path(cr);

  /* slate back over pale, finely barred underparts */
  cairo_pattern_t *g = cairo_pattern_create_linear(0, -6, 0, 5);
  cairo_pattern_add_color_stop_rgb(g, 0.00, K_BACK.r,  K_BACK.g,  K_BACK.b);
  cairo_pattern_add_color_stop_rgb(g, 0.42, K_BACK2.r, K_BACK2.g, K_BACK2.b);
  cairo_pattern_add_color_stop_rgb(g, 0.56, K_PALE2.r, K_PALE2.g, K_PALE2.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, K_PALE.r,  K_PALE.g,  K_PALE.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, K_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);

  /* the fine dark barring across the pale breast and belly */
  cairo_save(cr);
  cairo_new_path(cr);
  cairo_move_to(cr, 11.5, -3.2);
  cairo_curve_to(cr,   6.0, -7.0,  -4.0, -7.0, -11.0, -3.8);
  cairo_curve_to(cr, -12.8, -2.8, -13.0, -0.4, -11.6,  1.2);
  cairo_curve_to(cr,  -7.0,  5.6,   0.5,  6.6,   7.2,  4.8);
  cairo_curve_to(cr,  10.6,  3.6,  12.4,  0.4,  11.5, -3.2);
  cairo_close_path(cr);
  cairo_clip(cr);
  av_set_rgba(cr, K_BAR, av_pixel_mode() ? 0.9 : 0.6);
  cairo_set_line_width(cr, 0.6);
  for (int i = 0; i < 4; i++) {
    double yy = 0.4 + i * 1.5;
    cairo_move_to(cr, -9.0, yy);
    av_quad_to(cr, 0.0, yy + 0.8, 9.0, yy - 0.2);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

static void draw_wing(Hawk *b, cairo_t *cr, int near, int raised) {
  Flyer *f = &b->f;
  if (av_pixel_mode() && !near && !raised) return;

  double y_off = near ? 0 : 2.4;
  double shade = near ? 0.0 : 0.3;
  double alpha = near ? 1.0 : 0.85;
  /* raised = the mantle: wings thrown up into a braking V — high, but not so
   * vertical that the two of them merge into one dark blob at this size */
  double range = raised ? av_lerp(86.0, 124.0, b->mantle)
                        : av_lerp(80.0, 116.0, b->clap);

  WingPose w;
  wing_pose(f, range, raised ? 16.0 : 26.0, WING_LEN, near ? 1.0 : 0.9,
            SH_X, SH_Y, y_off, &w);

  Rgb base = av_mix(K_BACK,  K_BACK2, shade);
  Rgb edge = av_mix(K_BACK2, K_HOOD,  shade);
  Rgb tipc = av_mix(K_HOOD,  K_HOOD,  shade);

  double ca = cos(w.hand_phi), sa = sin(w.hand_phi);
  double tipx = w.wx + ca * w.hand * 1.30;      /* long pointed hand */
  double tipy = w.wy + sa * w.hand * 1.30;
  double ta = w.hand_phi + 26 * D2R;
  double trx = w.wx + cos(ta) * w.hand * 0.72;
  double try_ = w.wy + sin(ta) * w.hand * 0.72;

  cairo_new_path(cr);
  cairo_move_to(cr, SH_X + 1.4, SH_Y + y_off);
  av_quad_to(cr, w.bx, w.by, tipx, tipy);
  cairo_line_to(cr, trx, try_);
  av_quad_to(cr, av_lerp(trx, -10.0, 0.5) - 2.0,
             av_lerp(try_, -1.0 + y_off, 0.5) + 1.4, -10.0, -1.0 + y_off);
  av_quad_to(cr, -2.0, SH_Y + 1.4 + y_off, SH_X + 1.4, SH_Y + y_off);
  cairo_close_path(cr);
  av_set_rgba(cr, base, alpha);
  cairo_fill_preserve(cr);
  av_set_rgba(cr, K_OUTLINE, alpha);
  cairo_set_line_width(cr, 0.65 / f->scale);
  cairo_stroke(cr);

  /* dark pointed tip */
  cairo_new_path(cr);
  cairo_move_to(cr, av_lerp(w.wx, tipx, 0.5), av_lerp(w.wy, tipy, 0.5));
  cairo_line_to(cr, tipx, tipy);
  cairo_line_to(cr, trx, try_);
  cairo_line_to(cr, av_lerp(w.wx, trx, 0.55), av_lerp(w.wy, try_, 0.55));
  cairo_close_path(cr);
  av_set_rgba(cr, tipc, alpha);
  cairo_fill(cr);

  /* pale, barred underwing shows when the wing is thrown up */
  if (raised && b->mantle > 0.3) {
    av_set_rgba(cr, K_PALE, alpha * 0.7 * b->mantle);
    cairo_set_line_width(cr, 1.4);
    cairo_move_to(cr, SH_X, SH_Y + 1.0 + y_off);
    av_quad_to(cr, av_lerp(SH_X, w.wx, 0.5), av_lerp(SH_Y, w.wy, 0.5) + 1.0, w.wx, w.wy);
    cairo_stroke(cr);
  }
  (void)edge;
}

static void draw_head(Hawk *b, cairo_t *cr) {
  Flyer *f = &b->f;
  double dx = dip_dx(b) + b->look * 1.1;
  double dy = dip_dy(b);
  double cx = HEAD_X + dx, cy = HEAD_Y + dy;

  /* skull: dark slate hood */
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, HEAD_R, 0, TAU);
  av_set_rgba(cr, K_HOOD, 1);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, K_OUTLINE, 1);
    cairo_set_line_width(cr, 0.7 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }

  /* pale cheek/throat with the dark moustache dropping from below the eye */
  av_set_rgba(cr, K_PALE, 1);
  cairo_new_path(cr);
  cairo_move_to(cr, cx + 1.0, cy + 1.0);
  av_quad_to(cr, cx + HEAD_R, cy + 1.2, cx + HEAD_R - 0.4, cy + 3.6);
  av_quad_to(cr, cx + 1.6, cy + 4.2, cx + 0.6, cy + 2.4);
  cairo_close_path(cr);
  cairo_fill(cr);
  /* the black malar stripe */
  av_set_rgba(cr, K_HOOD, 1);
  cairo_set_line_width(cr, 1.4);
  cairo_move_to(cr, cx + 1.6, cy + 0.6);
  cairo_line_to(cr, cx + 2.2, cy + 3.8);
  cairo_stroke(cr);

  /* the hooked beak with a yellow cere at its base */
  av_set_rgba(cr, K_CERE, 1);
  cairo_new_path(cr);
  cairo_arc(cr, 17.0 + dx, -9.4 + dy, 1.4, 0, TAU);
  cairo_fill(cr);
  av_set_rgba(cr, K_BEAK, 1);
  cairo_new_path(cr);
  cairo_move_to(cr, 16.8 + dx, -10.0 + dy);
  av_quad_to(cr, BEAK_X + dx, -9.6 + dy, BEAK_X + 1.2 + dx, BEAK_Y + dy);
  av_quad_to(cr, BEAK_X + 0.6 + dx, BEAK_Y + 2.0 + dy, 18.0 + dx, -7.4 + dy);  /* the hook */
  av_quad_to(cr, 17.0 + dx, -8.2 + dy, 16.8 + dx, -10.0 + dy);
  cairo_close_path(cr);
  cairo_fill(cr);

  /* eye: dark, fierce, ringed yellow, set under a heavy slate brow */
  double ex = EYE_X + dx, ey = EYE_Y + dy;
  if (f->blink > 0) {
    av_set_rgba(cr, K_BACK2, 0.9);
    cairo_set_line_width(cr, 0.9);
    cairo_new_path(cr);
    cairo_arc(cr, ex, ey, 1.6, 0.2, M_PI - 0.2);
    cairo_stroke(cr);
  } else {
    av_set_rgba(cr, K_CERE, 1);
    cairo_arc(cr, ex, ey, 1.9, 0, TAU);
    cairo_fill(cr);
    av_set_rgba(cr, K_EYE, 1);
    cairo_arc(cr, ex + 0.1, ey, 1.4, 0, TAU);
    cairo_fill(cr);
    if (!av_pixel_mode()) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
      cairo_arc(cr, ex + 0.5, ey - 0.5, 0.4, 0, TAU);
      cairo_fill(cr);
    }
  }
  /* heavy brow: a dark line over the eye for the flat, cold stare */
  av_set_rgba(cr, K_HOOD, 1);
  cairo_set_line_width(cr, 1.1);
  cairo_move_to(cr, ex - 1.8, ey - 1.8);
  cairo_line_to(cr, ex + 2.0, ey - 1.6);
  cairo_stroke(cr);
}

void hawk_draw(Hawk *b, cairo_t *cr) {
  Flyer *f = &b->f;
  int raised = !b->grounded || b->mantle > 0.2;
  cairo_save(cr);
  flyer_transform(f, cr);

  draw_tail(b, cr);
  if (raised) draw_wing(b, cr, 0, 1);        /* far wing, thrown up on the brake */
  draw_leg(b, cr, 0, 0);
  draw_body(b, cr);
  draw_head(b, cr);
  if (raised) draw_wing(b, cr, 1, 1);        /* near wing */
  else {
    /* folded: pointed primaries laid back along the body to the tail */
    cairo_new_path(cr);
    cairo_move_to(cr, 5.0, -5.8);
    av_quad_to(cr, -3.0, -6.8, -12.0, -3.0);
    av_quad_to(cr, -14.5, -1.8, -13.0, 0.6);
    av_quad_to(cr, -6.0, 2.4, 2.0, 0.8);
    av_quad_to(cr, 5.2, -1.0, 5.0, -5.8);
    cairo_close_path(cr);
    av_set_rgba(cr, K_BACK, 1);
    if (av_pixel_mode()) {
      cairo_fill_preserve(cr);
      av_set_rgba(cr, K_OUTLINE, 1);
      cairo_set_line_width(cr, 0.7 / b->f.scale);
      cairo_stroke(cr);
    } else {
      cairo_fill(cr);
    }
    /* dark primary tips reaching the tail */
    av_set_rgba(cr, K_HOOD, 1);
    cairo_set_line_width(cr, 1.3);
    cairo_move_to(cr, -8.0, -2.0);
    av_quad_to(cr, -12.0, -1.0, -13.0, 0.4);
    cairo_stroke(cr);
  }
  draw_leg(b, cr, 1, 1);
  draw_carried_letter(b, cr);

  cairo_restore(cr);
}

void hawk_bbox(Hawk *b, double *x0, double *y0, double *x1, double *y1) {
  double r = 42 * b->f.scale;
  double cx = b->f.x, cy = b->f.y + b->f.bob;
  if (cx - r < *x0) *x0 = cx - r;
  if (cy - r < *y0) *y0 = cy - r;
  if (cx + r > *x1) *x1 = cx + r;
  if (cy + r > *y1) *y1 = cy + r;
}
