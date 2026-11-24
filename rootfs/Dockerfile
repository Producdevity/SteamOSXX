# Debian trixie arm64 userspace for the RG40XX-H, booted by ROCKNIX's mainline kernel.
# Build context is the repository root: docker build --platform linux/arm64 -f rootfs/Dockerfile .
FROM debian:trixie
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    systemd systemd-sysv systemd-timesyncd udev dbus dbus-user-session dbus-x11 iproute2 kbd libpam-systemd kmod sudo \
    openssh-server adbd network-manager wpasupplicant iw wireless-regdb rfkill \
    systemd-zram-generator cloud-guest-utils e2fsprogs dosfstools fdisk \
    ca-certificates curl unzip xz-utils file procps psmisc strace gdb binutils less nano lsof pciutils \
    usbutils evtest htop python3 gcc libc6-dev libx11-dev \
    xserver-xorg-core xserver-xorg-input-libinput xinit xauth x11-xserver-utils x11-utils x11-apps \
    xdotool openbox \
    libgl1-mesa-dri libglx-mesa0 libegl1 libgles2 libgbm1 mesa-vulkan-drivers libvulkan1 vulkan-tools mesa-utils \
    pipewire pipewire-pulse pipewire-alsa wireplumber rtkit alsa-utils \
    libnss3 libnspr4 libdrm2 libgl1 libgtk-3-0t64 libgtk2.0-0t64 libxss1 libxtst6 libxrandr2 libxcomposite1 \
    libxdamage1 libxkbcommon0 libxkbcommon-x11-0 libasound2t64 libpulse0 libcups2t64 \
    libatk-bridge2.0-0t64 libatspi2.0-0t64 libudev1 libusb-1.0-0 libsdl2-2.0-0 libfontconfig1 \
    libfreetype6 fonts-dejavu-core fonts-liberation libxcursor1 libxi6 libxinerama1 libxshmfence1 \
    libxxf86vm1 libcurl4t64 libdbus-1-3 libopenal1 libva2 libibus-1.0-5 libpipewire-0.3-0t64 \
    libxcb-res0 libxcb-xinput0 libxcb-xfixes0 libxcb-shape0 libxcb-randr0 libxcb-dri3-0 \
    libnm0 libsecret-1-0 libgdk-pixbuf-2.0-0 libpango-1.0-0 libcairo2 libpng16-16t64 libjpeg62-turbo \
    zenity locales \
 && sed -i 's/^# *en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen && locale-gen \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y --no-install-recommends avahi-daemon libnss-mdns \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

# Cortex-A53 is ARMv8.0: no LSE atomics, which the arm64 Steam client uses inline.
COPY rootfs/src/lse_emu.c /usr/local/src/lse_emu.c
RUN mkdir -p /usr/local/lib/lse && gcc -shared -fPIC -O2 -march=armv8-a -mno-outline-atomics \
      -Wl,-z,now -o /usr/local/lib/lse/liblse_emu.so /usr/local/src/lse_emu.c -ldl \
 && strip --strip-unneeded /usr/local/lib/lse/liblse_emu.so \
 && ! objdump -d /usr/local/lib/lse/liblse_emu.so | grep -qE $'\t((ld|st)(add|clr|eor|set|smax|smin|umax|umin)|swp|cas)[a-z]*\t'
COPY rootfs/src/padmap.c /usr/local/src/padmap.c
RUN gcc -O2 -Wall -o /usr/local/bin/padmap /usr/local/src/padmap.c

# ROCKNIX kernel 7.2.0 modules (joypad, zram, ...) and Wi-Fi/BT firmware.
COPY cache/overlay/usr/lib/kernel-overlays/base/lib/modules/ /usr/lib/modules/
COPY cache/overlay/usr/lib/kernel-overlays/base/lib/firmware/ /usr/lib/firmware/
COPY cache/overlay/usr/lib/udev/hwdb.d/20-joypad.hwdb /usr/lib/udev/hwdb.d/20-joypad.hwdb

RUN useradd -m -s /bin/bash -G sudo,video,input,audio,render,netdev steam \
 && echo 'steam ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/steam && chmod 440 /etc/sudoers.d/steam \
 && passwd -d root && passwd -d steam \
 && rm -f /etc/ssh/ssh_host_* \
 && systemctl disable adbd.service ssh.service 2>/dev/null; true

COPY overlay/ /
COPY secrets/authorized_keys /root/.ssh/authorized_keys
COPY secrets/authorized_keys /home/steam/.ssh/authorized_keys
COPY secrets/nm/ /etc/NetworkManager/system-connections/
RUN chmod 700 /root/.ssh /home/steam/.ssh && chown -R steam:steam /home/steam/.ssh \
 && chmod 600 /etc/NetworkManager/system-connections/* \
 && find /etc/systemd /etc/ssh/sshd_config.d /etc/NetworkManager/conf.d /etc/steam-session.conf -type f -exec chmod 644 {} + \
 && chmod +x /usr/local/bin/* /usr/local/sbin/* \
 && systemctl enable ssh.service NetworkManager.service usb-gadget.service firstboot.service \
      wifi-kick.service steam-session.service systemd-timesyncd.service reach-guard.service \
 && ln -sfn /usr/lib/systemd/system/reboot.target /etc/systemd/system/poweroff.target \
 && ln -sfn /usr/lib/systemd/system/reboot.target /etc/systemd/system/halt.target \
 && systemctl set-default multi-user.target \
 && systemctl mask apt-daily.timer apt-daily-upgrade.timer man-db.timer e2scrub_all.timer dpkg-db-backup.timer \
 && depmod 7.2.0 && udevadm hwdb --update
