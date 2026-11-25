// Reboots (back to muOS in slot 1) when the device has been unreachable for 4 minutes, so a
// running-but-unreachable system cannot strand the device while PID 1 keeps feeding the watchdog.
// Under memory thrash adbd and sshd stay "up" but stop answering, and anything that must be paged in
// (a shell, systemctl) stalls too, so this is locked in memory, probes sshd with a real banner read
// and calls reboot(2) itself.
// Before it comes to that, it ends the running game when memory stalls stay high: a game larger than RAM
// makes the system crawl long before swap runs out, which earlyoom's free-memory thresholds never see.
#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>

#define TICK 5
#define REACH_TICKS 6     // reachability probe every 30 s
#define REACH_LIMIT 8     // reboot after 8 failed probes (4 min)
#define STALL_FULL 30.0   // /proc/pressure/memory "full avg10" percent that counts as thrashing
#define STALL_TICKS 6     // for 30 s in a row

static int read_file(const char *path, char *buf, int len) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  int n = read(fd, buf, len - 1);
  close(fd);
  if (n < 0) return -1;
  buf[n] = 0;
  return n;
}

static int adb_up(void) {
  char buf[64];
  if (read_file("/sys/kernel/config/usb_gadget/g1/UDC", buf, sizeof buf) <= 1) return 0;
  DIR *dir = opendir("/proc");
  if (!dir) return 0;
  int found = 0;
  struct dirent *e;
  while (!found && (e = readdir(dir))) {
    if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
    char path[300];
    snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
    found = read_file(path, buf, sizeof buf) > 0 && !strcmp(buf, "adbd\n");
  }
  closedir(dir);
  return found;
}

static int wifi_up(void) {
  struct ifaddrs *ifs, *i;
  int up = 0;
  if (getifaddrs(&ifs)) return 0;
  for (i = ifs; i; i = i->ifa_next)
    up |= i->ifa_addr && i->ifa_addr->sa_family == AF_INET && !strcmp(i->ifa_name, "wlan0");
  freeifaddrs(ifs);
  return up;
}

static int sshd_answers(int timeout_ms) {
  int s = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (s < 0) return 0;
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(22), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  connect(s, (struct sockaddr *)&a, sizeof a);
  struct pollfd p = {.fd = s, .events = POLLIN};
  char buf[8] = {0};
  int ok = poll(&p, 1, timeout_ms) == 1 && read(s, buf, 4) == 4 && !memcmp(buf, "SSH-", 4);
  close(s);
  return ok;
}

static double memory_stall(void) {
  char buf[256];
  if (read_file("/proc/pressure/memory", buf, sizeof buf) <= 0) return 0;
  char *full = strstr(buf, "full avg10=");
  return full ? atof(full + 11) : 0;
}

struct proc { int pid, ppid, reaper; long rss; };

// Ends every game Steam launched: the process trees under `reaper SteamLaunch ...`. Steam itself, Xorg
// and the services are never in those trees.
static int kill_games(void) {
  static struct proc ps[4096];
  int n = 0;
  DIR *dir = opendir("/proc");
  if (!dir) return 0;
  struct dirent *e;
  char path[300], buf[4096];
  while (n < 4096 && (e = readdir(dir))) {
    if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
    snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
    if (read_file(path, buf, sizeof buf) <= 0) continue;
    char *paren = strrchr(buf, ')');
    struct proc p = {atoi(e->d_name), 0, 0, 0};
    if (!paren || sscanf(paren + 1, " %*c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u %ld",
                         &p.ppid, &p.rss) != 2)
      continue;
    snprintf(path, sizeof path, "/proc/%s/cmdline", e->d_name);
    int len = read_file(path, buf, sizeof buf);
    if (len > 0) {
      const char *base = strrchr(buf, '/');
      if (!strcmp(base ? base + 1 : buf, "reaper"))
        for (int i = 0; i < len; i += strlen(buf + i) + 1) p.reaper |= !strcmp(buf + i, "SteamLaunch");
    }
    ps[n++] = p;
  }
  closedir(dir);
  int killed = 0;
  for (int i = 0; i < n; i++) {
    int pid = ps[i].pid, in_game = 0;
    for (int depth = 0; pid > 1 && depth < 64 && !in_game; depth++) {
      int j = 0;
      while (j < n && ps[j].pid != pid) j++;
      if (j == n) break;
      in_game = ps[j].reaper;
      pid = ps[j].ppid;
    }
    if (in_game && !ps[i].reaper) {
      fprintf(stderr, "reach-guard: killing game process %d (%ld pages)\n", ps[i].pid, ps[i].rss);
      kill(ps[i].pid, SIGKILL);
      killed++;
    }
  }
  return killed;
}

int main(void) {
  if (mlockall(MCL_CURRENT | MCL_FUTURE)) perror("reach-guard: mlockall");
  int bad = 0, stalled = 0;
  for (unsigned long tick = 1;; tick++) {
    sleep(TICK);
    double stall = memory_stall();
    stalled = stall >= STALL_FULL ? stalled + 1 : 0;
    if (stalled >= STALL_TICKS) {
      fprintf(stderr, "reach-guard: memory stalled %.0f%% for %d s\n", stall, stalled * TICK);
      if (kill_games()) stalled = 0;
    }
    if (tick % REACH_TICKS) continue;
    if ((adb_up() || wifi_up()) && sshd_answers(20000)) { bad = 0; continue; }
    bad++;
    fprintf(stderr, "reach-guard: unreachable for %d s\n", bad * REACH_TICKS * TICK);
    if (bad >= REACH_LIMIT) {
      fprintf(stderr, "reach-guard: rebooting\n");
      reboot(RB_AUTOBOOT);
    }
  }
}
