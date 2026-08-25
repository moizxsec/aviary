#include "aviary.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Reference metrics, authored against a 1280-unit-wide scene. letter_plan
 * derives l->ui from the actual scene width, so the same panel works whether
 * it is drawn at full resolution or into a small pixel-art buffer. */
#define REF_W     1280.0
#define PAD_L     (48.0  * l->ui)
#define PAD_R     (96.0  * l->ui)
#define PAD_T     (44.0  * l->ui)
#define PAD_B     (34.0  * l->ui)
#define FONT_TEXT (19.0  * l->ui)
#define LINE_H    (31.0  * l->ui)
#define FONT_FROM (16.0  * l->ui)
#define FONT_BTN  (12.5  * l->ui)
#define MAX_LINES 48

/* ---- word wrap -------------------------------------------------------- */

static double text_w(cairo_t *cr, const char *s, int len) {
  char buf[512];
  if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
  memcpy(buf, s, (size_t)len);
  buf[len] = 0;
  cairo_text_extents_t e;
  cairo_text_extents(cr, buf, &e);
  return e.x_advance;
}

/* Wraps on spaces, and honours the newlines the sender typed. */
static int wrap_text(cairo_t *cr, const char *text, double maxw,
                     char lines[][512], int max_lines) {
  int n = 0;
  const char *p = text;

  while (*p && n < max_lines) {
    const char *eol = strchr(p, '\n');
    const char *end = eol ? eol : p + strlen(p);

    const char *start = p;
    while (start < end && n < max_lines) {
      const char *last_fit = NULL;
      const char *q = start;
      for (;;) {
        while (q < end && *q == ' ') q++;
        while (q < end && *q != ' ') q++;
        if (text_w(cr, start, (int)(q - start)) <= maxw) {
          last_fit = q;
          if (q >= end) break;
        } else {
          break;
        }
      }
      const char *cut = last_fit ? last_fit : (start + 1 < end ? start + 1 : end);
      if (!last_fit) {                 /* one very long word: hard-break it */
        cut = start;
        while (cut < end && text_w(cr, start, (int)(cut - start + 1)) <= maxw) cut++;
        if (cut == start) cut = start + 1;
      }
      int len = (int)(cut - start);
      if (len > 500) len = 500;
      memcpy(lines[n], start, (size_t)len);
      lines[n][len] = 0;
      /* trim trailing space */
      while (len > 0 && lines[n][len - 1] == ' ') lines[n][--len] = 0;
      n++;
      start = cut;
      while (start < end && *start == ' ') start++;
    }
    if (!eol) break;
    p = eol + 1;
    if (*p == 0 && n < max_lines) { lines[n][0] = 0; n++; }   /* trailing blank */
  }
  return n;
}

