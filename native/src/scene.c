#include "aviary.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const char *STATE_NAMES[] = {
  "enter", "settle", "drop", "watch", "burn", "ash",
  "land", "walk", "setdown", "stay", "takeoff",
  "charge", "strike", "tumble", "sprawl", "shake",
  "pickup",
  "reading", "done"
};

int scene_species_from_name(const char *name) {
  if (!name) return BIRD_PHOENIX;
  if (!strcmp(name, "pigeon") || !strcmp(name, "dove")) return BIRD_PIGEON;
  if (!strcmp(name, "owl") || !strcmp(name, "errol")) return BIRD_OWL;
  if (!strcmp(name, "swallow") || !strcmp(name, "rain")) return BIRD_SWALLOW;
  return BIRD_PHOENIX;
}

/* Both species live in the Scene; only one is ever running. They are a few
 * hundred bytes each, which is cheaper than the indirection would be. */
Flyer *scene_flyer(Scene *s) {
  if (s->species == BIRD_PIGEON)  return &s->pigeon.f;
  if (s->species == BIRD_OWL)     return &s->owl.f;
  if (s->species == BIRD_SWALLOW) return &s->swallow.f;
  return &s->phoenix.f;
}

static void bird_update(Scene *s, double dt) {
  if (s->species == BIRD_PIGEON)       pigeon_update(&s->pigeon, dt, &s->p);
  else if (s->species == BIRD_OWL)     owl_update(&s->owl, dt, &s->p);
  else if (s->species == BIRD_SWALLOW) swallow_update(&s->swallow, dt, &s->p);
  else if (!s->phoenix.consumed)       phoenix_update(&s->phoenix, dt, &s->p);
}

static void bird_draw(Scene *s, cairo_t *cr) {
  if (s->species == BIRD_PIGEON)       pigeon_draw(&s->pigeon, cr);
  else if (s->species == BIRD_OWL)     owl_draw(&s->owl, cr);
  else if (s->species == BIRD_SWALLOW) swallow_draw(&s->swallow, cr);
  else                                 phoenix_draw(&s->phoenix, cr);
}

static void bird_bbox(Scene *s, double *x0, double *y0, double *x1, double *y1) {
  if (s->species == BIRD_PIGEON)       pigeon_bbox(&s->pigeon, x0, y0, x1, y1);
  else if (s->species == BIRD_OWL)     owl_bbox(&s->owl, x0, y0, x1, y1);
  else if (s->species == BIRD_SWALLOW) swallow_bbox(&s->swallow, x0, y0, x1, y1);
  else if (!s->phoenix.consumed)       phoenix_bbox(&s->phoenix, x0, y0, x1, y1);
}

const char *scene_state_name(int state) {
  if (state < 0 || state > S_DONE) return "?";
  return STATE_NAMES[state];
}

/* Comes in off any edge, aimed inward but never straight at the target, so the
 * approach reads as a curve rather than a ruler line. */
static void pick_entry(Scene *s, double *ox, double *oy, Vec *mid,
                       double *ovx, double *ovy) {
  double pad = 170 * av_world();
  double x = 0, y = 0;
  /* sides and top are likelier: birds rarely arrive from underneath */
  int edge = s->species == BIRD_OWL ? (av_rand() < 0.5 ? 0 : 1) : av_rand_int(7);
  if (edge == 0 || edge == 4) { x = -pad;        y = av_rand_range(s->sh * 0.12, s->sh * 0.78); }
  else if (edge == 1 || edge == 5) { x = s->sw + pad; y = av_rand_range(s->sh * 0.12, s->sh * 0.78); }
  else if (edge == 2 || edge == 3) { x = av_rand_range(s->sw * 0.1, s->sw * 0.9); y = -pad; }
  else { x = av_rand_range(s->sw * 0.1, s->sw * 0.9); y = s->sh + pad; }

  double dx = s->perch.x - x, dy = s->perch.y - y;
  double d = hypot(dx, dy);
  if (d < 1e-6) d = 1;
  double ux = dx / d, uy = dy / d;

  double side = av_rand() < 0.5 ? 1 : -1;
  double swing = av_rand_range(0.22, 0.42) * d * side;
  mid->x = x + ux * d * 0.55 - uy * swing;
  mid->y = y + uy * d * 0.55 + ux * swing;

  double mdx = mid->x - x, mdy = mid->y - y;
  double md = hypot(mdx, mdy);
  if (md < 1e-6) md = 1;
  *ox = x; *oy = y;
  *ovx = mdx / md * 300 * av_world();
  *ovy = mdy / md * 300 * av_world();
}

void scene_start(Scene *s, int sw, int sh, const char *text, const char *from,
                 int species) {
  scene_start_ex(s, sw, sh, text, from, species, SM_DELIVER, 0, 0);
}

