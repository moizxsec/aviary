/* aviary — X11 overlay daemon, in the spirit of oneko: a small creature that
 * lives on the desktop itself, not in a browser.
 *
 *   aviary                       run the daemon (nothing appears until a bird flies)
 *   aviary send "text" [--from n]  release a bird
 *   aviary render DIR            dump the whole delivery to PNGs, no X needed
 *   aviary sheet FILE.png        dump a pose sheet of the bird
 */
#include "aviary.h"
#include "net.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <cairo/cairo-xlib.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

int render_main(int argc, char **argv);   /* render.c */
int sheet_main(int argc, char **argv);    /* render.c */
int sizes_main(int argc, char **argv);    /* render.c */
int strip_main(int argc, char **argv);    /* render.c */
int pigeon_sheet_main(int argc, char **argv);  /* render.c */
int owl_sheet_main(int argc, char **argv);     /* render.c */

#define FRAME_DT (1.0 / 60.0)
#define QUEUE_MAX 8

/* --------------------------------------------------------------- socket -- */

static int client_send(const char *text, const char *from, const char *bird,
                       int depart, double fx, double fy) {
  int rc = av_local_send(text, from, bird, depart, fx, fy);
  if (rc == 2) {
    char path[256];
    av_socket_path(path, sizeof(path));
    fprintf(stderr, "no aviary running (%s) — start it with `aviary` first.\n", path);
  }
  return rc ? 1 : 0;
}

static int fill_sun(struct sockaddr_un *a, const char *path) {
  memset(a, 0, sizeof(*a));
  a->sun_family = AF_UNIX;
  size_t l = strlen(path);
  if (l >= sizeof(a->sun_path)) {
    fprintf(stderr, "socket path too long: %s\n", path);
    return -1;
  }
  memcpy(a->sun_path, path, l);
  return 0;
}

static void socket_path(char *out, size_t n) { av_socket_path(out, n); }

static int server_listen(void) {
  char path[256];
  socket_path(path, sizeof(path));

  /* a stale socket from a crashed run should not block a fresh start, but a
   * live one must */
  int probe = socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe >= 0) {
    struct sockaddr_un a;
    if (fill_sun(&a, path) == 0 && connect(probe, (struct sockaddr *)&a, sizeof(a)) == 0) {
      close(probe);
      fprintf(stderr, "aviary is already running (%s)\n", path);
      return -1;
    }
    close(probe);
  }
  unlink(path);

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) { perror("socket"); return -1; }

  struct sockaddr_un a;
  if (fill_sun(&a, path) < 0) { close(fd); return -1; }
  if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); close(fd); return -1; }
  if (listen(fd, 8) < 0) { perror("listen"); close(fd); return -1; }
  chmod(path, 0600);

  fprintf(stderr, "[aviary] listening on %s\n", path);
  return fd;
}

/* ------------------------------------------------------------- terminal -- */
/* To launch the bird from the line under what you just typed, we need to know
 * where that line is. The terminal will tell us the cursor cell, and how many
 * cells it has; the daemon turns that into pixels using the focused window. */

static int query_cursor(int *row, int *col) {
  struct termios old, raw;
  if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &old) < 0) return 0;
  raw = old;
  raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 2;                       /* 200ms, then give up */
  if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) return 0;

  int ok = 0;
  if (write(STDOUT_FILENO, "\033[6n", 4) == 4) {
    char buf[32];
    size_t n = 0;
    while (n < sizeof(buf) - 1) {
      char c;
      if (read(STDIN_FILENO, &c, 1) != 1) break;
      buf[n++] = c;
      if (c == 'R') break;
    }
    buf[n] = 0;
    ok = (sscanf(buf, "\033[%d;%dR", row, col) == 2);
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  return ok;
}

static int term_cells(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) return 0;
  if (ws.ws_row < 2 || ws.ws_col < 2) return 0;
  *rows = ws.ws_row;
  *cols = ws.ws_col;
  return 1;
}

/* -------------------------------------------------------------- overlay -- */

