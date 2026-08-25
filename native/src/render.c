/* Offline harnesses. Both write PNGs and need no X server, which is the only
 * sane way to iterate on a 40px bird. */
#include "aviary.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RW 1920
#define RH 1200

static void blit_scaled(Pixelizer *p, cairo_t *dst, double dx, double dy, int factor);

static void fill_bg(cairo_t *cr, int w, int h) {
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgb(cr, 0.055, 0.047, 0.043);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cairo_restore(cr);
}

static void write_frame(cairo_surface_t *surf, const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.png", dir, name);
  cairo_status_t st = cairo_surface_write_to_png(surf, path);
  if (st != CAIRO_STATUS_SUCCESS)
    fprintf(stderr, "  write %s: %s\n", path, cairo_status_to_string(st));
}

int render_main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "shots";
  int P = argc > 2 ? atoi(argv[2]) : 2;
  int species = scene_species_from_name(argc > 3 ? argv[3] : "phoenix");
  int depart = (argc > 4 && !strcmp(argv[4], "depart"));
  if (P < 1) P = 1;
  av_seed(20240825);
  av_set_pixel_mode(1);

  /* The scene runs at sprite resolution; the PNG is the upscaled result, so
   * what lands on disk is exactly what lands on the desktop. */
  Pixelizer px;
  pixel_init(&px, RW, RH, P);

  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, RW, RH);
  cairo_t *cr = cairo_create(surf);

  Scene s;
  memset(&s, 0, sizeof(s));
  scene_start(&s, px.bw, px.bh,
              species == BIRD_SWALLOW
                ? "it is coming down out there.\nhe came anyway."
              : species == BIRD_OWL
                ? "sorry about the wall.\nhe is very old."
              : species == BIRD_PIGEON
                ? "you asked for the pigeon.\nso here he is, finally."
                : "the pigeon never came.\nso I sent something that burns.",
              "me", species);
  if (depart) {
    scene_free(&s);
    memset(&s, 0, sizeof(s));
    scene_start_ex(&s, px.bw, px.bh, "taking this one with me", "me",
                   species, SM_DEPART, px.bw * 0.38, px.bh * 0.72);
  }

  typedef struct { const char *name; int state; double after; int done; } Beat;
  Beat phoenix_beats[] = {
    { "01-entry",    S_ENTER,  0.30, 0 },
    { "02-cruise",   S_ENTER,  0.90, 0 },
    { "03-approach", S_ENTER,  1.60, 0 },
    { "04-flare",    S_SETTLE, 0.45, 0 },
    { "05-release",  S_DROP,   0.18, 0 },
    { "06-letter",   S_WATCH,  0.80, 0 },
    { "07-charge",   S_BURN,   0.42, 0 },
    { "08-charge2",  S_BURN,   0.82, 0 },
    { "09-ignite",   S_BURN,   1.32, 0 },
    { "10-ignite2",  S_BURN,   1.72, 0 },
    { "11-flash",    S_BURN,   2.00, 0 },
    { "12-embers",   S_ASH,    0.05, 0 },
    { "13-ash",      S_ASH,    0.60, 0 },
    { "14-settle",   S_ASH,    1.60, 0 },
  };
  Beat pigeon_beats[] = {
    { "01-entry",    S_ENTER,   0.30, 0 },
    { "02-cruise",   S_ENTER,   1.10, 0 },
    { "03-approach", S_LAND,    0.30, 0 },
    { "04-flare",    S_LAND,    0.90, 0 },
    { "05-touchdown",S_WALK,    0.10, 0 },
    { "06-walking",  S_WALK,    0.55, 0 },
    { "07-arrived",  S_SETDOWN, 0.20, 0 },
    { "08-untying",  S_SETDOWN, 0.70, 0 },
    { "09-letter",   S_STAY,    0.60, 0 },
    { "10-waiting",  S_STAY,    1.60, 0 },
    { "11-idling",   S_STAY,    3.10, 0 },
    { "12-crouch",   S_TAKEOFF, 0.18, 0 },
    { "13-clap",     S_TAKEOFF, 0.42, 0 },
    { "14-away",     S_TAKEOFF, 1.00, 0 },
  };
  Beat owl_beats[] = {
    { "01-entry",    S_ENTER,   2.00, 0 },
    { "02-cruise",   S_ENTER,   3.00, 0 },
    { "03-charge",   S_CHARGE,  0.35, 0 },
    { "04-strike",   S_STRIKE,  0.04, 0 },
    { "05-strike2",  S_STRIKE,  0.16, 0 },
    { "06-tumble",   S_TUMBLE,  0.20, 0 },
    { "07-falling",  S_TUMBLE,  0.42, 0 },
    { "08-sprawl",   S_SPRAWL,  0.25, 0 },
    { "09-righting", S_SHAKE,   0.20, 0 },
    { "10-shaking",  S_SHAKE,   0.75, 0 },
    { "11-untie",    S_SETDOWN, 0.55, 0 },
    { "12-letter",   S_STAY,    0.60, 0 },
    { "13-crouch",   S_TAKEOFF, 0.22, 0 },
    { "14-away",     S_TAKEOFF, 1.10, 0 },
  };
  Beat swallow_beats[] = {
    { "01-entry",    S_ENTER,   0.90, 0 },
    { "02-through",  S_ENTER,   1.90, 0 },
    { "03-approach", S_LAND,    0.30, 0 },
    { "04-flare",    S_LAND,    0.85, 0 },
    { "05-soaked",   S_SHAKE,   0.12, 0 },
    { "06-shaking",  S_SHAKE,   0.55, 0 },
    { "07-shaking2", S_SHAKE,   0.95, 0 },
    { "08-drier",    S_SHAKE,   1.60, 0 },
    { "09-untie",    S_SETDOWN, 0.55, 0 },
    { "10-letter",   S_STAY,    0.60, 0 },
    { "11-reading",  S_STAY,    2.40, 0 },
    { "12-easing",   S_STAY,    3.60, 0 },
    { "13-crouch",   S_TAKEOFF, 0.14, 0 },
    { "14-away",     S_TAKEOFF, 0.80, 0 },
  };
  Beat depart_beats[] = {
    { "01-typed",    S_ENTER,   0.35, 0 },
    { "02-incoming", S_ENTER,   1.20, 0 },
    { "03-arrives",  S_PICKUP,  0.20, 0 },
    { "04-takes-it", S_PICKUP,  0.70, 0 },
    { "05-tied-on",  S_PICKUP,  1.15, 0 },
    { "06-lifts",    S_TAKEOFF, 0.30, 0 },
    { "07-away",     S_TAKEOFF, 0.90, 0 },
  };
  int ndepart = (int)(sizeof(depart_beats) / sizeof(depart_beats[0]));
  Beat *beats = depart ? depart_beats
              : species == BIRD_PIGEON  ? pigeon_beats
              : species == BIRD_OWL     ? owl_beats
              : species == BIRD_SWALLOW ? swallow_beats : phoenix_beats;
  int nbeats = depart ? ndepart
             : (int)(sizeof(phoenix_beats) / sizeof(phoenix_beats[0]));
  (void)owl_beats; (void)swallow_beats;

  int    last_state = -1;
  double state_t = 0, t = 0;
  int    frames = 0;

  fprintf(stderr, "pixel size %d, scene %dx%d units\n", P, px.bw, px.bh);
  fprintf(stderr, "state timeline:\n");

  for (int i = 0; i < 60 * 40 && !s.done; i++) {
    scene_update(&s, 1.0 / 60.0);
    t += 1.0 / 60.0;
    frames++;

    if (s.state != last_state) {
      fprintf(stderr, "  %-8s @ %5.2fs\n", scene_state_name(s.state), t);
      last_state = s.state;
      state_t = 0;
    } else {
      state_t += 1.0 / 60.0;
    }

    for (int b = 0; b < nbeats; b++) {
      if (beats[b].done || beats[b].state != s.state || state_t < beats[b].after) continue;
      beats[b].done = 1;

      pixel_clear(&px, 0, 0, px.bw, px.bh);
      scene_draw(&s, px.cr);
      pixel_quantize(&px, 0, 0, px.bw, px.bh);

      fill_bg(cr, RW, RH);
      pixel_blit_op(&px, cr, 0, 0, px.bw, px.bh, CAIRO_OPERATOR_OVER);
      write_frame(surf, dir, beats[b].name);
      fprintf(stderr, "    -> %-12s particles=%-4d y=%.0f%% a=%.2f b=%.2f letter=%d\n",
              beats[b].name, s.p.n,
              100.0 * scene_flyer(&s)->y / px.bh,
              species == BIRD_SWALLOW ? s.swallow.wet
                : species == BIRD_OWL ? s.owl.f.roll
                : species == BIRD_PIGEON ? s.pigeon.walk_speed : s.phoenix.heat,
              species == BIRD_SWALLOW ? s.rain
                : species == BIRD_OWL ? s.owl.shake
                : species == BIRD_PIGEON ? s.pigeon.flare : s.phoenix.burn_t,
              s.letter.open);
    }

    if (s.state == S_READING && s.clock > 1.0) letter_dismiss(&s.letter);
    if (species != BIRD_PHOENIX && s.state == S_STAY && s.clock > 5.2)
      letter_dismiss(&s.letter);
  }

  fprintf(stderr, "finished=%s after %.2fs (%d frames)\n",
          s.done ? "yes" : "NO", t, frames);

  int missed = 0;
  for (int b = 0; b < nbeats; b++)
    if (!beats[b].done) { fprintf(stderr, "  MISSED beat %s\n", beats[b].name); missed++; }

  scene_free(&s);
  pixel_free(&px);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return (s.done && !missed) ? 0 : 1;
}

