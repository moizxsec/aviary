/* aviary — tiny birds that carry letters across an X11 desktop.
 *
 * Bird one: the phoenix. Flies in off a screen edge, drops a sealed scroll,
 * then burns itself down to embers while you read.
 */
#ifndef AVIARY_H
#define AVIARY_H

#include <cairo/cairo.h>
#include <stdint.h>

/* ------------------------------------------------------------------ util -- */

#define TAU 6.283185307179586
#define D2R 0.017453292519943295

typedef struct { double x, y; } Vec;
typedef struct { double r, g, b; } Rgb;

double av_clamp(double v, double a, double b);
double av_lerp(double a, double b, double t);
double av_smooth(double t);
double av_smoother(double t);
double av_ease_out_cubic(double t);
double av_ease_in_cubic(double t);
double av_ease_in_out(double t);
double av_wrap_angle(double a);
double av_angle_towards(double cur, double target, double rate, double dt);
double av_damp(double cur, double target, double rate, double dt);

void   av_seed(uint64_t s);
double av_rand(void);                    /* [0,1) */
double av_rand_range(double a, double b);
double av_rand_sym(double a);            /* [-a,a] */
int    av_rand_int(int n);

double av_noise(double x, int stream);   /* value noise, -1..1 */

Rgb    av_fire_color(double t);          /* 0 = white hot, 1 = dead ash */

/* The scene runs in whatever coordinate space the output buffer uses, which is
 * much smaller in pixel-art mode. Every speed, gravity and radius is authored
 * against a reference 800-unit-tall scene and multiplied by this. */
void   av_set_world(double w);
double av_world(void);

/* Pixel-art mode: solid shapes, no soft alpha, hard outlines. Soft gradients
 * turn into dither noise once a sprite is only ~28px across. */
void   av_set_pixel_mode(int on);
int    av_pixel_mode(void);
Rgb    av_mix(Rgb a, Rgb b, double t);

/* cairo helpers */
void av_quad_to(cairo_t *cr, double qx, double qy, double x, double y);
void av_set_rgba(cairo_t *cr, Rgb c, double a);
void av_round_rect(cairo_t *cr, double x, double y, double w, double h, double r);

/* The letter, rolled and bound to a leg with thread — the way a message
 * actually travelled by pigeon. Drawn in body units, at the leg. */
void av_draw_tied_letter(cairo_t *cr, double x, double y, double rot, double tie_dx);

/* ------------------------------------------------------------- particles -- */

enum { P_FIRE, P_EMBER, P_SPARK, P_ASH, P_SMOKE, P_RING, P_FEATHER, P_WATER };

typedef struct {
  int    kind;
  double x, y, vx, vy;
  double r, ttl, life;
  double buoy, grav, drag;
  double wob, wob_rate, wob_amp;
  double spin, rot, grow, alpha;
  double heat0, len, glow;
  double r1, w0, hue;               /* ring only */
} Particle;

typedef struct {
  Particle *a;
  int n, cap;
} Particles;

void particles_init(Particles *p, int cap);
void particles_free(Particles *p);
void particles_clear(Particles *p);
void particles_update(Particles *p, double dt);
void particles_draw(Particles *p, cairo_t *cr);
void particles_bbox(Particles *p, double *x0, double *y0, double *x1, double *y1);

void p_fire (Particles *p, double x, double y, double vx, double vy,
             double r, double ttl, double buoy);
void p_ember(Particles *p, double x, double y, double vx, double vy,
             double spread, double r, double ttl);
void p_spark(Particles *p, double x, double y, double spread);
void p_ash  (Particles *p, double x, double y, double vx, double vy,
             double ttl, double glow);
void p_smoke(Particles *p, double x, double y, double r, double alpha);
void p_ring (Particles *p, double x, double y, double r0, double r1,
             double w0, double ttl, double hue);
/* A knocked-loose feather: falls slowly, rocking side to side the whole way. */
void p_feather(Particles *p, double x, double y, double vx, double vy, double shade);
/* Rain, and water flung off a wet bird. Drawn as a streak whose length follows
 * its own speed, so falling rain is a line and thrown spray is nearly a dot. */
void p_water(Particles *p, double x, double y, double vx, double vy, int is_spray);

/* Where falling water stops and splashes. Set per scene; 0 disables it. */
void   av_set_water_floor(double y);
double av_water_floor(void);

