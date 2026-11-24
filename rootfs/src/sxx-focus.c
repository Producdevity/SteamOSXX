// Gamescope's focus protocol on a plain X server. Steam in -steamos3 mode publishes which apps should be
// on top in GAMESCOPECTRL_BASELAYER_APPID (and GAMESCOPECTRL_BASELAYER_WINDOW) and reads back which apps
// have windows from GAMESCOPE_FOCUSABLE_APPS / _WINDOWS and GAMESCOPE_FOCUSED_*. This daemon plays the
// gamescope side: it tags game windows with STEAM_GAME, publishes the focusable lists and activates the
// window of the first app in Steam's base-layer list through the window manager.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define STEAM_APPID 769
#define MAXW 256

static Display *d;
static Window root;
static Atom a_client_list, a_active, a_steam_game, a_pid, a_base_appid, a_base_window,
    a_focusable_apps, a_focusable_windows, a_focused_app, a_focused_app_gfx, a_focused_window;
static int verbose;

static int x_error(Display *dpy, XErrorEvent *e) { (void)dpy; (void)e; return 0; }

static long *get_cardinals(Window w, Atom prop, Atom type, unsigned long *n) {
  Atom t; int f; unsigned long after; unsigned char *data = NULL;
  *n = 0;
  if (XGetWindowProperty(d, w, prop, 0, 4096, False, type, &t, &f, n, &after, &data) != Success || !data)
    return NULL;
  if (f != 32) { XFree(data); *n = 0; return NULL; }
  return (long *)data;
}

static unsigned long get_cardinal(Window w, Atom prop, unsigned long dflt) {
  unsigned long n; long *v = get_cardinals(w, prop, XA_CARDINAL, &n);
  unsigned long r = n ? (unsigned long)v[0] : dflt;
  if (v) XFree(v);
  return r;
}

static unsigned long appid_from_pid(unsigned long pid) {
  char path[64], buf[65536];
  snprintf(path, sizeof path, "/proc/%lu/environ", pid);
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  size_t len = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[len] = 0;
  for (size_t i = 0; i < len; i += strlen(buf + i) + 1) {
    if (!strncmp(buf + i, "SteamGameId=", 12) || !strncmp(buf + i, "SteamAppId=", 11)) {
      unsigned long id = strtoul(strchr(buf + i, '=') + 1, NULL, 10);
      if (id) return id;
    }
  }
  return 0;
}

static unsigned long window_appid(Window w, unsigned long pid) {
  unsigned long id = get_cardinal(w, a_steam_game, 0);
  if (id) return id;
  XClassHint ch;
  if (XGetClassHint(d, w, &ch)) {
    if (ch.res_name && !strncmp(ch.res_name, "steam_app_", 10)) id = strtoul(ch.res_name + 10, NULL, 10);
    if (ch.res_name) XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
  }
  if (!id && pid) id = appid_from_pid(pid);
  if (id) {  // Steam sets this itself under gamescope; games launched in the container are not matched.
    long v = (long)id;
    XChangeProperty(d, w, a_steam_game, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&v, 1);
  }
  return id;
}

struct win { Window w; unsigned long appid, pid; long area; };

