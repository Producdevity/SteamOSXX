#!/bin/bash
# Builds the arm64 Debian userspace and exports it to out/rootfs.tar.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -d cache/overlay ] || tools/fetch-rocknix.sh
# A CRLF script or config in the image breaks at boot (`/bin/sh^M`); Windows editors and Python's text mode write them.
crlf=$(find overlay rootfs debug -type f -exec grep -lU "$(printf '\r')" {} + 2>/dev/null || true)
[ -z "$crlf" ] || { echo "CRLF line endings in: $crlf"; exit 1; }
mkdir -p out
docker build --platform linux/arm64 -t steamos-rg40xx-rootfs -f rootfs/Dockerfile .
id=$(docker create --platform linux/arm64 steamos-rg40xx-rootfs)
trap 'docker rm -f "$id" >/dev/null' EXIT
docker export "$id" -o out/rootfs.tar
ls -la out/rootfs.tar