/* ---- pose sheet -------------------------------------------------------- */

typedef struct {
  const char *label;
  double phase, speed, heading, bank, spread, heat, crest, legs;
  double tail_spread, range, burn, trail_spacing, scale;
  int    facing, gliding, carrying, has_burn;
  double flap_override;
  int    has_flap_override;
} Pose;

static void fabricate_trail(Phoenix *b, double dir_x, double spacing) {
  b->ntrail = TRAIL_MAX;
  for (int i = 0; i < TRAIL_MAX; i++) {
    b->trail[i].x = b->f.x - 11.6 * b->f.scale - dir_x * i * spacing;
    /* the wobble is in body units, or it swamps the plume at sprite scale */
    b->trail[i].y = b->f.y + sin(i * 0.28) * 3 * b->f.scale;
  }
  b->arc = spacing * (TRAIL_MAX - 1);
}

static void draw_pose(cairo_t *cr, const Pose *p, double x, double y) {
  Phoenix b;
  phoenix_init(&b, x, y, p->scale > 0 ? p->scale : 3.0);
  Flyer *f = &b.f;

  f->t = 1.2;
  f->heading = p->heading;
  f->facing = p->facing ? p->facing : 1;
  f->facing_blend = f->facing;
  f->vx = f->facing * (p->speed > 0 ? p->speed : 240);
  f->vy = 0;
  f->wing_phase = p->phase;
  f->flap = p->has_flap_override ? p->flap_override : flap_curve(p->phase);
  f->fold = p->has_flap_override ? 0.06 : fold_curve(p->phase);
  f->spread = p->spread > 0 ? p->spread : 1;
  f->bank = p->bank;
  f->bob = -f->flap * 2.2 * f->scale;
  f->gliding = p->gliding;
  f->max_speed = 340;

  b.heat = p->heat;
  b.crest_sway = p->crest;
  b.legs_out = p->legs;
  b.carrying = p->carrying;
  b.tail_spread = p->tail_spread > 0 ? p->tail_spread : 1;
  b.wing_range_mul = p->range > 0 ? p->range : 1;
  if (p->has_burn) { b.burning = 1; b.burn_t = 1.0; b.burn_front = p->burn; }

  fabricate_trail(&b, f->facing, p->trail_spacing > 0 ? p->trail_spacing : 5.5);
  phoenix_draw(&b, cr);
}

