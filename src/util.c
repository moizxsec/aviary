#include "aviary.h"
#include <math.h>
#include <stdlib.h>

double av_clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
double av_lerp(double a, double b, double t)  { return a + (b - a) * t; }
double av_smooth(double t)   { return t * t * (3 - 2 * t); }
double av_smoother(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }

double av_ease_out_cubic(double t) { double u = 1 - t; return 1 - u * u * u; }
double av_ease_in_cubic(double t)  { return t * t * t; }
double av_ease_in_out(double t) {
  return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2;
}

double av_wrap_angle(double a) {
  while (a >  M_PI) a -= TAU;
  while (a < -M_PI) a += TAU;
  return a;
}

double av_angle_towards(double cur, double target, double rate, double dt) {
  double d = av_wrap_angle(target - cur);
  return cur + d * (1 - exp(-rate * dt));
}

double av_damp(double cur, double target, double rate, double dt) {
  return cur + (target - cur) * (1 - exp(-rate * dt));
}

/* ------------------------------------------------------------------ rng -- */

static uint64_t rng_state = 0x853c49e6748fea9bULL;

void av_seed(uint64_t s) { rng_state = s ? s : 1; }

static uint64_t xorshift(void) {
  uint64_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  return rng_state = x;
}

double av_rand(void) { return (double)(xorshift() >> 11) / 9007199254740992.0; }
double av_rand_range(double a, double b) { return a + av_rand() * (b - a); }
double av_rand_sym(double a) { return (av_rand() * 2 - 1) * a; }
int    av_rand_int(int n) { return n <= 0 ? 0 : (int)(av_rand() * n) % n; }

/* --------------------------------------------------------- world scale -- */

static double world_scale = 1.0;
static int    pixel_mode = 0;

void   av_set_world(double w) { world_scale = w > 0.05 ? w : 0.05; }
double av_world(void) { return world_scale; }

void   av_set_pixel_mode(int on) { pixel_mode = on ? 1 : 0; }

static double water_floor = 0;
void   av_set_water_floor(double y) { water_floor = y; }
double av_water_floor(void) { return water_floor; }
int    av_pixel_mode(void) { return pixel_mode; }

/* --------------------------------------------------------------- noise -- */
/* Cheap 1D value noise. Several independent streams so different parts of a
 * bird can drift without visibly sharing a rhythm. */

#define NOISE_N 512
#define NOISE_STREAMS 4
static double noise_tab[NOISE_STREAMS][NOISE_N];
static int    noise_ready = 0;

static void noise_init(void) {
  uint32_t s = 0x9e3779b9u;
  for (int k = 0; k < NOISE_STREAMS; k++)
    for (int i = 0; i < NOISE_N; i++) {
      s = s * 1664525u + 1013904223u;
      noise_tab[k][i] = (double)s / 4294967296.0;
    }
  noise_ready = 1;
}

double av_noise(double x, int stream) {
  if (!noise_ready) noise_init();
  stream = ((stream % NOISE_STREAMS) + NOISE_STREAMS) % NOISE_STREAMS;
  double fl = floor(x);
  int    i  = (int)((long long)fl & (NOISE_N - 1));
  double f  = x - fl;
  double a  = noise_tab[stream][i];
  double b  = noise_tab[stream][(i + 1) & (NOISE_N - 1)];
  return (a + (b - a) * av_smoother(f)) * 2 - 1;
}

/* --------------------------------------------------------------- colour -- */

Rgb av_mix(Rgb a, Rgb b, double t) {
  Rgb o = { av_lerp(a.r, b.r, t), av_lerp(a.g, b.g, t), av_lerp(a.b, b.b, t) };
  return o;
}

/* 0 = white hot, 1 = dead ash. Every flame in the app samples this ramp, which
 * is what keeps the fire, the embers and the glow looking like one substance. */
Rgb av_fire_color(double t) {
  t = av_clamp(t, 0, 1);
  double r, g, b;
  if (t < 0.22) {
    double k = t / 0.22;
    r = 1.0; g = av_lerp(0.969, 0.820, k); b = av_lerp(0.886, 0.463, k);
  } else if (t < 0.50) {
    double k = (t - 0.22) / 0.28;
    r = 1.0; g = av_lerp(0.820, 0.518, k); b = av_lerp(0.463, 0.125, k);
  } else if (t < 0.78) {
    double k = (t - 0.50) / 0.28;
    r = av_lerp(1.0, 0.816, k); g = av_lerp(0.518, 0.165, k); b = av_lerp(0.125, 0.063, k);
  } else {
    double k = (t - 0.78) / 0.22;
    r = av_lerp(0.816, 0.290, k); g = av_lerp(0.165, 0.118, k); b = av_lerp(0.063, 0.102, k);
  }
  Rgb c = { r, g, b };
  return c;
}

/* ---------------------------------------------------------------- cairo -- */

/* canvas quadraticCurveTo has no cairo equivalent; lift it to a cubic */
void av_quad_to(cairo_t *cr, double qx, double qy, double x, double y) {
  double x0, y0;
  if (!cairo_has_current_point(cr)) cairo_move_to(cr, qx, qy);
  cairo_get_current_point(cr, &x0, &y0);
  cairo_curve_to(cr,
                 x0 + 2.0 / 3.0 * (qx - x0), y0 + 2.0 / 3.0 * (qy - y0),
                 x  + 2.0 / 3.0 * (qx - x ), y  + 2.0 / 3.0 * (qy - y ),
                 x, y);
}

void av_set_rgba(cairo_t *cr, Rgb c, double a) {
  cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
}

void av_round_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r,     r, -M_PI / 2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0,          M_PI / 2);
  cairo_arc(cr, x + r,     y + h - r, r, M_PI / 2,   M_PI);
  cairo_arc(cr, x + r,     y + r,     r, M_PI,       3 * M_PI / 2);
  cairo_close_path(cr);
}


/* A small roll of paper bound to the bird's leg with a couple of turns of
 * thread. Every bird carries it the same way. */
void av_draw_tied_letter(cairo_t *cr, double x, double y, double rot, double tie_dx) {
  cairo_save(cr);

  /* the thread running up to the leg, drawn before the roll so it tucks under */
  cairo_set_source_rgba(cr, 0.30, 0.26, 0.22, 0.95);
  cairo_set_line_width(cr, 0.5);
  cairo_move_to(cr, x + tie_dx, y - 2.6);
  cairo_line_to(cr, x, y - 0.9);
  cairo_stroke(cr);

  cairo_translate(cr, x, y);
  cairo_rotate(cr, rot);

  /* the roll */
  cairo_set_source_rgb(cr, 0.882, 0.847, 0.741);
  av_round_rect(cr, -2.4, -1.15, 4.8, 2.3, 1.0);
  cairo_fill(cr);

  /* the cut ends, darker */
  cairo_set_source_rgb(cr, 0.741, 0.686, 0.565);
  av_round_rect(cr, -2.7, -1.35, 1.0, 2.7, 0.5);
  cairo_fill(cr);
  av_round_rect(cr, 1.7, -1.35, 1.0, 2.7, 0.5);
  cairo_fill(cr);

  /* two turns of thread holding it shut */
  cairo_set_source_rgba(cr, 0.28, 0.24, 0.20, 1.0);
  cairo_set_line_width(cr, 0.45);
  for (int i = 0; i < 2; i++) {
    double tx = -0.7 + i * 1.4;
    cairo_move_to(cr, tx, -1.3);
    cairo_line_to(cr, tx, 1.3);
    cairo_stroke(cr);
  }

  cairo_restore(cr);
}