void scene_start_ex(Scene *s, int sw, int sh, const char *text, const char *from,
                    int species, int mode, double ox, double oy) {
  memset(s, 0, sizeof(*s));
  s->mode = mode;
  s->sw = sw;
  s->sh = sh;
  s->state = S_ENTER;
  s->species = species;

  /* Everything below is authored against a reference 800-unit-tall scene. In
   * pixel-art mode the scene is only a few hundred units tall, so speeds and
   * sizes must come down with it or the bird crosses the screen in a blink. */
  av_set_world(sh / 800.0);
  av_set_water_floor(0);
  double W = av_world();

  particles_init(&s->p, 4200);
  letter_plan(&s->letter, sw, sh);

  /* Each bird hands over a different object, in the state it arrived in.
   * None of them leave on a timer — every letter waits to be let go. */
  switch (species) {
    case BIRD_PHOENIX: s->letter.style = LS_BURNT;  break;  /* scorched */
    case BIRD_PIGEON:  s->letter.style = LS_BRIGHT; break;  /* clean and bright */
    case BIRD_SWALLOW: s->letter.style = LS_WET;    break;  /* the ink has run */
    case BIRD_OWL:     s->letter.style = LS_DIRTY;  break;  /* creased, smudged */
  }
  s->letter.auto_t = 0;

  s->landing.x = s->letter.x + s->letter.w / 2;
  s->landing.y = s->letter.y + (species == BIRD_PIGEON ? 5 : 18);
  s->ground    = s->letter.y;          /* the pigeon stands on the paper's edge */

  /* Sized against oneko's 32x32 cat. The pigeon is a heavier bird, so it gets
   * a little more of the screen than the phoenix does. */
  double scale = (sh / 800.0) * (av_pixel_mode()
                   ? (species == BIRD_PIGEON ? 0.62 : species == BIRD_OWL ? 0.60
                      : species == BIRD_SWALLOW ? 0.58 : 0.55)
                   : 1.35);

  if (species == BIRD_OWL) {
    /* He crosses the screen high, hits the far side, and falls the whole way
     * to the floor. The letter ends up down there with him. */
    s->ground   = sh * 0.95;   /* the actual floor, not most of the way down */
    s->perch.x  = sw * 0.5;
    s->perch.y  = sh * 0.20;
    s->touch.x  = 0;                 /* filled in when he actually hits */
    s->touch.y  = s->ground;
  } else if (species == BIRD_SWALLOW) {
    /* It has been raining where he came from, not here. The evidence is the
     * bird: soaked, dripping the whole way in, and having to shake it off
     * before he can do anything. Water he sheds splashes on the ground. */
    s->rain = 0.0;
    av_set_water_floor(s->letter.y + 2);
    s->ground  = s->letter.y;
    s->touch.x = s->landing.x + av_rand_sym(20) * W;
    s->touch.y = s->ground;
    s->perch.x = s->touch.x + av_rand_sym(60) * W;
    s->perch.y = s->ground - 170 * W;
  } else if (species == BIRD_PIGEON) {
    /* touch down off to one side, so there is a walk to watch */
    double side = av_rand() < 0.5 ? -1 : 1;
    s->touch.x = s->landing.x + side * av_rand_range(46, 78) * W;
    s->touch.y = s->ground;
    s->perch.x = s->touch.x - side * 34 * W;
    s->perch.y = s->ground - 150 * W;
  } else {
    s->perch.x = s->landing.x + av_rand_sym(26);
    s->perch.y = s->landing.y - 112 * av_clamp(W, 0.4, 1.6);
  }

  /* A departing bird ignores all of that: it comes to the spot you typed at,
   * takes what is there, and leaves. */
  if (mode == SM_DEPART) {
    s->origin.x = av_clamp(ox, 30 * W, sw - 30 * W);
    s->origin.y = av_clamp(oy, 30 * W, sh - 24 * W);
    s->perch = s->origin;
    s->ground = s->origin.y;
    s->touch = s->origin;
    s->landing = s->origin;
    s->scroll.alive = 1;
    s->scroll.x = s->origin.x;
    s->scroll.y = s->origin.y;
    s->scroll.appear = 0;
    s->scroll.rot = av_rand_sym(0.12);
  }

  double x, y, vx, vy;
  Vec mid;
  pick_entry(s, &x, &y, &mid, &vx, &vy);

  Flyer *f;
  if (species == BIRD_SWALLOW) {
    swallow_init(&s->swallow, x, y, scale);
    f = &s->swallow.f;
  } else if (species == BIRD_OWL) {
    owl_init(&s->owl, x, y, scale);
    f = &s->owl.f;
  } else if (species == BIRD_PIGEON) {
    pigeon_init(&s->pigeon, x, y, scale);
    f = &s->pigeon.f;
  } else {
    phoenix_init(&s->phoenix, x, y, scale);
    f = &s->phoenix.f;
  }

  f->vx = vx;
  f->vy = vy;
  f->heading = atan2(vy, fabs(vx));
  f->facing = vx >= 0 ? 1 : -1;
  f->facing_blend = f->facing;

  f->nwp = 2;
  f->wp[0].p = mid;
  f->wp[0].radius = 150;
  f->wp[1].p = s->perch;
  f->wp[1].slow = 210;
  f->wp[1].radius = 26;

  if (species == BIRD_PHOENIX) {
    /* prime the tail so it does not snap into place on the first frame */
    s->phoenix.ntrail = 1;
    s->phoenix.trail[0] = flyer_to_world(f, -11.6, -0.6);
  }

  if (mode == SM_DEPART) {
    /* it arrives empty-handed and leaves with something */
    s->phoenix.carrying = 0;
    s->pigeon.carrying = 0;
    s->owl.carrying = 0;
    s->swallow.carrying = 0;
    s->swallow.wet = 0;                 /* it did not come out of any weather */
    f->wp[1].p = s->origin;
    f->wp[1].slow = 190 * W;
  }

  snprintf(s->letter.text, sizeof(s->letter.text), "%s", text ? text : "");
  snprintf(s->letter.from, sizeof(s->letter.from), "%s", from ? from : "");
}

