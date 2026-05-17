# I2C LCD Kernel Module

A Linux kernel module that drives a 16x2 HD44780 LCD connected via a PCF8574AT I2C expander on Raspberry Pi. Provides sysfs interfaces for writing messages and controlling the backlight with mutex-protected concurrent write access.

## Features

- Drives HD44780 LCD in 4-bit mode via PCF8574AT I2C expander
- Message display via sysfs `message` attribute — supports two lines with `\n`
- Both lines always fully overwritten (space-padded) to clear stale content
- Backlight on/off control via sysfs `backlight` attribute
- Mutex protection (`mutex_trylock`) on all writes — concurrent writes return `-EBUSY`
- Device Tree overlay for I2C device registration

## Hardware

- **Board:** Raspberry Pi 3 B (BCM2837, ARM 32-bit)
- **LCD:** 16x2 HD44780 display controller
- **I2C Expander:** PCF8574AT — converts I2C to 8 GPIO pins wired to the LCD
- **I2C Bus:** bus 1 (SDA=GPIO2, SCL=GPIO3)
- **I2C Address:** `0x3F`

### PCF8574AT pin mapping

| PCF8574 pin | Bit  | LCD pin              |
|-------------|------|----------------------|
| P0          | 0x01 | RS (Register Select) |
| P1          | 0x02 | RW (always 0)        |
| P2          | 0x04 | E (Enable)           |
| P3          | 0x08 | Backlight            |
| P4–P7       | 0x10–0x80 | D4–D7          |

## Prerequisites

- Linux kernel source tree (matching target kernel)
- ARM cross-compiler toolchain (`arm-unknown-linux-musleabihf-`)
- Device Tree compiler (`dtc`)
- I2C enabled on the Pi (`dtparam=i2c_arm=on` in `config.txt`)

## Building

Source the cross-compilation environment and build with **required** `KDIR` parameter:

```bash
source ~/Documents/arm-env.sh
make KDIR=/path/to/kernel/source
```

**Important:** `KDIR` is mandatory and must point to the **Raspberry Pi kernel source**, not the host kernel. Example:

```bash
make KDIR=/home/studentas/Teltonikos_darbai/Projektai/operating_systems/os/src/linux
```

Attempting to build without `KDIR` will result in an error:

```
Makefile:5: *** KDIR is not set. Usage: make KDIR=/path/to/kernel/source.  Stop.
```

Clean build artifacts:

```bash
make KDIR=/path/to/kernel/source clean
```

## Device Tree Overlay

Compile the overlay (if not already compiled):

```bash
dtc -@ -I dts -O dtb -o lcd-driver.dtbo lcd-driver-overlay.dts
```

## Installation

Copy the module and overlay to the Raspberry Pi, then:

```bash
# Mount boot partition
mount /dev/mmcblk0p1 /mnt

# Install overlay
cp lcd-driver.dtbo /mnt/overlays/
echo "dtoverlay=lcd-driver" >> /mnt/config.txt

# Install module for modprobe
mkdir -p /lib/modules/$(uname -r)/extra
cp lcd-driver.ko /lib/modules/$(uname -r)/extra/
depmod -a

reboot
```

After reboot, load the module:

```bash
modprobe lcd-driver
```

## Usage

### Display a message

```bash
# Single line
echo "Hello World" > /sys/bus/i2c/devices/1-003f/message

# Two lines (\n splits to line 2)
echo -e "Line 1\nLine 2" > /sys/bus/i2c/devices/1-003f/message
```

### Backlight control

```bash
# Turn backlight off
echo 0 > /sys/bus/i2c/devices/1-003f/backlight

# Turn backlight on
echo 1 > /sys/bus/i2c/devices/1-003f/backlight
```

### Mutex protection

Concurrent writes are rejected with `EBUSY`:

```bash
# Terminal 1 — acquires mutex
echo "Hello" > /sys/bus/i2c/devices/1-003f/message

# Terminal 2 — rejected if Terminal 1 is still writing
echo "World" > /sys/bus/i2c/devices/1-003f/message
# -sh: write error: Resource busy
```

## Unloading

```bash
modprobe -r lcd-driver
```

The display is cleared automatically on unload.

## File Structure

| File | Description |
|------|-------------|
| `lcd-driver.c` | Kernel module source |
| `lcd-driver-overlay.dts` | Device Tree overlay source |
| `Makefile` | Cross-compilation build system |
| `README-driver.md` | Step-by-step guide and line-by-line code analysis |

## License

GPL-2.0

## Author

Domantas Racys