static void set_letter_font(cairo_t *cr, double size, int italic) {
  cairo_select_font_face(cr, "serif",
                         italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
}

/* ---- geometry --------------------------------------------------------- */

void letter_plan(Letter *l, int sw, int sh) {
  /* At sprite resolution the panel has to come down further, or a letter that
   * is merely readable ends up twenty times the size of the bird. */
  double k = av_pixel_mode() ? 0.62 : 1.0;
  l->ui = av_clamp(sw / REF_W * k, 0.28, 1.0);
  l->w = fmin(520.0 * l->ui, sw * 0.62);
  l->h = fmin(360.0 * l->ui, sh * 0.50);
  l->x = (sw - l->w) / 2.0;
  l->y = sh * 0.54 - l->h / 2.0;
}

/* An irregular burnt edge. Straight sawtooth reads as pinking shears, so the
 * depth is built from several frequencies plus the occasional deep bite where
 * the fire ate further in. Regenerated per letter, so no two are alike. */
static void make_charred_edge(Letter *l) {
  /* A clean sheet has a shallow deckled edge; a burnt or battered one is
   * eaten much further in. */
  double bite = l->style == LS_BRIGHT ? 0.34
              : l->style == LS_WET    ? 0.70
              : l->style == LS_DIRTY  ? 1.15 : 1.0;
  double sa = av_rand_range(0, 100);
  double sb = av_rand_range(0, 100);
  double sc = av_rand_range(0, 100);
  int n = 0;

  #define DEPTH(t, edge) ({                                            \
      double _p = (t) * CHAR_STEPS;                                    \
      double _d = 1.5                                                  \
        + sin(_p * 0.31 + sa + (edge) * 2.3) * 1.5                     \
        + sin(_p * 0.87 + sb + (edge) * 1.1) * 0.9                     \
        + sin(_p * 2.10 + sc + (edge) * 3.7) * 0.5                     \
        + av_rand_sym(0.5);                                            \
      if (av_rand() < 0.06) _d += av_rand_range(1.8, 4.2);             \
      av_clamp(_d * bite, 0.2, 8.5) / 100.0; })

  for (int i = 0; i <= CHAR_STEPS; i++) {
    double t = (double)i / CHAR_STEPS;
    l->edge[n].x = t;
    l->edge[n].y = DEPTH(t, 0);
    n++;
  }
  for (int i = 1; i <= CHAR_STEPS; i++) {
    double t = (double)i / CHAR_STEPS;
    l->edge[n].x = 1.0 - DEPTH(t, 1) * 0.62;
    l->edge[n].y = t;
    n++;
  }
  for (int i = 1; i <= CHAR_STEPS; i++) {
    double t = (double)i / CHAR_STEPS;
    l->edge[n].x = 1.0 - t;
    l->edge[n].y = 1.0 - DEPTH(t, 2);
    n++;
  }
  for (int i = 1; i < CHAR_STEPS; i++) {
    double t = (double)i / CHAR_STEPS;
    l->edge[n].x = DEPTH(t, 3) * 0.62;
    l->edge[n].y = 1.0 - t;
    n++;
  }
  #undef DEPTH
  l->nedge = n;
}

void letter_show(Letter *l, const char *text, const char *from) {
  double x = l->x, y = l->y, w = l->w, h = l->h, ui = l->ui;
  int style = l->style;
  double auto_t = l->auto_t;
  memset(l, 0, sizeof(*l));
  l->x = x; l->y = y; l->w = w; l->h = h;
  l->ui = ui > 0 ? ui : 1.0;
  l->style = style;
  l->auto_t = auto_t;

  snprintf(l->text, sizeof(l->text), "%s", text ? text : "");
  snprintf(l->from, sizeof(l->from), "%s", from ? from : "");
  make_charred_edge(l);

  /* size the panel to the words rather than padding out a fixed box */
  cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t *cr = cairo_create(tmp);
  set_letter_font(cr, FONT_TEXT, 0);
  char lines[MAX_LINES][512];
  int nl = wrap_text(cr, l->text, l->w - PAD_L - PAD_R, lines, MAX_LINES);
  cairo_destroy(cr);
  cairo_surface_destroy(tmp);

  double need = PAD_T + nl * LINE_H + PAD_B + 34 * l->ui;
  if (l->from[0]) need += 30 * l->ui;
  l->h = fmax(190.0 * l->ui, fmin(need, 620.0 * l->ui));

  l->open = 1;
  l->open_t = 0;
  l->gone = 0;
  l->gone_t = 0;
  l->age = 0;
}

void letter_dismiss(Letter *l) {
  if (!l->open || l->gone || l->burning) return;
  /* Paper the phoenix touched does not fade politely; letting it go sets it
   * off. Everything else simply lifts away. */
  if (l->style == LS_BURNT) { l->burning = 1; l->burn = 0; }
  else { l->gone = 1; l->gone_t = 0; }
}

void letter_scorch(Letter *l) {
  if (!l->open) return;
  l->scorch = 1;
  l->ember = 1;
}