/* ---------------------------------------------------------------- flight -- */

#define MAX_WP 4

typedef struct {
  Vec    p;
  double slow;      /* arrival slow-down radius, 0 = none */
  double radius;    /* how close counts as reached */
} Waypoint;

/* Everything true of any bird. A new species subclasses this by embedding it
 * and adding anatomy: see Phoenix below. */
typedef struct {
  double x, y, vx, vy, ax, ay;
  double max_speed, max_force, scale;

  double heading;        /* body angle, always the rightward-equivalent one */
  double roll;           /* free rotation on top of heading, for tumbling */
  double facing;         /* -1 left, +1 right */
  double facing_blend;   /* smoothed, passes through zero on a turn */
  double bank, pitch, bob;

  double wing_phase, wing_hz, flap, fold, spread, effort;
  double fore_floor;     /* a broad-winged bird never goes fully edge-on */
  double glide_sink;     /* how fast it loses height coasting; 0 = default */
  int    hover, gliding, bounding;
  double bound_timer;

  double t, noise_off;
  double wander_gain;

  double head_bob, head_phase, blink_at, blink;

  Waypoint wp[MAX_WP];
  int      nwp;
  double   arrive_radius;
} Flyer;

void   flyer_init(Flyer *f, double x, double y);
void   flyer_update(Flyer *f, double dt);
void   flyer_update_pose(Flyer *f, double dt);
void   flyer_update_wings(Flyer *f, double dt);
void   flyer_force(Flyer *f, double fx, double fy);
double flyer_speed(const Flyer *f);
double flyer_seek(Flyer *f, double tx, double ty, double slow_radius);
double flyer_xscale(const Flyer *f);
void   flyer_transform(const Flyer *f, cairo_t *cr);
Vec    flyer_to_world(const Flyer *f, double lx, double ly);

double flap_curve(double p);
double fold_curve(double p);

/* Wing geometry is true of any bird, so it lives with the flight model rather
 * than with a species. The caller supplies proportions and draws the feathers. */
typedef struct {
  double phi, hand_phi;     /* arm and hand angles, radians */
  double wx, wy;            /* wrist */
  double tx, ty;            /* tip */
  double bx, by;            /* bowed leading-edge control point */
  double L, arm, hand, fore;
} WingPose;

void wing_pose(const Flyer *f, double range_deg, double sweep_extra,
               double wing_len, double len_k,
               double sh_x, double sh_y, double y_off, WingPose *w);

/* --------------------------------------------------------------- phoenix -- */

#define TRAIL_MAX 48
#define PLUMES 3

typedef struct {
  Flyer  f;

  Vec    trail[TRAIL_MAX];
  int    ntrail;
  double arc, plume_len;

  int    carrying;
  double legs_out, crest_sway, wing_range_mul, tail_spread;

  double heat;
  int    burning, consumed, flashed;
  double burn_t, burn_front;
  double ember_clock, crackle;
  double wing_hz_override;
} Phoenix;

void phoenix_init(Phoenix *b, double x, double y, double scale);
void phoenix_update(Phoenix *b, double dt, Particles *p);
void phoenix_draw(Phoenix *b, cairo_t *cr);
void phoenix_ignite(Phoenix *b);
Vec  phoenix_release_point(Phoenix *b);
void phoenix_bbox(Phoenix *b, double *x0, double *y0, double *x1, double *y1);

/* ---------------------------------------------------------------- pigeon -- */

/* The bird that was actually owed. It does not burn, it does not hover: it
 * lands, walks, puts the message down, waits to be noticed, and leaves. */
typedef struct {
  Flyer  f;

  int    carrying;          /* message capsule still strapped to the leg */
  double capsule_drop;      /* 0 on the leg .. 1 handed over */

  /* on the ground */
  int    grounded;
  double ground_y;          /* world y the feet rest on */
  double stand_h;           /* body origin to foot, in body units */
  double walk_phase;        /* advances with DISTANCE, not time */
  double walk_speed;        /* units/sec; 0 = standing still */
  double walk_target;       /* world x to walk to */

  /* pose */
  double tail_fan;          /* 0 closed .. 1 spread as an airbrake */
  double tail_drop;
  double legs_out;          /* swung forward for touchdown */
  double crouch;            /* compressed before a launch */
  double head_dip;          /* bent down to the leg, or pecking */
  double look;              /* idle head turn */
  double puff;              /* throat swelling on a coo */
  double flare;             /* 0 .. 1 landing flare */

  /* takeoff */
  int    clapped;
  double clap;              /* wingtips meeting over the back */

  /* idling */
  double idle_t, next_idle, act_t;
  int    act;               /* 0 none, 1 peck, 2 look, 3 coo, 4 shuffle */
} Pigeon;