typedef struct {
  Display         *dpy;
  int              screen;
  Window           root, win;
  Visual          *visual;
  Colormap         cmap;
  int              w, h;              /* device pixels */
  cairo_surface_t *xsurf;
  Pixelizer        px;                /* the scene lives here, at sprite scale */
  int              mapped;
  int              have_shape;
  int              in_x, in_y, in_w, in_h;   /* current input region */
} Overlay;

static int overlay_open(Overlay *o, int pixel_size) {
  memset(o, 0, sizeof(*o));

  o->dpy = XOpenDisplay(NULL);
  if (!o->dpy) { fprintf(stderr, "cannot open display (is DISPLAY set?)\n"); return -1; }
  o->screen = DefaultScreen(o->dpy);
  o->root   = RootWindow(o->dpy, o->screen);
  o->w = DisplayWidth(o->dpy, o->screen);
  o->h = DisplayHeight(o->dpy, o->screen);

  /* a 32-bit visual is what makes real per-pixel alpha possible; without a
   * compositor running, the desktop simply will not blend it */
  XVisualInfo vi;
  if (!XMatchVisualInfo(o->dpy, o->screen, 32, TrueColor, &vi)) {
    fprintf(stderr, "no 32-bit TrueColor visual — this needs a compositing X server\n");
    return -1;
  }
  o->visual = vi.visual;
  o->cmap = XCreateColormap(o->dpy, o->root, o->visual, AllocNone);

  XSetWindowAttributes at;
  memset(&at, 0, sizeof(at));
  at.colormap = o->cmap;
  at.background_pixel = 0;
  at.border_pixel = 0;
  at.override_redirect = True;      /* same trick oneko uses: bypass the WM */
  at.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;

  o->win = XCreateWindow(o->dpy, o->root, 0, 0, (unsigned)o->w, (unsigned)o->h, 0,
                         32, InputOutput, o->visual,
                         CWColormap | CWBackPixel | CWBorderPixel |
                         CWOverrideRedirect | CWEventMask, &at);

  /* harmless with override-redirect, but correct if a WM ever does look */
  Atom type = XInternAtom(o->dpy, "_NET_WM_WINDOW_TYPE", False);
  Atom notif = XInternAtom(o->dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
  XChangeProperty(o->dpy, o->win, type, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)&notif, 1);
  Atom state = XInternAtom(o->dpy, "_NET_WM_STATE", False);
  Atom above = XInternAtom(o->dpy, "_NET_WM_STATE_ABOVE", False);
  Atom sticky = XInternAtom(o->dpy, "_NET_WM_STATE_STICKY", False);
  Atom states[2] = { above, sticky };
  XChangeProperty(o->dpy, o->win, state, XA_ATOM, 32, PropModeReplace,
                  (unsigned char *)states, 2);
  Atom desktop = XInternAtom(o->dpy, "_NET_WM_DESKTOP", False);
  unsigned long all = 0xFFFFFFFF;
  XChangeProperty(o->dpy, o->win, desktop, XA_CARDINAL, 32, PropModeReplace,
                  (unsigned char *)&all, 1);

  int base, err;
  o->have_shape = XShapeQueryExtension(o->dpy, &base, &err);
  if (!o->have_shape)
    fprintf(stderr, "[aviary] no XShape: the overlay will swallow clicks\n");

  o->xsurf = cairo_xlib_surface_create(o->dpy, o->win, o->visual, o->w, o->h);
  av_set_pixel_mode(1);
  pixel_init(&o->px, o->w, o->h, pixel_size);
  fprintf(stderr, "[aviary] %dx%d screen, %d device px per sprite pixel (%dx%d scene)\n",
          o->w, o->h, o->px.size, o->px.bw, o->px.bh);
  return 0;
}

/* Buffer coordinates in, device rectangle out. */
/* An empty input region is the whole point: the overlay covers the screen but
 * every click falls straight through to whatever is underneath. The letter is
 * the one rectangle that catches them. */
