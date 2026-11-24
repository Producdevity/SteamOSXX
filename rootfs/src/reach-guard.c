// Reboots (back to muOS in slot 1) when the device has been unreachable for 4 minutes, so a
// running-but-unreachable system cannot strand the device while PID 1 keeps feeding the watchdog.
// Under memory thrash adbd and sshd stay "up" but stop answering, and anything that must be paged in
// (a shell, systemctl) stalls too, so this is locked in memory, probes sshd with a real banner read
// and calls reboot(2) itself.
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

#define PERIOD 30
#define LIMIT 8

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

int main(void) {
  if (mlockall(MCL_CURRENT | MCL_FUTURE)) perror("reach-guard: mlockall");
  int bad = 0;
  for (;;) {
    sleep(PERIOD);
    if ((adb_up() || wifi_up()) && sshd_answers(20000)) { bad = 0; continue; }
    bad++;
    fprintf(stderr, "reach-guard: unreachable for %d s\n", bad * PERIOD);
    if (bad >= LIMIT) {
      fprintf(stderr, "reach-guard: rebooting\n");
      reboot(RB_AUTOBOOT);
    }
  }
}