static void sync_focus(void) {
  unsigned long n;
  long *clients = get_cardinals(root, a_client_list, XA_WINDOW, &n);
  struct win wins[MAXW]; int nw = 0;
  for (unsigned long i = 0; i < n && nw < MAXW; i++) {
    Window w = (Window)clients[i];
    XWindowAttributes wa;
    if (!XGetWindowAttributes(d, w, &wa) || wa.map_state != IsViewable || wa.width < 64 || wa.height < 64) continue;
    unsigned long pid = get_cardinal(w, a_pid, 0);
    unsigned long id = window_appid(w, pid);
    if (!id) continue;
    wins[nw++] = (struct win){w, id, pid, (long)wa.width * wa.height};
  }
  if (clients) XFree(clients);

  long apps[MAXW], triples[MAXW * 3]; int na = 0;
  for (int i = 0; i < nw; i++) {
    int seen = 0;
    for (int j = 0; j < na; j++) seen |= apps[j] == (long)wins[i].appid;
    if (!seen) apps[na++] = (long)wins[i].appid;
    triples[i * 3] = (long)wins[i].w; triples[i * 3 + 1] = (long)wins[i].appid; triples[i * 3 + 2] = (long)wins[i].pid;
  }
  XChangeProperty(d, root, a_focusable_apps, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)apps, na);
  XChangeProperty(d, root, a_focusable_windows, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)triples, nw * 3);

  struct win *pick = NULL;
  unsigned long base_win = get_cardinal(root, a_base_window, 0);
  for (int i = 0; i < nw && base_win; i++) if (wins[i].w == base_win) pick = &wins[i];
  unsigned long nb;
  long *base = get_cardinals(root, a_base_appid, XA_CARDINAL, &nb);
  for (unsigned long b = 0; b < nb && !pick; b++)
    for (int i = 0; i < nw; i++)
      if ((long)wins[i].appid == base[b] && (!pick || wins[i].area >= pick->area)) pick = &wins[i];
  if (base) XFree(base);
  // Without a base-layer request gamescope shows the game over Steam.
  for (int i = 0; i < nw && !pick && !nb; i++) if (wins[i].appid != STEAM_APPID) pick = &wins[i];
  for (int i = 0; i < nw && !pick; i++) pick = &wins[i];
  if (!pick) { XFlush(d); return; }

  long app = (long)pick->appid, w = (long)pick->w;
  XChangeProperty(d, root, a_focused_app, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&app, 1);
  XChangeProperty(d, root, a_focused_app_gfx, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&app, 1);
  XChangeProperty(d, root, a_focused_window, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&w, 1);
  Window active = (Window)get_cardinal(root, a_active, 0);
  if (active != pick->w) {
    if (verbose) fprintf(stderr, "sxx-focus: activating 0x%lx (app %lu)\n", pick->w, pick->appid);
    XEvent ev = {0};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = pick->w;
    ev.xclient.message_type = a_active;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;  // source: pager, so the WM does not apply focus-stealing prevention
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XRaiseWindow(d, pick->w);
  }
  XFlush(d);
}

int main(int argc, char **argv) {
  verbose = argc > 1 && !strcmp(argv[1], "-v");
  if (!(d = XOpenDisplay(NULL))) { fprintf(stderr, "sxx-focus: cannot open display\n"); return 1; }
  XSetErrorHandler(x_error);
  root = DefaultRootWindow(d);
#define A(v, s) v = XInternAtom(d, s, False)
  A(a_client_list, "_NET_CLIENT_LIST"); A(a_active, "_NET_ACTIVE_WINDOW"); A(a_steam_game, "STEAM_GAME");
  A(a_pid, "_NET_WM_PID"); A(a_base_appid, "GAMESCOPECTRL_BASELAYER_APPID");
  A(a_base_window, "GAMESCOPECTRL_BASELAYER_WINDOW"); A(a_focusable_apps, "GAMESCOPE_FOCUSABLE_APPS");
  A(a_focusable_windows, "GAMESCOPE_FOCUSABLE_WINDOWS"); A(a_focused_app, "GAMESCOPE_FOCUSED_APP");
  A(a_focused_app_gfx, "GAMESCOPE_FOCUSED_APP_GFX"); A(a_focused_window, "GAMESCOPE_FOCUSED_WINDOW");
  XSelectInput(d, root, PropertyChangeMask | SubstructureNotifyMask);
  sync_focus();
  int fd = ConnectionNumber(d);
  for (;;) {
    int relevant = 0;
    while (XPending(d)) {
      XEvent ev;
      XNextEvent(d, &ev);
      if (ev.type == PropertyNotify) {
        Atom a = ev.xproperty.atom;
        relevant |= a == a_client_list || a == a_base_appid || a == a_base_window || a == a_active;
      } else if (ev.type == MapNotify || ev.type == UnmapNotify || ev.type == ConfigureNotify) {
        relevant = 1;
      }
    }
    if (relevant) sync_focus();
    fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
    struct timeval tv = {2, 0};
    // The periodic pass picks up windows whose size or class changed without a root-level event.
    if (select(fd + 1, &fds, NULL, NULL, &tv) == 0) sync_focus();
  }
}