static void overlay_input_region(Overlay *o, int x, int y, int w, int h) {
  if (!o->have_shape) return;
  if (o->in_x == x && o->in_y == y && o->in_w == w && o->in_h == h) return;
  o->in_x = x; o->in_y = y; o->in_w = w; o->in_h = h;

  if (w <= 0 || h <= 0) {
    XShapeCombineRectangles(o->dpy, o->win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
  } else {
    XRectangle r;
    r.x = (short)x; r.y = (short)y;
    r.width = (unsigned short)w; r.height = (unsigned short)h;
    XShapeCombineRectangles(o->dpy, o->win, ShapeInput, 0, 0, &r, 1, ShapeSet, Unsorted);
  }
}

/* Turn "0.62 across, 0.78 down the terminal window" into a point on the root
 * window. The terminal still holds the input focus at this moment, because the
 * overlay is override-redirect and never takes it. */
static void overlay_resolve_origin(Overlay *o, double fx, double fy,
                                   double *ax, double *ay) {
  *ax = o->w * 0.5;
  *ay = o->h * 0.75;
  if (fx < 0 || fy < 0) return;

  Window focus = None;
  int revert = 0;
  XGetInputFocus(o->dpy, &focus, &revert);
  if (focus == None || focus == PointerRoot || focus == o->root) {
    /* no usable focus: fall back to wherever the pointer is */
    Window r, c;
    int rx, ry, wx, wy;
    unsigned mask;
    if (XQueryPointer(o->dpy, o->root, &r, &c, &rx, &ry, &wx, &wy, &mask)) {
      *ax = rx;
      *ay = ry;
    }
    return;
  }

  XWindowAttributes wa;
  if (!XGetWindowAttributes(o->dpy, focus, &wa) || wa.width < 8 || wa.height < 8)
    return;

  int rx = 0, ry = 0;
  Window child;
  if (!XTranslateCoordinates(o->dpy, focus, o->root, 0, 0, &rx, &ry, &child))
    return;

  *ax = rx + fx * wa.width;
  *ay = ry + fy * wa.height;
}

static void overlay_show(Overlay *o) {
  if (o->mapped) return;
  overlay_input_region(o, 0, 0, 0, 0);
  XMapRaised(o->dpy, o->win);
  o->mapped = 1;
  XFlush(o->dpy);
}

static void overlay_hide(Overlay *o) {
  if (!o->mapped) return;
  XUnmapWindow(o->dpy, o->win);
  o->mapped = 0;
  XFlush(o->dpy);
}

/* all in buffer (sprite) coordinates */
static void overlay_clear(Overlay *o, int x, int y, int w, int h) {
  pixel_clear(&o->px, x, y, w, h);
}

static void overlay_present(Overlay *o, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  pixel_quantize(&o->px, x, y, w, h);
  cairo_t *xc = cairo_create(o->xsurf);
  pixel_blit_op(&o->px, xc, x, y, w, h, CAIRO_OPERATOR_SOURCE);
  cairo_destroy(xc);
  cairo_surface_flush(o->xsurf);
  XFlush(o->dpy);
}

static void overlay_close(Overlay *o) {
  pixel_free(&o->px);
  if (o->xsurf) cairo_surface_destroy(o->xsurf);
  if (o->dpy) { XDestroyWindow(o->dpy, o->win); XCloseDisplay(o->dpy); }
}

/* ----------------------------------------------------------------- loop -- */

static volatile sig_atomic_t running = 1;
static void on_signal(int sig) { (void)sig; running = 0; }

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

typedef struct {
  char   text[LETTER_MAX_TEXT];
  char   from[LETTER_MAX_FROM];
  char   bird[24];
  int    mode;          /* SM_DELIVER or SM_DEPART */
  double fx, fy;        /* where in the focused window to start; <0 unknown */
} Delivery;

static int read_delivery(int fd, Delivery *d) {
  char buf[LETTER_MAX_TEXT + LETTER_MAX_FROM + 8];
  size_t got = 0;
  ssize_t n;
  while (got < sizeof(buf) - 1 && (n = read(fd, buf + got, sizeof(buf) - 1 - got)) > 0)
    got += (size_t)n;
  buf[got] = 0;
  if (!got) return 0;

  char *nl = strchr(buf, '\n');
  if (!nl) return 0;
  *nl = 0;

  /* "from TAB bird TAB mode TAB fx TAB fy" — every field after the first is
   * optional, so older senders still work */
  d->fx = d->fy = -1;
  d->mode = SM_DELIVER;
  char *tab = strchr(buf, '\t');
  if (tab) {
    *tab = 0;
    char *fields[4] = { NULL, NULL, NULL, NULL };
    char *p2 = tab + 1;
    for (int i = 0; i < 4 && p2; i++) {
      fields[i] = p2;
      char *nx = strchr(p2, '\t');
      if (nx) { *nx = 0; p2 = nx + 1; } else p2 = NULL;
    }
    if (fields[0]) {
      size_t bl = strlen(fields[0]);
      if (bl >= sizeof(d->bird)) bl = sizeof(d->bird) - 1;
      memcpy(d->bird, fields[0], bl);
      d->bird[bl] = 0;
    }
    if (fields[1] && !strcmp(fields[1], "out")) d->mode = SM_DEPART;
    if (fields[2]) d->fx = atof(fields[2]);
    if (fields[3]) d->fy = atof(fields[3]);
  }

  size_t fl = strlen(buf);
  if (fl >= sizeof(d->from)) fl = sizeof(d->from) - 1;
  memcpy(d->from, buf, fl);
  d->from[fl] = 0;

  size_t tl = strlen(nl + 1);
  if (tl >= sizeof(d->text)) tl = sizeof(d->text) - 1;
  memcpy(d->text, nl + 1, tl);
  d->text[tl] = 0;

  /* trim trailing whitespace so a stray newline does not add a blank line */
  size_t l = strlen(d->text);
  while (l && (d->text[l - 1] == '\n' || d->text[l - 1] == '\r' || d->text[l - 1] == ' '))
    d->text[--l] = 0;
  return l > 0;
}

static int daemon_main(int pixel_size) {
  Overlay o;
  if (overlay_open(&o, pixel_size) != 0) return 1;

  int lfd = server_listen();
  if (lfd < 0) { overlay_close(&o); return 1; }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  signal(SIGPIPE, SIG_IGN);

  /* If this machine is linked to another one, run the relay listener as a
   * child. Keeping it in its own process means a network hiccup can never
   * stall the overlay, and it restarts cleanly on its own. */
  AvConfig netcfg;
  pid_t listener = -1;
  if (av_config_load(&netcfg)) {
    listener = fork();
    if (listener == 0) {
      _exit(net_listen());
    }
    if (listener > 0)
      fprintf(stderr, "[aviary] linked — watching for letters from the other laptop\n");
  } else {
    fprintf(stderr, "[aviary] not linked (see `aviary link`) — local letters only\n");
  }

  av_seed((uint64_t)time(NULL) ^ (uint64_t)getpid());

  Scene scene;
  memset(&scene, 0, sizeof(scene));
  int active = 0;
  double next_frame = 0, last_raise = 0;
  int prev_x = 0, prev_y = 0, prev_w = 0, prev_h = 0;

  Delivery queue[QUEUE_MAX];
  int qn = 0;

  int xfd = ConnectionNumber(o.dpy);

  while (running) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(xfd, &rf);
    FD_SET(lfd, &rf);
    int maxfd = xfd > lfd ? xfd : lfd;

    struct timeval tv, *ptv = NULL;
    if (active) {
      double wait = next_frame - now_sec();
      if (wait < 0) wait = 0;
      tv.tv_sec = (time_t)wait;
      tv.tv_usec = (suseconds_t)((wait - tv.tv_sec) * 1e6);
      ptv = &tv;
    }
    /* never block while X already has events buffered */
    if (XPending(o.dpy)) { tv.tv_sec = 0; tv.tv_usec = 0; ptv = &tv; }

    int r = select(maxfd + 1, &rf, NULL, NULL, ptv);
    if (r < 0) {
      if (errno == EINTR) continue;
      perror("select");
      break;
    }

    if (FD_ISSET(lfd, &rf)) {
      int cfd = accept(lfd, NULL, NULL);
      if (cfd >= 0) {
        Delivery d;
        memset(&d, 0, sizeof(d));
        if (read_delivery(cfd, &d) && qn < QUEUE_MAX) queue[qn++] = d;
        close(cfd);
      }
    }

    while (XPending(o.dpy)) {
      XEvent ev;
      XNextEvent(o.dpy, &ev);
      if (ev.type == ButtonPress && active) {
        /* clicks arrive in device pixels; the scene thinks in sprite pixels */
        if (letter_hit(&scene.letter, ev.xbutton.x / o.px.size, ev.xbutton.y / o.px.size))
          letter_dismiss(&scene.letter);
      } else if (ev.type == Expose && active) {
        overlay_present(&o, 0, 0, o.px.bw, o.px.bh);
      }
    }

    if (!active && qn > 0) {
      Delivery d = queue[0];
      memmove(&queue[0], &queue[1], sizeof(Delivery) * (size_t)(qn - 1));
      qn--;
      if (scene.p.a) scene_free(&scene);
      if (d.mode == SM_DEPART) {
        double ax, ay;
        overlay_resolve_origin(&o, d.fx, d.fy, &ax, &ay);
        scene_start_ex(&scene, o.px.bw, o.px.bh, d.text, d.from,
                       scene_species_from_name(d.bird), SM_DEPART,
                       ax / o.px.size, ay / o.px.size);
      } else {
        scene_start(&scene, o.px.bw, o.px.bh, d.text, d.from,
                    scene_species_from_name(d.bird));
      }
      active = 1;
      next_frame = now_sec();
      prev_w = prev_h = 0;
      overlay_clear(&o, 0, 0, o.px.bw, o.px.bh);
      overlay_show(&o);
      last_raise = now_sec();
    }

    if (active && now_sec() >= next_frame) {
      scene_update(&scene, FRAME_DT);

      int bx, by, bw, bh;
      scene_bbox(&scene, &bx, &by, &bw, &bh);

      /* clear last frame's footprint, draw this one, push the union */
      overlay_clear(&o, prev_x, prev_y, prev_w, prev_h);
      overlay_clear(&o, bx, by, bw, bh);
      cairo_save(o.px.cr);
      if (bw > 0 && bh > 0) {
        cairo_rectangle(o.px.cr, bx, by, bw, bh);
        cairo_clip(o.px.cr);
      }
      scene_draw(&scene, o.px.cr);
      cairo_restore(o.px.cr);

      int ux = bx, uy = by, ux1 = bx + bw, uy1 = by + bh;
      if (prev_w > 0) {
        if (prev_x < ux) ux = prev_x;
        if (prev_y < uy) uy = prev_y;
        if (prev_x + prev_w > ux1) ux1 = prev_x + prev_w;
        if (prev_y + prev_h > uy1) uy1 = prev_y + prev_h;
      }
      overlay_present(&o, ux, uy, ux1 - ux, uy1 - uy);
      prev_x = bx; prev_y = by; prev_w = bw; prev_h = bh;

      /* Only the "let it go" pill catches clicks; the rest of the letter is
       * as click-through as the rest of the overlay, so nothing is ever
       * trapped behind a piece of paper. */
      if (scene.letter.open && scene.letter.open_t > 0.6 &&
          !scene.letter.gone && !scene.letter.burning && scene.letter.btn_w > 0) {
        int pad = 5;
        overlay_input_region(&o,
                             (int)(scene.letter.btn_x - pad) * o.px.size,
                             (int)(scene.letter.btn_y - pad) * o.px.size,
                             (int)(scene.letter.btn_w + pad * 2) * o.px.size,
                             (int)(scene.letter.btn_h + pad * 2) * o.px.size);
      }
      else
        overlay_input_region(&o, 0, 0, 0, 0);

      /* an override-redirect window can be covered by anything mapped later */
      if (now_sec() - last_raise > 0.7) {
        XRaiseWindow(o.dpy, o.win);
        last_raise = now_sec();
      }

      next_frame += FRAME_DT;
      if (now_sec() - next_frame > 0.25) next_frame = now_sec();   /* fell behind */

      if (scene.done) {
        active = 0;
        overlay_input_region(&o, 0, 0, 0, 0);
        overlay_hide(&o);
        overlay_clear(&o, 0, 0, o.px.bw, o.px.bh);
      }
    }
  }

  if (listener > 0) { kill(listener, SIGTERM); waitpid(listener, NULL, 0); }
  fprintf(stderr, "\n[aviary] bye\n");
  if (scene.p.a) scene_free(&scene);
  overlay_hide(&o);
  overlay_close(&o);
  close(lfd);
  char path[256];
  socket_path(path, sizeof(path));
  unlink(path);
  return 0;
}

