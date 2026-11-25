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

### Memory livelock: the one strand the recovery loop did not cover (2026-11-24)
- Launching Geometry Wars on the ARMv8.0 Proton 11.0-3 build with Big Picture still up filled zram and the
  device livelocked: ping answered (kernel networking), adb went `offline`, sshd stopped sending its banner,
  and the kernel OOM killer never fired. PID 1 kept feeding the hardware watchdog and the shell `reach-guard`
  saw adbd "running" and :22 "listening", so nothing rebooted. 12 parallel pre-auth SSH connections (to add
  pressure and force an OOM kill) did not break it either.
- Fixes, all in the image:
  - `earlyoom` (`/etc/default/earlyoom`): kills at <6 % RAM and <15 % swap free, preferring Wine/FEX/game
    processes and never the session, Steam client, Xorg or access services.
  - MGLRU thrash protection: `/sys/kernel/mm/lru_gen/min_ttl_ms = 1000` (tmpfiles.d) makes the kernel OOM
    kill instead of evicting a working set younger than 1 s (`CONFIG_LRU_GEN_ENABLED=y` in the ROCKNIX kernel).
  - `reach-guard` is now C, `mlockall()`ed, probes sshd with a real banner read on 127.0.0.1:22 and calls
    `reboot(2)` itself after 4 minutes: a shell script or `systemctl` would need paging in and stall too.
  - `MemoryMin=24M` + `OOMScoreAdjust=-900` for ssh, usb-gadget (adbd), reach-guard and earlyoom.
  - Tried and dropped: a 2 GB `/swapfile` behind zram, as SteamOS keeps one. On this device it shares the SD
    card with the root filesystem, so while it swapped every exec and file read queued behind swap I/O.
    `sxx-memhog` (incompressible allocations) then held sshd's banner back for up to 50 s, and the extra 2 GB
    of swap kept earlyoom's swap-free threshold out of reach. With zram alone the kernel OOM killer (MGLRU
    `min_ttl_ms`) or earlyoom ends the hog.
  - `reach-guard` also watches PSI (`/proc/pressure/memory`, `full avg10` >= 30 % for 30 s) and then kills
    the processes under `reaper SteamLaunch` (the running game, never Steam). A game larger than RAM makes the
    system crawl long before memory or swap runs out, so neither earlyoom nor the kernel acts. That is what
    happened here: the screen still updated, but SSH handshakes took longer than any client timeout for
    about 35 minutes (sshd logged them as `Connection closed ... [preauth]`).
### Game windows under Big Picture: gamescope's focus protocol
- With `-steamos3` Steam believes it runs under gamescope: it writes `GAMESCOPECTRL_BASELAYER_APPID` on the
  root window (seen: `413091, 769`; 769 is Steam itself) and never raises game windows on a plain X server, so
  a launched game stays hidden behind Big Picture.
- Steam's own windows carry `STEAM_GAME=769`; Proton game windows had no `STEAM_GAME`, only
  `WM_CLASS steam_app_8400`.
- Gamescope's own rules (src/steamcompmgr.cpp, read at master 7d957c5): a window's app is `STEAM_GAME` if set,
  else `STEAM_BIGPICTURE` -> 769, else from its PID: the `app-steam-app<N>-*.scope` cgroup or the nearest
  `reaper SteamLaunch AppId=<N>` ancestor. Gamescope never writes `STEAM_GAME` and has no `WM_CLASS` rule.
  Focus: `GAMESCOPECTRL_BASELAYER_WINDOW` if it names a candidate, else the first appid in
  `GAMESCOPECTRL_BASELAYER_APPID` that has a window (most recently mapped wins); no match keeps the old focus.
  `STEAM_OVERLAY` windows are never base-layer candidates (they are composited on top). It publishes
  `GAMESCOPE_FOCUSABLE_APPS`, `GAMESCOPE_FOCUSABLE_WINDOWS` (`[xid, appid, pid]` triplets),
  `GAMESCOPE_FOCUSED_APP`, `_APP_GFX` and `_WINDOW` on the root. 413091 does not appear in its source.
- `sxx-focus` does the same on Xorg, using `_NET_WM_PID` and activating the chosen window through openbox
  (`_NET_ACTIVE_WINDOW` with the pager source, so focus-stealing prevention does not apply).

### Games in front of Big Picture, the SteamOS way (2026-11-24)
- Steam's UI only runs its gamescope logic when `IN_GAMESCOPE` is true, which the client derives from
  `GAMESCOPE_WAYLAND_DISPLAY`. Without it the UI never hides itself for a game and keeps
  `GAMESCOPECTRL_BASELAYER_APPID` at `[413091, 769]`. The session now sets
  `GAMESCOPE_WAYLAND_DISPLAY=gamescope-0` (`GAMESCOPE_COMPAT=1` in `steam-session.conf`). Nothing listens
  on that socket, and Steam starts and runs normally.
