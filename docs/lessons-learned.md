# Lessons learned: Steam on the Anbernic RG40XX-H

Everything that cost time on this device, with the evidence. Read this before changing the boot chain,
the RAM settings or the recovery path.

## The device

- Anbernic RG40XX-H, Allwinner H700 (H616 family, SoC id `0x1823`), 4x Cortex-A53 (ARMv8.0, **no LSE
  atomics**), Mali-G31, 640x480 panel, AXP717 PMIC, Realtek RTL8821CS Wi-Fi on SDIO.
- **RAM: LPDDR4 @ 672 MHz** on this unit (read from muOS's boot0, see below).
- Two TF slots: slot 1 is the boot ROM's boot device (`mmc0`, controller `mmc@4020000`, card detect PF6);
  slot 2 is controller `mmc@4022000` (kernel `mmc1` alias in ROCKNIX's DT, U-Boot `mmc 2` with our DT
  patch), card detect PE22, powered by a fixed 3.3 V regulator switched by GPIO PE4.
- The data-capable USB-C port is the one muOS used for adb. The yellow LED is charging; a **red LED** only
  comes on once our mainline U-Boot proper is running (`CONFIG_LED_STATUS`, GPIO 267).

## RAM settings (the reason the first card was dead)

- ROCKNIX's H700 images come in DDR3 and DDR4 variants. Their updater picks by the `vdd-dram` voltage
  (1.2 V DDR3, 1.1 V DDR4). This unit reads 1.1 V, and its RAM really is LPDDR4, **but ROCKNIX's LPDDR4
  timing values do not train on it**: the SPL resets the board, no LED, no screen, nothing on USB.
- The right values are in muOS's own boot0 (`dd if=/dev/mmcblk0 bs=1K skip=8 count=128` on muOS, then
  `tools/boot0-dram.py`). Differences from ROCKNIX's `anbernic_rg35xx_h700_lpddr4_defconfig`:

  | | ROCKNIX LPDDR4 | this unit (muOS boot0) |
  |---|---|---|
  | TPR6 | 0x40808080 | 0x44808080 |
  | TPR11 | 0x1b1f1e1c | 0x23272624 |
  | TPR12 | 0x06060606 | 0x05070706 |

  `uboot/rg40xxh_2025_lpddr4_defconfig` uses the muOS values; with it the SPL trains RAM and U-Boot runs.
- Other Anbernic H700 revisions may differ again. **Always compare against the unit's own boot0** before
  trusting a distribution's bootloader.
- ROCKNIX's config also sets `CONFIG_AXP_DISABLE_BOOT_ON_POWERON`, which refuses to boot when the power-on
  cause was not the power key. Our configs drop it.

## FEL (USB recovery mode) on Windows

- The boot ROM enters FEL (`1f3a:efe8`) when no slot-1 card has a valid `eGON.BT0` header, or when the
  vendor kernel reboots with the `efex` reason. FEL needs a WinUSB driver on Windows (Zadig, user-approved,
  one time). `sunxi-fel.exe` is cross-built from linux-sunxi/sunxi-tools master with MinGW, libusb 1.0.27
  static, libfdt, `-lz -lws2_32 -lpthread`.
- **Getting into FEL without touching the device:** from muOS, busybox `reboot efex` drops the argument
  and just powers off. Use the real syscall so the vendor RTC driver writes its flag:
  `syscall(142, 0xfee1dead, 672274793, 0xA1B2C3D4, "efex")` (python3 from the Debian chroot). The device
  comes back in FEL with both cards inserted. `tools/fel-boot.sh` does this.
- **Never read or write DRAM over FEL before an SPL has trained it.** `sunxi-fel readl 0x40000000` hangs
  the chip; with USB connected the board stays powered and needs a full power cycle (unplug USB, long press).
- `sunxi-fel spl` on this SoC does not return to FEL; the USB connection drops ("Pipe error"). That is not
  a RAM failure. Use `sunxi-fel uboot` instead: it reported "Executing the SPL... done", loaded TF-A and
  U-Boot into DRAM and started them, which proves RAM works.
- After U-Boot takes over from FEL, Windows keeps showing the FEL device as present (stale), because
  U-Boot does not reset the USB link. Check with `sunxi-fel ver`, not Device Manager.
- A script passed with `sunxi-fel uboot ... write 0x4FC00000 boot.scr` was not run by this U-Boot. Bake
  the boot command into the build instead (`uboot/fel-boot.config`).

## Power and recovery quirks

- A short tap of the power key does not always wake it from a clean shutdown; hold 2-3 s.
- With USB attached, a hung SoC may never lose power: a 10-25 s hold plus unplugging USB was needed.
  "USB device not recognized / Device Descriptor Request Failed" on the PC meant the SoC was hung, not
  that the port was bad.
- **Recovery layout:** keep muOS in slot 1 (known-good boot chain + adb) and the OS under development in
  slot 2. Boot the slot-2 OS through FEL from muOS; any reset returns to muOS.

## The mainline OS image

- ROCKNIX's prebuilt kernel (7.2.0) has its own initramfs built in, which expects ROCKNIX's SYSTEM
  squashfs. Passing `rdinit=/no-initramfs` (a path that does not exist) makes Linux skip it and mount
  `root=` directly. The kernel config has no forced command line, so extlinux's APPEND is honored.
- ROCKNIX's boot partition has **no `dtb.img`** although its extlinux points at it: U-Boot falls back to
  its built-in generic DT on first boot and ROCKNIX installs the right one later. Our extlinux names the
  RG40XX-H DTB explicitly. muOS's cmdline says `lcd_type=old`, so the non-`v2-panel` DTB.
