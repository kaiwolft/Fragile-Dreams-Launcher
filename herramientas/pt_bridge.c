/*
 * pt_bridge - puente de mando/ventanas para Linux (Dionixu's Launcher · Pandora's Tower)
 * Mismo protocolo que bridge.ps1 en Windows.
 *
 * Emite (stdout):  READY · PAD <nombre> · WIN <xid> · COMBO (L3+R3) · COMBO KB (F1 con el juego delante)
 *                  BTN A|B|UP|DOWN|LEFT|RIGHT (con repeticion al mantener). Letras con la posicion de Nintendo,
 *                  igual que bridge.ps1: BTN A = boton derecho (B de Xbox), BTN B = boton de abajo (A de Xbox).
 * Acepta (stdin):  GAMEPID <pid> · FOCUS <xid> · FOCUSPID <pid> · CLOSEPID <pid>
 *                  BORDERLESS <pid> <w> <h> · QUIT
 *
 * Mando: API de joystick del kernel (/dev/input/js*), legible sin permisos especiales.
 * Ventanas/teclado: X11 cargado en tiempo de ejecucion (Dolphin usa X11 tambien en Wayland
 * via XWayland). Si no hay X11, el mando sigue funcionando.
 */
#define _DEFAULT_SOURCE   /* no _GNU_SOURCE: evita simbolos de glibc 2.38 (strtol C23) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>
#include <linux/input-event-codes.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

/* ------------------------------------------------------------------ salida */
static pthread_mutex_t out_mx = PTHREAD_MUTEX_INITIALIZER;
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void say(const char *fmt, ...)
{
  va_list ap; va_start(ap, fmt);
  pthread_mutex_lock(&out_mx); vprintf(fmt, ap); putchar('\n'); fflush(stdout); pthread_mutex_unlock(&out_mx);
  va_end(ap);
}
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

/* ------------------------------------------------------------------ comandos (hilo de stdin) */
#define QMAX 32
static char queue[QMAX][160];
static int q_head, q_tail;
static pthread_mutex_t q_mx = PTHREAD_MUTEX_INITIALIZER;
static volatile int quit_req;
static void *stdin_thread(void *u)
{
  char buf[160]; (void)u;
  while (fgets(buf, sizeof buf, stdin)) {
    pthread_mutex_lock(&q_mx);
    if ((q_tail + 1) % QMAX != q_head) { strncpy(queue[q_tail], buf, sizeof queue[0] - 1); queue[q_tail][sizeof queue[0] - 1] = 0; q_tail = (q_tail + 1) % QMAX; }
    pthread_mutex_unlock(&q_mx);
  }
  quit_req = 1;   /* el launcher cerro la tuberia */
  return NULL;
}
static int pop_cmd(char *dst)
{
  int ok = 0;
  pthread_mutex_lock(&q_mx);
  if (q_head != q_tail) { strcpy(dst, queue[q_head]); q_head = (q_head + 1) % QMAX; ok = 1; }
  pthread_mutex_unlock(&q_mx);
  return ok;
}

/* ------------------------------------------------------------------ X11 (dlopen) */
static void *xl;
static Display *dpy;
static Window root;
#define XF(name) static __typeof__(name) *p_##name
XF(XOpenDisplay); XF(XInternAtom); XF(XGetWindowProperty); XF(XFree); XF(XSendEvent); XF(XFlush);
XF(XRaiseWindow); XF(XSetInputFocus); XF(XMoveResizeWindow); XF(XChangeProperty); XF(XQueryKeymap);
XF(XKeysymToKeycode); XF(XDefaultScreen); XF(XDisplayWidth); XF(XDisplayHeight); XF(XGetGeometry);
XF(XSetErrorHandler); XF(XSync); XF(XMapRaised);
static Atom A_CLIENTS, A_ACTIVE, A_PID, A_CLASS, A_MOTIF, A_PROTO, A_DELETE;
static KeyCode kc_f1;
static int xerr(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }   /* nunca abortar por ventanas que desaparecen */