void scene_free(Scene *s) { particles_free(&s->p); }

static void release_scroll(Scene *s) {
  double W = av_world();
  Vec p;
  Flyer *f = scene_flyer(s);

  if (s->species == BIRD_SWALLOW) {
    p = swallow_letter_point(&s->swallow);
    s->swallow.carrying = 0;
  } else if (s->species == BIRD_OWL) {
    p = owl_letter_point(&s->owl);
    s->owl.carrying = 0;
  } else if (s->species == BIRD_PIGEON) {
    p = pigeon_capsule_point(&s->pigeon);
    s->pigeon.carrying = 0;
  } else {
    p = phoenix_release_point(&s->phoenix);
    s->phoenix.carrying = 0;
  }

  s->scroll.alive = 1;
  s->scroll.x = p.x;
  s->scroll.y = p.y;
  s->scroll.vx = f->vx * 0.3 + av_rand_sym(14) * W;
  s->scroll.vy = (s->species == BIRD_PHOENIX ? 40 : 6) * W;
  s->scroll.rot = av_rand_range(-0.3, 0.3);
  s->scroll.rot_v = av_rand_sym(s->species == BIRD_PHOENIX ? 2.4 : 0.9);

  if (s->species == BIRD_PHOENIX)
    for (int i = 0; i < 8; i++)
      p_ember(&s->p, p.x, p.y, 0, 0, 40, av_rand_range(0.7, 1.6), av_rand_range(0.4, 0.9));
}

/* The bird does not detonate. The fire finishes eating it and what is left
 * goes up as smoke: a column off the pyre, ash drifting down, a few last coals
 * riding the heat. Quiet. */
static void expire_to_smoke(Scene *s) {
  double W = av_world();
  double x = s->phoenix.f.x;
  double y = s->phoenix.f.y + s->phoenix.f.bob;

  s->pyre.x = x;
  s->pyre.y = y;
  s->smoulder = 2.0;

  for (int i = 0; i < 26; i++)
    p_ember(&s->p, x + av_rand_sym(6) * W, y + av_rand_sym(5) * W,
            av_rand_sym(18) * W, -av_rand_range(20, 70) * W,
            14, av_rand_range(0.6, 1.6), av_rand_range(0.9, 2.0));

  for (int i = 0; i < 46; i++)
    p_ash(&s->p, x + av_rand_sym(9) * W, y + av_rand_sym(7) * W,
          av_rand_sym(22) * W, -av_rand_range(8, 46) * W,
          av_rand_range(2.4, 5.0), av_rand() < 0.3 ? av_rand_range(0.3, 0.8) : 0);

  for (int i = 0; i < 22; i++)
    p_smoke(&s->p, x + av_rand_sym(7) * W, y + av_rand_sym(5) * W,
            av_rand_range(2.5, 5.5), av_rand_range(0.18, 0.34));

  letter_scorch(&s->letter);
  for (int i = 0; i < 16; i++)
    p_ash(&s->p, s->letter.x + av_rand() * s->letter.w,
          s->letter.y - av_rand_range(0, 30) * W,
          av_rand_sym(14) * W, av_rand_range(3, 18) * W,
          av_rand_range(2.2, 4.4), 0);
}

static void smoulder(Scene *s, double dt) {
  if (s->smoulder <= 0) return;
  s->smoulder -= dt;
  double k = av_clamp(s->smoulder / 2.0, 0, 1);
  double W = av_world();
  double x = s->pyre.x, y = s->pyre.y;

  if (av_rand() < 0.75 + 0.25 * k)
    p_smoke(&s->p, x + av_rand_sym(4 + 7 * (1 - k)) * W, y - av_rand_range(0, 6) * W,
            av_rand_range(2.0, 4.5) * (0.7 + 0.5 * k),
            av_rand_range(0.14, 0.30) * (0.4 + 0.6 * k));

  if (av_rand() < 0.30 * k)
    p_ember(&s->p, x + av_rand_sym(4) * W, y + av_rand_sym(3) * W,
            av_rand_sym(10) * W, -av_rand_range(15, 45) * W,
            10, av_rand_range(0.5, 1.1), av_rand_range(0.5, 1.2));

  if (av_rand() < 0.22 * k)
    p_ash(&s->p, x + av_rand_sym(10) * W, y + av_rand_sym(6) * W,
          av_rand_sym(16) * W, -av_rand_range(5, 30) * W,
          av_rand_range(2.0, 4.2), 0);
}

