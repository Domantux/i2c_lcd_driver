# I2C LCD Kernel Driver

A Linux kernel module that drives a 16x2 HD44780 LCD connected via a PCF8574AT I2C expander on a Raspberry Pi 3 B.

---

## Hardware

- **Raspberry Pi 3 B**
- **16x2 HD44780 LCD** — the display controller
- **PCF8574AT I2C expander** — converts I2C to 8 GPIO pins wired to the LCD
- I2C address: `0x3F` (PCF8574AT range: `0x38–0x3F`)
- I2C bus: bus 1 (pins: SDA=GPIO2, SCL=GPIO3)

### PCF8574AT pin mapping

| PCF8574 pin | Bit  | LCD pin                  |
|-------------|------|--------------------------|
| P0          | 0x01 | RS (Register Select)     |
| P1          | 0x02 | RW (always 0 = write)    |
| P2          | 0x04 | E (Enable)               |
| P3          | 0x08 | Backlight                |
| P4          | 0x10 | D4                       |
| P5          | 0x20 | D5                       |
| P6          | 0x40 | D6                       |
| P7          | 0x80 | D7                       |

---

## Step-by-step: How we built and deployed this

### 1. Enable I2C on the Pi

I2C is disabled by default. On our buildroot-based Pi, we edited the boot config directly over serial console. The boot partition was not auto-mounted, so:

```sh
mount /dev/mmcblk0p1 /mnt
echo "dtparam=i2c_arm=on" >> /mnt/config.txt
reboot
```

After reboot, verify the device is detected:

```sh
i2cdetect -y 1
# Should show 3f at address 0x3F
```

### 2. Write the driver

Created `lcd-driver.c` — a Linux kernel I2C driver using:
- `i2c_driver` / `i2c_client` kernel interfaces
- `i2c_smbus_write_byte` to send bytes over I2C
- sysfs attributes (`message`, `backlight`) for userspace control
- `mutex` to prevent concurrent access
- Device tree matching via `compatible = "custom,i2c-lcd"`

### 3. Write the device tree overlay

Created `lcd-driver-overlay.dts` to tell the kernel there is an LCD device at address `0x3F` on I2C bus 1:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2837";

    fragment@0 {
        target = <&i2c1>;
        __overlay__ {
            #address-cells = <1>;
            #size-cells = <0>;

            lcd@3f {
                compatible = "custom,i2c-lcd";
                reg = <0x3f>;
            };
        };
    };
};
```

Key points:
- `target = <&i2c1>` — attaches this overlay to the i2c1 bus node
- `#address-cells = <1>` — addresses are 1 cell (32-bit), used for I2C addresses
- `#size-cells = <0>` — I2C devices have no memory range, so size is 0
- `lcd@3f` — device node name, `3f` is the I2C address in hex
- `compatible = "custom,i2c-lcd"` — must match `.compatible` in the driver's `of_match` table
- `reg = <0x3f>` — the I2C address

### 4. Write the Makefile

```makefile
obj-m += lcd-driver.o
ccflags-y += -fno-stack-protector

ifndef KDIR
$(error KDIR is not set. Usage: make KDIR=/path/to/kernel/source)
endif

ARCH ?= arm
CROSS_COMPILE ?= arm-unknown-linux-musleabihf-

all:
    make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
    make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
```

- `obj-m` — tells the kernel build system to build this as a loadable module (`.ko`)
- `-fno-stack-protector` — disables stack canaries (not available in kernel context)
- `-C $(KDIR)` — run make inside the kernel source tree
- `M=$(PWD)` — tells the kernel build system where our out-of-tree module source is
- `ARCH=arm` — target architecture
- `CROSS_COMPILE` — prefix for the cross-compiler toolchain

### 5. Build the module

On the host machine (x86), cross-compile for ARM:

```sh
make KDIR=/path/to/kernel/source
```

This produces `lcd-driver.ko`.

### 6. Compile the device tree overlay

