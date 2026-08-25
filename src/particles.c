#include "aviary.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void particles_init(Particles *p, int cap) {
  p->a = (Particle *)calloc((size_t)cap, sizeof(Particle));
  p->cap = p->a ? cap : 0;
  p->n = 0;
}

void particles_free(Particles *p) { free(p->a); p->a = NULL; p->n = p->cap = 0; }
void particles_clear(Particles *p) { p->n = 0; }

static Particle *spawn(Particles *p, int kind) {
  if (p->n >= p->cap) return NULL;
  Particle *q = &p->a[p->n++];
  memset(q, 0, sizeof(*q));
  q->kind = kind;
  return q;
}

void p_fire(Particles *p, double x, double y, double vx, double vy,
            double r, double ttl, double buoy) {
  Particle *q = spawn(p, P_FIRE);
  if (!q) return;
  double W = av_world();
  q->x = x; q->y = y;
  q->vx = vx + av_rand_sym(34) * W;
  q->vy = vy - av_rand_range(22, 78) * W;
  q->r = r * W; q->ttl = ttl;
  q->buoy = buoy * W; q->drag = 2.2;
  q->wob = av_rand_range(0, TAU);
  q->wob_rate = av_rand_range(5, 11);
  q->heat0 = av_rand_range(0, 0.16);
}

void p_ember(Particles *p, double x, double y, double vx, double vy,
             double spread, double r, double ttl) {
  Particle *q = spawn(p, P_EMBER);
  if (!q) return;
  double W = av_world();
  q->x = x; q->y = y;
  q->vx = vx + av_rand_sym(spread) * W;
  q->vy = vy - av_rand_range(30, 130) * W;
  q->r = r * W; q->ttl = ttl;
  q->buoy = av_rand_range(30, 95) * W;
  q->grav = av_rand_range(60, 150) * W;
  q->drag = 0.9;
  q->wob = av_rand_range(0, TAU);
  q->wob_rate = av_rand_range(3, 8);
  q->wob_amp = av_rand_range(14, 46) * W;
  q->heat0 = av_rand_range(0, 0.2);
}

void p_spark(Particles *p, double x, double y, double spread) {
  Particle *q = spawn(p, P_SPARK);
  if (!q) return;
  double W = av_world();
  q->x = x; q->y = y;
  q->vx = av_rand_sym(spread) * W;
  q->vy = (av_rand_sym(spread) - 40) * W;
  q->r = av_rand_range(0.5, 1.2) * W;
  q->ttl = av_rand_range(0.18, 0.5);
  q->drag = 3.4; q->grav = 240 * W;
  q->len = av_rand_range(3, 9) * W;
}

void p_ash(Particles *p, double x, double y, double vx, double vy,
           double ttl, double glow) {
  Particle *q = spawn(p, P_ASH);
  if (!q) return;
  double W = av_world();
  q->x = x; q->y = y;
  q->vx = vx + av_rand_sym(26) * W;
  q->vy = vy - av_rand_range(10, 60) * W;
  q->r = av_rand_range(0.9, 2.6) * W;
  q->ttl = ttl;
  q->grav = av_rand_range(9, 26) * W;
  q->drag = 1.6;
  q->wob = av_rand_range(0, TAU);
  q->wob_rate = av_rand_range(0.7, 2.1);
  q->wob_amp = av_rand_range(16, 52) * W;
  q->spin = av_rand_sym(3);
  q->rot = av_rand_range(0, TAU);
  q->glow = glow;
}

void p_smoke(Particles *p, double x, double y, double r, double alpha) {
  Particle *q = spawn(p, P_SMOKE);
  if (!q) return;
  double W = av_world();
  q->x = x; q->y = y;
  q->vx = av_rand_sym(14) * W;
  q->vy = -av_rand_range(14, 44) * W;
  q->r = r * W;
  q->grow = av_rand_range(6, 15) * W;
  q->ttl = av_rand_range(1.2, 2.6);
  q->drag = 1.1;
  q->wob = av_rand_range(0, TAU);
  q->wob_rate = av_rand_range(0.5, 1.4);
  q->wob_amp = av_rand_range(8, 24) * W;
  q->alpha = alpha;
}

/* A feather is mostly drag and no mass: it barely falls, and it rocks the
 * whole way down. */