static void update_scroll(Scene *s, double dt) {
  Scroll *c = &s->scroll;
  if (!c->alive) return;
  c->vy += 900 * av_world() * dt;
  c->vx *= exp(-1.2 * dt);
  c->x += c->vx * dt;
  c->y += c->vy * dt;
  c->rot += c->rot_v * dt;
  c->rot_v *= exp(-1.4 * dt);

  if (c->y >= s->landing.y) {
    c->alive = 0;
    /* letter_show clears the struct, so the strings must leave it first */
    char text[LETTER_MAX_TEXT], from[LETTER_MAX_FROM];
    memcpy(text, s->letter.text, sizeof(text));
    memcpy(from, s->letter.from, sizeof(from));
    letter_show(&s->letter, text, from);

    /* The owl is standing on the floor, so his letter rests ON the floor: its
     * bottom edge sits on the ground line and it unrolls down to meet it.
     * Every other bird stands on the letter's top edge instead. */
    if (s->species == BIRD_OWL)
      s->letter.y = fmax(8.0, s->ground - s->letter.h);

    for (int i = 0; i < 10; i++)
      p_smoke(&s->p, s->landing.x + av_rand_sym(30), s->letter.y + 8,
              av_rand_range(2.0, 4.5), 0.08);
    if (s->species == BIRD_PHOENIX)
      for (int i = 0; i < 12; i++)
        p_spark(&s->p, s->landing.x + av_rand_sym(20), s->letter.y + 6, 90);
  }
}

/* dust kicked off whatever the bird just hit or left */
static void scuff(Scene *s, double x, double y, int n) {
  double W = av_world();
  for (int i = 0; i < n; i++)
    p_ash(&s->p, x + av_rand_sym(9) * W, y,
          av_rand_sym(34) * W, -av_rand_range(8, 34) * W,
          av_rand_range(0.5, 1.3), 0);
}

/* ---- the phoenix: hover, deliver, burn -------------------------------- */

static void update_phoenix(Scene *s, double dt) {
  Phoenix *b = &s->phoenix;
  Flyer *f = &b->f;
  double W = av_world();

  switch (s->state) {
    case S_ENTER: {
      double d = hypot(f->x - s->perch.x, f->y - s->perch.y);
      if (d < 46 && flyer_speed(f) < 150) { s->state = S_SETTLE; s->clock = 0; }
      break;
    }
    case S_SETTLE:
      f->hover = 1;
      f->wander_gain = 90 * W;
      f->pitch = av_damp(f->pitch, -0.22, 4, dt);
      f->max_speed = av_damp(f->max_speed, 150 * W, 3, dt);
      flyer_force(f, sin(f->t * 1.7) * 130 * W, cos(f->t * 2.6) * 90 * W);
      if (s->clock > 0.95) { s->state = S_DROP; s->clock = 0; release_scroll(s); }
      break;

    case S_DROP:
      f->hover = 1;
      flyer_force(f, sin(f->t * 1.7) * 110 * W, (cos(f->t * 2.6) * 80 - 60) * W);
      update_scroll(s, dt);
      if (!s->scroll.alive && s->clock > 0.25) { s->state = S_WATCH; s->clock = 0; }
      break;

    case S_WATCH:
      f->hover = 1;
      flyer_force(f, sin(f->t * 1.4) * 90 * W, (cos(f->t * 2.1) * 70 - 90) * W);
      if (s->clock > 0.9) { s->state = S_BURN; s->clock = 0; phoenix_ignite(b); }
      break;

    case S_BURN:
      f->hover = 1;
      f->wander_gain = 40 * W;
      if (!b->flashed && b->burn_t >= 1.95) { b->flashed = 1; expire_to_smoke(s); }
      if (b->consumed && s->clock > 2.4) { s->state = S_ASH; s->clock = 0; }
      break;

    case S_ASH:
      if ((s->clock > 3.4 && s->smoulder <= 0) || s->p.n < 24) {
        s->state = s->letter.open ? S_READING : S_DONE;
        s->clock = 0;
      }
      break;

    case S_READING:
      if (!s->letter.open) { s->state = S_DONE; s->clock = 0; }
      break;

    case S_DONE:
      if (s->clock > 0.9 && s->p.n == 0) s->done = 1;
      break;
  }
  smoulder(s, dt);
}

/* ---- the pigeon: land, walk, hand it over, wait, leave ---------------- */

