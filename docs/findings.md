# Findings log

Dated observations from bringing up the SteamOS-style image on the RG40XX-H. Newest last. Lessons that
change how we work also go into `lessons-learned.md`.

## 2026-11-24

### Boot
- First boot through FEL from muOS with our U-Boot (muOS LPDDR4 timings) booted: kernel 7.2.0, Tux + console
  on the panel, Xorg, Steam. The earlier attempt with `rdinit=/no-initramfs` (skipping ROCKNIX's built-in
  initramfs) reset within ~30 s and never reached our userspace (no journal, no first-boot marker); the same
  kernel with an external busybox initramfs (`INITRD`) boots. Not isolated further; the image now always uses
  the logging initramfs.
- Docker leaves `/etc/hostname`, `/etc/hosts`, `/etc/resolv.conf` empty in an exported rootfs; the kernel
  default hostname is then `ROCKNIX`. `build-image.sh` writes them after unpacking.
- Overlay files copied from Windows keep executable bits; systemd warns on every unit. The Dockerfile now
  chmods unit/config files 644.
- First boot grew the root partition to the 128 GB card (117 GB ext4). zram swap 975 MB is active.

### Hardware status on mainline (ROCKNIX DT `sun50i-h700-anbernic-rg40xx-h.dtb`)
- Display: DSI panel through sun4i-drm (`card1-DSI-1` connected, 640x480), backlight device present. HDMI
  connector exists (`card1-HDMI-A-1`, disconnected).
- GPU: Panfrost, desktop GL 3.1 / GLES 3.1 accelerated (Mesa 25.0.7, Debian), Xorg modesetting with glamor.
  Vulkan: only llvmpipe; Debian's Mesa does not ship PanVK for Bifrost v7 (needs a newer Mesa and
  `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`; gamescope additionally needs `VK_EXT_robustness2`/nullDescriptor,
  which PanVK only enables for v10+).
- Wi-Fi: RTL8821CS (`rtw88_8821cs`, built in) came up on first boot and joined the network from the profile
  converted out of muOS's wpa_supplicant.conf. `wifi-kick` was not needed this time.
- Input: `H700 Gamepad` (rocknix-singleadc-joypad, 484b:14df) uses standard codes: ABS_X/Y + ABS_RX/RY
  sticks, BTN_* face/shoulder buttons, BTN_DPAD_*; no remapper needed (muOS's `muOS-Keys` needed padmap).
  Volume keys are a separate `gpio-keys-volume` device; power key is `axp20x-pek`.
- Audio: H616 codec, card 0. The speaker works, but defaults are about -42 dB (DAC -21 dB plus Line Out
  -21 dB) under a 40 % PipeWire volume: inaudible. DAC and Line Out at 100 % (0 dB), `Speaker` on, volume
  left to PipeWire. `Line Out Source` stays `Stereo`. `steam-session-prep` sets this at every session start.

### Session
- Running Steam through `runuser` from a root service gave no logind session: no user systemd, so
  PipeWire/WirePlumber failed ("Dependency failed") and Xorg ran as root. Now `steam-session.service` is
  `User=steam` + `PAMName=login` + `TTYPath=/dev/tty7` running `startx`: logind session on seat0/tty7, rootless
  Xorg as steam, PipeWire + WirePlumber + pipewire-pulse active. Needs `kbd` (chvt) and `dbus-x11`.
- RTKit missing makes PipeWire fall back to nice 0; `rtkit` is now in the image.
- Steam's self-update stalled once at 10.7 MB with idle CPU and a working network; restarting the session
  resumed it. Watch for a repeat (possible LSE-Emu patching race in the downloader threads; fallback is
  `LSE_EMU_PATCH=0`, trap-only).
- Update download speed on mainline Wi-Fi: about 1-3 MB/s (muOS 4.9: about 0.45 MB/s).

### ROCKNIX H700 platform quirks worth reusing (from its SYSTEM squashfs, `usr/lib/autostart/quirks/platforms/H700`)
- Suspend mode `mem` by default (their TF-A carries an H616 PSCI system-suspend patch; ours is upstream
  TF-A lts-v2.12.1, which does not, so deep sleep needs that patch).
- Sleep hooks: mute the DAC before suspend and unmute after; LED handling; re-create the USB gadget after
  resume (musb drops the gadget session on suspend).
- CPU governor policy `cpufreq/policy0`; GPU devfreq at `/sys/devices/platform/soc/1800000.gpu/devfreq/...`;
  GPU overclock option; thermal sensor `thermal_zone2`.
- Panfrost GL extension override: `MESA_EXTENSION_OVERRIDE="-GL_OES_copy_image -GL_EXT_copy_image -GL_NV_copy_image -GL_ARB_copy_image"`
  (copy_image is broken/slow on G31 for some apps).
- Wi-Fi probe retry for RTL8821CS (`sdio:*v024CdC821*`), like our `wifi-kick`.
- Audio latency default 64 (RetroArch setting).
- Steam's bootstrapper re-downloads the whole package (~580 MB) when the cached zips in
  `Steam/package/` are missing ("Package file ... missing or incorrect size"), even with a valid
  `.installed` manifest. Seeding a Steam install from another device must include `package/*.zip*`.
- With the logind session, Xorg runs as `steam` with an auth cookie; root needs
  `runuser -u steam -- env DISPLAY=:0 XAUTHORITY=/home/steam/.Xauthority xwd -root` for screenshots.
- Volume keys (`gpio-keys-volume`) and menu+volume (backlight) handled by `handheld-keys.service`
  (backlight max 2499 on this panel).

### Sleep / wake (parked)
- ROCKNIX gets `mem` suspend on H700 from a TF-A patch (`uboot/tfa-patches/001-allwinner-h616-psci-system-suspend.patch`,
  from kailashrs' H700_rocknix_enhancement): BL31 copies a stub into SRAM A1, puts DRAM into self-refresh,
  WFIs, and on wake **re-trains DRAM with the parameters the stub was built with**. TF-A only enables system
  suspend when the running DRAM type and clock match a stub (`SUNXI_SUSPEND_STUB`, optional `..._STUB2`).
- So the stub must be built from this unit's LPDDR4 timings (the muOS values in `rg40xxh_2025_lpddr4_defconfig`);
  ROCKNIX's stub carries the ROCKNIX timings that do not train here and would hang on wake. ROCKNIX builds the
  stub in its u-boot package; that build has to be reproduced with our defconfig before testing.
- Test only with an RTC wake alarm (`rtcwake -m mem -s 20`) and the recovery loop in place: a hang during
  suspend is the one state the watchdog does not cover.

### SteamOS client mode (from ChimeraOS gamescope-session-steam, MIT)
- SteamOS UI features (system settings, power menu, OS update page) need
  `steam -gamepadui -steamos3 -steampal -steamdeck` plus helper commands on PATH: `steamos-session-select`
  (switch session), `steamos-update` (exit 7 = no update; `--supports-duplicate-detection` exits 0),
  `steamos-select-branch`, `jupiter-biosupdate` (exit 0). Stubs are in `overlay/usr/local/bin`.
- Power menu: Steam touches `$STEAMOS_STEAM_REBOOT_SENTINEL` / `$STEAMOS_STEAM_SHUTDOWN_SENTINEL` and exits;
  the session script then reboots or powers off. `STEAM_ENABLE_VOLUME_HANDLER=1` lets Steam handle the
  volume keys itself. The gamescope-specific variables (VRR, HDR, NIS, mangoapp) do not apply without gamescope.
- `upower` provides the battery to Steam (`battery_battery` and `line_power_axp20x_usb` on this device).