void p_feather(Particles *p, double x, double y, double vx, double vy, double shade) {
  Particle *q = spawn(p, P_FEATHER);
  if (!q) return;
  double W = av_world();
  q->x = x; q->y = y;
  q->vx = vx + av_rand_sym(20) * W;
  q->vy = vy + av_rand_range(-10, 14) * W;
  q->r = av_rand_range(1.4, 2.8) * W;
  q->ttl = av_rand_range(1.6, 3.0);
  q->grav = av_rand_range(22, 44) * W;
  q->drag = 2.6;
  q->wob = av_rand_range(0, TAU);
  q->wob_rate = av_rand_range(1.8, 3.6);
  q->wob_amp = av_rand_range(34, 78) * W;
  q->spin = av_rand_sym(2.2);
  q->rot = av_rand_range(0, TAU);
  q->heat0 = shade;
  q->len = av_rand_range(2.6, 4.6) * W;
}

void p_water(Particles *p, double x, double y, double vx, double vy, int is_spray) {
  Particle *q = spawn(p, P_WATER);
  if (!q) return;
  double W = av_world();
  q->x = x; q->y = y;
  q->vx = vx; q->vy = vy;
  q->r = av_rand_range(0.7, 1.5) * W;
  q->ttl = is_spray ? av_rand_range(0.35, 0.95) : av_rand_range(1.2, 2.6);
  q->grav = (is_spray ? 900 : 520) * W;
  q->drag = is_spray ? 1.4 : 0.05;
  q->heat0 = is_spray ? 1.0 : 0.0;     /* spray never splashes again */
}

void p_ring(Particles *p, double x, double y, double r0, double r1,
            double w0, double ttl, double hue) {
  Particle *q = spawn(p, P_RING);
  if (!q) return;
  q->x = x; q->y = y;
  q->r = r0; q->r1 = r1; q->w0 = w0; q->ttl = ttl; q->hue = hue;
}

/* ------------------------------------------------------------- update -- */

void particles_update(Particles *p, double dt) {
  int w = 0;
  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    q->life += dt;
    if (q->life >= q->ttl) continue;

    if (q->kind != P_RING) {
      if (q->drag != 0) {
        double k = exp(-q->drag * dt);
        q->vx *= k; q->vy *= k;
      }
      if (q->buoy != 0) q->vy -= q->buoy * dt;
      if (q->grav != 0) q->vy += q->grav * dt;
      if (q->wob_amp != 0) {
        q->wob += q->wob_rate * dt;
        q->x += sin(q->wob) * q->wob_amp * dt;
      } else if (q->wob_rate != 0) {
        q->wob += q->wob_rate * dt;
      }
      if (q->spin != 0) q->rot += q->spin * dt;
      q->x += q->vx * dt;
      q->y += q->vy * dt;
      if (q->grow != 0) q->r += q->grow * dt;
    }

    if (w != i) p->a[w] = *q;
    w++;
  }
  p->n = w;
}

void particles_bbox(Particles *p, double *x0, double *y0, double *x1, double *y1) {
  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    double r = q->r;
    if (q->kind == P_FIRE)  r = q->r * 4.5;
    if (q->kind == P_EMBER) r = q->r * 5.0;
    if (q->kind == P_SMOKE) r = q->r * 1.4;
    if (q->kind == P_SPARK) r = q->len + 3;
    if (q->kind == P_FEATHER) r = q->len + q->r + 2;
    if (q->kind == P_WATER) r = 10;
    if (q->kind == P_RING) {
      double e = 1 - pow(1 - q->life / q->ttl, 3);
      r = av_lerp(q->r, q->r1, e) + q->w0 * 3;
    }
    if (q->x - r < *x0) *x0 = q->x - r;
    if (q->y - r < *y0) *y0 = q->y - r;
    if (q->x + r > *x1) *x1 = q->x + r;
    if (q->y + r > *y1) *y1 = q->y + r;
  }
}

/* --------------------------------------------------------------- draw -- */

static void radial(cairo_t *cr, double x, double y, double r,
                   Rgb c0, double a0, double mid, Rgb c1, double a1, Rgb c2) {
  cairo_pattern_t *g = cairo_pattern_create_radial(x, y, 0, x, y, r);
  cairo_pattern_add_color_stop_rgba(g, 0,   c0.r, c0.g, c0.b, a0);
  cairo_pattern_add_color_stop_rgba(g, mid, c1.r, c1.g, c1.b, a1);
  cairo_pattern_add_color_stop_rgba(g, 1,   c2.r, c2.g, c2.b, 0);
  cairo_set_source(cr, g);
  cairo_arc(cr, x, y, r, 0, TAU);
  cairo_fill(cr);
  cairo_pattern_destroy(g);
}