/* ----------------------------------------------------------------- main -- */

/* Write a message and watch it leave. The bird lifts off from the line under
 * the one you typed on, so it reads as though it is taking that text with it. */
static int compose_main(int argc, char **argv) {
  AvConfig cfg;
  int linked = av_config_load(&cfg);
  const char *from = linked && cfg.name[0] ? cfg.name : "";
  const char *bird = linked && cfg.bird[0] ? cfg.bird : "pigeon";
  int local_only = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--from") && i + 1 < argc) { from = argv[++i]; continue; }
    if (!strcmp(argv[i], "--bird") && i + 1 < argc) { bird = argv[++i]; continue; }
    if (!strcmp(argv[i], "--local")) { local_only = 1; continue; }
    fprintf(stderr, "aviary compose: unknown option %s\n", argv[i]);
    return 1;
  }

  int rows = 0, cols = 0;
  int have_cells = term_cells(&rows, &cols);

  printf("\033[2m%s\033[0m \033[1m>\033[0m ", bird);
  fflush(stdout);

  int row = 0, col = 0;
  int have_cur = query_cursor(&row, &col);

  char text[LETTER_MAX_TEXT];
  if (!fgets(text, sizeof(text), stdin)) { printf("\n"); return 0; }
  size_t l = strlen(text);
  while (l && (text[l - 1] == '\n' || text[l - 1] == '\r')) text[--l] = 0;
  if (!l) { fprintf(stderr, "nothing to send.\n"); return 1; }

  /* the spot just past what was typed, one line down */
  double fx = -1, fy = -1;
  if (have_cur && have_cells) {
    double end_col = col + (double)l;
    while (end_col > cols) end_col -= cols;          /* it wrapped */
    fx = (end_col - 0.5) / cols;
    fy = (row + 0.5) / rows;
    if (fy > 0.995) fy = 0.995;
  }

  /* the bird lifts off your screen carrying it ... */
  client_send(text, from, bird, 1, fx, fy);

  /* ... and the same letter goes to the other laptop */
  if (!local_only && linked) {
    if (net_publish(text, from, bird) == 0)
      printf("\033[2m  ...off it goes.\033[0m\n");
    else
      printf("\033[2m  the bird left, but the relay did not take it.\033[0m\n");
  } else if (!linked) {
    printf("\033[2m  ...off it goes. (only your screen — run `aviary link` to reach"
           " the other laptop)\033[0m\n");
  } else {
    printf("\033[2m  ...off it goes. (local only)\033[0m\n");
  }
  return 0;
}