static void update_pigeon(Scene *s, double dt) {
  Pigeon *b = &s->pigeon;
  Flyer *f = &b->f;
  double W = av_world();

  switch (s->state) {
    case S_ENTER: {
      double d = hypot(f->x - s->perch.x, f->y - s->perch.y);
      if (d < 60 * W || s->clock > 6.0) {
        s->state = S_LAND;
        s->clock = 0;
        f->nwp = 1;
        f->wp[0].p.x = s->touch.x;
        f->wp[0].p.y = s->ground - b->stand_h * f->scale;
        f->wp[0].slow = 140 * W;
        f->wp[0].radius = 4 * W;
        f->wander_gain = 30 * W;
      }
      break;
    }

    case S_LAND: {
      double ty = s->ground - b->stand_h * f->scale;
      double d = hypot(f->x - s->touch.x, f->y - ty);
      /* the flare comes on as the ground arrives */
      b->flare = av_clamp(1.0 - d / (110 * W), 0, 1);
      f->max_speed = av_damp(f->max_speed, av_lerp(200, 46, b->flare) * W, 4, dt);

      if ((d < 7 * W && flyer_speed(f) < 60 * W) || s->clock > 5.0) {
        pigeon_touch_down(b, s->ground);
        scuff(s, f->x, s->ground, 7);
        s->state = S_WALK;
        s->clock = 0;
        pigeon_walk_to(b, s->landing.x);
      }
      break;
    }

    case S_WALK:
      if (!pigeon_walking(b) || s->clock > 6.0) {
        s->state = S_SETDOWN;
        s->clock = 0;
        b->capsule_drop = 0.001;      /* head goes down to the leg */
      }
      break;

    case S_SETDOWN:
      b->capsule_drop = av_clamp(s->clock / 1.1, 0.001, 1.0);
      if (b->carrying && s->clock > 0.55) release_scroll(s);
      update_scroll(s, dt);
      if (s->clock > 1.25) {
        b->capsule_drop = 0;
        s->state = S_STAY;
        s->clock = 0;
      }
      break;

    case S_STAY:
      /* it waits to be noticed, then goes */
      if (!s->letter.open || s->clock > 26.0) { s->state = S_TAKEOFF; s->clock = 0; }
      break;

    case S_TAKEOFF:
      if (b->grounded) {
        b->crouch = av_clamp(s->clock / 0.26, 0, 1);
        if (s->clock > 0.26) {
          pigeon_launch(b);
          scuff(s, f->x, s->ground, 10);
          f->nwp = 1;
          f->wp[0].p.x = f->x + f->facing * s->sw * 0.9;
          f->wp[0].p.y = -120 * W;
          f->wp[0].radius = 30;
          f->wp[0].slow = 0;
          f->max_speed = 300 * W;
          f->wander_gain = 90 * W;
        }
      } else if (f->y < -70 * W || f->x < -90 * W || f->x > s->sw + 90 * W) {
        s->state = S_DONE;
        s->clock = 0;
      }
      break;

    case S_READING:
      if (!s->letter.open) { s->state = S_DONE; s->clock = 0; }
      break;

    case S_DONE:
      if (s->clock > 0.6 && s->p.n == 0) s->done = 1;
      break;
  }
}

/* ---- the owl: he does not so much deliver as arrive ------------------- */

static void update_owl(Scene *s, double dt) {
  Owl *b = &s->owl;
  Flyer *f = &b->f;
  double W = av_world();

  switch (s->state) {
    case S_ENTER: {
      /* head for the middle at strike height, wobbling all the way */
      double d = fabs(f->x - s->perch.x);
      if (d < s->sw * 0.18 || s->clock > 5.0) {
        s->state = S_CHARGE;
        s->clock = 0;
        f->nwp = 1;
        f->wp[0].p.x = f->facing > 0 ? s->sw + 60 * W : -60 * W;
        f->wp[0].p.y = s->perch.y;
        f->wp[0].slow = 0;
        f->wp[0].radius = 10;
        f->wander_gain = 60 * W;      /* he has committed to something */
      }
      break;
    }

    case S_CHARGE: {
      /* no slowing down, no turn. He has not seen the wall. */
      f->max_speed = av_damp(f->max_speed, 300 * W, 2.5, dt);
      double edge = f->facing > 0 ? s->sw - 3 : 3;
      if ((f->facing > 0 && f->x >= edge) || (f->facing < 0 && f->x <= edge) ||
          s->clock > 6.0) {
        f->x = edge;
        s->touch.x = f->x;
        owl_strike(b, &s->p, f->facing > 0 ? 1.0 : -1.0);
        s->state = S_STRIKE;
        s->clock = 0;
      }
      break;
    }

    case S_STRIKE:
      if (s->clock > 0.22) { s->state = S_TUMBLE; s->clock = 0; }
      break;

    case S_TUMBLE: {
      double floor_y = s->ground - b->stand_h * f->scale;
      if (f->y >= floor_y || s->clock > 6.0) {
        owl_touch_down(b, s->ground);
        s->touch.x = f->x;
        /* dust and a last few feathers where he came down */
        for (int i = 0; i < 8; i++)
          p_ash(&s->p, f->x + av_rand_sym(12) * W, s->ground,
                av_rand_sym(50) * W, -av_rand_range(10, 50) * W,
                av_rand_range(0.5, 1.2), 0);
        for (int i = 0; i < 6; i++)
          p_feather(&s->p, f->x + av_rand_sym(14) * W, s->ground - av_rand_range(0, 10) * W,
                    av_rand_sym(60) * W, -av_rand_range(10, 60) * W, av_rand_range(0, 0.9));
        s->state = S_SPRAWL;
        s->clock = 0;
      }
      break;
    }

    case S_SPRAWL:
      /* flat on his back, working out what happened */
      if (s->clock > 1.25) {
        b->sprawl = 0;                /* rights himself */
        s->state = S_SHAKE;
        s->clock = 0;
      }
      break;

    case S_SHAKE:
      if (s->clock < 0.05) { b->shake = 1; b->shake_t = 0; }
      if (b->shake == 0 && s->clock > 0.4) {
        /* The letter ends up where he did, not where he was aiming — down on
         * the floor, and beside him rather than under him, since he is
         * standing on the floor too. */
        double lw = s->letter.w;
        s->letter.x = f->x > s->sw * 0.5
                        ? av_clamp(f->x - lw - 14, 8, s->sw - lw - 8)
                        : av_clamp(f->x + 14, 8, s->sw - lw - 8);
        s->landing.x = s->letter.x + lw / 2;
        s->landing.y = s->ground - 3;
        s->state = S_SETDOWN;
        s->clock = 0;
        b->head_dip = 0.001;
      }
      break;

    case S_SETDOWN:
      b->head_dip = av_clamp(s->clock / 1.0, 0.001, 1.0);
      if (b->carrying && s->clock > 0.5) release_scroll(s);
      update_scroll(s, dt);
      if (s->clock > 1.2) {
        b->head_dip = 0;
        s->state = S_STAY;
        s->clock = 0;
      }
      break;

    case S_STAY:
      if (!s->letter.open || s->clock > 24.0) { s->state = S_TAKEOFF; s->clock = 0; }
      break;

    case S_TAKEOFF:
      if (b->grounded) {
        b->crouch = av_clamp(s->clock / 0.35, 0, 1);
        if (s->clock > 0.35) {
          owl_launch(b);
          for (int i = 0; i < 6; i++)
            p_feather(&s->p, f->x + av_rand_sym(10) * W, s->ground,
                      av_rand_sym(40) * W, -av_rand_range(20, 70) * W,
                      av_rand_range(0, 0.9));
          f->nwp = 1;
          f->wp[0].p.x = f->x + f->facing * s->sw * 0.9;
          f->wp[0].p.y = -140 * W;
          f->wp[0].radius = 30;
          f->wp[0].slow = 0;
          f->max_speed = 230 * W;
          f->wander_gain = 150 * W;
        }
      } else if (f->y < -90 * W || f->x < -110 * W || f->x > s->sw + 110 * W) {
        s->state = S_DONE;
        s->clock = 0;
      }
      break;

    case S_READING:
      if (!s->letter.open) { s->state = S_DONE; s->clock = 0; }
      break;

    case S_DONE:
      if (s->clock > 0.6 && s->p.n == 0) s->done = 1;
      break;
  }
}