- Modules (52, incl. `rocknix-singleadc-joypad`) and Wi-Fi/BT firmware live in ROCKNIX's SYSTEM under
  `usr/lib/kernel-overlays/base`, not `usr/lib/modules` (that is a symlink to `/run`).
- `rtw88_8821cs` is built in and may probe before the root filesystem (with its firmware) is mounted;
  `wifi-kick.service` rebinds it if no `wlan` interface appears.

## Windows host tooling

- Raw SD writes need admin. `C:\Users\kurt\sdflash\sdflash-helper.ps1` runs elevated (one UAC prompt per
  start) and only writes to the card reader's LUN (`...\000000002960&1`, USB, 4-256 GB). Jobs:
  `<name>.img` + `<name>.go` in `C:\Users\kurt\sdflash\queue`.
- Windows mounts partitions from a half-written partition table and then refuses further writes
  ("Access denied"); removable media cannot be set offline. The helper writes the first MiB (MBR and
  U-Boot) **last**.
- `Clear-Disk` raises a media-change event that invalidates a handle opened right after it ("The media in
  the drive may have changed"); a second attempt on the now-blank disk succeeds.
- Git Bash rewrites `/opt/...` arguments to `C:/Program Files/Git/opt/...`; set `MSYS_NO_PATHCONV=1` for
  every adb call with device paths.
- Docker: `debian:trixie` may resolve to the arm64 image after an arm64 pull; pass `--platform linux/amd64`
  for host tools or they run under emulation (an extraction took 12 minutes instead of 1).
- Keep `cache/` (4 GB of ROCKNIX images and the Steam tarball) out of the Docker build context
  (`.dockerignore`).

- Python's text mode on Windows writes CRLF (`open(p, "w")`), and so do some editors; a CRLF
  `#!/bin/sh` script or fstab line in the image fails at boot. `.gitattributes` keeps the repository LF, but
  Docker copies the working tree, so `tools/build-rootfs.sh` refuses to build when any file has a CR. Write
  with `open(p, "w", newline="")`.

## muOS-side lessons (from the chroot project, steam-rg40xx)

- muOS's 4.9 kernel has no SysV IPC (`semget` fails, Steam's tier0 asserts), no user namespaces, no DRM.
- Steam's web UI needs `lsof` (it identifies its own websocket peers with it); without it: error 0x3008.
- Valve's `steamwebhelper.sh` pins `taskset 0x7c` (Deck cores 2-6), leaving 2 of 4 cores here; Steam's
  hang watchdog then kills the slow renderer. Rewrite the wrapper with the same byte size (the client's
  startup check only compares sizes) and add `--disable-hang-monitor`.
- The main Steam window is never mapped without a window manager (openbox).
- `pkill -f PATTERN` over `adb shell` matches the adb shell's own command line; put such commands in a
  script on the device.
- muOS's adbd has no `exec-out`; tar to a file on the device and `adb pull` it.

## Staying reachable: the recovery playbook (verified 2026-11-24)

Layout: muOS in slot 1 (the boot ROM's device, known-good, adb), our OS in slot 2, booted through FEL.
`tools/rescue.sh` finds the device in any of these states and gets a shell back; `tools/fel-boot.sh`
boots our OS again. A full round trip (our OS reboot -> muOS -> FEL -> reset -> muOS -> FEL -> our OS)
took 131 s with nobody touching the device.

| Failure | What brings it back |
|---|---|
| our OS hangs (kernel or PID 1) | sunxi hardware watchdog, `RuntimeWatchdogSec=30s` -> reset -> muOS |
| kernel panic / oops / soft lockup | `panic=10 oops=panic softlockup_panic=1` -> reboot -> muOS |
| our OS alive but neither adb nor SSH serving | `reach-guard` (C, memory-locked, real sshd banner probe) calls `reboot(2)` after 4 min -> muOS |
| memory thrash (zram full, userspace livelocked, kernel still answers ping) | `earlyoom`, MGLRU `min_ttl_ms`, swapfile behind zram; `reach-guard` as the backstop |
| anything asks our OS to power off | dev mode: `poweroff.target`/`halt.target` link to `reboot.target`, logind ignores the power key (`tools/devmode.sh`) |
| slot-2 card does not boot | `fel-boot.bin` resets instead of waiting -> muOS, which can rewrite `/dev/mmcblk1` |
| device stuck in FEL | `sunxi-fel uboot out/uboot/fel-reset.bin` (bootcmd `reset`) -> muOS |
| muOS itself | idle sleep is off (`settings/power/idle_sleep = 0`); it only suspends on a power-key press |

A thrash livelock is invisible to the watchdog (PID 1 still runs) and to process/port checks (adbd
"running", :22 "listening"): only a probe that needs an answer catches it.

Still needs a person: a SoC hung while USB keeps it powered and the watchdog is not running (before the
kernel arms it, or during suspend). So: never probe DRAM over FEL before an SPL, and only test suspend
with an RTC wake alarm (`rtcwake`).

Reaching it: adb (our OS serial `33806c00e4f7909a`, muOS `0c001080c69249221d2`), `ssh root@rg40xx.local`
with `secrets/id_ed25519`, screenshots with `tools/dev.sh shot` (X root window via xwd).

Logs from a boot that failed: the initramfs writes `bootlog-N.txt` to the slot-2 FAT (muOS mounts it at
`/mnt/sdcard`); later boots keep the systemd journal (`journalctl -b -1`).