static int x11_init(void)
{
  const char *libs[] = { "libX11.so.6", "libX11.so", NULL };
  int i;
  if (!getenv("DISPLAY")) return 0;
  for (i = 0; libs[i] && !xl; i++) xl = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
  if (!xl) return 0;
#define L(name) if (!(p_##name = (__typeof__(p_##name))dlsym(xl, #name))) return 0
  L(XOpenDisplay); L(XInternAtom); L(XGetWindowProperty); L(XFree); L(XSendEvent); L(XFlush);
  L(XRaiseWindow); L(XSetInputFocus); L(XMoveResizeWindow); L(XChangeProperty); L(XQueryKeymap);
  L(XKeysymToKeycode); L(XDefaultScreen); L(XDisplayWidth); L(XDisplayHeight); L(XGetGeometry);
  L(XSetErrorHandler); L(XSync); L(XMapRaised);
#undef L
  if (!(dpy = p_XOpenDisplay(NULL))) return 0;
  p_XSetErrorHandler(xerr);
  root = DefaultRootWindow(dpy);
  A_CLIENTS = p_XInternAtom(dpy, "_NET_CLIENT_LIST", False);
  A_ACTIVE = p_XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
  A_PID = p_XInternAtom(dpy, "_NET_WM_PID", False);
  A_CLASS = XA_WM_CLASS;
  A_MOTIF = p_XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
  A_PROTO = p_XInternAtom(dpy, "WM_PROTOCOLS", False);
  A_DELETE = p_XInternAtom(dpy, "WM_DELETE_WINDOW", False);
  kc_f1 = p_XKeysymToKeycode(dpy, XK_F1);
  return 1;
}
/* lee una propiedad; devuelve n elementos y el buffer (liberar con XFree) */
static unsigned char *getprop(Window w, Atom prop, Atom type, unsigned long *n)
{
  Atom at; int af; unsigned long ni, after; unsigned char *data = NULL;
  *n = 0;
  if (p_XGetWindowProperty(dpy, w, prop, 0, 4096, False, type, &at, &af, &ni, &after, &data) != Success || !data) return NULL;
  *n = ni;
  return data;
}
static int has_ci(const char *h, const char *n)   /* strcasestr portable */
{
  size_t ln = strlen(n);
  for (; *h; h++) if (strncasecmp(h, n, ln) == 0) return 1;
  return 0;
}
static int is_dolphin(Window w)
{
  unsigned long n; char *c = (char *)getprop(w, A_CLASS, XA_STRING, &n); int r = 0;
  if (c) {   /* WM_CLASS = "instancia\0clase\0" */
    unsigned long i; for (i = 0; i < n; i++) if (!c[i]) c[i] = ' ';
    r = has_ci(c, "dolphin");
    p_XFree(c);
  }
  return r;
}
static Window active_window(void)
{
  unsigned long n; Window *w = (Window *)getprop(root, A_ACTIVE, XA_WINDOW, &n), r = 0;
  if (w) { if (n) r = w[0]; p_XFree(w); }
  return r;
}
/* la ventana de Dolphin visible mas grande (la del juego) */
static Window dolphin_window(unsigned *ww, unsigned *hh)
{
  unsigned long n, i; Window *list = (Window *)getprop(root, A_CLIENTS, XA_WINDOW, &n), best = 0; unsigned long area = 0;
  if (!list) return 0;
  for (i = 0; i < n; i++) {
    Window r; int x, y; unsigned w, h, bw, d;
    if (!is_dolphin(list[i])) continue;
    if (!p_XGetGeometry(dpy, list[i], &r, &x, &y, &w, &h, &bw, &d)) continue;
    if ((unsigned long)w * h > area) { area = (unsigned long)w * h; best = list[i]; if (ww) *ww = w; if (hh) *hh = h; }
  }
  p_XFree(list);
  return best;
}
static void focus(Window w)
{
  XEvent e;
  if (!dpy || !w) return;
  memset(&e, 0, sizeof e);
  e.xclient.type = ClientMessage; e.xclient.window = w; e.xclient.message_type = A_ACTIVE; e.xclient.format = 32;
  e.xclient.data.l[0] = 2;   /* origen: "pager" (las peticiones del usuario no se bloquean) */
  e.xclient.data.l[1] = CurrentTime;
  p_XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
  p_XMapRaised(dpy, w); p_XRaiseWindow(dpy, w);
  p_XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
  p_XFlush(dpy);
}
static void close_window(Window w)
{
  XEvent e;
  if (!dpy || !w) return;
  memset(&e, 0, sizeof e);
  e.xclient.type = ClientMessage; e.xclient.window = w; e.xclient.message_type = A_PROTO; e.xclient.format = 32;
  e.xclient.data.l[0] = (long)A_DELETE; e.xclient.data.l[1] = CurrentTime;
  p_XSendEvent(dpy, w, False, NoEventMask, &e);
  p_XFlush(dpy);
}
static void borderless(Window w, int bw, int bh)
{
  long hints[5] = { 2, 0, 0, 0, 0 };   /* flags=decoraciones, decoraciones=ninguna */
  int scr, sw, sh;
  if (!dpy || !w) return;
  p_XChangeProperty(dpy, w, A_MOTIF, A_MOTIF, 32, PropModeReplace, (unsigned char *)hints, 5);
  scr = p_XDefaultScreen(dpy); sw = p_XDisplayWidth(dpy, scr); sh = p_XDisplayHeight(dpy, scr);
  if (bw > sw || bh > sh) { bw = sw; bh = sh; }
  p_XMoveResizeWindow(dpy, w, (sw - bw) / 2, (sh - bh) / 2, (unsigned)bw, (unsigned)bh);
  p_XFlush(dpy);
}
static int f1_down(void)
{
  char keys[32];
  if (!dpy || !kc_f1) return 0;
  p_XQueryKeymap(dpy, keys);
  return (keys[kc_f1 >> 3] >> (kc_f1 & 7)) & 1;
}