```sh
dtc -@ -I dts -O dtb -o lcd-driver.dtbo lcd-driver-overlay.dts
```

- `-@` — keep symbol information (required for overlays)
- `-I dts` — input format is DTS (source text)
- `-O dtb` — output format is DTB (binary blob)

### 7. Deploy to the Pi

```sh
# Copy the kernel module
scp -O lcd-driver.ko root@<pi-ip>:~/

# Copy the overlay
scp -O lcd-driver.dtbo root@<pi-ip>:~/
```

### 8. Install on the Pi

```sh
# Mount boot partition
mount /dev/mmcblk0p1 /mnt

# Install overlay
cp lcd-driver.dtbo /mnt/overlays/

# Enable overlay in config
echo "dtoverlay=lcd-driver" >> /mnt/config.txt

# Install module for modprobe
mkdir -p /lib/modules/$(uname -r)/extra
cp lcd-driver.ko /lib/modules/$(uname -r)/extra/
depmod -a

reboot
```

`depmod -a` scans all `.ko` files in `/lib/modules/$(uname -r)/` and writes a dependency map so `modprobe` can find modules by name.

### 9. Load and test

```sh
modprobe lcd-driver

# Send a message to the LCD
echo "Hello World" > /sys/bus/i2c/devices/1-003f/message

# Control backlight (1=on, 0=off)
echo 1 > /sys/bus/i2c/devices/1-003f/backlight

# Unload
modprobe -r lcd-driver
```

The sysfs path `1-003f` means: bus 1, address 0x3F.

---

## Code walkthrough: lcd-driver.c

### Includes

```c
#include <linux/module.h>   // MODULE_LICENSE, module_init/exit macros
#include <linux/i2c.h>      // i2c_driver, i2c_client, i2c_smbus_write_byte
#include <linux/delay.h>    // msleep, udelay
#include <linux/slab.h>     // devm_kzalloc (kernel memory allocation)
#include <linux/of.h>       // of_device_id, device tree matching
#include <linux/mutex.h>    // mutex_init, mutex_trylock, mutex_unlock
```

### Driver state struct

```c
struct lcd_data {
    struct i2c_client *client;  // pointer to the I2C device (bus + address)
    struct mutex lock;          // prevents two writes happening at the same time
    bool backlight;             // current backlight state (on/off)
};
```

This struct holds everything the driver needs to know at runtime. One instance is allocated per device in `lcd_probe`.

### Defines

```c
#define BACKLIGHT   0x08   // bit 3 = P3 on PCF8574 = backlight transistor
#define ENABLE      0x04   // bit 2 = P2 on PCF8574 = E pin on LCD
#define RS          0x01   // bit 0 = P0 on PCF8574 = RS pin on LCD
#define LCD_MAX_CHARS 33   // 16 chars + newline + 16 chars
```

Each define is a bitmask for one pin on the PCF8574. When we OR these together into a single byte and send it over I2C, the PCF8574 sets those GPIO pins high.

### `lcd_write_byte`

```c
static int lcd_write_byte(struct i2c_client *client, u8 data)
{
    int ret = i2c_smbus_write_byte(client, data);

    if (ret)
        dev_err(&client->dev, "I2C write failed: %d\n", ret);
    return ret;
}
```

`i2c_smbus_write_byte` is the kernel function that sends a single byte to the I2C device. SMBus is a subset of I2C — this function sends: START, address byte, data byte, STOP.

Returns 0 on success, negative error code on failure. `dev_err` prints to the kernel log with the device name as a prefix.

### `lcd_pulse_enable`

```c
static int lcd_pulse_enable(struct lcd_data *data, u8 val)
{
    u8 bl = data->backlight ? BACKLIGHT : 0;
    int ret;

    ret = lcd_write_byte(data->client, val | ENABLE | bl);  // set E=1
    if (ret)
        return ret;
    udelay(500);
    ret = lcd_write_byte(data->client, (val & ~ENABLE) | bl);  // set E=0
    udelay(100);
    return ret;
}
```

