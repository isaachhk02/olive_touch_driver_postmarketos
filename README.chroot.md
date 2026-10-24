# Install the ilitek_spi Touch Driver inside a postmarketOS arm64 Chroot

This guide builds `linux-xiaomi-olive-itk9881h` (the Ilitek ITK9881H SPI touch
driver for Xiaomi Redmi 8 / olive) as an `.apk` inside an arm64 chroot, then
installs it on the postmarketOS device.

The chroot approach lets you cross-build arm64 packages from an x86_64 host
using `pmbootstrap` + `qemu-aarch64-static`, or run natively on an arm64
machine.

---

## Prerequisites (host)

On your host (Debian/Ubuntu shown; adjust for your distro):

```bash
sudo apt update
sudo apt install pmbootstrap qemu-user-static binfmt-support git \
                 abuild alpine-sdk
```

If you prefer not to use `pmbootstrap`, see the **Manual Chroot** section below.

---

## Method 1: pmbootstrap (recommended)

### 1. Initialize pmbootstrap

```bash
pmbootstrap init
```

During setup choose an arm64 device (e.g. `xiaomi-olive`) so the target
architecture is `aarch64`. You can also edit
`~/.local/var/pmbootstrap/config` and set:

```
architecture = "aarch64"
target = "xiaomi-olive"
```

### 2. Copy the package source into the chroot workspace

On the host:

```bash
mkdir -p ~/.local/var/pmbootstrap/packages/aarch64/src
cp -r /home/isaac/olive-touch-driver-postmarketos/driver/* \
      ~/.local/var/pmbootstrap/packages/aarch64/src/
cp /home/isaac/olive-touch-driver-postmarketos/pmaports/APKBUILD \
   ~/.local/var/pmbootstrap/packages/aarch64/APKBUILD
cp /home/isaac/olive-touch-driver-postmarketos/pmaports/deviceinfo \
   ~/.local/var/pmbootstrap/packages/aarch64/deviceinfo
```

### 3. Enter the arm64 chroot

```bash
pmbootstrap chroot
```

You are now inside an arm64 postmarketOS shell.

---

## Method 2: Manual arm64 chroot (no pmbootstrap)

```bash
# Download an alpine/postmarketOS arm64 rootfs
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/alpine-minirootfs-3.20.3-aarch64.tar.gz
mkdir -p ~/pmos-arm64
sudo tar xzf alpine-minirootfs-3.20.3-aarch64.tar.gz -C ~/pmos-arm64

# Enable qemu-user-static binfmt emulation (needed on x86_64 hosts)
sudo cp /usr/bin/qemu-aarch64-static ~/pmos-arm64/usr/bin/

# Enter
sudo chroot ~/pmos-arm64 /bin/sh
```

Inside the chroot, add postmarketOS package repositories to
`/etc/apk/repositories` so `linux-xiaomi-olive-dev` headers are available.

---

## 4. Inside the chroot: install build tools

Run these commands **inside** the arm64 chroot:

```bash
apk update
apk add abuild alpine-sdk linux-headers

# The kernel headers for the olive kernel:
apk add linux-xiaomi-olive-dev
```

If `linux-xiaomi-olive-dev` is unavailable, build against the running
kernel's headers instead (see Troubleshooting).

### Set up a non-root build user (abuild requirement)

`abuild` must not run as root:

```bash
adduser -D builder
echo "builder ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
su - builder
cd ~
mkdir -p packages/aarch64
```

Generate a build key:

```bash
abuild-keygen -a -n
```

---

## 5. Add the APKBUILD source

Copy the driver source into the builder's home inside the chroot:

```bash
mkdir -p ~/device/testing/linux-xiaomi-olive-itk9881h/src
cp /path/to/ilitek_spi.c ~/device/testing/linux-xiaomi-olive-itk9881h/src/
cp /path/to/ilitek_core.h ~/device/testing/linux-xiaomi-olive-itk9881h/src/
cp /path/to/Makefile ~/device/testing/linux-xiaomi-olive-itk9881h/src/Makefile
cp /path/to/Kconfig ~/device/testing/linux-xiaomi-olive-itk9881h/src/Kconfig
cp /path/to/APKBUILD ~/device/testing/linux-xiaomi-olive-itk9881h/APKBUILD
```

Adjust `APKBUILD` `source=` paths to point at `src/` if needed.

---

## 6. Build the package

Inside the chroot as `builder`:

```bash
cd ~/device/testing/linux-xiaomi-olive-itk9881h

# Generate checksums for the source files
abuild checksum

# Build (the -r flag installs build dependencies automatically)
abuild -r
```

On success you get:

```
~/packages/aarch64/linux-xiaomi-olive-itk9881h-2.3.1.0-r0.aarch64.apk
```

If you are using pmbootstrap, the `.apk` also appears on your **host** at:

```
~/.local/var/pmbootstrap/packages/aarch64/linux-xiaomi-olive-itk9881h-*.apk
```

---

## 7. Install on the postmarketOS device

Copy the `.apk` to the device (via `scp`, `adb push`, USB, etc.), then:

```bash
# On the postmarketOS device
sudo apk add linux-xiaomi-olive-itk9881h-2.3.1.0-r0.aarch64.apk
```

Load the driver:

```bash
sudo modprobe ilitek_spi
```

If `modprobe` is unavailable, use `insmod`:

```bash
sudo insmod /lib/modules/$(uname -r)/ilitek_spi.ko
```

---

## 8. Verify

Check kernel messages:

```bash
dmesg | grep -i ilitek
```

Expected output:

```
ILITEK TP driver init
ILITEK CHIP found: xch=18 ych=32 panel=720x1520
```

Test touch input:

```bash
sudo apk add evtest
ls /dev/input/event*
sudo evtest /dev/input/eventX   # choose the touchscreen event node
```

Touch the screen; you should see `ABS_MT_POSITION_X/Y` events.

---

## 9. Auto-load on boot

```bash
echo "ilitek_spi" | sudo tee /etc/modules-load.d/ilitek.conf
```

---

## Troubleshooting

| Problem | Cause / Fix |
|---------|-------------|
| `Exec format error` loading `.ko` | The module was built for a different kernel version. Rebuild inside a chroot whose `uname -r` matches the device exactly, using `linux-xiaomi-olive-dev` headers. |
| `abuild` permission denied | `abuild` must run as a non-root user. Create a `builder` user and use `su - builder`. |
| Missing `linux-xiaomi-olive-dev` | Add postmarketOS repositories to `/etc/apk/repositories` inside the chroot, or point `KBUILD` at the device's running kernel headers. |
| Driver does not probe | Ensure `&spi_3` is enabled (`status = "okay"`) in the device tree and the DT node `compatible = "tchip,ilitek"` exists under it. |

---

## Files reference

```
driver/ilitek_spi.c        # mainline driver source
driver/ilitek_core.h       # driver header
driver/Makefile            # obj-m Makefile
driver/Kconfig             # CONFIG_TOUCHSCREEN_ITK9881H
driver/olive-touchscreen.dtsi   # DT node for &spi_3
pmaports/APKBUILD          # postmarketOS package build script
pmaports/deviceinfo        # kernel config fragment
```