static void usage(void) {
  fprintf(stderr,
    "aviary — tiny birds that carry letters across your desktop\n"
    "\n"
    "  aviary                    write a line; a bird takes it to her\n"
    "\n"
    "setting up the pair, once\n"
    "  aviary invite             on your laptop: prints a pairing code\n"
    "  aviary join <code>        on hers: paste that code. that is all.\n"
    "      --name <you>          how you are signed on the letters\n"
    "      --bird <B>            your usual bird\n"
    "      B = phoenix | pigeon | owl | swallow\n"
    "\n"
    "now and then\n"
    "  aviary --bird owl         send with a different bird this once\n"
    "  aviary send \"text\"        fly one on your own screen, nothing sent\n"
    "  aviary daemon [--pixel N] run the overlay by hand (systemd does this)\n"
    "\n"
    "less often\n"
    "  aviary link <topic> <passphrase>   pair using a topic you chose yourself\n"
    "  aviary listen                      watch the relay by hand\n"
    "  aviary render DIR / sheet F / sizes F   offline frame dumps\n"
    "\n"
    "  --pixel N   device pixels per sprite pixel (default 2; 1 = oneko 1:1)\n");
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "send")) {
    const char *from = "";
    const char *bird = "phoenix";
    char text[LETTER_MAX_TEXT] = {0};
    size_t tl = 0;
    for (int i = 2; i < argc; i++) {
      if (!strcmp(argv[i], "--from") && i + 1 < argc) { from = argv[++i]; continue; }
      if (!strcmp(argv[i], "--bird") && i + 1 < argc) { bird = argv[++i]; continue; }
      int n = snprintf(text + tl, sizeof(text) - tl, "%s%s", tl ? " " : "", argv[i]);
      if (n > 0) tl += (size_t)n;
      if (tl >= sizeof(text) - 1) break;
    }
    if (!tl) { fprintf(stderr, "aviary send: nothing to say\n"); return 1; }
    return client_send(text, from, bird, 0, -1, -1);
  }
  if (argc >= 2 && !strcmp(argv[1], "invite")) {
    const char *name = "", *bird = "pigeon";
    for (int i = 2; i < argc; i++) {
      if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
      else if (!strcmp(argv[i], "--bird") && i + 1 < argc) bird = argv[++i];
    }
    return net_invite(name, bird);
  }
  if (argc >= 2 && !strcmp(argv[1], "join")) {
    const char *code = argc > 2 ? argv[2] : NULL;
    const char *name = "", *bird = "pigeon";
    for (int i = 3; i < argc; i++) {
      if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
      else if (!strcmp(argv[i], "--bird") && i + 1 < argc) bird = argv[++i];
    }
    if (!code) { fprintf(stderr, "usage: aviary join <code> [--name N]\n"); return 1; }
    return net_join(code, name, bird);
  }
  if (argc >= 2 && !strcmp(argv[1], "link")) {
    const char *topic = argc > 2 ? argv[2] : NULL;
    const char *pass  = argc > 3 ? argv[3] : NULL;
    const char *name = "", *bird = "pigeon";
    for (int i = 4; i < argc; i++) {
      if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
      else if (!strcmp(argv[i], "--bird") && i + 1 < argc) bird = argv[++i];
    }
    if (!topic || !pass) {
      fprintf(stderr, "usage: aviary link <topic> <passphrase> [--name N] [--bird B]\n");
      return 1;
    }
    return net_link(topic, pass, name, bird);
  }
  if (argc >= 2 && !strcmp(argv[1], "listen")) return net_listen();
  if (argc >= 2 && (!strcmp(argv[1], "compose") || !strcmp(argv[1], "write")))
    return compose_main(argc - 1, argv + 1);
  if (argc >= 2 && !strcmp(argv[1], "render")) return render_main(argc - 1, argv + 1);
  if (argc >= 2 && !strcmp(argv[1], "sheet"))  return sheet_main(argc - 1, argv + 1);
  if (argc >= 2 && !strcmp(argv[1], "psheet")) return pigeon_sheet_main(argc - 1, argv + 1);
  if (argc >= 2 && !strcmp(argv[1], "osheet")) return owl_sheet_main(argc - 1, argv + 1);
  if (argc >= 2 && !strcmp(argv[1], "sizes"))  return sizes_main(argc - 1, argv + 1);
  if (argc >= 2 && !strcmp(argv[1], "strip"))  return strip_main(argc - 1, argv + 1);
  if (argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) { usage(); return 0; }

  if (argc >= 2 && !strcmp(argv[1], "daemon")) {
    int pixel_size = 2;
    for (int i = 2; i < argc; i++) {
      if (!strcmp(argv[i], "--pixel") && i + 1 < argc) {
        pixel_size = atoi(argv[++i]);
        if (pixel_size < 1) pixel_size = 1;
        if (pixel_size > 8) pixel_size = 8;
      } else { usage(); return 1; }
    }
    return daemon_main(pixel_size);
  }

  /* Bare `aviary`, or `aviary --bird owl`, is the thing a person actually wants
   * to do: write a line and watch a bird take it. The daemon runs itself, out
   * of systemd. */
  if (argc == 1 || argv[1][0] == '-') return compose_main(argc, argv);

  usage();
  return 1;
}