void pigeon_init(Pigeon *b, double x, double y, double scale);
void pigeon_update(Pigeon *b, double dt, Particles *p);
void pigeon_draw(Pigeon *b, cairo_t *cr);
void pigeon_bbox(Pigeon *b, double *x0, double *y0, double *x1, double *y1);
Vec  pigeon_capsule_point(Pigeon *b);
void pigeon_touch_down(Pigeon *b, double ground_y);
void pigeon_walk_to(Pigeon *b, double world_x);
int  pigeon_walking(const Pigeon *b);
void pigeon_launch(Pigeon *b);

/* ------------------------------------------------------------------- owl -- */

/* Errol. Enormous wing area, so the beat is slow and deep and the whole thing
 * floats. He is also very old, and does not really land so much as arrive. */
typedef struct {
  Flyer  f;

  int    carrying;
  int    grounded;
  double ground_y, stand_h;

  double head_turn;      /* owls swivel; a dazed one swivels slowly */
  double tufts;          /* ear tufts, raised when alarmed */
  double fluff;          /* 0 sleek .. 1 fully puffed up */
  double shake;          /* the rouse: fluff up, then shake it all out */
  double shake_t;
  double dazed;          /* crossed-out eyes, more or less */
  double sprawl;         /* on his back, legs in the air */
  double tail_fan, legs_out, crouch, flare;
  double head_dip;
  double wing_limp;      /* wings hanging useless during the fall */

  double wobble;         /* he is not a well bird */
  double roll_v;         /* spin rate while tumbling */
  int    struck;
} Owl;

void owl_init(Owl *b, double x, double y, double scale);
void owl_update(Owl *b, double dt, Particles *p);
void owl_draw(Owl *b, cairo_t *cr);
void owl_bbox(Owl *b, double *x0, double *y0, double *x1, double *y1);
Vec  owl_letter_point(Owl *b);
void owl_strike(Owl *b, Particles *p, double dir_x);
void owl_touch_down(Owl *b, double ground_y);
void owl_launch(Owl *b);

/* --------------------------------------------------------------- swallow -- */

/* The one that actually flies in weather. Long narrow wings and a forked tail
 * make it fast and absurdly manoeuvrable — the exact opposite of the owl. It
 * arrives soaked, and has to shake itself dry before it can do anything else.
 */
typedef struct {
  Flyer  f;

  int    carrying;
  int    grounded;
  double ground_y, stand_h;

  double wet;            /* 1 soaked .. 0 dry: changes colour AND silhouette */
  double drip_clock;

  double shake;          /* the dry-off: violent, and it throws water */
  double shake_t;

  double tail_spread;    /* the fork opens as a rudder when it banks */
  double legs_out, crouch, flare, head_dip, look;
} Swallow;

void swallow_init(Swallow *b, double x, double y, double scale);
void swallow_update(Swallow *b, double dt, Particles *p);
void swallow_draw(Swallow *b, cairo_t *cr);
void swallow_bbox(Swallow *b, double *x0, double *y0, double *x1, double *y1);
Vec  swallow_letter_point(Swallow *b);
void swallow_touch_down(Swallow *b, double ground_y);
void swallow_launch(Swallow *b);

/* ---------------------------------------------------------------- letter -- */

/* Each bird's letter arrives in the state that bird left it in. */
enum { LS_BURNT, LS_BRIGHT, LS_WET, LS_DIRTY };

#define LETTER_MAX_TEXT 4096
#define LETTER_MAX_FROM 64
#define CHAR_STEPS 46
#define CHAR_POINTS (CHAR_STEPS * 4 + 2)