/* One cell: render the pose into its own sprite buffer, then blow it up so the
 * individual pixels can be judged. */
static void sheet_cell(cairo_t *cr, const Pose *p, double ox, double oy) {
  Pixelizer px;
  pixel_init(&px, 152, 116, 2);
  Pose q = *p;
  if (q.scale <= 0) q.scale = 0.46;
  if (q.trail_spacing <= 0) q.trail_spacing = 2.4 * q.scale;
  draw_pose(px.cr, &q, px.bw * 0.60, px.bh * 0.52);
  pixel_quantize(&px, 0, 0, px.bw, px.bh);
  blit_scaled(&px, cr, ox, oy, 4);
  pixel_free(&px);
}

int sheet_main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "shots/sheet.png";
  av_seed(7);
  av_set_pixel_mode(1);
  av_set_world(1.0);

  const int W = 1500, H = 960;
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t *cr = cairo_create(surf);
  fill_bg(cr, W, H);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
  cairo_set_line_width(cr, 1);
  for (int i = 1; i < 5; i++) { cairo_move_to(cr, i * 300, 0); cairo_line_to(cr, i * 300, H); }
  for (int i = 1; i < 4; i++) { cairo_move_to(cr, 0, i * 240); cairo_line_to(cr, W, i * 240); }
  cairo_stroke(cr);

  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);

  #define LABEL(lx, ly, s) do {                                   \
      cairo_set_source_rgba(cr, 0.86, 0.75, 0.59, 0.6);           \
      cairo_move_to(cr, lx, ly); cairo_show_text(cr, s);          \
    } while (0)

  /* row 1: one wingbeat cycle */
  const double phases[5] = { 0.00, 0.14, 0.28, 0.42, 0.62 };
  for (int i = 0; i < 5; i++) {
    Pose p; memset(&p, 0, sizeof(p));
    p.phase = phases[i];
    sheet_cell(cr, &p, i * 300 + 16, 8);
    char buf[32];
    snprintf(buf, sizeof(buf), "beat p=%.2f", phases[i]);
    LABEL(20 + i * 300, 226, buf);
  }

  /* row 2: flight modes */
  {
    Pose p; memset(&p, 0, sizeof(p));
    p.gliding = 1; p.spread = 0.88; p.has_flap_override = 1; p.flap_override = 0.18;
    sheet_cell(cr, &p, 16, 248); LABEL(20, 466, "glide");
  }
  { Pose p; memset(&p, 0, sizeof(p)); p.phase = 0.2; p.speed = 40;
    p.trail_spacing = 1.4; p.legs = 1; p.crest = 0.15;
    sheet_cell(cr, &p, 316, 248); LABEL(320, 466, "hover"); }
  { Pose p; memset(&p, 0, sizeof(p)); p.phase = 0.3; p.bank = 0.8; p.heading = 0.35;
    sheet_cell(cr, &p, 616, 248); LABEL(620, 466, "bank"); }
  { Pose p; memset(&p, 0, sizeof(p)); p.phase = 0.18; p.facing = -1; p.heading = -0.25;
    sheet_cell(cr, &p, 916, 248); LABEL(920, 466, "left"); }
  { Pose p; memset(&p, 0, sizeof(p)); p.phase = 0.24; p.legs = 1; p.carrying = 1;
    p.speed = 90; p.trail_spacing = 2.6;
    sheet_cell(cr, &p, 1216, 248); LABEL(1220, 466, "carrying"); }

  /* row 3: the burn */
  { Pose p; memset(&p, 0, sizeof(p)); p.heat = 0.35; p.range = 1.2; p.tail_spread = 1.3;
    p.speed = 30; p.trail_spacing = 1.6; p.legs = 0.7;
    sheet_cell(cr, &p, 16, 488); LABEL(20, 726, "heat .35"); }
  { Pose p; memset(&p, 0, sizeof(p)); p.heat = 0.7; p.range = 1.45; p.tail_spread = 1.7;
    p.speed = 20; p.trail_spacing = 1.3; p.legs = 0.7;
    sheet_cell(cr, &p, 316, 488); LABEL(320, 726, "heat .7 wide"); }
  { Pose p; memset(&p, 0, sizeof(p)); p.heat = 0.8; p.range = 1.5; p.has_burn = 1;
    p.burn = -8; p.tail_spread = 2; p.speed = 20; p.trail_spacing = 1.2;
    sheet_cell(cr, &p, 616, 488); LABEL(620, 726, "front -8"); }
  { Pose p; memset(&p, 0, sizeof(p)); p.heat = 0.9; p.range = 1.5; p.has_burn = 1;
    p.burn = 4; p.tail_spread = 2; p.speed = 20; p.trail_spacing = 1.2;
    sheet_cell(cr, &p, 916, 488); LABEL(920, 726, "front +4"); }
  { Pose p; memset(&p, 0, sizeof(p)); p.heat = 1.0; p.range = 1.55; p.has_burn = 1;
    p.burn = 16; p.tail_spread = 2.1; p.speed = 20; p.trail_spacing = 1.2;
    sheet_cell(cr, &p, 1216, 488); LABEL(1220, 726, "front +16"); }

  /* row 4: how big it actually is */
  const double scales[4] = { 0.7, 1.0, 1.3, 1.7 };
  for (int i = 0; i < 4; i++) {
    Pose p; memset(&p, 0, sizeof(p));
    p.phase = 0.2; p.scale = scales[i] * 0.46;
    sheet_cell(cr, &p, i * 320 + 30, 728);
    char buf[32];
    snprintf(buf, sizeof(buf), "x%.1f", scales[i]);
    LABEL(60 + i * 320, 930, buf);
  }
  #undef LABEL

  cairo_status_t st = cairo_surface_write_to_png(surf, out);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  if (st != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr, "sheet: %s\n", cairo_status_to_string(st));
    return 1;
  }
  fprintf(stderr, "wrote %s\n", out);
  #undef CELL_W
  #undef CELL_H
  #undef COLS
  #undef ROWS
  return 0;
}