The HD44780 reads data on the **falling edge of the E pin**. This function creates that pulse:

1. Send byte with E bit set (`ENABLE = 0x04`) — LCD sees E going high
2. Wait 500µs — give the LCD time to read the data pins
3. Send same byte with E bit cleared (`val & ~ENABLE`) — falling edge: LCD latches data
4. Wait 100µs — settling time

`bl` is OR'd into every write so the backlight state is preserved across all I2C writes. Without this, writing a nibble would turn off the backlight.

`~ENABLE` is the bitwise NOT of `0x04` = `0xFB` = `11111011`. AND-ing with it clears only bit 2, leaving all other bits unchanged.

### `lcd_send_nibble`

```c
static int lcd_send_nibble(struct lcd_data *data, u8 nibble, u8 mode)
{
    return lcd_pulse_enable(data, (nibble & 0xF0) | mode);
}
```

Puts 4 data bits on D4–D7 (bits P4–P7 of the PCF8574, which are the high nibble of the byte) and sets RS via `mode` (either 0 for command or `RS=0x01` for character). Then pulses E so the LCD reads it.

`nibble & 0xF0` masks off the low 4 bits so only the high nibble reaches D4–D7. The OR with `mode` sets the RS pin.

### `lcd_send_byte`

```c
static int lcd_send_byte(struct lcd_data *data, u8 byte, u8 mode)
{
    int ret;

    ret = lcd_send_nibble(data, byte & 0xF0, mode);   // high nibble first
    if (ret)
        return ret;
    return lcd_send_nibble(data, (byte << 4) & 0xF0, mode);  // low nibble second
}
```

The HD44780 is in 4-bit mode — it only has 4 data pins (D4–D7). To send one full byte you must send it in two halves:

1. `byte & 0xF0` — isolates the high nibble (e.g. `0xAC & 0xF0 = 0xA0`)
2. `(byte << 4) & 0xF0` — shifts the low nibble up into the high position (e.g. `0xAC << 4 = 0xC0`)

Both halves are sent with the same `mode` (RS=0 or RS=1).

### `lcd_cmd` and `lcd_char`

```c
static int lcd_cmd(struct lcd_data *data, u8 cmd)
{
    return lcd_send_byte(data, cmd, 0);   // RS=0: command register
}

static int lcd_char(struct lcd_data *data, u8 ch)
{
    return lcd_send_byte(data, ch, RS);   // RS=1: data register
}
```

The HD44780 has two registers selected by the RS pin:
- `RS=0` → command register (move cursor, clear, configure)
- `RS=1` → data register (write a character to the display)

### `lcd_init`

```c
static int lcd_init(struct lcd_data *data)
{
    struct i2c_client *client = data->client;
    int ret;

    msleep(50);  // wait for LCD power-up stabilization (datasheet: min 40ms)

    // Three 0x30 pulses in 8-bit mode — resets the LCD to a known state
    // These are sent as single nibbles (not full bytes) because the LCD
    // starts in 8-bit mode and we haven't switched it to 4-bit yet.
    ret = lcd_write_byte(client, 0x30 | BACKLIGHT); msleep(5);   // pulse 1
    if (ret) return ret;
    ret = lcd_pulse_enable(data, 0x30);             msleep(5);   // pulse 2
    if (ret) return ret;
    ret = lcd_pulse_enable(data, 0x30);             udelay(200); // pulse 3
    if (ret) return ret;
    ret = lcd_pulse_enable(data, 0x20);             udelay(200); // switch to 4-bit
    if (ret) return ret;

    // Now in 4-bit mode — send full commands as two nibbles
    ret = lcd_cmd(data, 0x28); if (ret) return ret;  // 4-bit, 2 lines, 5x8 font
    ret = lcd_cmd(data, 0x08); if (ret) return ret;  // display off
    ret = lcd_cmd(data, 0x01); if (ret) return ret;  // clear display
    msleep(2);                                        // clear takes ~1.5ms
    ret = lcd_cmd(data, 0x06); if (ret) return ret;  // entry mode: cursor moves right
    return lcd_cmd(data, 0x0C);                      // display on, cursor off
}
```

