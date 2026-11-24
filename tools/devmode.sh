#!/bin/bash
# Dev mode on the device: power-off and halt become reboot (back to muOS), power key ignored.
#   tools/devmode.sh on|off|status     (runs over tools/dev.sh)
set -euo pipefail
cd "$(dirname "$0")/.."
case "${1:-status}" in
  on) tools/dev.sh sh 'ln -sfn /usr/lib/systemd/system/reboot.target /etc/systemd/system/poweroff.target;
        ln -sfn /usr/lib/systemd/system/reboot.target /etc/systemd/system/halt.target;
        [ -f /etc/systemd/logind.conf.d/10-devmode.conf.off ] && mv /etc/systemd/logind.conf.d/10-devmode.conf.off /etc/systemd/logind.conf.d/10-devmode.conf;
        systemctl daemon-reload; echo devmode on' ;;
  off) tools/dev.sh sh 'rm -f /etc/systemd/system/poweroff.target /etc/systemd/system/halt.target;
        [ -f /etc/systemd/logind.conf.d/10-devmode.conf ] && mv /etc/systemd/logind.conf.d/10-devmode.conf /etc/systemd/logind.conf.d/10-devmode.conf.off;
        systemctl daemon-reload; echo devmode off' ;;
  status) tools/dev.sh sh 'readlink /etc/systemd/system/poweroff.target || echo "poweroff is real"; ls /etc/systemd/logind.conf.d/' ;;
esac