/* ---- size comparison --------------------------------------------------- */
/* Renders the bird at a range of pixel sizes next to an actual 32x32 box —
 * the exact footprint of oneko's cat — so the choice is made by eye, at true
 * scale, rather than by guessing numbers. */

static void blit_scaled(Pixelizer *p, cairo_t *dst, double dx, double dy, int factor) {
  cairo_save(dst);
  cairo_translate(dst, dx, dy);
  cairo_scale(dst, factor, factor);
  cairo_set_source_surface(dst, p->buf, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(dst), CAIRO_FILTER_NEAREST);
  cairo_paint(dst);
  cairo_restore(dst);
}

static void oneko_box(cairo_t *cr, double x, double y) {
  cairo_save(cr);
  cairo_set_source_rgba(cr, 0.45, 0.85, 1.0, 0.75);
  cairo_set_line_width(cr, 1);
  cairo_rectangle(cr, x + 0.5, y + 0.5, 32, 32);
  cairo_stroke(cr);
  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 10);
  cairo_move_to(cr, x, y + 45);
  cairo_show_text(cr, "oneko 32");
  cairo_restore(cr);
}

int sizes_main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "shots/sizes.png";
  av_seed(11);
  av_set_pixel_mode(1);
  av_set_world(1.0);

  #define CELL_W 300
  #define CELL_H 300
  #define COLS 4
  #define ROWS 2
  const int W = CELL_W * COLS, H = CELL_H * ROWS + 30;

  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t *cr = cairo_create(surf);
  fill_bg(cr, W, H);

  struct { int p; double scale; } variants[ROWS][COLS] = {
    { {1, 0.70}, {1, 0.90}, {1, 1.10}, {1, 1.30} },
    { {2, 0.36}, {2, 0.45}, {2, 0.55}, {2, 0.65} },
  };

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int    P = variants[r][c].p;
      double sc = variants[r][c].scale;
      double ox = c * CELL_W, oy = r * CELL_H;

      /* render one bird into its own little pixel buffer */
      Pixelizer px;
      pixel_init(&px, 210, 90, P);
      Pose pose;
      memset(&pose, 0, sizeof(pose));
      pose.phase = 0.42;
      pose.scale = sc;
      pose.trail_spacing = 2.2 * sc;
      pose.speed = 200;
      draw_pose(px.cr, &pose, px.bw * 0.62, px.bh * 0.5);
      pixel_quantize(&px, 0, 0, px.bw, px.bh);

      blit_scaled(&px, cr, ox + 14, oy + 18, P);          /* true size */
      oneko_box(cr, ox + 14, oy + 18);
      blit_scaled(&px, cr, ox + 6, oy + 96, P * 5);       /* 5x, to judge detail */

      cairo_set_source_rgba(cr, 0.86, 0.75, 0.59, 0.65);
      cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      char buf[64];
      snprintf(buf, sizeof(buf), "px=%d scale=%.2f  body~%dpx", P, sc, (int)(34 * sc * P));
      cairo_move_to(cr, ox + 14, oy + CELL_H - 8);
      cairo_show_text(cr, buf);

      pixel_free(&px);
    }
  }

  cairo_status_t st = cairo_surface_write_to_png(surf, out);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  if (st != CAIRO_STATUS_SUCCESS) { fprintf(stderr, "sizes: fail\n"); return 1; }
  fprintf(stderr, "wrote %s\n", out);
  return 0;
}

/* ---- filmstrip --------------------------------------------------------- */
/* Runs one delivery and tiles crops of the key beats into a single image, at
 * true device scale, so the sequence can be read at a glance. */

#define TILE_W 430
#define TILE_H 270
#define STRIP_COLS 3
#define STRIP_ROWS 3