The first `lcd_write_byte(client, 0x30 | BACKLIGHT)` is slightly different from the rest — it sets the data pins but does NOT pulse E. This is intentional: the first reset pulse is sent by simply putting `0x30` on the bus. The subsequent pulses use `lcd_pulse_enable` which toggles E.

The three `0x30` pulses force the LCD into 8-bit mode regardless of its current state. Then `0x20` switches it to 4-bit mode. After that all communication uses two nibbles per byte.

### `lcd_print`

```c
static void lcd_print(struct lcd_data *data, const char *str)
{
    int col = 0;   // current column (0–15)
    int line = 0;  // current line (0 or 1)

    while (line < 2) {
        if (*str && *str != '\n') {
            // Normal character — send it and advance column
            if (lcd_char(data, *str++))
                return;
            col++;
        } else {
            // End of string or newline — pad the rest of this line with spaces
            // This overwrites any leftover characters from a previous message
            while (col < 16) {
                if (lcd_char(data, ' '))
                    return;
                col++;
            }
            // Move to line 2 or stop
            if (line == 0) {
                lcd_cmd(data, 0xC0);     // 0xC0 = set cursor to start of line 2
                if (*str == '\n')
                    str++;               // consume the newline
                col = 0;
                line++;
            } else {
                break;                   // done with line 2, stop
            }
        }

        // If line 1 is full (16 chars), move to line 2
        if (col == 16 && line == 0) {
            lcd_cmd(data, 0xC0);
            if (*str == '\n')
                str++;
            col = 0;
            line++;
        }
    }
}
```

Both lines are always fully written (padded with spaces if needed). This ensures a short new message doesn't leave stale characters from a longer previous message visible on the display.

`\n` in the string jumps to line 2 early. If the string is shorter than 16 chars, the rest of the line is space-padded.

### `message_store` — sysfs write handler

```c
static ssize_t message_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf, size_t count)
{
    struct lcd_data *data = dev_get_drvdata(dev);

    if (count > LCD_MAX_CHARS)    // reject messages that are too long
        return -EINVAL;

    if (!mutex_trylock(&data->lock))  // fail immediately if already busy
        return -EBUSY;

    lcd_cmd(data, 0x01);   // clear display
    msleep(2);             // wait for clear to finish
    lcd_cmd(data, 0x80);   // set cursor to start of line 1
    lcd_print(data, buf);  // write the message

    mutex_unlock(&data->lock);
    return count;   // must return count to indicate all bytes were consumed
}

static DEVICE_ATTR_WO(message);  // creates /sys/.../message (write-only)
```

`dev_get_drvdata` retrieves the `lcd_data` pointer we stored in `lcd_probe` via `i2c_set_clientdata`. This is how kernel drivers pass private state through the generic device layer.

`mutex_trylock` returns 1 if the lock was acquired, 0 if it was already held. We use `trylock` instead of `lock` because blocking in a sysfs handler would hang the writing process. Returning `-EBUSY` lets userspace retry.

### `backlight_store` — sysfs write handler

```c
static ssize_t backlight_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
    struct lcd_data *data = dev_get_drvdata(dev);
    unsigned long val;

    if (kstrtoul(buf, 10, &val))  // parse decimal string → unsigned long
        return -EINVAL;

    if (!mutex_trylock(&data->lock))
        return -EBUSY;

    data->backlight = val ? true : false;
    // Send a byte with only the backlight bit set/cleared — no E pulse,
    // just directly sets the PCF8574 output pins
    lcd_write_byte(data->client, data->backlight ? BACKLIGHT : 0);

    mutex_unlock(&data->lock);
    return count;
}

static DEVICE_ATTR_WO(backlight);  // creates /sys/.../backlight (write-only)
```

`kstrtoul(buf, 10, &val)` converts the ASCII string from sysfs (e.g. `"1\n"`) to an unsigned long. Base 10. Returns 0 on success, non-zero on error.

