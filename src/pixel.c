/* Pixel-art pipeline.
 *
 * The whole scene is drawn into a small buffer with antialiasing off, snapped
 * to a fixed palette, then blown up with nearest-neighbour sampling. Nothing
 * in the bird or the scene knows about any of this — they just run at a lower
 * resolution, which is exactly what a sprite is.
 */
#include "aviary.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* A deliberately small, curated palette. Fire ramp, char and ash, parchment,
 * ink, wax. Snapping to it is what stops the result looking like a blurry
 * downscale and makes it look drawn. */
static const uint8_t PALETTE[][3] = {
  /* Fire. Weighted toward the bright middle on purpose: a 28px bird whose
   * ramp is mostly deep reds just reads as a dark smudge. */
  { 255, 250, 235 }, { 255, 232, 170 }, { 255, 205, 105 }, { 252, 170,  60 },
  { 245, 130,  35 }, { 230,  95,  25 }, { 205,  60,  20 }, { 160,  32,  24 },
  {  96,  18,  22 }, {  48,  14,  18 },
  /* char and ash */
  {  38,  26,  22 }, {  62,  50,  44 }, {  96,  84,  76 }, { 140, 128, 118 },
  /* parchment */
  { 246, 234, 206 }, { 232, 214, 176 }, { 210, 188, 142 }, { 178, 152, 106 },
  { 138, 110,  70 }, {  96,  70,  40 },
  /* ink and wax */
  {  58,  42,  24 }, {  92,  62,  30 }, { 176,  40,  44 }, { 120,  20,  30 },
  /* pigeon: slate blues, the wing bars, and the pale rump */
  { 206, 212, 222 }, { 168, 178, 194 }, { 124, 136, 156 },
  {  92, 104, 126 }, {  64,  74,  94 }, {  42,  50,  66 },
  /* the neck, which is the only part of a pigeon that is not grey */
  {  74, 158, 126 }, { 128,  96, 162 },
  /* bill, cere, feet, eye */
  {  46,  44,  52 }, { 228, 230, 236 }, { 214, 118, 122 }, { 166,  78,  86 },
  { 236, 146,  38 },
  /* owl: warm greys and browns, cream facial disc, amber eye */
  { 226, 216, 196 }, { 200, 190, 170 }, { 164, 152, 132 },
  { 140, 128, 112 }, {  98,  88,  74 }, {  62,  54,  46 },
  { 232, 168,  60 }, {  72,  68,  76 },
  /* swallow: glossy blue-black back, rust throat, cream underparts */
  {  32,  38,  62 }, {  52,  62,  98 }, {  84,  98, 142 },
  { 168,  72,  48 }, { 206, 104,  66 }, { 236, 226, 204 },
  /* rain */
  { 186, 204, 222 }, { 132, 156, 184 }, {  84, 104, 132 },
};
#define NPAL ((int)(sizeof(PALETTE) / sizeof(PALETTE[0])))

/* 4x4 ordered dither. Used on alpha only: soft glows become the stippled
 * half-tone that pixel-art fire is actually made of. */
static const int BAYER[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5
};

static int nearest_palette_slow(int r, int g, int b) {
  int best = 0;
  long bestd = 1L << 40;
  for (int i = 0; i < NPAL; i++) {
    long dr = r - PALETTE[i][0];
    long dg = g - PALETTE[i][1];
    long db = b - PALETTE[i][2];
    /* luma-weighted: the eye forgives blue error far more than green */
    long d = 3 * dr * dr + 6 * dg * dg + 1 * db * db;
    if (d < bestd) { bestd = d; best = i; }
  }
  return best;
}

/* Searching the whole palette per pixel is fine for a bird and hopeless for a
 * screenful of rain: a full-screen dirty rect is half a million pixels a
 * frame. Precompute the answer for every 5-bit colour once instead. */
#define LUT_BITS 5
#define LUT_SIZE (1 << (LUT_BITS * 3))
static uint8_t pal_lut[LUT_SIZE];
static int     lut_ready = 0;

static void build_lut(void) {
  const int N = 1 << LUT_BITS;
  for (int r = 0; r < N; r++)
    for (int g = 0; g < N; g++)
      for (int b = 0; b < N; b++) {
        int rr = r * 255 / (N - 1), gg = g * 255 / (N - 1), bb = b * 255 / (N - 1);
        pal_lut[(r << (LUT_BITS * 2)) | (g << LUT_BITS) | b] =
          (uint8_t)nearest_palette_slow(rr, gg, bb);
      }
  lut_ready = 1;
}

