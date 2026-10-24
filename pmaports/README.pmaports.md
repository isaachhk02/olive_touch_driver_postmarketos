# pmaports integration — Xiaomi Redmi 8 (olive) touch driver

Files:
- `driver/ilitek_spi.c`       mainline driver (SPI transport, probe/remove, GPIO, IRQ, input)
- `driver/ilitek_core.h`      core header / constants / packet parsing
- `driver/olive-touchscreen.dtsi` DT node (`tchip,ilitek` on `&spi_3`)
- `driver/Makefile` / `driver/Kconfig` build integration (`CONFIG_TOUCHSCREEN_ITK9881H`)
- `pmaports/APKBUILD`          out-of-tree module package
- `pmaports/deviceinfo`        kernel config fragment

## How to add to pmaports

1. Copy `driver/` into the kernel tree:
   `linux-xiaomi-olive/drivers/input/touchscreen/olive/`
   (add `olive/Makefile` + `olive/Kconfig`, and `obj-y`/`obj-` to the parent
   `drivers/input/touchscreen/Makefile`).
2. Add `CONFIG_TOUCHSCREEN_ITK9881H` to `config-xiaomi-olive.aarch64`.
3. Patch the olive DTS to include `olive-touchscreen.dtsi` under `&spi_3`
   (spi3 = `/soc/spi@78b7000` on MSM8937).
4. Build the kernel with `abuild` (linux-xiaomi-olive APKBUILD); the driver
   builds as `ilitek_spi.ko` and installs to `/lib/modules/<kver>/`.
5. Reboot with the new boot.img; check `dmesg | grep -i ilitek`.

## Reverse-engineering notes (from Firmware/ boot.img DTB)

- Model: Qualcomm MSM8937 + PMI632
- Touch IC: Ilitek ITK9881H (ILI9881H), incell panel, vendor DTS
  `arch/arm64/boot/dts/qcom/sdm439-ctp-olive.dtsi`
- Bus: `spi@78b7000` (spi3), CS 0, 10 MHz, SPI_MODE_0, CS active high
- Reset GPIO: tlmm gpio64  |  IRQ GPIO: tlmm gpio65 (edge)
- Regulators: `vdd`/`vcc` @ 1800000 uV
- Panel: 720x1520, channels 18x32, max 10 touches

## Build notes

No aarch64 cross-compiler or dtc was present on this host, so the code was
not compiled here. Compile against the pmapotos `linux-xiaomi-olive`
kernel tree (4.9-based downstream) or mainline 6.x; the input-report path
(`input_mt_report_slot_state`) may need signature adjustment to match the
target kernel's `<linux/input.h>`.
