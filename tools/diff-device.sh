#!/bin/bash
# Compares the running device with the repository: overlay files that differ or are missing, files in the
# usual config places that exist only on the device, and packages installed there but not in the Dockerfile.
#   tools/diff-device.sh
set -uo pipefail
cd "$(dirname "$0")/.."
local_sums=$(cd overlay && find . -type f ! -name .keep -print0 | sort -z | xargs -0 sha256sum | sed -E 's| \*?\./| /|')
remote_sums=$(echo "$local_sums" | awk '{print $2}' | tools/dev.sh sh 'xargs sha256sum 2>&1' 2>/dev/null)
echo "== overlay files that differ on the device"
join -1 2 -2 2 <(echo "$local_sums" | sort -k2) <(echo "$remote_sums" | sort -k2) | awk '$2 != $3 {print "  differs: " $1}'
echo "$remote_sums" | grep -E "No such file" | sed 's/^/  missing: /'
echo "== files only on the device (config places)"
tools/dev.sh sh 'find /usr/local/bin /usr/local/sbin /etc/systemd/system /etc/udev/rules.d /etc/pam.d /etc/ld.so.preload /usr/bin/steamos-polkit-helpers -maxdepth 1 -type f -newer /etc/os-release 2>/dev/null' \
  | while read -r f; do [ -f "overlay$f" ] || echo "  device-only: $f"; done
echo "== packages installed by hand on the device but not named in rootfs/Dockerfile"
tools/dev.sh sh 'sed -n "s/^Commandline: apt-get install //p" /var/log/apt/history.log | tr " " "
" | grep -v "^-" | sort -u'   | while read -r p; do [ -n "$p" ] && ! grep -qw -- "$p" rootfs/Dockerfile && echo "  $p"; done