int letter_hit(Letter *l, int x, int y) {
  if (!l->open || l->gone || l->burning || l->open_t < 0.6) return 0;
  if (l->btn_w <= 0) return 0;
  double pad = 4 * l->ui;
  return x >= l->btn_x - pad && x <= l->btn_x + l->btn_w + pad &&
         y >= l->btn_y - pad && y <= l->btn_y + l->btn_h + pad;
}

double letter_burn_y(Letter *l) {
  if (!l->burning) return 0;
  return l->y + l->h * (1.0 - l->burn) - l->h * 0.06;
}

void letter_update(Letter *l, double dt) {
  if (!l->open) return;
  l->age += dt;
  if (l->open_t < 1) l->open_t = fmin(1.0, l->open_t + dt / 0.78);
  if (l->ember > 0) l->ember = fmax(0.0, l->ember - dt / 2.6);

  if (l->burning) {
    /* paper does not fade, it is eaten from the bottom up */
    l->burn += dt / 1.7;
    if (l->burn >= 1.06) { l->open = 0; l->burning = 0; }
    return;
  }

  if (l->gone) {
    l->gone_t += dt / 0.7;
    if (l->gone_t >= 1) { l->open = 0; l->gone = 0; }
    return;
  }

  if (l->age > 180) letter_dismiss(l);   /* backstop, not a feature */
}

/* ---- paper fibre ------------------------------------------------------ */

static cairo_surface_t *paper_cache = NULL;
static int paper_w = 0, paper_h = 0;

static cairo_surface_t *paper_texture(double w, double h) {
  int iw = (int)w, ih = (int)h;
  if (paper_cache && paper_w == iw && paper_h == ih) return paper_cache;
  if (paper_cache) cairo_surface_destroy(paper_cache);
  paper_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
  paper_w = iw; paper_h = ih;

  cairo_t *cr = cairo_create(paper_cache);
  cairo_set_line_width(cr, 0.7);
  for (int i = 0; i < 900; i++) {
    double x = av_rand() * iw, y = av_rand() * ih;
    double a = av_rand_range(0, M_PI);
    double len = av_rand_range(2, 11);
    cairo_set_source_rgba(cr, 0.42, 0.31, 0.17, av_rand_range(0.015, 0.055));
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + cos(a) * len, y + sin(a) * len);
    cairo_stroke(cr);
  }
  cairo_destroy(cr);
  return paper_cache;
}

/* ---- draw ------------------------------------------------------------- */

/* CSS radial-gradients are ellipses fitted to the box; cairo only makes
 * circles. Squash a unit circle onto the panel instead — without this the
 * paper lights up as a big oval with dark corners. */
static cairo_pattern_t *ellipse_gradient(double cx, double cy, double rx, double ry) {
  cairo_pattern_t *g = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
  cairo_matrix_t m;
  cairo_matrix_init_identity(&m);
  cairo_matrix_scale(&m, 1.0 / rx, 1.0 / ry);
  cairo_matrix_translate(&m, -cx, -cy);
  cairo_pattern_set_matrix(g, &m);
  return g;
}

static void draw_seal(Letter *l, cairo_t *cr, Rgb hi, Rgb lo);

static void edge_path(Letter *l, cairo_t *cr) {
  cairo_new_path(cr);
  for (int i = 0; i < l->nedge; i++) {
    double px = l->edge[i].x * l->w;
    double py = l->edge[i].y * l->h;
    if (i == 0) cairo_move_to(cr, px, py);
    else cairo_line_to(cr, px, py);
  }
  cairo_close_path(cr);
}

