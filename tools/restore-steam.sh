#!/bin/bash
# Restores cache/steam-backup.tar onto the running device (the session is stopped meanwhile), or, after a
# reflash, use STEAM_SEED=cache/steam-backup.tar when building the image instead.
#   tools/restore-steam.sh
set -euo pipefail
export MSYS_NO_PATHCONV=1
cd "$(dirname "$0")/.."
s=$(adb devices | awk '$2 == "device" && $1 !~ /tls/ {print $1}' | while read -r d; do [ "$(adb -s "$d" shell cat /etc/hostname | tr -d '\r')" = rg40xx ] && echo "$d" && break; done)
[ -n "$s" ] || { echo "device not on adb"; exit 1; }
[ -f cache/steam-backup.tar ] || { echo "no cache/steam-backup.tar"; exit 1; }
adb -s "$s" push cache/steam-backup.tar /var/tmp/steam-backup.tar
adb -s "$s" shell 'systemctl stop steam-session; tar -C /home/steam --numeric-owner -xf /var/tmp/steam-backup.tar && chown -R steam:steam /home/steam/.local /home/steam/.steam && rm -f /var/tmp/steam-backup.tar; systemctl start steam-session; echo restored'