/* Pixel-art path: solid blocks in palette colours, no additive blending and no
 * soft alpha. A radial gradient the size of three pixels is just dither noise;
 * a solid 2x2 block of the right orange is a spark. */
static void particles_draw_pixel(Particles *p, cairo_t *cr) {
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

  /* Smoke first, behind everything. Drawn as solid blocks at partial alpha:
   * the ordered dither in the quantiser stipples them, which is how pixel art
   * has always drawn something you can see through. */
  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    if (q->kind != P_SMOKE) continue;
    double t = q->life / q->ttl;
    double a = q->alpha * sin(fmin(1.0, t * 1.35) * M_PI) * 2.4;
    if (a <= 0.02) continue;
    if (a > 0.62) a = 0.62;
    /* round, not square: a filled rectangle at uniform alpha reads as a grey
     * box floating in the air, however nicely it is dithered */
    double rr = fmax(1.0, q->r * 0.5);
    if (t < 0.45)      cairo_set_source_rgba(cr, 0.376, 0.329, 0.298, a);
    else if (t < 0.80) cairo_set_source_rgba(cr, 0.243, 0.196, 0.173, a);
    else               cairo_set_source_rgba(cr, 0.149, 0.114, 0.098, a);
    cairo_arc(cr, floor(q->x) + 0.5, floor(q->y) + 0.5, rr, 0, TAU);
    cairo_fill(cr);
  }

  /* ash next, so live fire always sits on top of dead matter */
  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    if (q->kind != P_ASH) continue;
    double t = q->life / q->ttl;
    if (t > 0.9) continue;
    int s = q->r > 1.6 ? 2 : 1;
    if (q->glow > 0 && sin(q->life * 9 + q->wob) > 0.2)
      av_set_rgba(cr, av_fire_color(0.62), 1);
    else
      cairo_set_source_rgb(cr, 0.243, 0.196, 0.173);
    cairo_rectangle(cr, floor(q->x), floor(q->y), s, s);
    cairo_fill(cr);
  }

  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    double t = q->life / q->ttl;

    if (q->kind == P_WATER) {
      double sp = hypot(q->vx, q->vy);
      double len = av_clamp(sp * 0.016, 0.0, 7.0);
      double a = q->heat0 > 0.5 ? (1 - t) : 0.85;
      cairo_set_source_rgba(cr, 0.596, 0.678, 0.760, a);
      if (len < 1.2) {
        cairo_rectangle(cr, floor(q->x), floor(q->y), 1, 1);
        cairo_fill(cr);
      } else {
        double ux = q->vx / (sp > 1 ? sp : 1), uy = q->vy / (sp > 1 ? sp : 1);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, floor(q->x) + 0.5, floor(q->y) + 0.5);
        cairo_line_to(cr, floor(q->x - ux * len) + 0.5, floor(q->y - uy * len) + 0.5);
        cairo_stroke(cr);
      }
      continue;
    }
    if (q->kind == P_FEATHER) {
      if (t > 0.9) continue;
      double a = t < 0.1 ? t / 0.1 : (t > 0.7 ? (1 - t) / 0.3 : 1);
      double sh = q->heat0;
      cairo_set_source_rgba(cr,
                            av_lerp(0.886, 0.549, sh),
                            av_lerp(0.847, 0.502, sh),
                            av_lerp(0.769, 0.439, sh), a);
      double ang = q->rot + sin(q->wob) * 0.5;
      cairo_set_line_width(cr, fmax(1.0, q->r));
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_move_to(cr, floor(q->x) + 0.5, floor(q->y) + 0.5);
      cairo_line_to(cr, floor(q->x + cos(ang) * q->len) + 0.5,
                        floor(q->y + sin(ang) * q->len) + 0.5);
      cairo_stroke(cr);
      continue;
    }
    if (q->kind == P_FIRE) {
      if (t > 0.94) continue;
      double heat = av_clamp(q->heat0 + t * 0.95, 0, 1);
      int s = (int)fmax(1, floor(q->r * 1.5 * (1 - t * 0.45)));
      av_set_rgba(cr, av_fire_color(heat), 1);
      cairo_rectangle(cr, floor(q->x - s / 2.0), floor(q->y - s / 2.0), s, s);
      cairo_fill(cr);
    } else if (q->kind == P_EMBER) {
      /* flicker by dropping frames rather than by fading alpha */
      if (t > 0.92 || sin(q->life * (9 + q->wob_rate * 2) + q->wob) < -0.55) continue;
      double heat = av_clamp(q->heat0 + t * 1.05, 0, 1);
      int s = q->r > 1.4 ? 2 : 1;
      av_set_rgba(cr, av_fire_color(heat), 1);
      cairo_rectangle(cr, floor(q->x), floor(q->y), s, s);
      cairo_fill(cr);
    } else if (q->kind == P_SPARK) {
      if (t > 0.9) continue;
      double sp = hypot(q->vx, q->vy);
      double ux = sp > 1 ? q->vx / sp : 1;
      double uy = sp > 1 ? q->vy / sp : 0;
      double len = fmax(1.0, q->len * 0.6);
      av_set_rgba(cr, av_fire_color(0.10 + t * 0.45), 1);
      cairo_set_line_width(cr, 1);
      cairo_move_to(cr, floor(q->x) + 0.5, floor(q->y) + 0.5);
      cairo_line_to(cr, floor(q->x - ux * len) + 0.5, floor(q->y - uy * len) + 0.5);
      cairo_stroke(cr);
    } else if (q->kind == P_RING) {
      double e = 1 - pow(1 - t, 3);
      double r = av_lerp(q->r, q->r1, e);
      if (t > 0.85) continue;
      av_set_rgba(cr, av_fire_color(0.30 + t * 0.45), 1);
      cairo_set_line_width(cr, 1);
      cairo_arc(cr, q->x, q->y, r, 0, TAU);
      cairo_stroke(cr);
    }
  }
  cairo_restore(cr);
}