static void draw_seal(Letter *l, cairo_t *cr, Rgb hi, Rgb lo) {
  double cx = l->w - 51 * l->ui, cy = 43 * l->ui, r = 21 * l->ui;
  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_rotate(cr, -0.12);

  /* wax never pours out round */
  cairo_new_path(cr);
  for (int i = 0; i <= 40; i++) {
    double a = i / 40.0 * TAU;
    double rr = r * (1 + 0.06 * sin(a * 3 + 1.1) + 0.04 * sin(a * 5 - 0.4));
    double px = cos(a) * rr, py = sin(a) * rr;
    if (i == 0) cairo_move_to(cr, px, py); else cairo_line_to(cr, px, py);
  }
  cairo_close_path(cr);

  Rgb mid = av_mix(hi, lo, 0.55);
  cairo_pattern_t *g = cairo_pattern_create_radial(-r * 0.28, -r * 0.4, 1, 0, 0, r * 1.25);
  cairo_pattern_add_color_stop_rgb(g, 0.00, hi.r, hi.g, hi.b);
  cairo_pattern_add_color_stop_rgb(g, 0.58, mid.r, mid.g, mid.b);
  cairo_pattern_add_color_stop_rgb(g, 1.00, lo.r, lo.g, lo.b);
  cairo_set_source(cr, g);
  cairo_fill_preserve(cr);
  cairo_pattern_destroy(g);
  cairo_set_source_rgba(cr, 0.2, 0.02, 0.03, 0.5);
  cairo_set_line_width(cr, 1.1);
  cairo_stroke(cr);

  /* stamped flame */
  cairo_set_source_rgba(cr, 1.0, 0.839, 0.698, 0.62);
  cairo_scale(cr, l->ui, l->ui);
  cairo_new_path(cr);
  cairo_move_to(cr, 0, -10.5);
  cairo_curve_to(cr,  4.2, -4.6,  -2.0, -2.4,  -1.0,  2.0);
  cairo_curve_to(cr, -0.4,  4.6,   1.6,  5.4,   1.6,  5.4);
  cairo_curve_to(cr, -3.4,  4.6,  -5.6,  1.4,  -5.6, -1.2);
  cairo_curve_to(cr, -5.6,  5.6,  -2.2,  9.8,   1.2,  9.8);
  cairo_curve_to(cr,  5.0,  9.8,   8.0,  6.4,   8.0,  2.2);
  cairo_curve_to(cr,  8.0, -3.4,   1.8, -5.2,   0.0, -10.5);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_restore(cr);
}

/* Every bird hands over a different object. The phoenix's is scorched, the
 * pigeon's is clean and bright, the swallow's has been out in the rain, and
 * the owl's has been through a wall. */
typedef struct { Rgb paper, ink, edge, seal_hi, seal_lo; } Skin;

static Skin skin_for(int style) {
  Skin k;
  switch (style) {
    case LS_BRIGHT:
      k.paper   = (Rgb){ 0.969, 0.945, 0.898 };
      k.ink     = (Rgb){ 0.267, 0.212, 0.141 };
      k.edge    = (Rgb){ 0.663, 0.612, 0.514 };
      k.seal_hi = (Rgb){ 0.259, 0.596, 0.404 };
      k.seal_lo = (Rgb){ 0.106, 0.325, 0.224 };
      break;
    case LS_WET:
      k.paper   = (Rgb){ 0.769, 0.784, 0.769 };
      k.ink     = (Rgb){ 0.157, 0.192, 0.251 };
      k.edge    = (Rgb){ 0.400, 0.447, 0.463 };
      k.seal_hi = (Rgb){ 0.663, 0.239, 0.239 };
      k.seal_lo = (Rgb){ 0.322, 0.106, 0.129 };
      break;
    case LS_DIRTY:
      k.paper   = (Rgb){ 0.800, 0.757, 0.655 };
      k.ink     = (Rgb){ 0.298, 0.263, 0.212 };
      k.edge    = (Rgb){ 0.333, 0.278, 0.196 };
      k.seal_hi = (Rgb){ 0.522, 0.376, 0.235 };
      k.seal_lo = (Rgb){ 0.278, 0.184, 0.106 };
      break;
    default:
      k.paper   = (Rgb){ 0.910, 0.863, 0.776 };
      k.ink     = (Rgb){ 0.227, 0.165, 0.094 };
      k.edge    = (Rgb){ 0.114, 0.051, 0.024 };
      k.seal_hi = (Rgb){ 0.812, 0.173, 0.169 };
      k.seal_lo = (Rgb){ 0.420, 0.043, 0.075 };
      break;
  }
  return k;
}