/* ------------------------------------------------------------------ mando (joystick del kernel) */
#define MAXJS 8
typedef struct { int fd; uint16_t btnmap[KEY_MAX - BTN_MISC + 1]; uint8_t axmap[ABS_CNT]; int nbtn, nax; uint32_t bits; int ax[ABS_CNT]; } Pad;
static Pad pads[MAXJS];
enum { P_UP = 1, P_DOWN = 2, P_LEFT = 4, P_RIGHT = 8, P_L3 = 0x40, P_R3 = 0x80, P_A = 0x1000, P_B = 0x2000 };

static void pads_scan(void)
{
  int i;
  for (i = 0; i < MAXJS; i++) {
    char path[32], name[128] = "";
    Pad *p = &pads[i];
    if (p->fd > 0) continue;
    snprintf(path, sizeof path, "/dev/input/js%d", i);
    p->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (p->fd < 0) { p->fd = 0; continue; }
    { uint8_t n = 0; ioctl(p->fd, JSIOCGBUTTONS, &n); p->nbtn = n; ioctl(p->fd, JSIOCGAXES, &n); p->nax = n; }
    ioctl(p->fd, JSIOCGBTNMAP, p->btnmap);
    ioctl(p->fd, JSIOCGAXMAP, p->axmap);
    ioctl(p->fd, JSIOCGNAME(sizeof name), name);
    p->bits = 0; memset(p->ax, 0, sizeof p->ax);
    say("PAD %s", name[0] ? name : "?");
  }
}
static uint32_t btn_bit(uint16_t code)
{
  switch (code) {
    case BTN_SOUTH: return P_B;   /* posicion de Nintendo (igual que en Windows) */
    case BTN_EAST: return P_A;
    case BTN_THUMBL: return P_L3;
    case BTN_THUMBR: return P_R3;
    case BTN_DPAD_UP: return P_UP;
    case BTN_DPAD_DOWN: return P_DOWN;
    case BTN_DPAD_LEFT: return P_LEFT;
    case BTN_DPAD_RIGHT: return P_RIGHT;
  }
  return 0;
}
static uint32_t pads_read(void)
{
  uint32_t all = 0; int i;
  for (i = 0; i < MAXJS; i++) {
    Pad *p = &pads[i]; struct js_event ev; uint32_t dir = 0;
    if (p->fd <= 0) continue;
    for (;;) {
      ssize_t r = read(p->fd, &ev, sizeof ev);
      if (r != sizeof ev) { if (r < 0 && errno != EAGAIN) { close(p->fd); p->fd = 0; } break; }
      if ((ev.type & ~JS_EVENT_INIT) == JS_EVENT_BUTTON && ev.number < p->nbtn) {
        uint32_t b = btn_bit(p->btnmap[ev.number]);
        if (ev.value) p->bits |= b; else p->bits &= ~b;
      } else if ((ev.type & ~JS_EVENT_INIT) == JS_EVENT_AXIS && ev.number < p->nax) {
        p->ax[p->axmap[ev.number]] = ev.value;
      }
    }
    if (p->ax[ABS_X] < -20000 || p->ax[ABS_HAT0X] < 0) dir |= P_LEFT;
    if (p->ax[ABS_X] > 20000 || p->ax[ABS_HAT0X] > 0) dir |= P_RIGHT;
    if (p->ax[ABS_Y] < -20000 || p->ax[ABS_HAT0Y] < 0) dir |= P_UP;
    if (p->ax[ABS_Y] > 20000 || p->ax[ABS_HAT0Y] > 0) dir |= P_DOWN;
    all |= p->bits | dir;
  }
  return all;
}