int strip_main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "shots/strip.png";
  int P = argc > 2 ? atoi(argv[2]) : 2;
  int species = scene_species_from_name(argc > 3 ? argv[3] : "phoenix");
  if (P < 1) P = 1;

  av_seed(20240825);
  av_set_pixel_mode(1);

  Pixelizer px;
  pixel_init(&px, RW, RH, P);

  const int LABEL_H = 26;
  const int W = TILE_W * STRIP_COLS;
  const int H = (TILE_H + LABEL_H) * STRIP_ROWS;
  cairo_surface_t *strip = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t *cr = cairo_create(strip);
  fill_bg(cr, W, H);

  Scene s;
  memset(&s, 0, sizeof(s));
  scene_start(&s, px.bw, px.bh,
              species == BIRD_SWALLOW
                ? "it was coming down where he set off.\nhe came anyway."
              : species == BIRD_OWL
                ? "sorry about the wall.\nhe is very old, and he tries."
              : species == BIRD_PIGEON
                ? "you asked for the pigeon.\nhe took his time, but here he is."
                : "the pigeon never came.\nso I sent something that burns.",
              "muez", species);

  typedef struct { const char *name; int state; double after; int done; } SBeat;
  SBeat phoenix_b[] = {
    { "1. in off the edge",   S_ENTER,  1.10, 0 },
    { "2. crossing",          S_ENTER,  2.00, 0 },
    { "3. flare and hover",   S_SETTLE, 0.55, 0 },
    { "4. lets the scroll go",S_DROP,   0.22, 0 },
    { "5. the letter opens",  S_WATCH,  0.75, 0 },
    { "6. it starts to burn", S_BURN,   0.80, 0 },
    { "7. fire takes it",     S_BURN,   1.45, 0 },
    { "8. it goes to smoke",  S_ASH,    0.20, 0 },
    { "9. ash on the paper",  S_ASH,    1.90, 0 },
  };
  SBeat pigeon_b[] = {
    { "1. in off the edge",     S_ENTER,   1.70, 0 },
    { "2. coming down",         S_LAND,    0.30, 0 },
    { "3. flare, feet forward", S_LAND,    1.05, 0 },
    { "4. down, and walking",   S_WALK,    0.60, 0 },
    { "5. bends to his leg",    S_SETDOWN, 0.45, 0 },
    { "6. the letter unrolls",  S_STAY,    0.55, 0 },
    { "7. waits to be noticed", S_STAY,    2.40, 0 },
    { "8. crouch, then clap",   S_TAKEOFF, 0.34, 0 },
    { "9. gone",                S_TAKEOFF, 0.95, 0 },
  };
  SBeat owl_b[] = {
    { "1. in off the edge",     S_ENTER,   2.10, 0 },
    { "2. and he speeds up",    S_CHARGE,  0.45, 0 },
    { "3. the wall",            S_STRIKE,  0.05, 0 },
    { "4. down he goes",        S_TUMBLE,  0.34, 0 },
    { "5. flat on his back",    S_SPRAWL,  0.30, 0 },
    { "6. shakes himself out",  S_SHAKE,   0.80, 0 },
    { "7. unties it at last",   S_SETDOWN, 0.60, 0 },
    { "8. delivered",           S_STAY,    0.70, 0 },
    { "9. off he goes",         S_TAKEOFF, 0.62, 0 },
  };
  SBeat swallow_b[] = {
    { "1. in out of it",        S_ENTER,   1.00, 0 },
    { "2. still dripping",      S_ENTER,   1.90, 0 },
    { "3. flare to land",       S_LAND,    0.80, 0 },
    { "4. down, and soaked",    S_SHAKE,   0.10, 0 },
    { "5. shakes it off",       S_SHAKE,   0.60, 0 },
    { "6. and again",           S_SHAKE,   1.00, 0 },
    { "7. drier, unties it",    S_SETDOWN, 0.60, 0 },
    { "8. delivered",           S_STAY,    0.80, 0 },
    { "9. away",                S_TAKEOFF, 0.55, 0 },
  };
  SBeat *beats = species == BIRD_PIGEON  ? pigeon_b
               : species == BIRD_OWL     ? owl_b
               : species == BIRD_SWALLOW ? swallow_b : phoenix_b;
  int nbeats = (int)(sizeof(phoenix_b) / sizeof(phoenix_b[0]));
  int placed = 0;

  int last_state = -1;
  double state_t = 0;

  for (int i = 0; i < 60 * 40 && !s.done; i++) {
    scene_update(&s, 1.0 / 60.0);
    if (s.state != last_state) { last_state = s.state; state_t = 0; }
    else state_t += 1.0 / 60.0;

    for (int b = 0; b < nbeats; b++) {
      if (beats[b].done || beats[b].state != s.state || state_t < beats[b].after) continue;
      beats[b].done = 1;

      pixel_clear(&px, 0, 0, px.bw, px.bh);
      scene_draw(&s, px.cr);
      pixel_quantize(&px, 0, 0, px.bw, px.bh);

      /* Centre the crop on whatever is actually happening. scene_bbox clamps
       * to the screen, so a bird still outside it yields nothing — fall back
       * to the bird's own position rather than to the middle of the screen. */
      /* Centre on the bird itself. Centring on the scene bbox lets a cloud of
       * drifting feathers drag the frame right off the subject. */
      Flyer *bf = scene_flyer(&s);
      double cx = av_clamp(bf->x * P, TILE_W / 2.0, RW - TILE_W / 2.0);
      double cy = av_clamp(bf->y * P, TILE_H / 2.0, RH - TILE_H / 2.0);
      double crop_x = av_clamp(cx - TILE_W / 2.0, 0, RW - TILE_W);
      double crop_y = av_clamp(cy - TILE_H / 2.0, 0, RH - TILE_H);

      int col = placed % STRIP_COLS, row = placed / STRIP_COLS;
      double tx = col * TILE_W, ty = row * (TILE_H + LABEL_H);
      placed++;

      cairo_save(cr);
      cairo_rectangle(cr, tx, ty, TILE_W, TILE_H);
      cairo_clip(cr);
      cairo_translate(cr, tx - crop_x, ty - crop_y);
      pixel_blit_op(&px, cr, 0, 0, px.bw, px.bh, CAIRO_OPERATOR_OVER);
      cairo_restore(cr);

      cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
      cairo_set_line_width(cr, 1);
      cairo_rectangle(cr, tx + 0.5, ty + 0.5, TILE_W - 1, TILE_H - 1);
      cairo_stroke(cr);

      cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 14);
      cairo_set_source_rgba(cr, 0.86, 0.75, 0.59, 0.75);
      cairo_move_to(cr, tx + 10, ty + TILE_H + 18);
      cairo_show_text(cr, beats[b].name);
    }

    if (s.state == S_READING && s.clock > 0.5) letter_dismiss(&s.letter);
    if (species != BIRD_PHOENIX && s.state == S_STAY && s.clock > 3.2)
      letter_dismiss(&s.letter);
  }

  fprintf(stderr, "%d/%d beats placed\n", placed, nbeats);
  cairo_status_t st = cairo_surface_write_to_png(strip, out);
  scene_free(&s);
  pixel_free(&px);
  cairo_destroy(cr);
  cairo_surface_destroy(strip);
  if (st != CAIRO_STATUS_SUCCESS) return 1;
  fprintf(stderr, "wrote %s\n", out);
  return placed == nbeats ? 0 : 1;
}