/* ---- the swallow: in out of the weather ------------------------------- */

static void update_swallow(Scene *s, double dt) {
  Swallow *b = &s->swallow;
  Flyer *f = &b->f;
  double W = av_world();

  switch (s->state) {
    case S_ENTER: {
      double d = hypot(f->x - s->perch.x, f->y - s->perch.y);
      if (d < 70 * W || s->clock > 5.0) {
        s->state = S_LAND;
        s->clock = 0;
        f->nwp = 1;
        f->wp[0].p.x = s->touch.x;
        f->wp[0].p.y = s->ground - b->stand_h * f->scale;
        f->wp[0].slow = 150 * W;
        f->wp[0].radius = 4 * W;
        f->wander_gain = 60 * W;
      }
      break;
    }

    case S_LAND: {
      double ty = s->ground - b->stand_h * f->scale;
      double d = hypot(f->x - s->touch.x, f->y - ty);
      b->flare = av_clamp(1.0 - d / (120 * W), 0, 1);
      f->max_speed = av_damp(f->max_speed, av_lerp(300, 55, b->flare) * W, 5, dt);
      if ((d < 7 * W && flyer_speed(f) < 70 * W) || s->clock > 5.0) {
        swallow_touch_down(b, s->ground);
        s->state = S_SHAKE;
        s->clock = 0;
      }
      break;
    }

    case S_SHAKE:
      /* it cannot do anything else until it has got the water off */
      if (s->clock < 0.05) { b->shake = 1; b->shake_t = 0; }
      if (b->shake == 0 && s->clock > 0.4) {
        s->state = S_SETDOWN;
        s->clock = 0;
        b->head_dip = 0.001;
      }
      break;

    case S_SETDOWN:
      b->head_dip = av_clamp(s->clock / 0.9, 0.001, 1.0);
      if (b->carrying && s->clock > 0.45) release_scroll(s);
      update_scroll(s, dt);
      if (s->clock > 1.1) {
        b->head_dip = 0;
        s->state = S_STAY;
        s->clock = 0;
      }
      break;

    case S_STAY:
      if (!s->letter.open || s->clock > 24.0) { s->state = S_TAKEOFF; s->clock = 0; }
      break;

    case S_TAKEOFF:
      if (b->grounded) {
        b->crouch = av_clamp(s->clock / 0.18, 0, 1);
        if (s->clock > 0.18) {
          swallow_launch(b);
          f->nwp = 1;
          f->wp[0].p.x = f->x + f->facing * s->sw * 1.1;
          f->wp[0].p.y = -130 * W;
          f->wp[0].radius = 30;
          f->wp[0].slow = 0;
          f->max_speed = 380 * W;
          f->wander_gain = 300 * W;
        }
      } else if (f->y < -80 * W || f->x < -110 * W || f->x > s->sw + 110 * W) {
        s->state = S_DONE;
        s->clock = 0;
      }
      break;

    case S_READING:
      if (!s->letter.open) { s->state = S_DONE; s->clock = 0; }
      break;

    case S_DONE:
      if (s->clock > 0.6 && s->p.n == 0) s->done = 1;
      break;
  }
}

