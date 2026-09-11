/* The hummingbird.
 *
 * The opposite of the owl in every way. It does not land, it does not walk, it
 * does not wait to be read: it darts in on wings that are already a blur, holds
 * itself dead still in the air on nothing but that blur, lets the letter go,
 * flicks about the spot once out of what looks like curiosity, and is gone
 * before you have looked up. Blink and you miss it — which is the whole point.
 *
 * Tiny. A needle for a bill, a throat that catches the light and burns for a
 * frame, and wings drawn not as feathers but as the smear feathers make when
 * they beat fifty times a second. It never burns, and it never holds still for
 * long enough to be sure you saw it.
 */
#include "aviary.h"
#include <math.h>
#include <string.h>

/* ---- palette ---------------------------------------------------------- */
static const Rgb H_BACK    = { 0.157, 0.549, 0.361 };   /* iridescent green back */
static const Rgb H_BACK2   = { 0.267, 0.706, 0.447 };   /* the lit edge of it */
static const Rgb H_BELLY   = { 0.859, 0.867, 0.831 };   /* pale grey underside */
static const Rgb H_BELLYSH = { 0.694, 0.718, 0.706 };
/* Reds the shared palette can actually hold: a deep crimson and a rose it
 * flashes to. Pinker than these and the quantiser snaps the throat to orange. */
static const Rgb H_GORGET  = { 0.690, 0.157, 0.188 };   /* the throat, unlit */
static const Rgb H_GORGET2 = { 0.839, 0.463, 0.478 };   /* the throat, catching light */
static const Rgb H_DARK    = { 0.114, 0.220, 0.176 };
static const Rgb H_BILL    = { 0.110, 0.102, 0.114 };
static const Rgb H_WING    = { 0.749, 0.769, 0.792 };   /* the wing-smear */
static const Rgb H_FOOT    = { 0.216, 0.180, 0.184 };
static const Rgb H_OUTLINE = { 0.086, 0.169, 0.133 };
static const Rgb H_EYEWHT  = { 0.937, 0.941, 0.925 };

/* ---- anatomy, in body units (+x forward, +y down) --------------------- */
/* A small bird, authored small on top of a small scale, so it comes out the
 * tiniest thing in the aviary. */
#define SH_X     1.8
#define SH_Y    -3.8
#define TAIL_X  -9.0
#define TAIL_Y  -0.6
#define TAIL_LEN 8.5
#define HEAD_X   9.6
#define HEAD_Y  -5.4
#define HEAD_R   3.2
#define EYE_X    11.0
#define EYE_Y   -5.9
#define BILL_X   13.4
#define BILL_Y   -5.0
#define BILL_END 25.5      /* a long straight needle */
#define GORGET_X 9.4
#define GORGET_Y -2.6
#define LEG_X    0.6
#define LEG_Y    2.6
#define WING_LEN 15.0

void hummingbird_init(Hummingbird *b, double x, double y, double scale) {
  memset(b, 0, sizeof(*b));
  double W = av_world();
  flyer_init(&b->f, x, y);
  b->f.scale = scale;
  b->f.max_speed = 360 * W;      /* quick, but the *character* is the jitter */
  b->f.max_force = 2200 * W;     /* it can change direction on a pin */
  b->f.wander_gain = 240 * W;
  b->carrying = 1;
  b->tail_fan = 0.3;
  b->next_dart = av_rand_range(0.18, 0.5);
}

Vec hummingbird_letter_point(Hummingbird *b) {
  return flyer_to_world(&b->f, LEG_X - 0.4, LEG_Y + 3.2 + b->dip * 3.0);
}

/* ---- flight ------------------------------------------------------------ */
/* No grounded state at all: it is always in the air. Body integration is the
 * hover model — the same as the phoenix — but with a harder speed cap when
 * hovering so it truly parks in one spot, plus a fast micro-jitter of its own
 * so "still" never means motionless. */
