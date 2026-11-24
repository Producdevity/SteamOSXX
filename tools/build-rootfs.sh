#!/bin/bash
# Builds the arm64 Debian userspace and exports it to out/rootfs.tar.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -d cache/overlay ] || tools/fetch-rocknix.sh
mkdir -p out
docker build --platform linux/arm64 -t steamos-rg40xx-rootfs -f rootfs/Dockerfile .
id=$(docker create --platform linux/arm64 steamos-rg40xx-rootfs)
trap 'docker rm -f "$id" >/dev/null' EXIT
docker export "$id" -o out/rootfs.tar
ls -la out/rootfs.tar