void particles_draw(Particles *p, cairo_t *cr) {
  if (!p->n) return;
  if (av_pixel_mode()) { particles_draw_pixel(p, cr); return; }

  /* pass 1, normal blend: smoke sits behind everything, then ash flakes */
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    if (q->kind != P_SMOKE) continue;
    double t = q->life / q->ttl;
    double a = q->alpha * sin(fmin(1.0, t * 1.4) * M_PI);
    if (a <= 0.002) continue;
    cairo_pattern_t *g = cairo_pattern_create_radial(q->x, q->y, 0, q->x, q->y, q->r);
    cairo_pattern_add_color_stop_rgba(g, 0,    0.188, 0.157, 0.141, a);
    cairo_pattern_add_color_stop_rgba(g, 0.55, 0.133, 0.110, 0.102, a * 0.6);
    cairo_pattern_add_color_stop_rgba(g, 1,    0.094, 0.078, 0.075, 0);
    cairo_set_source(cr, g);
    cairo_arc(cr, q->x, q->y, q->r, 0, TAU);
    cairo_fill(cr);
    cairo_pattern_destroy(g);
  }

  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    if (q->kind != P_WATER) continue;
    double t = q->life / q->ttl;
    double sp = hypot(q->vx, q->vy);
    double len = av_clamp(sp * 0.016, 0.0, 8.0);
    double a = (q->heat0 > 0.5 ? (1 - t) : 0.7) * 0.9;
    double ux = q->vx / (sp > 1 ? sp : 1), uy = q->vy / (sp > 1 ? sp : 1);
    cairo_set_source_rgba(cr, 0.596, 0.678, 0.760, a);
    cairo_set_line_width(cr, q->r);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, q->x, q->y);
    cairo_line_to(cr, q->x - ux * fmax(len, 0.6), q->y - uy * fmax(len, 0.6));
    cairo_stroke(cr);
  }

  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    if (q->kind != P_FEATHER) continue;
    double t = q->life / q->ttl;
    double a = t < 0.1 ? t / 0.1 : (t > 0.7 ? (1 - t) / 0.3 : 1);
    double sh = q->heat0;
    cairo_save(cr);
    cairo_translate(cr, q->x, q->y);
    cairo_rotate(cr, q->rot + sin(q->wob) * 0.5);
    cairo_set_source_rgba(cr, av_lerp(0.886, 0.549, sh),
                          av_lerp(0.847, 0.502, sh),
                          av_lerp(0.769, 0.439, sh), a);
    cairo_move_to(cr, -q->len * 0.5, 0);
    av_quad_to(cr, 0, -q->r, q->len * 0.5, 0);
    av_quad_to(cr, 0, q->r, -q->len * 0.5, 0);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
  }

  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    if (q->kind != P_ASH) continue;
    double t = q->life / q->ttl;
    double a = (1 - t) * (t < 0.08 ? t / 0.08 : 1);

    cairo_save(cr);
    cairo_translate(cr, q->x, q->y);
    cairo_rotate(cr, q->rot);
    cairo_scale(cr, 1, 0.52);
    cairo_set_source_rgba(cr, 0.200, 0.184, 0.173, a * 0.85);
    cairo_arc(cr, 0, 0, q->r, 0, TAU);
    cairo_fill(cr);
    cairo_restore(cr);

    /* a dying coal still trapped in the flake */
    if (q->glow > 0) {
      double heat = av_clamp(q->glow * (1 - t) * (0.6 + 0.4 * sin(q->life * 9 + q->wob)), 0, 1);
      if (heat > 0.02) {
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
        av_set_rgba(cr, av_fire_color(0.55), heat);
        cairo_arc(cr, q->x, q->y, q->r * 0.45, 0, TAU);
        cairo_fill(cr);
        cairo_restore(cr);
      }
    }
  }
  cairo_restore(cr);

  /* pass 2, additive: fire, embers, sparks, shockwaves */
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_ADD);

  for (int i = 0; i < p->n; i++) {
    Particle *q = &p->a[i];
    double t = q->life / q->ttl;

    if (q->kind == P_FIRE) {
      double heat = av_clamp(q->heat0 + t * 0.95, 0, 1);
      double a = (1 - t) * (1 - t) * 0.9;
      double r = q->r * (1 + t * 0.9) * (1 - t * 0.35);
      double wob = sin(q->wob + q->life * q->wob_rate) * r * 0.25;
      radial(cr, q->x + wob, q->y, r * 2.1,
             av_fire_color(heat * 0.4), a, 0.4,
             av_fire_color(heat), a * 0.55,
             av_fire_color(1));
    } else if (q->kind == P_EMBER) {
      double heat = av_clamp(q->heat0 + t * 1.05, 0, 1);
      double flick = 0.65 + 0.35 * sin(q->life * (9 + q->wob_rate * 2) + q->wob);
      double a = (1 - t) * flick;
      double r = q->r * (1 - t * 0.4);
      radial(cr, q->x, q->y, r * 4,
             av_fire_color(heat * 0.35), a, 0.3,
             av_fire_color(heat), a * 0.5,
             av_fire_color(0.95));
    } else if (q->kind == P_SPARK) {
      double a = 1 - t;
      double sp = hypot(q->vx, q->vy);
      double ux = sp > 1 ? q->vx / sp : 1;
      double uy = sp > 1 ? q->vy / sp : 0;
      double len = q->len * av_clamp(sp / 260, 0.2, 1.6);
      av_set_rgba(cr, av_fire_color(0.12 + t * 0.5), a);
      cairo_set_line_width(cr, q->r * 1.4);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_move_to(cr, q->x, q->y);
      cairo_line_to(cr, q->x - ux * len, q->y - uy * len);
      cairo_stroke(cr);
    } else if (q->kind == P_RING) {
      /* three feathered passes: one hard stroke reads as a geometry artefact,
       * not as heat moving through air */
      double e = 1 - pow(1 - t, 3);
      double r = av_lerp(q->r, q->r1, e);
      double a = pow(1 - t, 2.4) * 0.34;
      double w = fmax(0.5, q->w0 * (1 - e));
      for (int k = -1; k <= 1; k++) {
        av_set_rgba(cr, av_fire_color(q->hue + t * 0.5), a * (k == 0 ? 1 : 0.4));
        cairo_set_line_width(cr, w * (k == 0 ? 1 : 1.9));
        cairo_arc(cr, q->x, q->y, fmax(0.5, r + k * w * 1.7), 0, TAU);
        cairo_stroke(cr);
      }
    }
  }
  cairo_restore(cr);
}