/* ---- pigeon pose sheet -------------------------------------------------- */

typedef struct {
  double walk_phase, walk_speed, scale;
  int    grounded, facing, carrying;
  double flap_phase, spread, flare, tail_fan, tail_drop, legs_out, crouch;
  double head_dip, look, puff, clap, pitch, heading;
  int    gliding;
} PPose;

static void draw_ppose(cairo_t *cr, const PPose *p, double x, double y) {
  Pigeon b;
  pigeon_init(&b, x, y, p->scale > 0 ? p->scale : 0.62);
  Flyer *f = &b.f;

  f->t = 1.4;
  f->facing = p->facing ? p->facing : 1;
  f->facing_blend = f->facing;
  f->heading = p->heading;
  f->pitch = p->pitch;
  f->wing_phase = p->flap_phase;
  f->flap = flap_curve(p->flap_phase);
  f->fold = fold_curve(p->flap_phase) * 0.6;
  f->gliding = p->gliding;
  if (p->gliding) { f->flap = -0.30; f->fold = 0.04; }
  f->spread = p->grounded ? 0.0 : (p->spread > 0 ? p->spread : 1.0);
  f->bob = p->grounded ? 0 : -f->flap * 2.4 * f->scale;

  b.grounded  = p->grounded;
  b.ground_y  = y + b.stand_h * f->scale;
  b.walk_phase = p->walk_phase;
  b.walk_speed = p->walk_speed;
  b.walk_target = x;
  b.carrying  = p->carrying;
  b.tail_fan  = p->tail_fan > 0 ? p->tail_fan : 0.12;
  b.tail_drop = p->tail_drop;
  b.legs_out  = p->grounded ? 1.0 : p->legs_out;
  b.crouch    = p->crouch;
  b.head_dip  = p->head_dip;
  b.look      = p->look;
  b.puff      = p->puff;
  b.clap      = p->clap;
  b.flare     = p->flare;

  if (p->grounded) f->y = b.ground_y - b.stand_h * f->scale + b.crouch * 3.0 * f->scale;

  pigeon_draw(&b, cr);
}

static void pcell(cairo_t *cr, const PPose *p, double ox, double oy) {
  Pixelizer px;
  pixel_init(&px, 152, 116, 2);
  PPose q = *p;
  if (q.scale <= 0) q.scale = 0.62;
  draw_ppose(px.cr, &q, px.bw * 0.52, px.bh * 0.56);
  pixel_quantize(&px, 0, 0, px.bw, px.bh);
  blit_scaled(&px, cr, ox, oy, 4);
  pixel_free(&px);
}