/* The phoenix's letter goes the same way the phoenix did. */
static void burn_letter(Scene *s, double dt) {
  Letter *l = &s->letter;
  if (!l->burning) return;
  double W = av_world();
  double y = letter_burn_y(l);
  int n = 2 + av_rand_int(3);
  for (int i = 0; i < n; i++) {
    double x = l->x + av_rand() * l->w;
    p_fire(&s->p, x, y + av_rand_sym(3) * W, av_rand_sym(24) * W, 0,
           av_rand_range(1.4, 3.2), av_rand_range(0.24, 0.5), av_rand_range(160, 300));
  }
  if (av_rand() < 0.6)
    p_ash(&s->p, l->x + av_rand() * l->w, y, av_rand_sym(22) * W,
          -av_rand_range(10, 50) * W, av_rand_range(1.6, 3.4),
          av_rand() < 0.3 ? av_rand_range(0.2, 0.7) : 0);
  if (av_rand() < 0.35)
    p_smoke(&s->p, l->x + av_rand() * l->w, y - 4 * W,
            av_rand_range(2.0, 4.0), av_rand_range(0.10, 0.20));
  (void)dt;
}

/* ---- taking one away -------------------------------------------------- */
/* Species-neutral helpers, so the departure runs the same for every bird and
 * only the anatomy differs. */

static void bird_set_carrying(Scene *s, int on) {
  switch (s->species) {
    case BIRD_PIGEON:  s->pigeon.carrying  = on; break;
    case BIRD_OWL:     s->owl.carrying     = on; break;
    case BIRD_SWALLOW: s->swallow.carrying = on; break;
    default:           s->phoenix.carrying = on; break;
  }
}

static void bird_set_dip(Scene *s, double v) {
  switch (s->species) {
    case BIRD_PIGEON:  s->pigeon.capsule_drop = v; break;
    case BIRD_OWL:     s->owl.head_dip        = v; break;
    case BIRD_SWALLOW: s->swallow.head_dip    = v; break;
    default: break;
  }
}

static double bird_stand_h(Scene *s) {
  switch (s->species) {
    case BIRD_PIGEON:  return s->pigeon.stand_h;
    case BIRD_OWL:     return s->owl.stand_h;
    case BIRD_SWALLOW: return s->swallow.stand_h;
    default:           return 0;
  }
}

static int bird_can_land(Scene *s) { return s->species != BIRD_PHOENIX; }

static int bird_grounded(Scene *s) {
  switch (s->species) {
    case BIRD_PIGEON:  return s->pigeon.grounded;
    case BIRD_OWL:     return s->owl.grounded;
    case BIRD_SWALLOW: return s->swallow.grounded;
    default:           return 0;
  }
}

static void bird_land(Scene *s, double ground_y) {
  switch (s->species) {
    case BIRD_PIGEON:  pigeon_touch_down(&s->pigeon, ground_y);
                       pigeon_walk_to(&s->pigeon, s->pigeon.f.x); break;
    case BIRD_OWL:     owl_touch_down(&s->owl, ground_y); s->owl.sprawl = 0; break;
    case BIRD_SWALLOW: swallow_touch_down(&s->swallow, ground_y); break;
    default: break;
  }
}

static void bird_launch(Scene *s) {
  switch (s->species) {
    case BIRD_PIGEON:  pigeon_launch(&s->pigeon); break;
    case BIRD_OWL:     owl_launch(&s->owl); break;
    case BIRD_SWALLOW: swallow_launch(&s->swallow); break;
    default: break;
  }
}

