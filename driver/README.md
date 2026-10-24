# Touch Driver for Xiaomi Redmi 8 (olive) — PostMarketOS 26.06

## Reverse-engineering findings (from Firmware/images/boot.img)

Touch controller: **Ilitek ITK9881H** (ILI9881H) incell panel, Xiaomi vendor DTS
`arch/arm64/boot/dts/qcom/sdm439-ctp-olive.dtsi`

| Property | Value |
|----------|-------|
| Bus | `spi@78b7000` (spi3), CS 0 |
| Clock | 10 MHz, SPI_MODE_0, CS active high |
| Reset GPIO | tlmm gpio64 (`touch,reset-gpio`) |
| IRQ GPIO | tlmm gpio65 falling edge (`touch,irq-gpio`, flags 0x2001) |
| Regulators | `vdd` / `vcc` @ 1800000 uV |
| Panel | 720 × 1520, 18 × 32 sense channels, max 10 touches |
| Compatible | `tchip,ilitek` |

DTB was extracted by decompressing the gzip kernel appended to `boot.img` and
parsing the appended Flattened Device Tree (magic `d00dfeed`).

## Ported driver (mainline Linux 6.x)

- `ilitek_core.h` — core header, SPI command set, constants, state struct.
- `ilitek_spi.c` — mainline driver: devm regulators/GPIOs, threaded IRQ, SPI
  write/read/write-then-read, HW reset, IC init, input device registration,
  demo-mode packet parse, probe/remove/shutdown.
- `olive-touchscreen.dtsi` — DT node under `&spi_3` with all recovered bindings.
- `Makefile` / `Kconfig` — `CONFIG_TOUCHSCREEN_ITK9881H`.

## Verified build

Cross-compiled as an out-of-tree module against Linux 6.12 (Debian trixie
kernel-source) with `aarch64-linux-gnu-gcc`:

```
make -C <kernel-tree> M=$PWD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
```

Produced `ilitek_spi.ko` — ELF 64-bit LSB relocatable, ARM aarch64.
(Out-of-tree modpost unresolved-symbol warnings are expected; the symbols
resolve once loaded into the pmapotos `linux-xiaomi-olive` kernel which has
SPI/regulator/input subsystems built in.)

## pmaports packaging

See `pmaports/` — `APKBUILD`, `deviceinfo` (config fragment
`CONFIG_TOUCHSCREEN_ITK9881H=m`), and `README.pmaports.md` with integration
steps for the `linux-xiaomi-olive` kernel package.

## Next steps

1. Add `driver/` to `linux-xiaomi-olive` at
   `drivers/input/touchscreen/olive/`; wire `Makefile`/`Kconfig`.
2. Add `CONFIG_TOUCHSCREEN_ITK9881H=m` to
   `config-xiaomi-olive.aarch64`.
3. Include `olive-touchscreen.dtsi` in the olive DTS under `&spi_3`.
4. Build `linux-xiaomi-olive` via `abuild`; flash new boot.img.
5. Verify: `dmesg | grep -i ilitek` → "ILITEK TP driver init", "CHIP found";
   `evtest` on `/dev/input/event*` → touch events.

EOF
