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
#include <X11/extensions/Xrandr.h>
#include <cairo/cairo-xlib.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/prctl.h>
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
                       int depart, double fx, double fy, double delay) {
  int rc = av_local_send(text, from, bird, depart, fx, fy, delay);
  if (rc == 2) {
    char path[256];
    av_socket_path(path, sizeof(path));
    fprintf(stderr, "  no daemon on this machine, so nothing flew here.\n"
                    "  start one and it will work from then on:  aviary daemon &\n");
    (void)path;
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
      fprintf(stderr, "[aviary] already running (%s)\n", path);
      return -2;             /* not a failure: someone got here first */
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

/* The cursor sits in a character cell; the daemon wants a fraction of the
 * terminal window. `extra` is how many columns further along the line the
 * letter should be — the length of what was typed, when it was typed here. */
static void spot_from_cell(int row, int col, int rows, int cols, double extra,
                           double *fx, double *fy) {
  double end_col = col + extra;
  while (end_col > cols) end_col -= cols;              /* it wrapped */
  *fx = (end_col - 0.5) / cols;
  *fy = (row + 0.5) / rows;
  if (*fy > 0.995) *fy = 0.995;
}

/* Where the shell has left the cursor, as a fraction of the terminal. */
static int terminal_spot(double extra, double *fx, double *fy) {
  int rows = 0, cols = 0, row = 0, col = 0;
  if (!term_cells(&rows, &cols) || !query_cursor(&row, &col)) return 0;
  spot_from_cell(row, col, rows, cols, extra, fx, fy);
  return 1;
}

/* How long the bird is still on this screen after the line is typed: it flies
 * in, collects the letter and carries it off the edge. Measured from the
 * render harness, which runs the same scene the desktop does:
 *
 *     aviary render DIR 2 <bird> depart
 *
 * If those timings are ever retuned, re-run it and put the numbers back here —
 * they are what the other laptop waits out before flying its own bird in. */
static double depart_seconds(int species) {
  switch (species) {
    case BIRD_PIGEON:  return 10.0;
    case BIRD_OWL:     return 11.1;
    case BIRD_SWALLOW: return 10.5;
    default:           return  8.8;      /* phoenix */
  }
}

/* -------------------------------------------------------------- overlay -- */

typedef struct {
  Display         *dpy;
  int              screen;
  Window           root, win;
  Visual          *visual;
  Colormap         cmap;
  int              ox, oy;            /* where the window sits on the root */
  int              w, h;              /* device pixels — one monitor, not all */
  int              root_w, root_h;
  cairo_surface_t *xsurf;
  Pixelizer        px;                /* the scene lives here, at sprite scale */
  int              mapped;
  int              have_shape;
  int              in_x, in_y, in_w, in_h;   /* current input region */
} Overlay;

typedef struct { int x, y, w, h; } Rect;

/* A laptop with a second screen plugged in is still one X screen: the root
 * window is the two of them side by side, with the desktops laid out inside
 * it. A window covering that root spans both, and anything drawn at its centre
 * lands exactly on the seam — half a letter on each screen, which is where
 * hers was arriving. RandR is what knows where the seams are.
 *
 * Returns the monitor containing the point, or the primary one, or nothing at
 * all if the server has no RandR — in which case the caller uses the root and
 * behaves as it always did. */
static int monitor_for_point(Display *dpy, Window root, int px, int py, Rect *out) {
  int n = 0;
  XRRMonitorInfo *m = XRRGetMonitors(dpy, root, True, &n);   /* active only */
  if (!m || n <= 0) { if (m) XRRFreeMonitors(m); return 0; }

  int best = -1;
  if (px >= 0 && py >= 0)
    for (int i = 0; i < n; i++)
      if (px >= m[i].x && px < m[i].x + m[i].width &&
          py >= m[i].y && py < m[i].y + m[i].height) { best = i; break; }
  if (best < 0)
    for (int i = 0; i < n; i++) if (m[i].primary) { best = i; break; }
  if (best < 0) best = 0;

  out->x = m[best].x;      out->y = m[best].y;
  out->w = m[best].width;  out->h = m[best].height;
  XRRFreeMonitors(m);
  return out->w > 0 && out->h > 0;
}

/* The rectangle the bird should have to itself. Looked up fresh every time,
 * which is also how a screen being plugged in, unplugged or rotated gets
 * noticed — there is no event to miss if nothing is ever cached. */
static void overlay_target_rect(Overlay *o, int px, int py, Rect *r) {
  XWindowAttributes wa;
  if (XGetWindowAttributes(o->dpy, o->root, &wa)) {
    o->root_w = wa.width;
    o->root_h = wa.height;
  }
  if (!monitor_for_point(o->dpy, o->root, px, py, r)) {
    r->x = 0; r->y = 0; r->w = o->root_w; r->h = o->root_h;
  }
}

/* At login the daemon can easily be up before the X server is, and a systemd
 * user unit may have no DISPLAY in its environment at all. Neither is worth
 * dying over — waiting costs nothing and the alternative is a service that
 * fails five times in fifteen seconds and is never tried again. */
static Display *open_display_waiting(int seconds) {
  static const char *guesses[] = { ":0", ":1", NULL };
  const char *want = getenv("DISPLAY");
  int said = 0;

  for (int i = 0; i < seconds * 2 + 1; i++) {
    if (want && *want) {
      Display *d = XOpenDisplay(want);
      if (d) return d;
    } else {
      for (int j = 0; guesses[j]; j++) {
        Display *d = XOpenDisplay(guesses[j]);
        if (d) { setenv("DISPLAY", guesses[j], 1); return d; }
      }
    }
    if (!said && seconds > 0) {
      fprintf(stderr, "[aviary] no X server yet%s — waiting up to %ds\n",
              (want && *want) ? "" : " (DISPLAY is not set)", seconds);
      said = 1;
    }
    if (i < seconds * 2) usleep(500000);
  }
  return NULL;
}

static void overlay_input_region(Overlay *o, int x, int y, int w, int h);

static int overlay_open(Overlay *o, int pixel_size, int wait_secs) {
  memset(o, 0, sizeof(*o));

  o->dpy = open_display_waiting(wait_secs);
  if (!o->dpy) {
    fprintf(stderr, "cannot open display (is DISPLAY set? is X running?)\n");
    return -1;
  }
  o->screen = DefaultScreen(o->dpy);
  o->root   = RootWindow(o->dpy, o->screen);
  o->root_w = DisplayWidth(o->dpy, o->screen);
  o->root_h = DisplayHeight(o->dpy, o->screen);

  Rect r;
  overlay_target_rect(o, -1, -1, &r);        /* the primary one, to begin with */
  o->ox = r.x; o->oy = r.y; o->w = r.w; o->h = r.h;

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

  o->win = XCreateWindow(o->dpy, o->root, o->ox, o->oy,
                         (unsigned)o->w, (unsigned)o->h, 0,
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

  /* A window with no input shape set catches everything inside its bounds, and
   * this one is the size of a monitor. The cached rectangle starts at all
   * zeroes, so the first request for an empty region matched it and was
   * skipped — and the empty region was never actually applied. Every click
   * during a delivery went into the overlay and nowhere else. Start the cache
   * at something no rectangle can be, and set the region now. */
  o->in_x = o->in_y = o->in_w = o->in_h = -1;
  overlay_input_region(o, 0, 0, 0, 0);

  o->xsurf = cairo_xlib_surface_create(o->dpy, o->win, o->visual, o->w, o->h);
  av_set_pixel_mode(1);
  pixel_init(&o->px, o->w, o->h, pixel_size);
  fprintf(stderr, "[aviary] %dx%d screen at +%d+%d (root is %dx%d), "
                  "%d device px per sprite pixel (%dx%d scene)\n",
          o->w, o->h, o->ox, o->oy, o->root_w, o->root_h,
          o->px.size, o->px.bw, o->px.bh);
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

/* Where on the root window is this delivery actually happening?
 *
 * For a letter being sent, that is the spot just past what was typed — the
 * terminal still holds the input focus at this moment, because the overlay is
 * override-redirect and never takes it. For one arriving there is no such
 * hint, and the pointer is the best guess at which screen is being looked at.
 *
 * Returns 1 if the point is a real launch spot, 0 if it is only good enough to
 * choose a monitor with. Either way the coordinates are root coordinates. */
static int delivery_anchor(Overlay *o, double fx, double fy, int *ax, int *ay) {
  *ax = -1; *ay = -1;

  if (fx >= 0 && fy >= 0) {
    Window focus = None;
    int revert = 0;
    XGetInputFocus(o->dpy, &focus, &revert);
    if (focus != None && focus != PointerRoot && focus != o->root) {
      XWindowAttributes wa;
      Window child;
      int rx = 0, ry = 0;
      if (XGetWindowAttributes(o->dpy, focus, &wa) &&
          wa.width >= 8 && wa.height >= 8 &&
          XTranslateCoordinates(o->dpy, focus, o->root, 0, 0, &rx, &ry, &child)) {
        *ax = rx + (int)(fx * wa.width);
        *ay = ry + (int)(fy * wa.height);
        return 1;
      }
    }
  }

  Window r, c;
  int rx, ry, wx, wy;
  unsigned mask;
  if (XQueryPointer(o->dpy, o->root, &r, &c, &rx, &ry, &wx, &wy, &mask)) {
    *ax = rx;
    *ay = ry;
  }
  return 0;
}

/* Move the overlay onto the screen this delivery belongs to. Checked before
 * every delivery, so a laptop that gets docked, unplugged or rotated between
 * two letters is simply right the second time without anyone noticing. */
static void overlay_place(Overlay *o, int px, int py) {
  Rect r;
  overlay_target_rect(o, px, py, &r);
  if (r.x == o->ox && r.y == o->oy && r.w == o->w && r.h == o->h) return;

  fprintf(stderr, "[aviary] flying on the %dx%d screen at +%d+%d\n",
          r.w, r.h, r.x, r.y);
  o->ox = r.x; o->oy = r.y; o->w = r.w; o->h = r.h;

  XMoveResizeWindow(o->dpy, o->win, o->ox, o->oy, (unsigned)o->w, (unsigned)o->h);
  XFlush(o->dpy);
  cairo_xlib_surface_set_size(o->xsurf, o->w, o->h);

  int size = o->px.size;
  pixel_free(&o->px);
  pixel_init(&o->px, o->w, o->h, size);
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
  double delay;         /* seconds still to fly before it gets here */
  double due;           /* monotonic clock, once it is in the queue */
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

  /* "from TAB bird TAB mode TAB fx TAB fy TAB delay" — every field after the
   * first is optional, so older senders still work */
  d->fx = d->fy = -1;
  d->mode = SM_DELIVER;
  d->delay = 0;
  char *tab = strchr(buf, '\t');
  if (tab) {
    *tab = 0;
    char *fields[5] = { NULL, NULL, NULL, NULL, NULL };
    char *p2 = tab + 1;
    for (int i = 0; i < 5 && p2; i++) {
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
    if (fields[4]) {
      d->delay = atof(fields[4]);
      if (d->delay < 0 || d->delay > 120) d->delay = 0;
    }
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

/* Started from a desktop autostart entry there is nowhere for stderr to go, so
 * a daemon that fails at login fails silently and there is nothing at all to
 * look at afterwards. Keep a log beside the config instead. */
static void daemon_log_open(void) {
  if (isatty(STDERR_FILENO)) return;

  char p[512];
  av_config_file(p, sizeof(p), "daemon.log");
  int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) return;

  struct stat st;
  if (fstat(fd, &st) == 0 && st.st_size > 256 * 1024) {   /* do not grow forever */
    if (ftruncate(fd, 0) == 0) lseek(fd, 0, SEEK_SET);
  }
  dup2(fd, STDOUT_FILENO);
  dup2(fd, STDERR_FILENO);
  if (fd > 2) close(fd);
  setvbuf(stderr, NULL, _IOLBF, 0);

  time_t t = time(NULL);
  char when[64];
  struct tm tmv;
  if (localtime_r(&t, &tmv) && strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv))
    fprintf(stderr, "\n[aviary] --- started %s ---\n", when);
}

static int daemon_main(int pixel_size) {
  daemon_log_open();

  if (av_daemon_is_up()) {
    fprintf(stderr, "[aviary] already running — leaving it to it\n");
    return 0;
  }

  /* An autostart entry and a systemd unit may both fire at login; whichever
   * loses the race must not look like a crash, or the unit gets restarted five
   * times and then given up on for good. */
  Overlay o;
  int wait = isatty(STDERR_FILENO) ? 2 : 60;
  if (overlay_open(&o, pixel_size, wait) != 0) return 1;

  int lfd = server_listen();
  if (lfd == -2) { overlay_close(&o); return 0; }
  if (lfd < 0) { overlay_close(&o); return 1; }
  av_daemon_claim();

  /* glibc's signal() installs handlers with SA_RESTART, so select() would be
   * restarted after a SIGTERM instead of returning EINTR — the loop never got
   * back to its condition and the daemon ignored every polite request to stop.
   * sigaction without SA_RESTART is what actually interrupts it. */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);

  /* If this machine is linked to another one, run the relay listener as a
   * child. Keeping it in its own process means a network hiccup can never
   * stall the overlay, and it restarts cleanly on its own. */
  AvConfig netcfg;
  pid_t listener = -1;
  if (av_config_load(&netcfg)) {
    listener = fork();
    if (listener == 0) {
      /* if the overlay ever dies without cleaning up, the kernel takes this
       * one down too — no more orphans left polling the relay forever */
      prctl(PR_SET_PDEATHSIG, SIGKILL);
      if (getppid() == 1) _exit(0);
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
  int said_pill = 0;

  int xfd = ConnectionNumber(o.dpy);

  while (running) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(xfd, &rf);
    FD_SET(lfd, &rf);
    int maxfd = xfd > lfd ? xfd : lfd;

    struct timeval tv, *ptv = NULL;
    double wake = -1;
    if (active) wake = next_frame;
    /* a letter still in the air is the other thing worth waking for */
    if (!active && qn > 0 && (wake < 0 || queue[0].due < wake)) wake = queue[0].due;
    if (wake >= 0) {
      double wait = wake - now_sec();
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
        if (read_delivery(cfd, &d) && qn < QUEUE_MAX) {
          d.due = now_sec() + (d.delay > 0 ? d.delay : 0);
          if (d.delay > 0.05)
            fprintf(stderr, "[aviary] a %s is %.1fs out\n",
                    d.bird[0] ? d.bird : "bird", d.delay);
          queue[qn++] = d;
        }
        close(cfd);
      }
    }

    while (XPending(o.dpy)) {
      XEvent ev;
      XNextEvent(o.dpy, &ev);
      if (ev.type == ButtonPress && active) {
        /* clicks arrive in device pixels; the scene thinks in sprite pixels */
        int hit = letter_hit(&scene.letter,
                             ev.xbutton.x / o.px.size, ev.xbutton.y / o.px.size);
        /* Where XShape is available the only clicks this window can receive at
         * all are the ones the input region let through, and that region is
         * the pill. A click that arrives and then matches nothing means the
         * two disagree by a pixel; the shape is what the person aimed at. */
        if (hit || (o.have_shape && o.in_w > 0)) {
          letter_dismiss(&scene.letter);
          fprintf(stderr, "[aviary] let go\n");
        } else
          fprintf(stderr, "[aviary] a click at %d,%d landed on nothing\n",
                  ev.xbutton.x, ev.xbutton.y);
      } else if (ev.type == Expose && active) {
        overlay_present(&o, 0, 0, o.px.bw, o.px.bh);
      }
    }

    if (!active && qn > 0 && now_sec() >= queue[0].due) {
      Delivery d = queue[0];
      memmove(&queue[0], &queue[1], sizeof(Delivery) * (size_t)(qn - 1));
      qn--;
      if (scene.p.a) scene_free(&scene);

      /* the point first, then the screen it falls on, then the scene */
      int ax = -1, ay = -1;
      int have_spot = delivery_anchor(&o, d.fx, d.fy, &ax, &ay);
      overlay_place(&o, ax, ay);
      fprintf(stderr, "[aviary] %s %s%s%s\n",
              d.mode == SM_DEPART ? "a" : "flying in a",
              d.bird[0] ? d.bird : "bird",
              d.mode == SM_DEPART ? " is coming for it" : " from ",
              d.mode == SM_DEPART ? "" : (d.from[0] ? d.from : "somewhere"));
      if (d.mode == SM_DEPART && have_spot)
        fprintf(stderr, "[aviary] collecting from the cursor, at %d,%d\n", ax, ay);
      else if (d.mode == SM_DEPART)
        fprintf(stderr, "[aviary] collecting from the middle — the terminal did "
                        "not say where its cursor was\n");

      if (d.mode == SM_DEPART) {
        double lx = o.px.bw * 0.5, ly = o.px.bh * 0.75;
        if (have_spot) {
          lx = (ax - o.ox) / (double)o.px.size;
          ly = (ay - o.oy) / (double)o.px.size;
        }
        scene_start_ex(&scene, o.px.bw, o.px.bh, d.text, d.from,
                       scene_species_from_name(d.bird), SM_DEPART, lx, ly);
      } else {
        scene_start(&scene, o.px.bw, o.px.bh, d.text, d.from,
                    scene_species_from_name(d.bird));
      }
      active = 1;
      said_pill = 0;
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
      double pill_x, pill_y, pill_w, pill_h;
      if (letter_button(&scene.letter, &pill_x, &pill_y, &pill_w, &pill_h)) {
        /* floor the corner and ceil the far edge, so the region is never a
         * pixel smaller than the thing it is standing in for */
        int rx = (int)floor(pill_x) * o.px.size;
        int ry = (int)floor(pill_y) * o.px.size;
        int rw = (int)ceil(pill_x + pill_w) * o.px.size - rx;
        int rh = (int)ceil(pill_y + pill_h) * o.px.size - ry;
        /* the rectangle is still settling while the panel opens; one line
         * when it is finally the real one is worth having in the log */
        if (!said_pill && scene.letter.open_t >= 1) {
          fprintf(stderr, "[aviary] let it go: %dx%d at +%d+%d\n", rw, rh, rx, ry);
          said_pill = 1;
        }
        overlay_input_region(&o, rx, ry, rw, rh);
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

  if (listener > 0) {
    kill(listener, SIGTERM);
    for (int i = 0; i < 20; i++) {          /* one second, then insist */
      if (waitpid(listener, NULL, WNOHANG) == listener) { listener = -1; break; }
      usleep(50000);
    }
    if (listener > 0) { kill(listener, SIGKILL); waitpid(listener, NULL, 0); }
  }
  fprintf(stderr, "\n[aviary] bye\n");
  if (scene.p.a) scene_free(&scene);
  overlay_hide(&o);
  overlay_close(&o);
  close(lfd);
  av_daemon_release();
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

  av_ensure_daemon();

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

  /* the spot just past what was typed, on the line it was typed on */
  double fx = -1, fy = -1;
  if (have_cur && have_cells)
    spot_from_cell(row, col, rows, cols, (double)l, &fx, &fy);

  /* the bird lifts off your screen carrying it ... */
  int flew = (client_send(text, from, bird, 1, fx, fy, 0) == 0);

  /* ... and the same letter goes to the other laptop, which is told to hold it
   * until this bird has actually gone. Two birds moving at the same moment on
   * two desks reads as a copy; one leaving and then the other arriving reads
   * as the same bird. */
  int sent = 0;
  if (!local_only && linked) {
    double flight = depart_seconds(scene_species_from_name(bird)) + cfg.travel;
    sent = (net_publish(text, from, bird, flight) == 0);
  }

  if (!linked) {
    printf("\033[2m  not paired yet, so this went nowhere.\033[0m\n");
    printf("\033[2m  run `aviary invite` here, and `aviary join <code>` there.\033[0m\n");
  } else if (local_only) {
    printf("\033[2m  ...off it goes. (local only)\033[0m\n");
  } else if (sent && flew) {
    printf("\033[2m  ...off it goes.\033[0m\n");
  } else if (sent) {
    printf("\033[2m  ...off it goes. (delivered, but no bird flew here)\033[0m\n");
  } else {
    printf("\033[2m  the relay would not take it — nothing was sent.\033[0m\n");
  }
  return sent || local_only ? 0 : 1;
}

/* When it does not work, this is the thing to run. Everything that has to be
 * true for a letter to arrive, checked and printed in one screen. */
static void status_screens(void) {
  Display *dpy = open_display_waiting(0);
  if (!dpy) {
    const char *d = getenv("DISPLAY");
    printf("  display     cannot open %s — no bird can fly here\n",
           (d && *d) ? d : "(DISPLAY is not set)");
    return;
  }

  Window root = RootWindow(dpy, DefaultScreen(dpy));
  int n = 0;
  XRRMonitorInfo *m = XRRGetMonitors(dpy, root, True, &n);
  const char *dname = getenv("DISPLAY");
  printf("  display     %s — root %dx%d, %d screen%s\n",
         (dname && *dname) ? dname : "(unset)",
         DisplayWidth(dpy, DefaultScreen(dpy)),
         DisplayHeight(dpy, DefaultScreen(dpy)),
         n > 0 ? n : 1, n == 1 ? "" : "s");

  if (m && n > 0) {
    for (int i = 0; i < n; i++) {
      char *nm = XGetAtomName(dpy, m[i].name);
      printf("              %-10s %dx%d+%d+%d%s\n",
             nm ? nm : "?", m[i].width, m[i].height, m[i].x, m[i].y,
             m[i].primary ? "  (primary)" : "");
      if (nm) XFree(nm);
    }
  } else {
    printf("              no RandR — the whole root is used as one screen\n");
  }
  if (m) XRRFreeMonitors(m);

  XVisualInfo vi;
  if (!XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, TrueColor, &vi))
    printf("  compositor  missing a 32-bit visual — the overlay cannot blend\n");

  if (getenv("WAYLAND_DISPLAY"))
    printf("  session     Wayland (through XWayland) — the overlay may sit\n"
           "              behind native windows\n");

  XCloseDisplay(dpy);
}

static void status_login(void) {
  char p[512];
  const char *home = getenv("HOME");
  struct stat st;

  snprintf(p, sizeof(p), "%s/.config/autostart/aviary.desktop", home ? home : "");
  printf("  at login    autostart entry %s\n",
         stat(p, &st) == 0 ? "present" : "MISSING — run ./install.sh again");

  snprintf(p, sizeof(p), "%s/.config/systemd/user/aviary.service", home ? home : "");
  if (stat(p, &st) != 0) {
    printf("              no systemd unit (the autostart entry covers it)\n");
    return;
  }
  int enabled = system("systemctl --user is-enabled aviary.service >/dev/null 2>&1") == 0;
  int active  = system("systemctl --user is-active  aviary.service >/dev/null 2>&1") == 0;
  printf("              systemd unit %s, %s\n",
         enabled ? "enabled" : "NOT enabled",
         active ? "running" : "not running");
}

static void status_log(void) {
  char p[512];
  av_config_file(p, sizeof(p), "daemon.log");
  struct stat st;
  if (stat(p, &st) != 0) { printf("  log         none yet (%s)\n", p); return; }
  printf("  log         %s\n", p);

  char cmd[600];
  snprintf(cmd, sizeof(cmd), "tail -n 6 '%s' 2>/dev/null | sed 's/^/              /'", p);
  fflush(stdout);              /* or tail's output lands ahead of ours */
  if (system(cmd) != 0) { /* nothing to do */ }
}

static int status_main(void) {
  printf("\n  aviary\n\n");

  int pid = av_daemon_pid();
  if (av_daemon_is_up())
    printf("  daemon      running%s\n", pid ? "" : " (started before pids were kept)");
  else
    printf("  daemon      NOT running — nothing can be delivered here\n");
  if (pid) printf("              pid %d\n", pid);

  char sock[256];
  av_socket_path(sock, sizeof(sock));
  printf("  socket      %s\n", sock);

  net_status_print();
  status_screens();
  status_login();
  status_log();
  printf("\n");
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
    "  aviary send \"text\"        send in one line, without the prompt\n"
    "      --local               do not send it: fly the arriving bird here,\n"
    "                            so you can see what she sees\n"
    "  aviary status             what is running, what is paired, what is wrong\n"
    "  aviary restart            bounce the overlay (it starts itself anyway)\n"
    "  aviary stop               stop the overlay\n"
    "  aviary daemon [--pixel N] run it in the foreground; blocks this terminal\n"
    "\n"
    "less often\n"
    "  aviary link <topic> <passphrase>   pair using a topic you chose yourself\n"
    "  aviary listen                      watch the relay by hand\n"
    "  aviary render DIR / sheet F / sizes F   offline frame dumps\n"
    "\n"
    "  --pixel N   device pixels per sprite pixel (default 2; 1 = oneko 1:1)\n");
}

int main(int argc, char **argv) {
  /* `send` used to fly a bird on this screen and go no further, which is a
   * trap with a name on it: the word means what it means, and a letter that
   * quietly never left is indistinguishable from one that arrived. It goes to
   * the other laptop too now, unless --local says otherwise. */
  if (argc >= 2 && !strcmp(argv[1], "send")) {
    AvConfig cfg;
    int linked = av_config_load(&cfg);
    const char *from = linked && cfg.name[0] ? cfg.name : "";
    const char *bird = linked && cfg.bird[0] ? cfg.bird : "phoenix";
    int local_only = 0;
    char text[LETTER_MAX_TEXT] = {0};
    size_t tl = 0;
    for (int i = 2; i < argc; i++) {
      if (!strcmp(argv[i], "--from") && i + 1 < argc) { from = argv[++i]; continue; }
      if (!strcmp(argv[i], "--bird") && i + 1 < argc) { bird = argv[++i]; continue; }
      if (!strcmp(argv[i], "--local")) { local_only = 1; continue; }
      int n = snprintf(text + tl, sizeof(text) - tl, "%s%s", tl ? " " : "", argv[i]);
      if (n > 0) tl += (size_t)n;
      if (tl >= sizeof(text) - 1) break;
    }
    if (!tl) { fprintf(stderr, "aviary send: nothing to say\n"); return 1; }

    av_ensure_daemon();

    /* --local is for looking at it: fly the bird that *arrives*, letter and
     * all, which is the half you otherwise never see from this end. A real
     * send is the other half — a bird coming to the cursor to collect it. */
    if (local_only)
      return client_send(text, from, bird, 0, -1, -1, 0);

    /* the cursor is already on the line below the command, which is where the
     * bird should come for it */
    double fx = -1, fy = -1;
    terminal_spot(0, &fx, &fy);

    int flew = client_send(text, from, bird, 1, fx, fy, 0) == 0;
    if (!linked) {
      printf("  not paired, so this stayed here. `aviary invite` sets that up.\n");
      return flew ? 0 : 1;
    }
    double flight = depart_seconds(scene_species_from_name(bird)) + cfg.travel;
    int sent = net_publish(text, from, bird, flight) == 0;
    if (sent && !flew) printf("  delivered, but no bird flew here.\n");
    return sent ? 0 : 1;
  }
  if (argc >= 2 && !strcmp(argv[1], "status")) return status_main();
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
  if (argc >= 2 && !strcmp(argv[1], "stop")) {
    /* systemd would only start it straight back up again */
    if (system("systemctl --user stop aviary.service >/dev/null 2>&1") != 0) { /* fine */ }
    printf(av_daemon_stop() ? "  stopped.\n" : "  it was not running.\n");
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "restart")) {
    return av_daemon_bounce() ? (printf("  daemon running.\n"), 0)
                              : (fprintf(stderr, "  could not start it\n"), 1);
  }
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