/* deterministic per-letter blotch placement, so it does not crawl per frame */
static double blot(int i, int k) {
  return fabs(sin(i * 12.9898 + k * 78.233) * 43758.5453
              - floor(sin(i * 12.9898 + k * 78.233) * 43758.5453));
}

static void draw_wet_marks(Letter *l, cairo_t *cr) {
  /* damp patches soaked through the paper */
  for (int i = 0; i < 6; i++) {
    double bx = blot(i, 1) * l->w;
    double by = blot(i, 2) * l->h;
    double br = (14 + blot(i, 3) * 26) * l->ui;
    cairo_save(cr);
    cairo_translate(cr, bx, by);
    cairo_scale(cr, 1.0 + blot(i, 4) * 0.7, 0.7 + blot(i, 5) * 0.5);
    cairo_set_source_rgba(cr, 0.400, 0.443, 0.451, 0.20);
    cairo_arc(cr, 0, 0, br, 0, TAU);
    cairo_fill(cr);
    cairo_restore(cr);
  }
  /* and beads still sitting on the surface */
  for (int i = 0; i < 5; i++) {
    double bx = blot(i, 7) * l->w;
    double by = blot(i, 8) * l->h;
    double br = (2.2 + blot(i, 9) * 2.4) * l->ui;
    cairo_set_source_rgba(cr, 0.769, 0.827, 0.882, 0.45);
    cairo_arc(cr, bx, by, br, 0, TAU);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.298, 0.353, 0.400, 0.35);
    cairo_arc(cr, bx, by + br * 0.35, br * 0.75, 0.15, M_PI - 0.15);
    cairo_fill(cr);
  }
}

static void draw_dirt_marks(Letter *l, cairo_t *cr) {
  /* creases, from being folded and then landed on */
  cairo_set_line_width(cr, 1.2 * l->ui);
  for (int i = 0; i < 3; i++) {
    double y = l->h * (0.22 + i * 0.27);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.22);
    cairo_move_to(cr, 0, y);
    av_quad_to(cr, l->w * 0.5, y + (i % 2 ? 4 : -4) * l->ui, l->w, y + 2 * l->ui);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, 0.298, 0.243, 0.169, 0.28);
    cairo_move_to(cr, 0, y + 1.4 * l->ui);
    av_quad_to(cr, l->w * 0.5, y + 1.4 * l->ui + (i % 2 ? 4 : -4) * l->ui,
               l->w, y + 3.4 * l->ui);
    cairo_stroke(cr);
  }
  /* and the marks of wherever he dragged it */
  for (int i = 0; i < 7; i++) {
    double bx = blot(i, 11) * l->w;
    double by = blot(i, 12) * l->h;
    double br = (6 + blot(i, 13) * 20) * l->ui;
    cairo_save(cr);
    cairo_translate(cr, bx, by);
    cairo_scale(cr, 1.0 + blot(i, 14) * 1.2, 0.5 + blot(i, 15) * 0.5);
    cairo_set_source_rgba(cr, 0.310, 0.259, 0.176, 0.20 + blot(i, 16) * 0.16);
    cairo_arc(cr, 0, 0, br, 0, TAU);
    cairo_fill(cr);
    cairo_restore(cr);
  }
}

