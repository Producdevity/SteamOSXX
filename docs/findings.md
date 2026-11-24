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

### First Big Picture on mainline (2026-11-24 20:56 UTC)
- After the self-update (download ~580 MB; extraction 4.5 min vs ~10 min on muOS), relaunch to web helper
  `BrowserReady` took 25 s (muOS: several minutes) and Big Picture painted about a minute later.
- The Steam login carried over from muOS through the seeded `~/.local/share/Steam` (no new QR scan).
- Big Picture shows battery and Wi-Fi in the status bar (upower, NetworkManager) and controller glyphs
  (the built-in pad is recognised with no remapping).
- Time zone was UTC; set to America/New_York (also in the image).
- adbd (Debian 34.0.5) does not recover its FunctionFS connection after a USB unplug/replug (adb shows
  `offline` until the gadget restarts). `90-usb-gadget-replug.rules` restarts `usb-gadget.service` when
  `axp20x-usb` reports power again. SSH over Wi-Fi was the way back in.
- On-screen keyboard in plain `-gamepadui`: A shows a mouse cursor in the screen centre and clicks there,
  which dismisses the keyboard; focus never leaves the Big Picture window and no new X window appears (the
  keyboard is drawn inside it). Trying SteamOS client mode (`-steamos3 -steampal -steamdeck`).
- **Root cause of the dismissing keyboard:** udev's input_id tags the `H700 Gamepad` as
  `ID_INPUT_TABLET`/`ID_INPUT_TABLET_PAD` (as well as joystick), so Xorg's "libinput tablet catchall" adopts it
  and its buttons become pointer clicks at the cursor, which starts in the screen centre. A press = click
  outside the keyboard. `80-h700-gamepad.rules` clears the tablet tags and sets `LIBINPUT_IGNORE_DEVICE=1`;
  Steam still reads the pad through evdev. Confirmed fixed on device. (SteamOS client mode did not change it.)
- SteamOS client mode works: SteamOS boot splash, then the Deck UI. The client calls
  `jupiter-initial-firmware-update check`, `gpu-trace --get-tracing-status`,
  `/usr/bin/steamos-polkit-helpers/steamos-devkit-mode --disable` and probes for a mangoapp window; stubs are in
  the overlay.