static void update_depart(Scene *s, double dt) {
  Flyer *f = scene_flyer(s);
  double W = av_world();

  /* the message rolls itself up while the bird is still on its way */
  if (s->scroll.alive && s->scroll.appear < 1)
    s->scroll.appear = fmin(1.0, s->scroll.appear + dt / 0.45);

  switch (s->state) {
    case S_ENTER: {
      double ty = bird_can_land(s) ? s->origin.y - bird_stand_h(s) * f->scale
                                   : s->origin.y - 26 * W;
      double d = hypot(f->x - s->origin.x, f->y - ty);
      f->max_speed = av_damp(f->max_speed, 150 * W, 2.5, dt);
      if (d < 12 * W || s->clock > 6.0) {
        if (bird_can_land(s)) bird_land(s, s->origin.y);
        else { f->hover = 1; f->wander_gain = 70 * W; }
        s->state = S_PICKUP;
        s->clock = 0;
        bird_set_dip(s, 0.001);
      }
      break;
    }

    case S_PICKUP:
      if (!bird_can_land(s)) {
        f->hover = 1;
        flyer_force(f, sin(f->t * 1.6) * 90 * W, (cos(f->t * 2.3) * 70 - 40) * W);
      }
      bird_set_dip(s, av_clamp(s->clock / 0.9, 0.001, 1.0));
      /* it takes the letter and ties it on */
      if (s->scroll.alive && s->clock > 0.55) {
        s->scroll.alive = 0;
        bird_set_carrying(s, 1);
      }
      if (s->clock > 1.35) {
        bird_set_dip(s, 0);
        s->state = S_TAKEOFF;
        s->clock = 0;
      }
      break;

    case S_TAKEOFF:
      if (bird_can_land(s) && bird_grounded(s)) {
        if (s->clock > 0.24) {
          bird_launch(s);
          f->nwp = 1;
          f->wp[0].p.x = f->x + f->facing * s->sw * 1.1;
          f->wp[0].p.y = -140 * W;
          f->wp[0].radius = 30;
          f->wp[0].slow = 0;
          f->max_speed = 320 * W;
          f->wander_gain = 160 * W;
        }
      } else if (!bird_can_land(s) && s->clock < 0.05) {
        f->hover = 0;
        f->nwp = 1;
        f->wp[0].p.x = f->x + f->facing * s->sw * 1.1;
        f->wp[0].p.y = -140 * W;
        f->wp[0].radius = 30;
        f->wp[0].slow = 0;
        f->max_speed = 330 * W;
        f->wander_gain = 200 * W;
      }
      if (f->y < -90 * W || f->x < -120 * W || f->x > s->sw + 120 * W) {
        s->state = S_DONE;
        s->clock = 0;
      }
      break;

    case S_DONE:
      if (s->clock > 0.5 && s->p.n == 0) s->done = 1;
      break;
  }
}

void scene_update(Scene *s, double dt) {
  s->clock += dt;
  burn_letter(s, dt);

  if (s->mode == SM_DEPART)            update_depart(s, dt);
  else if (s->species == BIRD_PIGEON)  update_pigeon(s, dt);
  else if (s->species == BIRD_OWL)     update_owl(s, dt);
  else if (s->species == BIRD_SWALLOW) update_swallow(s, dt);
  else                                 update_phoenix(s, dt);

  /* Once the scene is over the bird is gone; keeping it ticking means it goes
   * on shedding feathers off-screen and the particle count never reaches zero. */
  if (s->state != S_DONE) bird_update(s, dt);
  particles_update(&s->p, dt);
  letter_update(&s->letter, dt);
}

static void draw_scroll(Scene *s, cairo_t *cr) {
  Scroll *c = &s->scroll;
  if (!c->alive) return;
  double grow = s->mode == SM_DEPART ? av_ease_out_cubic(av_clamp(c->appear, 0, 1)) : 1.0;
  if (grow < 0.02) return;
  cairo_save(cr);
  cairo_translate(cr, c->x, c->y);
  cairo_rotate(cr, c->rot);
  cairo_scale(cr, 1.7 * grow, 1.7 * av_lerp(0.25, 1.0, grow));
  cairo_set_source_rgb(cr, 0.910, 0.843, 0.678);
  av_round_rect(cr, -5.2, -1.7, 10.4, 3.4, 1.6);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, 0.796, 0.706, 0.533);
  av_round_rect(cr, -5.8, -2.2, 1.7, 4.4, 0.85);
  cairo_fill(cr);
  av_round_rect(cr, 4.1, -2.2, 1.7, 4.4, 0.85);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, 0.553, 0.071, 0.125);
  cairo_arc(cr, 0, 0.1, 1.3, 0, TAU);
  cairo_fill(cr);
  cairo_restore(cr);
}

void scene_draw(Scene *s, cairo_t *cr) {
  letter_draw(&s->letter, cr);
  bird_draw(s, cr);
  draw_scroll(s, cr);
  particles_draw(&s->p, cr);
}

void scene_bbox(Scene *s, int *x, int *y, int *w, int *h) {
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  bird_bbox(s, &x0, &y0, &x1, &y1);
  particles_bbox(&s->p, &x0, &y0, &x1, &y1);
  if (s->scroll.alive) {
    if (s->scroll.x - 20 < x0) x0 = s->scroll.x - 20;
    if (s->scroll.y - 20 < y0) y0 = s->scroll.y - 20;
    if (s->scroll.x + 20 > x1) x1 = s->scroll.x + 20;
    if (s->scroll.y + 20 > y1) y1 = s->scroll.y + 20;
  }
  if (s->letter.open) {
    if (s->letter.x - 12 < x0) x0 = s->letter.x - 12;
    if (s->letter.y - 12 < y0) y0 = s->letter.y - 12;
    if (s->letter.x + s->letter.w + 12 > x1) x1 = s->letter.x + s->letter.w + 12;
    if (s->letter.y + s->letter.h + 12 > y1) y1 = s->letter.y + s->letter.h + 12;
  }

  if (x1 < x0 || y1 < y0) { *x = *y = *w = *h = 0; return; }

  int ix0 = (int)floor(av_clamp(x0, 0, s->sw));
  int iy0 = (int)floor(av_clamp(y0, 0, s->sh));
  int ix1 = (int)ceil(av_clamp(x1, 0, s->sw));
  int iy1 = (int)ceil(av_clamp(y1, 0, s->sh));
  *x = ix0; *y = iy0; *w = ix1 - ix0; *h = iy1 - iy0;
}