int pigeon_sheet_main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "shots/pigeon-sheet.png";
  av_seed(3);
  av_set_pixel_mode(1);
  av_set_world(1.0);

  const int W = 1500, H = 1000;
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t *cr = cairo_create(surf);
  fill_bg(cr, W, H);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
  cairo_set_line_width(cr, 1);
  for (int i = 1; i < 5; i++) { cairo_move_to(cr, i * 300, 0); cairo_line_to(cr, i * 300, H); }
  for (int i = 1; i < 4; i++) { cairo_move_to(cr, 0, i * 250); cairo_line_to(cr, W, i * 250); }
  cairo_stroke(cr);

  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  #define LBL(lx, ly, str) do { cairo_set_source_rgba(cr, 0.72, 0.78, 0.88, 0.7); \
      cairo_move_to(cr, lx, ly); cairo_show_text(cr, str); } while (0)

  /* row 1 — one stride. Watch the head: it should sit STILL over cells 2..5
   * while the body slides forward under it, then snap ahead. */
  const double ph[5] = { 0.00, 0.18, 0.42, 0.66, 0.90 };
  for (int i = 0; i < 5; i++) {
    PPose p; memset(&p, 0, sizeof(p));
    p.grounded = 1; p.walk_speed = 26; p.walk_phase = ph[i]; p.carrying = 1;
    pcell(cr, &p, i * 300 + 16, 10);
    char buf[40];
    snprintf(buf, sizeof(buf), "stride %.2f", ph[i]);
    LBL(20 + i * 300, 236, buf);
  }

  /* row 2 — standing business */
  { PPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.carrying=1;
    pcell(cr,&p,16,258); LBL(20,486,"stand"); }
  { PPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.head_dip=1;
    pcell(cr,&p,316,258); LBL(320,486,"peck / untie"); }
  { PPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.look=0.8;
    pcell(cr,&p,616,258); LBL(620,486,"look"); }
  { PPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.puff=1;
    pcell(cr,&p,916,258); LBL(920,486,"coo"); }
  { PPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.crouch=1;
    pcell(cr,&p,1216,258); LBL(1220,486,"crouch"); }

  /* row 3 — in the air */
  const double fp[5] = { 0.00, 0.20, 0.40, 0.62, 0.82 };
  for (int i = 0; i < 4; i++) {
    PPose p; memset(&p, 0, sizeof(p));
    p.flap_phase = fp[i]; p.carrying = 1;
    pcell(cr, &p, i * 300 + 16, 508);
    char buf[40];
    snprintf(buf, sizeof(buf), "beat %.2f", fp[i]);
    LBL(20 + i * 300, 736, buf);
  }
  { PPose p; memset(&p,0,sizeof(p)); p.gliding=1; p.spread=0.94; p.carrying=1;
    pcell(cr,&p,1216,508); LBL(1220,736,"glide (V)"); }

  /* row 4 — arriving and leaving */
  { PPose p; memset(&p,0,sizeof(p)); p.flap_phase=0.35; p.flare=0.45;
    p.tail_fan=0.55; p.tail_drop=0.45; p.legs_out=0.6; p.pitch=-0.28; p.heading=-0.28;
    p.carrying=1;
    pcell(cr,&p,16,756); LBL(20,984,"flare start"); }
  { PPose p; memset(&p,0,sizeof(p)); p.flap_phase=0.15; p.flare=1.0;
    p.tail_fan=1.0; p.tail_drop=0.85; p.legs_out=1.0; p.pitch=-0.62; p.heading=-0.62;
    p.carrying=1;
    pcell(cr,&p,316,756); LBL(320,984,"full flare"); }
  { PPose p; memset(&p,0,sizeof(p)); p.flap_phase=0.92; p.clap=1.0; p.spread=1;
    pcell(cr,&p,616,756); LBL(620,984,"clap (tips up)"); }
  { PPose p; memset(&p,0,sizeof(p)); p.flap_phase=0.30; p.clap=1.0; p.spread=1;
    p.heading=-0.4; p.pitch=-0.3;
    pcell(cr,&p,916,756); LBL(920,984,"climbing out"); }
  { PPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.walk_speed=26; p.walk_phase=0.42;
    p.facing=-1; p.carrying=1;
    pcell(cr,&p,1216,756); LBL(1220,984,"walking left"); }
  #undef LBL

  cairo_status_t st = cairo_surface_write_to_png(surf, out);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  if (st != CAIRO_STATUS_SUCCESS) return 1;
  fprintf(stderr, "wrote %s\n", out);
  return 0;
}

/* ---- owl pose sheet ----------------------------------------------------- */

typedef struct {
  double scale, flap_phase, spread, roll, heading, pitch;
  int    grounded, facing, carrying, gliding, limp;
  double fluff, tufts, dazed, sprawl, head_turn, head_dip;
  double tail_fan, legs_out, crouch, flare, blink;
} OPose;

static void draw_opose(cairo_t *cr, const OPose *p, double x, double y) {
  Owl b;
  owl_init(&b, x, y, p->scale > 0 ? p->scale : 0.60);
  Flyer *f = &b.f;

  f->t = 1.3;
  f->facing = p->facing ? p->facing : 1;
  f->facing_blend = f->facing;
  f->heading = p->heading;
  f->pitch = p->pitch;
  f->roll = p->roll;
  f->blink = p->blink;
  f->wing_phase = p->flap_phase;
  f->flap = flap_curve(p->flap_phase);
  f->fold = fold_curve(p->flap_phase) * 0.45;
  f->gliding = p->gliding;
  if (p->gliding) { f->flap = -0.16; f->fold = 0.02; }
  f->spread = p->grounded ? 0.0 : (p->spread > 0 ? p->spread : 1.0);
  f->bob = p->grounded ? 0 : -f->flap * 3.4 * f->scale;

  b.grounded  = p->grounded;
  b.ground_y  = y + b.stand_h * f->scale;
  b.carrying  = p->carrying;
  b.fluff     = p->fluff;
  b.tufts     = p->tufts > 0 ? p->tufts : 0.55;
  b.dazed     = p->dazed;
  b.sprawl    = p->sprawl;
  b.head_turn = p->head_turn;
  b.head_dip  = p->head_dip;
  b.tail_fan  = p->tail_fan > 0 ? p->tail_fan : 0.55;
  b.legs_out  = p->grounded ? 1.0 : p->legs_out;
  b.crouch    = p->crouch;
  b.flare     = p->flare;
  b.wing_limp = p->limp;

  if (p->grounded) f->y = b.ground_y - b.stand_h * f->scale + b.crouch * 3.0 * f->scale;

  owl_draw(&b, cr);
}