### Automation (no hands on the device)
- `sxx` (on the device, stdlib Python; DevTools client adapted from DroidDeck's droiddeck-agent):
  `sxx targets`, `sxx eval [-t TARGET] JS`, `sxx pad TOKENS` (virtual "SteamOSXX Test Pad", uinput,
  pid.codes 1209:5a5a, standard evdev codes; Steam maps it like the built-in pad).
- Steam DevTools: `-cef-enable-debugging -devtools-port 8080` (from `CDP_PORT`), bound to 127.0.0.1 only.
  Targets in Big Picture: `SharedJSContext` (route, e.g. `/routes/library/home`), `Steam Big Picture Mode`
  (the window: `document.activeElement` is the focused item), `MainMenu_uid2`, `QuickAccess_uid2`,
  `notificationtoasts_uid2`. The Big Picture window asks for `minwidth=853&minheight=534`; the panel is
  640x480.
- `tools/smoke.sh` checks reachability, services, display, GPU, Wi-Fi, audio sink, battery, gamepad
  (and that X ignores it), the Steam route, pad navigation (focus must move) and memory, and saves a
  screenshot.
- Cursor: SteamOS's gamescope-session hides the cursor until pointer motion (`--hide-cursor-delay 3000`);
  Steam's guide + right stick chord moves a pointer and must still show it. `-nocursor` was wrong; the
  session runs `unclutter --start-hidden --timeout 3` instead.
- adbd also dies on host-initiated link drops (USB suspend/reset: `FUNCTIONFS_SUSPEND`, `FUNCTIONFS_DISABLE`),
  not only unplugs. A udev rule on `axp20x-usb` power events was the wrong tool (charging-state changes fire
  it too). `usb-gadget` now watches `/sys/class/udc/*/state` and exits (systemd rebuilds the gadget) when the
  link stays out of `configured` for 3 s after having been up.
- `session-N.scope` units of earlier Steam sessions show as failed after a session restart; the prep script
  runs `systemctl reset-failed "session-*.scope"`.

### First game: Geometry Wars (AppID 8400) through Proton Experimental (ARM64) (2026-11-24)
Launched from Steam, renders its intro. Four separate blockers, in the order they were hit:
1. **bwrap "Unexpected capabilities but not setuid".** systemd 257's pam_systemd gives user sessions an ambient
   `CAP_WAKE_ALARM`; any ambient capability makes pressure-vessel's bwrap refuse to start, so no game ever
   launches. The `default-capability-ambient-set=` pam_systemd option with an empty value did not clear it;
   `steam-session.service` now starts the session through `setpriv --ambient-caps -all --inh-caps -all`.
2. **SIGILL in the shell Steam starts for the game.** Steam builds the game's `LD_PRELOAD` from its overlay
   (`steamrtarm64/gameoverlayrenderer.so`, built with inline LSE) plus whatever the client had. Preloads
   initialise in **reverse load order**, so with LSE-Emu first and the overlay after it, the overlay's
   constructor runs before the SIGILL handler exists (verified: `LD_PRELOAD=lse:overlay /bin/true` -> 132,
   `overlay:lse` -> 0). Fix: a second copy, `liblse_emu_sys.so`, in `/etc/ld.so.preload`; it loads after all
   `LD_PRELOAD` entries and so initialises first. (The same file in both places is loaded once, at the first
   position, which is why it has to be a separate copy.)
3. **SIGBUS (`BUS_ADRERR`) in the overlay.** `/dev/shm` (tmpfs, half of RAM = 488 MB) was 93 % full of Steam
   (`u1000-Shm_*`) and FEX segments leaked by killed sessions; mapping a new segment then faults.
   `steam-session-prep` deletes the steam user's `/dev/shm` leftovers before the client starts.
4. **SIGILL inside the game container.** The host's `/etc/ld.so.preload` does not apply inside
   pressure-vessel; host `LD_PRELOAD` entries are carried in. The session keeps
   `LD_PRELOAD=liblse_emu.so` and sets `PRESSURE_VESSEL_REMOVE_GAME_OVERLAY=1` so no LSE-built overlay runs in
   the container before the handler (cost: no Steam in-game overlay for now).
- LSE-Emu default changed for this ARMv8.0-only image: active for every non-root process unless `LSE_EMU=0`
  (upstream requires `LSE_EMU=1`, which launch environments can drop). In the game container it patched 1,897
  sites in Valve's ARMv8.2 Wine/FEX with nothing unhandled.
- Rendering is DXVK on **llvmpipe** (CPU Vulkan): works, but slow. Next: `PROTON_USE_WINED3D=1` (WineD3D on
  Panfrost GL 3.1) and the ARMv8.0 Proton/FEX rebuilds from sunshineinabox/proton-armv8.
- `steam-launch-wrapper` exports `VK_INSTANCE_LAYERS=VK_LAYER_VALVE_rpo:VK_LAYER_VALVE_fdm_injection` (Steam
  Frame VR layers) for every game on this client channel; harmless here so far.
- Tools: `sxx-wrap` (launch-option wrapper: env + output to `/tmp/sxx-wrap-<appid>.log`),
  `sxx-launch-debug` (run a game's chain by hand with Proton logging), `sxx eval
  'SteamClient.Apps.RunGame("8400","",-1,100)'` and `SteamClient.Apps.SetAppLaunchOptions(...)` to drive it.

### Geometry Wars performance (640x480, Proton Experimental ARM64, FEX, stock settings)
| Setup | Intro | Main menu (particles) |
|---|---|---|
| DXVK on llvmpipe (CPU Vulkan) | 6.4 FPS | 3.0 FPS (web helper SIGSTOPped), 1.3 FPS (web helper running) |
| WineD3D on Panfrost GL 3.1 (`PROTON_USE_WINED3D=1`) | 4.5 FPS, wrong colours/glow | - |
- During play, zram swap is ~95 % full (926-969 of 974 MB) and `kswapd0` is busy; the game takes ~250-280 MB
  RSS and up to 277 % CPU. Steam's web helper keeps 70-85 % of a core rendering Big Picture behind the game.
- The game's input arrives through Steam Input (`SteamVirtualGamepadInfo_Proton`); at 1-3 FPS a press must be
  held longer than a frame (`sxx pad hold:a:1500`).
- DXVK stays the default (faster and correct). The real limits are CPU-side: FEX translation and software
  Vulkan. Levers: web helper priority during games, FEX TSO settings, ARMv8.0 Proton/FEX builds (no LSE traps),
  PanVK on the GPU.

### Steam UI on the GPU (`CEF_GPU=1`): works, not stable (reverted)
- Chromium renders through ANGLE on Panfrost (`ANGLE (Mesa, Mali-G31 (Panfrost), OpenGL 3.1)`, GLES 2.0 over
  GL 3.1); gpu_compositing, rasterization, opengl and webgl all enabled; the UI renders correctly. `gbm` logs
  "Failed to export buffer to dma_buf" (display and GPU are separate DRM devices) but it still draws.
- Within a few minutes the GPU process exited (code 512) and the whole client then exited (session restarted).
  The kernel showed `panfrost_gem_shrinker_scan` under memory pressure: GPU buffers come out of the same 1 GB.
- `sxx-uibench` (12 pad moves over ~15 s): software 156-161 % web helper CPU; GPU 113-191 % (noisy).
  No clear CPU win to justify the instability. Back to `CEF_GPU=0`; revisit with more free RAM (game mode,
  fewer web helper processes).