### `lcd_probe`

```c
static int lcd_probe(struct i2c_client *client)
{
    struct lcd_data *data;
    int ret;

    // Allocate zeroed memory, managed by the device lifetime
    // devm_* memory is automatically freed when the device is removed
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    data->backlight = true;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, data);  // attach our struct to the device

    ret = lcd_init(data);
    if (ret) {
        dev_err(&client->dev, "LCD init failed: %d\n", ret);
        return ret;
    }

    // Create /sys/bus/i2c/devices/1-003f/message
    ret = device_create_file(&client->dev, &dev_attr_message);
    if (ret) {
        dev_err(&client->dev, "Failed to create sysfs entry: %d\n", ret);
        return ret;
    }

    // Create /sys/bus/i2c/devices/1-003f/backlight
    ret = device_create_file(&client->dev, &dev_attr_backlight);
    if (ret) {
        dev_err(&client->dev, "Failed to create backlight sysfs entry: %d\n", ret);
        device_remove_file(&client->dev, &dev_attr_message);  // clean up first attr
        return ret;
    }

    dev_info(&client->dev, "LCD driver loaded\n");
    return 0;
}
```

`probe` is called by the kernel when a device matching our `of_match` table or `id_table` is found. This happens after the overlay is loaded and the kernel sees the `lcd@3f` node in the device tree.

`GFP_KERNEL` means "normal kernel allocation, can sleep if needed".

### `lcd_remove`

```c
static void lcd_remove(struct i2c_client *client)
{
    struct lcd_data *data = i2c_get_clientdata(client);

    device_remove_file(&client->dev, &dev_attr_backlight);
    device_remove_file(&client->dev, &dev_attr_message);
    lcd_cmd(data, 0x01);  // clear display on unload
    dev_info(&client->dev, "LCD driver removed\n");
}
```

Called when the module is unloaded (`modprobe -r`). Removes the sysfs files and clears the display so it doesn't show stale content after the driver is gone. The `devm_kzalloc` memory is freed automatically by the kernel after `remove` returns.

### Driver registration

```c
static const struct of_device_id lcd_of_match[] = {
    { .compatible = "custom,i2c-lcd" },  // matches device tree node
    { }
};
MODULE_DEVICE_TABLE(of, lcd_of_match);  // exports table for udev/autoloading

static const struct i2c_device_id lcd_id[] = {
    { "i2c-lcd", 0 },  // fallback: match by name if no device tree
    { }
};
MODULE_DEVICE_TABLE(i2c, lcd_id);

static struct i2c_driver lcd_driver = {
    .probe    = lcd_probe,
    .remove   = lcd_remove,
    .driver   = {
        .name           = "i2c-lcd",
        .of_match_table = lcd_of_match,
    },
    .id_table = lcd_id,
};
module_i2c_driver(lcd_driver);  // expands to module_init + module_exit
```

`module_i2c_driver` is a macro that registers the driver on module load and unregisters it on unload. When the kernel finds an I2C device whose `compatible` string matches `"custom,i2c-lcd"`, it calls our `lcd_probe`.

```c
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Domantas Racys");
MODULE_DESCRIPTION("I2C LCD driver for HD44780 via PCF8574");
```

`MODULE_LICENSE("GPL")` is required for the module to use GPL-only kernel symbols (like `i2c_smbus_write_byte`). Without it the kernel would refuse to load the module or print "tainted kernel" warnings.

---

## References

- [HD44780 datasheet](https://cdn.sparkfun.com/assets/9/5/f/7/b/HD44780.pdf) — command table (p.24), init flowchart (p.45), timing (p.22)
- [PCF8574 datasheet](https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf) — I2C GPIO expander
- [Linux I2C driver API](https://docs.kernel.org/i2c/writing-clients.html) — i2c_driver, probe, smbus functions
- [Linux device tree overlays](https://docs.kernel.org/devicetree/overlay-notes.html)