/* the wavy line the fire has eaten up to, in panel coordinates */
static void burn_edge_path(Letter *l, cairo_t *cr, double front) {
  cairo_new_path(cr);
  cairo_move_to(cr, -6, -6);
  cairo_line_to(cr, l->w + 6, -6);
  for (double x = l->w + 6; x >= -6; x -= l->w / 22.0) {
    double w = sin(x * 0.09 + l->burn * 9.0) * 5.0 * l->ui
             + sin(x * 0.23 - l->burn * 14.0) * 2.4 * l->ui;
    cairo_line_to(cr, x, front + w);
  }
  cairo_close_path(cr);
}

void letter_draw(Letter *l, cairo_t *cr) {
  if (!l->open) return;

  Skin k = skin_for(l->style);

  double open = av_ease_out_cubic(av_clamp(l->open_t, 0, 1));
  double alpha = av_clamp(l->open_t * 3, 0, 1);
  if (l->gone) alpha *= 1 - av_clamp(l->gone_t, 0, 1);
  if (alpha <= 0.003) return;

  double sy = av_lerp(0.02, 1.0, open);
  double sx = av_lerp(0.55, 1.0, open);
  double lift = l->gone ? -14 * av_clamp(l->gone_t, 0, 1) : 0;

  cairo_save(cr);
  cairo_translate(cr, l->x + l->w / 2, l->y + lift);
  cairo_scale(cr, sx, sy);
  cairo_translate(cr, -l->w / 2, 0);

  cairo_push_group(cr);

  double front = l->h * (1.0 - l->burn) - l->h * 0.06;
  if (l->burning) {
    burn_edge_path(l, cr, front);
    cairo_clip(cr);
  }

  /* the paper */
  cairo_save(cr);
  edge_path(l, cr);
  cairo_clip(cr);

  int pix = av_pixel_mode();
  if (pix) {
    av_set_rgba(cr, k.paper, 1);
    cairo_paint(cr);
  } else {
    cairo_pattern_t *g = ellipse_gradient(l->w * 0.5, 0, l->w * 0.62, l->h * 0.95);
    Rgb hi = { fmin(1, k.paper.r * 1.06), fmin(1, k.paper.g * 1.06), fmin(1, k.paper.b * 1.06) };
    Rgb lo = { k.paper.r * 0.84, k.paper.g * 0.83, k.paper.b * 0.78 };
    cairo_pattern_add_color_stop_rgb(g, 0.00, hi.r, hi.g, hi.b);
    cairo_pattern_add_color_stop_rgb(g, 0.70, k.paper.r, k.paper.g, k.paper.b);
    cairo_pattern_add_color_stop_rgb(g, 1.00, lo.r, lo.g, lo.b);
    cairo_set_source(cr, g);
    cairo_paint(cr);
    cairo_pattern_destroy(g);
    cairo_set_source_surface(cr, paper_texture(l->w, l->h), 0, 0);
    cairo_paint_with_alpha(cr, 0.5);
  }

  if (l->style == LS_WET)   draw_wet_marks(l, cr);
  if (l->style == LS_DIRTY) draw_dirt_marks(l, cr);

  /* the band of damage that crowds the edge */
  double sc = l->scorch ? 1.0 : 0.0;
  double band = l->style == LS_BRIGHT ? 0.10 : (l->style == LS_BURNT ? 0.34 : 0.24);
  for (int i = 0; i < 3; i++) {
    edge_path(l, cr);
    cairo_set_line_width(cr, (11.0 - i * 3.4) * l->ui);
    av_set_rgba(cr, k.edge, (band + sc * 0.14) * (3 - i) / 3.0);
    cairo_stroke(cr);
  }
  cairo_restore(cr);

  /* the hard outline of the edge itself */
  for (int i = 0; i < 2; i++) {
    edge_path(l, cr);
    cairo_set_line_width(cr, (3.0 - i * 1.6) * l->ui);
    av_set_rgba(cr, k.edge, (l->style == LS_BRIGHT ? 0.18 : 0.42) * (2 - i));
    cairo_stroke(cr);
  }

  if (l->ember > 0.01) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    edge_path(l, cr);
    cairo_set_line_width(cr, 2.4 * l->ui);
    av_set_rgba(cr, av_fire_color(0.35), l->ember * 0.5);
    cairo_stroke(cr);
    cairo_restore(cr);
  }

  /* the words */
  double tin = av_clamp((l->open_t - 0.45) / 0.35, 0, 1);
  if (tin > 0.004) {
    cairo_save(cr);
    set_letter_font(cr, FONT_TEXT, 0);
    char lines[MAX_LINES][512];
    int nl = wrap_text(cr, l->text, l->w - PAD_L - PAD_R, lines, MAX_LINES);

    /* rain gets into the ink and it runs downward */
    if (l->style == LS_WET) {
      double y = PAD_T + FONT_TEXT;
      av_set_rgba(cr, k.ink, 0.30 * tin);
      for (int i = 0; i < nl; i++) {
        cairo_move_to(cr, PAD_L + 0.9 * l->ui, y + 1.6 * l->ui);
        cairo_show_text(cr, lines[i]);
        y += LINE_H;
      }
    }

    av_set_rgba(cr, k.ink, tin);
    double y = PAD_T + FONT_TEXT;
    for (int i = 0; i < nl; i++) {
      cairo_move_to(cr, PAD_L, y);
      cairo_show_text(cr, lines[i]);
      y += LINE_H;
    }

    if (l->from[0]) {
      set_letter_font(cr, FONT_FROM, 1);
      char buf[LETTER_MAX_FROM + 8];
      snprintf(buf, sizeof(buf), "\xE2\x80\x94 %s", l->from);
      cairo_text_extents_t e;
      cairo_text_extents(cr, buf, &e);
      cairo_set_source_rgba(cr, k.ink.r, k.ink.g, k.ink.b, 0.8 * tin);
      cairo_move_to(cr, l->w - PAD_L - e.x_advance, y + 14 * l->ui);
      cairo_show_text(cr, buf);
      y += 30 * l->ui;
    }

    double bw = 108 * l->ui, bh = 30 * l->ui;
    l->btn_x = l->x + PAD_L;
    l->btn_y = l->y + y + 12 * l->ui;
    l->btn_w = bw;
    l->btn_h = bh;
    av_round_rect(cr, PAD_L, y + 12 * l->ui, bw, bh, bh / 2);
    cairo_set_source_rgba(cr, k.ink.r, k.ink.g, k.ink.b, 0.34 * tin);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    set_letter_font(cr, FONT_BTN, 0);
    cairo_set_source_rgba(cr, k.ink.r, k.ink.g, k.ink.b, 0.85 * tin);
    cairo_move_to(cr, PAD_L + 22 * l->ui, y + 12 * l->ui + bh / 2 + 4 * l->ui);
    cairo_show_text(cr, "let it go");
    cairo_restore(cr);
  }

  draw_seal(l, cr, k.seal_hi, k.seal_lo);

  /* the fire itself, riding the line it has eaten to */
  if (l->burning) {
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
    for (int i = 0; i < 3; i++) {
      cairo_new_path(cr);
      for (double x = -6; x <= l->w + 6; x += l->w / 22.0) {
        double w = sin(x * 0.09 + l->burn * 9.0) * 5.0 * l->ui
                 + sin(x * 0.23 - l->burn * 14.0) * 2.4 * l->ui;
        if (x <= -6) cairo_move_to(cr, x, front + w);
        else cairo_line_to(cr, x, front + w);
      }
      cairo_set_line_width(cr, (7.0 - i * 2.2) * l->ui);
      av_set_rgba(cr, av_fire_color(0.06 + i * 0.22), 0.85 - i * 0.22);
      cairo_stroke(cr);
    }
    cairo_restore(cr);
  }

  cairo_pop_group_to_source(cr);
  cairo_paint_with_alpha(cr, alpha);
  cairo_restore(cr);
}