static inline int nearest_palette(int r, int g, int b) {
  const int SH = 8 - LUT_BITS;
  return pal_lut[((r >> SH) << (LUT_BITS * 2)) | ((g >> SH) << LUT_BITS) | (b >> SH)];
}

void pixel_init(Pixelizer *p, int screen_w, int screen_h, int pixel_size) {
  if (!lut_ready) build_lut();
  memset(p, 0, sizeof(*p));
  p->size = pixel_size < 1 ? 1 : pixel_size;
  p->bw = (screen_w + p->size - 1) / p->size;
  p->bh = (screen_h + p->size - 1) / p->size;
  p->buf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, p->bw, p->bh);
  p->cr = cairo_create(p->buf);

  /* the whole point: no smoothing anywhere */
  cairo_set_antialias(p->cr, CAIRO_ANTIALIAS_NONE);
  cairo_font_options_t *fo = cairo_font_options_create();
  cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
  cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
  cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_ON);
  cairo_set_font_options(p->cr, fo);
  cairo_font_options_destroy(fo);
}

void pixel_free(Pixelizer *p) {
  if (p->cr) cairo_destroy(p->cr);
  if (p->buf) cairo_surface_destroy(p->buf);
  memset(p, 0, sizeof(*p));
}

void pixel_clear(Pixelizer *p, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  cairo_save(p->cr);
  cairo_set_operator(p->cr, CAIRO_OPERATOR_CLEAR);
  cairo_rectangle(p->cr, x, y, w, h);
  cairo_fill(p->cr);
  cairo_restore(p->cr);
}

/* Snap the buffer to the palette and harden the alpha. Cairo stores ARGB32
 * premultiplied, so it has to be undone and redone around the quantisation. */
void pixel_quantize(Pixelizer *p, int x0, int y0, int w, int h) {
  cairo_surface_flush(p->buf);
  uint8_t *data = cairo_image_surface_get_data(p->buf);
  int stride = cairo_image_surface_get_stride(p->buf);
  if (!data) return;

  if (x0 < 0) { w += x0; x0 = 0; }
  if (y0 < 0) { h += y0; y0 = 0; }
  if (x0 + w > p->bw) w = p->bw - x0;
  if (y0 + h > p->bh) h = p->bh - y0;
  if (w <= 0 || h <= 0) return;

  for (int y = y0; y < y0 + h; y++) {
    uint32_t *row = (uint32_t *)(data + (size_t)y * stride);
    for (int x = x0; x < x0 + w; x++) {
      uint32_t px = row[x];
      int a = (int)(px >> 24);
      if (a == 0) { row[x] = 0; continue; }

      int r = (int)((px >> 16) & 0xff);
      int g = (int)((px >>  8) & 0xff);
      int b = (int)( px        & 0xff);
      if (a < 255) {                      /* un-premultiply */
        r = r * 255 / a; g = g * 255 / a; b = b * 255 / a;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
      }

      int thr = BAYER[(y & 3) * 4 + (x & 3)] * 16 + 8;
      if (a <= thr) { row[x] = 0; continue; }   /* dithered out */

      int idx = nearest_palette(r, g, b);
      row[x] = 0xff000000u
             | ((uint32_t)PALETTE[idx][0] << 16)
             | ((uint32_t)PALETTE[idx][1] <<  8)
             |  (uint32_t)PALETTE[idx][2];
    }
  }
  cairo_surface_mark_dirty_rectangle(p->buf, x0, y0, w, h);
}

/* Blow the buffer up onto a destination context, one buffer pixel to a
 * size x size block, with no interpolation. */
void pixel_blit_op(Pixelizer *p, cairo_t *dst, int bx, int by, int bw, int bh,
                   cairo_operator_t op) {
  if (bw <= 0 || bh <= 0) return;
  cairo_save(dst);
  cairo_set_operator(dst, op);
  cairo_scale(dst, p->size, p->size);
  cairo_set_source_surface(dst, p->buf, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(dst), CAIRO_FILTER_NEAREST);
  cairo_rectangle(dst, bx, by, bw, bh);
  cairo_fill(dst);
  cairo_restore(dst);
}

/* The overlay wants SOURCE so transparent sprite pixels really are holes. */
void pixel_blit(Pixelizer *p, cairo_t *dst, int bx, int by, int bw, int bh) {
  pixel_blit_op(p, dst, bx, by, bw, bh, CAIRO_OPERATOR_SOURCE);
}