static void hummingbird_air(Hummingbird *b, double dt, Particles *P) {
  Flyer *f = &b->f;
  f->t += dt;
  (void)P;

  if (f->nwp > 0) {
    int last = (f->nwp == 1);
    Waypoint *w = &f->wp[0];
    double d = flyer_seek(f, w->p.x, w->p.y,
                          last ? (w->slow > 0 ? w->slow : f->arrive_radius) : 0);
    double hit = w->radius > 0 ? w->radius : (last ? 22 : 110);
    if (d < hit && !last) {
      memmove(&f->wp[0], &f->wp[1], sizeof(Waypoint) * (size_t)(f->nwp - 1));
      f->nwp--;
    }
  }

  /* the jitter: a new tiny impulse a few times a second, so even a hovering
   * bird is never quite where it was */
  b->dart_t += dt;
  if (b->dart_t > b->next_dart) {
    b->dart_t = 0;
    b->next_dart = av_rand_range(0.16, 0.46);
    double g = f->hover ? 26 : 18;
    b->aim_x = av_rand_sym(g) * av_world();
    b->aim_y = av_rand_sym(g * 0.7) * av_world();
  }
  flyer_force(f, b->aim_x * 3.2, b->aim_y * 3.2);

  if (f->wander_gain > 0)
    flyer_force(f,
                av_noise(f->t * 0.9 + f->noise_off, 0) * f->wander_gain,
                av_noise(f->t * 1.1 + f->noise_off, 1) * f->wander_gain * 0.85);

  f->vx += f->ax * dt;
  f->vy += f->ay * dt;
  double sp = hypot(f->vx, f->vy);
  double cap = f->hover ? f->max_speed * 0.22 : f->max_speed;
  if (sp > cap && sp > 0) { f->vx = f->vx / sp * cap; f->vy = f->vy / sp * cap; }
  f->x += f->vx * dt;
  f->y += f->vy * dt;

  flyer_update_pose(f, dt);

  /* Hovering, hold the body near level with just a touch of nose-up, instead
   * of letting the jitter swing the whole body around as it does at speed. */
  if (f->hover) {
    f->heading = av_damp(f->heading, 0, 16, dt);
    f->pitch   = av_damp(f->pitch, -0.10, 6, dt);
  }

  /* wings are a permanent blur: advance a fast phase and let the draw code
   * pulse the smear off it. There is no glide, ever. */
  f->wing_hz = 34;
  f->wing_phase += f->wing_hz * dt;
  f->spread = 1;
  f->gliding = 0;
  b->blur = 0.72 + 0.28 * sin(f->wing_phase * TAU);

  /* the gorget catches the light on a slower cycle, then goes matte */
  double lit = 0.5 + 0.5 * sin(f->t * 2.3 + 0.8);
  b->gorget = av_damp(b->gorget, lit * lit, 6, dt);

  /* the little feet drop when it parks, tuck up when it moves off */
  int parked = f->hover || flyer_speed(f) < f->max_speed * 0.3;
  b->legs_out = av_damp(b->legs_out, parked ? 1.0 : 0.12, 6, dt);
  b->tail_fan = av_damp(b->tail_fan, parked ? 0.55 : 0.22, 5, dt);

  f->blink_at -= dt;
  if (f->blink_at <= 0) { f->blink = 0.08; f->blink_at = av_rand_range(2.4, 6.0); }
  if (f->blink > 0) f->blink -= dt;

  f->ax = 0;
  f->ay = 0;
}

void hummingbird_update(Hummingbird *b, double dt, Particles *P) {
  hummingbird_air(b, dt, P);
}

/* ---- drawing ----------------------------------------------------------- */

/* The wing as the smear it makes, not as feathers. A translucent lens swept
 * out from the shoulder; the quantiser turns the soft alpha into a stipple,
 * which is exactly what a beating wing looks like at this size. Drawn twice —
 * the far wing behind the body, dim; the near wing in front, brighter. */
static void draw_wing_blur(Hummingbird *b, cairo_t *cr, int near) {
  double t = b->f.wing_phase * TAU;
  /* A hovering hummingbird sweeps its wings in a near-horizontal figure-eight,
   * so the smear is a roughly level fore-and-aft blur that only rocks a little,
   * not a wing thrown up over the back. Flat reads as a hummingbird; steep
   * reads as any old bird mid-flap. */
  double tilt = -5 * D2R + sin(t) * 12 * D2R;
  double len = WING_LEN * (0.80 + 0.13 * b->blur);
  double thick = WING_LEN * 0.34;
  double alpha = (near ? 0.46 : 0.30) * (0.7 + 0.3 * b->blur);

  double cx = SH_X + cos(tilt) * len * 0.5;
  double cy = SH_Y + sin(tilt) * len * 0.5 + (near ? 0.6 : 1.8);

  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_rotate(cr, tilt);
  cairo_scale(cr, len * 0.5, thick * 0.5);
  cairo_new_path(cr);
  cairo_arc(cr, 0, 0, 1.0, 0, TAU);
  cairo_restore(cr);
  av_set_rgba(cr, H_WING, alpha);
  cairo_fill(cr);

  /* a brighter leading streak along the top edge of the smear */
  if (near) {
    double lx = SH_X + cos(tilt) * len;
    double ly = SH_Y + sin(tilt) * len;
    av_set_rgba(cr, H_WING, alpha * 0.9);
    cairo_set_line_width(cr, 1.1);
    cairo_move_to(cr, SH_X, SH_Y);
    cairo_line_to(cr, lx, ly);
    cairo_stroke(cr);
  }
}