- With it, Steam builds the queue itself (sp.js `EnsureCorrectComposition`): Opaque gives `[769, game]`,
  otherwise `[game, 769]`, and the webhelper prefixes 413091. On launch the queue went
  `[413091, 769, 8400]` -> `[413091, 8400, 769]` once the game window was focusable, and `sxx-focus` put the
  game in front with nothing pressed.
- The webhelper turns the UI composition mode (`SteamClient.Window.SetComposition(mode, appids, window)`)
  into `STEAM_OVERLAY`, `STEAM_NOTIFICATION`, `STEAM_INPUT_FOCUS` on its window and the base-layer root
  properties.
- Wine minimizes a fullscreen game that loses focus, and Geometry Wars stops (0 CPU) while minimized, so
  `sxx-focus` treats iconified clients as focusable (gamescope never iconifies).
- Steam's screensaver (a slideshow of the account's screenshots, `system_idle_screensaver_ac_sec=300`,
  `screensaver_while_ingame=true`) started during the slow launch. Its popup requests Opaque composition and
  pinned Big Picture over the game. Turned off through the client settings protobuf
  (`SteamClient.Settings.SetSetting`, see `sxx-steam-defaults`).
- Hidden this way, the web helper uses about 4 % of one core behind a game (it was 70-85 % while Big Picture
  kept rendering on top). `game-mode` (SCHED_IDLE web helper) is removed: it starved the Steam button
  in game, and SteamOS has nothing like it.
- Memory: zstd zram compresses Steam's memory about 3.5:1, so zram's 975 MB disk size, not RAM, was what ran
  out. Now zram = 2x RAM, `vm.swappiness=180`, `page-cluster=0`, `watermark_boost_factor=0`,
  `watermark_scale_factor=125`. `PROTON_USE_XALIA=0` (xalia took ~14 % CPU per game).
- Geometry Wars main menu on proton-armv8 11.0-3 (ARMv8.0 build, no LSE traps): 1.4-3.8 FPS.
- In-game Steam button: Steam switches its window to `STEAM_OVERLAY=1`, `STEAM_INPUT_FOCUS=1` (gamescope's
  overlay plane) and keeps the base layer on the game. `sxx-focus` raises an overlay window above the base
  window and gives it input focus while `STEAM_INPUT_FOCUS` is set. B returns to the game: Steam clears both
  properties and the game is activated again. Without a compositor the overlay's transparent parts are black.
- The Steam menu takes 10-20 s to appear in game: the web helper's memory sits compressed in zram while it
  is hidden. Memory census in game (`sxx-mem`, PSS + swap): Geometry Wars 441 MB, web helper 388 MB,
  Steam client 123 MB, Xorg 39 MB, Wine services ~90 MB. zram held 700 MB in 173 MB.
- `sxx-gametest <appid>` runs the whole flow on the device. Geometry Wars: in front 88 s after launch,
  Steam menu +7 s, back in game +5 s, quit and Big Picture back +4 s. A second `RunGame` while the game runs
  leaves a "Game already running" dialog that holds Steam in Overlay composition (the test quits a running
  copy first).

### Brightness and PanVK (2026-11-24)
- Steam's brightness slider works: `SteamClient.System.Display.SetBrightness(x)` writes
  `/sys/class/backlight/*/brightness` itself, on a perceptual curve (0.4 -> 376/2499), and
  `RegisterForBrightnessChanges` reports it back. `70-backlight.rules` lets the `video` group write it.
- PanVK in Debian's Mesa 25.0.7 lists no device for the Mali-G31 (Bifrost v7), even with
  `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`. GPU Vulkan needs a newer Mesa build; DXVK stays on llvmpipe.

### Direct3D 9 on the GPU: WineD3D on Panfrost is the faster path (2026-11-24)
- Screenshots of GL windows via `xwd` are wrong on this setup: the content reads as packed 24-bit pixels in a
  32-bit image (480 of 640 columns, grey stripes). The "wrong colours" noted for WineD3D earlier were this
  artifact. `tools/dev.sh shot` now grabs the KMS plane (`ffmpeg -f kmsgrab`), which is what the panel shows.
- Geometry Wars on proton-armv8 11.0-3, title screen: WineD3D on Panfrost GL 3.1 **8.6 FPS**, correct
  rendering. DXVK on llvmpipe: 1.4-3.8 FPS. With WineD3D the game process uses ~25 % CPU (it was CPU-bound
  at ~180-280 % with llvmpipe).
- Default now: `WINEDLLOVERRIDES=d3d8,d3d9=b` in the session (Proton keeps user overrides). D3D8/9 use
  WineD3D on the GPU; D3D10/11 stay on DXVK (WineD3D needs more than GL 3.1 for those).
