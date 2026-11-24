#!/bin/bash
# Applies the repository's overlay and helper sources to the running device without reflashing, so
# installed games and the Steam login survive. Installs packages the image gained since it was flashed,
# compiles the C helpers on the device and reloads systemd. Restart the session yourself when needed.
#   tools/push-live.sh [package ...]
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
KEY=secrets/id_ed25519
HOST=${DEV_HOST:-rg40xx.local}
SSH=(ssh -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=10)
pkgs="earlyoom openssh-client $*"
tar -C overlay -cf - --exclude=.keep . | "${SSH[@]}" "root@$HOST" 'tar -xf - -C / --no-same-owner --no-overwrite-dir'
tar -C rootfs/src -cf - . | "${SSH[@]}" "root@$HOST" 'mkdir -p /usr/local/src && tar -xf - -C /usr/local/src --no-same-owner --no-overwrite-dir'
"${SSH[@]}" "root@$HOST" "set -e
  missing=\$(for p in $pkgs; do dpkg -s \$p >/dev/null 2>&1 || echo \$p; done)
  [ -z \"\$missing\" ] || { apt-get update -qq && apt-get install -y -qq --no-install-recommends \$missing; }
  gcc -O2 -Wall -o /usr/local/bin/sxx-focus /usr/local/src/sxx-focus.c -lX11
  gcc -O2 -Wall -o /usr/local/sbin/reach-guard.new /usr/local/src/reach-guard.c && mv -f /usr/local/sbin/reach-guard.new /usr/local/sbin/reach-guard
  chmod +x /usr/local/bin/* /usr/local/sbin/*
  chmod 600 /etc/NetworkManager/system-connections/* 2>/dev/null || true
  systemctl daemon-reload
  systemd-tmpfiles --create /etc/tmpfiles.d/*.conf
  systemctl enable --now earlyoom.service >/dev/null 2>&1
  systemctl restart reach-guard.service earlyoom.service
  [ -e /swapfile ] || { fallocate -l 2G /swapfile && chmod 600 /swapfile && mkswap -q /swapfile; }
  swapon --show=NAME | grep -q /swapfile || swapon -p -2 /swapfile
  echo push-live: ok"