static void draw_tail(Hummingbird *b, cairo_t *cr) {
  double fan = b->tail_fan;
  double spread = av_lerp(2.0, 5.4, fan);
  double droop = 1.4;
  cairo_new_path(cr);
  cairo_move_to(cr, TAIL_X + 2.0, TAIL_Y - 1.0);
  cairo_line_to(cr, TAIL_X - TAIL_LEN, TAIL_Y - spread + droop);
  cairo_line_to(cr, TAIL_X - TAIL_LEN, TAIL_Y + spread + droop);
  cairo_line_to(cr, TAIL_X + 2.0, TAIL_Y + 1.2);
  cairo_close_path(cr);
  av_set_rgba(cr, H_DARK, 1);
  cairo_fill_preserve(cr);
  if (av_pixel_mode()) {
    av_set_rgba(cr, H_OUTLINE, 1);
    cairo_set_line_width(cr, 0.6 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_new_path(cr);
  }
  /* pale tips, the way a hummingbird's outer tail feathers flash */
  av_set_rgba(cr, H_BELLY, 0.9);
  cairo_set_line_width(cr, 0.8);
  cairo_move_to(cr, TAIL_X - TAIL_LEN + 0.5, TAIL_Y - spread + droop);
  cairo_line_to(cr, TAIL_X - TAIL_LEN + 0.5, TAIL_Y + spread + droop);
  cairo_stroke(cr);
}

static void draw_leg(Hummingbird *b, cairo_t *cr) {
  if (b->legs_out < 0.05) return;
  double reach = 2.4 * b->legs_out;
  av_set_rgba(cr, H_FOOT, 1);
  cairo_set_line_width(cr, 0.9);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  for (int i = 0; i < 2; i++) {
    double lx = LEG_X + (i ? 1.2 : -0.6);
    cairo_move_to(cr, lx, LEG_Y);
    cairo_line_to(cr, lx + 0.4, LEG_Y + reach);
    cairo_stroke(cr);
  }
}

static void draw_carried_letter(Hummingbird *b, cairo_t *cr) {
  if (!b->carrying) return;
  double x = LEG_X - 0.4;
  double y = LEG_Y + 2.4 + b->dip * 3.0;
  av_draw_tied_letter(cr, x, y, 0.24 + b->f.bob * 0.02, 0.5);
}

static void draw_body(Hummingbird *b, cairo_t *cr) {
  /* a compact teardrop: full at the shoulder, tapering back to the tail */
  cairo_new_path(cr);
  cairo_move_to(cr, 8.4, -3.6);
  cairo_curve_to(cr,  4.0, -6.4, -4.0, -5.8, -8.6, -2.2);
  cairo_curve_to(cr, -9.6, -1.4, -9.4,  0.8, -7.6,  1.8);
  cairo_curve_to(cr, -3.0,  4.2,  4.0,  4.0,  7.6,  1.8);
  cairo_curve_to(cr,  9.6,  0.6, 10.0, -1.8,  8.4, -3.6);
  cairo_close_path(cr);

  /* green back over a pale belly */
  cairo_pattern_t *g = cairo_pattern_create_linear(0, -6, 0, 4);
  cairo_pattern_add_color_stop_rgb(g, 0.00, H_BACK2.r, H_BACK2.g, H_BACK2.b);
  cairo_pattern_add_color_stop_rgb(g, 0.45, H_BACK.r,  H_BACK.g,  H_BACK.b);
  cairo_pattern_add_color_stop_rgb(g, 0.66, H_BELLYSH.r, H_BELLYSH.g, H_BELLYSH.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, H_BELLY.r, H_BELLY.g, H_BELLY.b);
  cairo_set_source(cr, g);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, H_OUTLINE, 1);
    cairo_set_line_width(cr, 0.65 / b->f.scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(g);
}

static void draw_head(Hummingbird *b, cairo_t *cr) {
  Flyer *f = &b->f;

  /* the gorget: a small angular throat patch that flares bright, then matte */
  Rgb gc = av_mix(H_GORGET, H_GORGET2, b->gorget);
  cairo_new_path(cr);
  cairo_move_to(cr, GORGET_X + 2.6, GORGET_Y - 1.2);
  cairo_line_to(cr, GORGET_X - 1.4, GORGET_Y + 0.2);
  cairo_line_to(cr, GORGET_X - 0.6, GORGET_Y + 2.4);
  cairo_line_to(cr, GORGET_X + 3.0, GORGET_Y + 1.4);
  cairo_close_path(cr);
  av_set_rgba(cr, gc, 1);
  cairo_fill(cr);
  if (!av_pixel_mode() && b->gorget > 0.5) {
    cairo_set_source_rgba(cr, 1, 1, 1, (b->gorget - 0.5) * 0.7);
    cairo_arc(cr, GORGET_X + 1.4, GORGET_Y + 0.4, 0.7, 0, TAU);
    cairo_fill(cr);
  }

  /* skull */
  double cx = HEAD_X, cy = HEAD_Y;
  cairo_new_path(cr);
  cairo_arc(cr, cx, cy, HEAD_R, 0, TAU);
  cairo_pattern_t *hg = cairo_pattern_create_radial(cx + 0.8, cy - 1.0, 0.3, cx, cy, HEAD_R * 1.5);
  cairo_pattern_add_color_stop_rgb(hg, 0.0, H_BACK2.r, H_BACK2.g, H_BACK2.b);
  cairo_pattern_add_color_stop_rgb(hg, 1.0, H_BACK.r,  H_BACK.g,  H_BACK.b);
  cairo_set_source(cr, hg);
  if (av_pixel_mode()) {
    cairo_fill_preserve(cr);
    av_set_rgba(cr, H_OUTLINE, 1);
    cairo_set_line_width(cr, 0.65 / f->scale);
    cairo_stroke(cr);
  } else {
    cairo_fill(cr);
  }
  cairo_pattern_destroy(hg);

  /* the needle bill: long, straight, black. Kept deliberately heavy — a
   * one-unit line is sub-pixel at this size and quantises to a broken dotted
   * thing, and the bill is the single clearest tell that this is a hummingbird. */
  av_set_rgba(cr, H_BILL, 1);
  cairo_set_line_width(cr, 2.0);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, HEAD_X + 1.0, BILL_Y + 0.2);
  cairo_line_to(cr, BILL_END, BILL_Y - 0.4);
  cairo_stroke(cr);

  /* eye: a small dark bead with a pale catch */
  double ex = EYE_X, ey = EYE_Y;
  if (f->blink > 0) {
    av_set_rgba(cr, H_DARK, 0.9);
    cairo_set_line_width(cr, 0.7);
    cairo_new_path(cr);
    cairo_arc(cr, ex, ey, 1.1, 0.2, M_PI - 0.2);
    cairo_stroke(cr);
  } else {
    av_set_rgba(cr, H_OUTLINE, 1);
    cairo_arc(cr, ex, ey, 1.35, 0, TAU);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.04, 0.04, 0.05);
    cairo_arc(cr, ex, ey, 0.95, 0, TAU);
    cairo_fill(cr);
    if (!av_pixel_mode()) {
      av_set_rgba(cr, H_EYEWHT, 0.9);
      cairo_arc(cr, ex + 0.4, ey - 0.4, 0.32, 0, TAU);
      cairo_fill(cr);
    }
  }
}

void hummingbird_draw(Hummingbird *b, cairo_t *cr) {
  Flyer *f = &b->f;
  cairo_save(cr);
  flyer_transform(f, cr);

  draw_wing_blur(b, cr, 0);      /* far wing, behind everything */
  draw_tail(b, cr);
  draw_leg(b, cr);
  draw_body(b, cr);
  draw_head(b, cr);
  draw_wing_blur(b, cr, 1);      /* near wing, over the body */
  draw_carried_letter(b, cr);

  cairo_restore(cr);
}

void hummingbird_bbox(Hummingbird *b, double *x0, double *y0, double *x1, double *y1) {
  double r = 30 * b->f.scale;
  double cx = b->f.x, cy = b->f.y + b->f.bob;
  if (cx - r < *x0) *x0 = cx - r;
  if (cy - r < *y0) *y0 = cy - r;
  if (cx + r > *x1) *x1 = cx + r;
  if (cy + r > *y1) *y1 = cy + r;
}
