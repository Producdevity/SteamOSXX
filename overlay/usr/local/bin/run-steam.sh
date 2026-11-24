#!/bin/bash
# Runs as the steam user with DISPLAY set. Settings come from /etc/steam-session.conf.
. /etc/steam-session.conf
root=$HOME/.local/share/Steam
log=$HOME/steam-run.log
if [ ! -x "$root/steamrtarm64/steam" ]; then
  until curl -fsI --max-time 10 https://client-update.steamstatic.com/ -o /dev/null || [ $? = 22 ]; do sleep 5; done
  /usr/local/bin/steam-install.sh >>"$log" 2>&1 || exit 1
fi
export STEAM_RUNTIME=1 STEAM_STEAMRT_RECURSION_GUARD=1
export LD_LIBRARY_PATH=$root/steamrtarm64:$root/steamrtarm64/panorama
export LD_PRELOAD=/usr/local/lib/lse/liblse_emu.so
export LSE_EMU=1 LSE_EMU_PATCH=1 LSE_EMU_LOG=$HOME/lse_emu.log
export SDL_VIDEO_X11_DGAMOUSE=0
export XDG_RUNTIME_DIR=/run/user/$(id -u)
[ -d "$XDG_RUNTIME_DIR" ] || XDG_RUNTIME_DIR=/tmp/xdg-$(id -u)
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
  if [ -S "$XDG_RUNTIME_DIR/bus" ]; then export DBUS_SESSION_BUS_ADDRESS=unix:path=$XDG_RUNTIME_DIR/bus
  else eval "$(dbus-launch --sh-syntax)"; fi
fi
ui=()
[ "${STEAM_UI:-gamepadui}" = gamepadui ] && ui=(-gamepadui)
# STEAMOS=1: SteamOS client mode (system settings, power menu); needs the steamos-* helpers on PATH.
if [ "${STEAMOS:-0}" = 1 ]; then
  ui=(-gamepadui -steamos3 -steampal -steamdeck)
  export STEAMOS_STEAM_REBOOT_SENTINEL=/tmp/steamos-reboot-sentinel
  export STEAMOS_STEAM_SHUTDOWN_SENTINEL=/tmp/steamos-shutdown-sentinel
  export STEAM_ENABLE_VOLUME_HANDLER=1
  rm -f "$STEAMOS_STEAM_REBOOT_SENTINEL" "$STEAMOS_STEAM_SHUTDOWN_SENTINEL"
fi
cef=(-cef-disable-gpu -cef-disable-gpu-compositing)
[ "${CEF_GPU:-0}" = 1 ] && cef=()
[ -n "${CDP_PORT:-}" ] && cef+=(-cef-enable-debugging -devtools-port "$CDP_PORT")
cd "$root"
while :; do
  /usr/local/bin/patch-webhelper.py >>"$log" 2>&1
  echo "== $(date -u +%FT%TZ) starting steam ${ui[*]} ${cef[*]} ${STEAM_EXTRA_ARGS:-}" >>"$log"
  "$root/steamrtarm64/steam" -clientbeta "$(cat "$root/package/beta")" "${ui[@]}" \
    -no-cef-sandbox -cef-ozone-platform=x11 "${cef[@]}" ${STEAM_EXTRA_ARGS:-} >>"$log" 2>&1
  rc=$?
  echo "== $(date -u +%FT%TZ) steam exited rc=$rc" >>"$log"
  [ "$rc" = 42 ] || break
done
# Steam's power menu leaves a sentinel and exits; in dev mode poweroff is linked to reboot.
if [ -e "${STEAMOS_STEAM_REBOOT_SENTINEL:-/nonexistent}" ]; then
  rm -f "$STEAMOS_STEAM_REBOOT_SENTINEL"; echo "== reboot requested by Steam" >>"$log"; systemctl reboot
elif [ -e "${STEAMOS_STEAM_SHUTDOWN_SENTINEL:-/nonexistent}" ]; then
  rm -f "$STEAMOS_STEAM_SHUTDOWN_SENTINEL"; echo "== shutdown requested by Steam" >>"$log"; systemctl poweroff
fi
exit $rc