static void ocell(cairo_t *cr, const OPose *p, double ox, double oy) {
  Pixelizer px;
  pixel_init(&px, 152, 116, 2);
  OPose q = *p;
  if (q.scale <= 0) q.scale = 0.60;
  draw_opose(px.cr, &q, px.bw * 0.50, px.bh * 0.54);
  pixel_quantize(&px, 0, 0, px.bw, px.bh);
  blit_scaled(&px, cr, ox, oy, 4);
  pixel_free(&px);
}

int owl_sheet_main(int argc, char **argv) {
  const char *out = argc > 1 ? argv[1] : "shots/owl-sheet.png";
  av_seed(5);
  av_set_pixel_mode(1);
  av_set_world(1.0);

  const int W = 1500, H = 1000;
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
  cairo_t *cr = cairo_create(surf);
  fill_bg(cr, W, H);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
  cairo_set_line_width(cr, 1);
  for (int i = 1; i < 5; i++) { cairo_move_to(cr, i * 300, 0); cairo_line_to(cr, i * 300, H); }
  for (int i = 1; i < 4; i++) { cairo_move_to(cr, 0, i * 250); cairo_line_to(cr, W, i * 250); }
  cairo_stroke(cr);
  cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  #define L(lx, ly, str) do { cairo_set_source_rgba(cr, 0.85, 0.82, 0.74, 0.7); \
      cairo_move_to(cr, lx, ly); cairo_show_text(cr, str); } while (0)

  /* row 1 — the slow deep beat */
  const double fp[5] = { 0.00, 0.18, 0.38, 0.58, 0.80 };
  for (int i = 0; i < 4; i++) {
    OPose p; memset(&p, 0, sizeof(p));
    p.flap_phase = fp[i]; p.carrying = 1;
    ocell(cr, &p, i * 300 + 16, 10);
    char buf[32]; snprintf(buf, sizeof(buf), "beat %.2f", fp[i]);
    L(20 + i * 300, 236, buf);
  }
  { OPose p; memset(&p,0,sizeof(p)); p.gliding=1; p.spread=0.96; p.carrying=1;
    ocell(cr,&p,1216,10); L(1220,236,"glide"); }

  /* row 2 — the accident */
  { OPose p; memset(&p,0,sizeof(p)); p.flap_phase=0.30; p.carrying=1; p.heading=0.1;
    ocell(cr,&p,16,258); L(20,486,"charging"); }
  { OPose p; memset(&p,0,sizeof(p)); p.flap_phase=0.30; p.carrying=1; p.limp=1;
    p.fluff=1; p.tufts=1; p.dazed=1; p.roll=0.5;
    ocell(cr,&p,316,258); L(320,486,"struck"); }
  { OPose p; memset(&p,0,sizeof(p)); p.carrying=1; p.limp=1; p.dazed=1;
    p.roll=2.1; p.fluff=0.8; p.legs_out=0.7; p.tail_fan=0.8;
    ocell(cr,&p,616,258); L(620,486,"tumbling"); }
  { OPose p; memset(&p,0,sizeof(p)); p.carrying=1; p.limp=1; p.dazed=1;
    p.roll=4.3; p.fluff=0.8; p.legs_out=0.7; p.tail_fan=0.8;
    ocell(cr,&p,916,258); L(920,486,"tumbling 2"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.carrying=1; p.sprawl=1;
    p.roll=-1.9; p.dazed=1; p.fluff=0.9; p.limp=1;
    ocell(cr,&p,1216,258); L(1220,486,"sprawled"); }

  /* row 3 — putting himself back together */
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.carrying=1; p.dazed=0.8; p.fluff=1;
    ocell(cr,&p,16,508); L(20,736,"upright, fluffed"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.carrying=1; p.fluff=1;
    p.roll=0.28; p.head_turn=0.8;
    ocell(cr,&p,316,508); L(320,736,"shaking"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.carrying=1; p.fluff=0.2;
    ocell(cr,&p,616,508); L(620,736,"settled"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.carrying=1; p.head_dip=1;
    ocell(cr,&p,916,508); L(920,736,"unties it"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.crouch=1;
    ocell(cr,&p,1216,508); L(1220,736,"crouch to go"); }

  /* row 4 — the head, which is most of an owl */
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.head_turn=-0.9;
    ocell(cr,&p,16,756); L(20,984,"head turned away"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.head_turn=0;
    ocell(cr,&p,316,756); L(320,984,"head level"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.head_turn=1.0;
    ocell(cr,&p,616,756); L(620,984,"facing you"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.blink=0.1;
    ocell(cr,&p,916,756); L(920,984,"blink"); }
  { OPose p; memset(&p,0,sizeof(p)); p.grounded=1; p.facing=-1; p.carrying=1;
    ocell(cr,&p,1216,756); L(1220,984,"facing left"); }
  #undef L

  cairo_status_t st = cairo_surface_write_to_png(surf, out);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  if (st != CAIRO_STATUS_SUCCESS) return 1;
  fprintf(stderr, "wrote %s\n", out);
  return 0;
}