/* ------------------------------------------------------------------ programa */
int main(void)
{
  pthread_t th;
  int x = x11_init(), combo_latch = 0, esc_latch = 0, rep[4] = {0, 0, 0, 0}, game_pid = 0, win_said = 0;
  uint32_t prev = 0;
  double last_scan = 0, last_win = 0;
  signal(SIGPIPE, SIG_IGN);
  pthread_create(&th, NULL, stdin_thread, NULL); pthread_detach(th);
  say("READY");
  say(x ? "X11 OK" : "X11 NO");
  pads_scan();
  while (!quit_req) {
    char cmd[160];
    uint32_t b, nw; int k;
    double t = now_ms();
    while (pop_cmd(cmd)) {
      char op[32] = "", *q = cmd, *e; unsigned long a1 = 0; int a2 = 0, a3 = 0, i = 0;
      while (*q == ' ') q++;
      while (*q && *q != ' ' && *q != '\n' && *q != '\r' && i < 31) op[i++] = *q++;
      op[i] = 0;
      a1 = strtoul(q, &e, 10); q = e; a2 = (int)strtol(q, &e, 10); q = e; a3 = (int)strtol(q, &e, 10);
      if (!strcmp(op, "QUIT")) quit_req = 1;
      else if (!strcmp(op, "GAMEPID")) { game_pid = (int)a1; win_said = 0; }
      else if (!strcmp(op, "FOCUS")) { if (x) focus((Window)a1); }
      else if (!strcmp(op, "FOCUSPID")) { if (x) focus(dolphin_window(NULL, NULL)); }
      else if (!strcmp(op, "CLOSEPID")) { if (x) close_window(dolphin_window(NULL, NULL)); if (a1) kill((pid_t)a1, SIGTERM); }
      else if (!strcmp(op, "BORDERLESS")) { if (x) borderless(dolphin_window(NULL, NULL), a2, a3); }
    }
    if (t - last_scan > 2000) { pads_scan(); last_scan = t; }
    /* ventana del juego lista (para el fundido del launcher) */
    if (x && game_pid && !win_said && t - last_win > 200) {
      unsigned w = 0, h = 0; Window dw = dolphin_window(&w, &h);
      last_win = t;
      if (dw && w >= 320 && h >= 240) { say("WIN %lu", (unsigned long)dw); win_said = 1; }
    }
    /* mando */
    b = pads_read();
    if ((b & (P_L3 | P_R3)) == (P_L3 | P_R3)) { if (!combo_latch) say("COMBO"); combo_latch = 1; } else combo_latch = 0;
    /* teclado: F1 con el juego delante (Esc es el boton + del juego) */
    if (x && game_pid) {
      int e = f1_down();
      if (e && !esc_latch && is_dolphin(active_window())) say("COMBO KB");
      esc_latch = e;
    }
    nw = b & ~prev;
    if (nw & P_A) say("BTN A");
    if (nw & P_B) say("BTN B");
    {
      static const uint32_t dm[4] = { P_UP, P_DOWN, P_LEFT, P_RIGHT };
      static const char *dn[4] = { "UP", "DOWN", "LEFT", "RIGHT" };
      for (k = 0; k < 4; k++) {
        if (nw & dm[k]) { say("BTN %s", dn[k]); rep[k] = 14; }
        else if (b & dm[k]) { if (--rep[k] <= 0) { say("BTN %s", dn[k]); rep[k] = 4; } }
      }
    }
    prev = b;
    usleep(30000);
  }
  return 0;
}
