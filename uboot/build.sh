#!/bin/bash
# Builds TF-A bl31 and U-Boot for the RG40XX-H in Docker.
#   uboot/build.sh <defconfig-in-this-dir> [extra .config fragment]
# Output: out/uboot/<defconfig>/u-boot-sunxi-with-spl.bin
set -euo pipefail
cd "$(dirname "$0")/.."
cfg=${1:?defconfig}
frag=${2:-uboot/fel-debug.config}
exec env MSYS_NO_PATHCONV=1 docker run --rm --platform linux/amd64 -v "$(pwd -W 2>/dev/null || pwd):/w" -w /w debian:trixie bash -c "
set -e; export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq git make gcc gcc-aarch64-linux-gnu bison flex bc python3 python3-pyelftools python3-setuptools python3-dev \
  swig libssl-dev libgnutls28-dev uuid-dev device-tree-compiler curl xxd >/dev/null 2>&1
mkdir -p /src && cd /src
[ -d tfa ] || git clone -q --depth 1 -b lts-v2.12.1 https://github.com/TrustedFirmware-A/trusted-firmware-a tfa
make -s -C tfa CROSS_COMPILE=aarch64-linux-gnu- PLAT=sun50i_h616 DEBUG=0 bl31 >/dev/null
curl -fsSL https://github.com/u-boot/u-boot/archive/v2026.01.tar.gz | tar xz && cd u-boot-2026.01
patch -p1 -s < /w/uboot/0001-dram.patch
cat /w/uboot/rg40xx-h-mmc2.dtsi >> dts/upstream/src/arm64/allwinner/sun50i-h700-anbernic-rg35xx-2024.dts
cp /w/uboot/$cfg configs/
make -s CROSS_COMPILE=aarch64-linux-gnu- $cfg
cat /w/$frag >> .config
make -s CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
grep -E 'AXP_DISABLE|UMS|MASS_STORAGE|USB_GADGET=|SUNXI_DRAM_H616_LP|SLOT_EXTRA|BOOTCOMMAND' .config
make -s -j\$(nproc) CROSS_COMPILE=aarch64-linux-gnu- BL31=/src/tfa/build/sun50i_h616/release/bl31.bin SCP=/dev/null
mkdir -p /w/out/uboot/$cfg && cp u-boot-sunxi-with-spl.bin /w/out/uboot/$cfg/
ls -la /w/out/uboot/$cfg/
"