typedef struct {
  int    open, gone;
  int    style;               /* LS_* — set before letter_show */
  double auto_t;              /* seconds on screen before it goes by itself */
  double burn;                /* 0 .. 1: the paper being eaten from below */
  int    burning;
  double x, y, w, h;
  double ui;                  /* metric scale: the panel runs in scene units */
  double open_t, gone_t;
  double scorch, ember;       /* singed by the phoenix going up next to it */
  double age;

  char   text[LETTER_MAX_TEXT];
  char   from[LETTER_MAX_FROM];

  Vec    edge[CHAR_POINTS];   /* the burnt outline, in 0..1 of the panel */
  int    nedge;

  double btn_x, btn_y, btn_w, btn_h;   /* screen coords, valid while open */
} Letter;

void letter_plan(Letter *l, int sw, int sh);
void letter_show(Letter *l, const char *text, const char *from);
void letter_update(Letter *l, double dt);
void letter_draw(Letter *l, cairo_t *cr);
void letter_dismiss(Letter *l);
int  letter_hit(Letter *l, int x, int y);
void letter_scorch(Letter *l);
/* world y of the burn line, while a letter is burning away */
double letter_burn_y(Letter *l);

/* ------------------------------------------------------------ pixel art -- */

typedef struct {
  int              size;      /* device pixels per sprite pixel */
  int              bw, bh;    /* buffer dimensions */
  cairo_surface_t *buf;
  cairo_t         *cr;
} Pixelizer;

void pixel_init(Pixelizer *p, int screen_w, int screen_h, int pixel_size);
void pixel_free(Pixelizer *p);
void pixel_clear(Pixelizer *p, int x, int y, int w, int h);
void pixel_quantize(Pixelizer *p, int x, int y, int w, int h);
void pixel_blit(Pixelizer *p, cairo_t *dst, int x, int y, int w, int h);
void pixel_blit_op(Pixelizer *p, cairo_t *dst, int x, int y, int w, int h,
                   cairo_operator_t op);

/* ----------------------------------------------------------------- scene -- */

enum { BIRD_PHOENIX, BIRD_PIGEON, BIRD_OWL, BIRD_SWALLOW };

/* A bird either brings a letter to you, or comes and takes one away. */
enum { SM_DELIVER, SM_DEPART };

enum {
  S_ENTER, S_SETTLE, S_DROP, S_WATCH, S_BURN, S_ASH,   /* the phoenix path */
  S_LAND, S_WALK, S_SETDOWN, S_STAY, S_TAKEOFF,        /* the pigeon path */
  S_CHARGE, S_STRIKE, S_TUMBLE, S_SPRAWL, S_SHAKE,     /* the owl's misadventure */
  S_PICKUP,                                            /* taking one away */
  S_READING, S_DONE
};

typedef struct {
  double x, y, vx, vy, rot, rot_v;
  double appear;              /* 0 .. 1 as it rolls itself up */
  int    alive;
} Scroll;

typedef struct {
  int       state;
  double    clock;
  int       species;
  Phoenix   phoenix;
  Pigeon    pigeon;
  Owl       owl;
  Swallow   swallow;        /* only one is live; they are a few hundred bytes */
  double    rain;           /* 0 dry .. 1 downpour */
  double    rain_debt;      /* fractional drops carried between frames */
  int       mode;           /* SM_DELIVER or SM_DEPART */
  Vec       origin;         /* where a departing bird collects the letter */
  Particles p;
  Letter    letter;
  Scroll    scroll;
  Vec       landing, perch;
  Vec       touch;            /* pigeon: where the feet first hit */
  double    ground;           /* pigeon: world y the feet rest on */
  Vec       pyre;             /* where the bird went out */
  double    smoulder;         /* seconds of smoke still to come off it */
  int       done;
  int       sw, sh;
} Scene;

void scene_start(Scene *s, int sw, int sh, const char *text, const char *from,
                 int species);
/* origin is where a departing bird picks the letter up, in scene units */
void scene_start_ex(Scene *s, int sw, int sh, const char *text, const char *from,
                    int species, int mode, double ox, double oy);
int  scene_species_from_name(const char *name);
Flyer *scene_flyer(Scene *s);
void scene_update(Scene *s, double dt);
void scene_draw(Scene *s, cairo_t *cr);
void scene_free(Scene *s);
/* region that changed this frame, clamped to the screen */
void scene_bbox(Scene *s, int *x, int *y, int *w, int *h);
const char *scene_state_name(int state);

#endif /* AVIARY_H */
