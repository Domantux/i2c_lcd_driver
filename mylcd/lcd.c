#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define LCD_ADDR    0x3F
#define I2C_DEV     "/dev/i2c-1"

#define BACKLIGHT   0x08
#define ENABLE      0x04
#define RS          0x01

static int fd;

static void lcd_write_byte(unsigned char data)
{
    write(fd, &data, 1);
}

static void lcd_pulse_enable(unsigned char data)
{
    lcd_write_byte(data | ENABLE | BACKLIGHT);
    usleep(500);
    lcd_write_byte((data & ~ENABLE) | BACKLIGHT);
    usleep(100);
}

static void lcd_send_nibble(unsigned char nibble, unsigned char mode)
{
    lcd_pulse_enable((nibble & 0xF0) | mode);
}

static void lcd_send_byte(unsigned char byte, unsigned char mode)
{
    lcd_send_nibble(byte & 0xF0, mode);
    lcd_send_nibble((byte << 4) & 0xF0, mode);
}

static void lcd_cmd(unsigned char cmd)
{
    lcd_send_byte(cmd, 0);
}

static void lcd_char(unsigned char ch)
{
    lcd_send_byte(ch, RS);
}

static void lcd_init(void)
{
    usleep(50000);

    /* Init sequence: switch to 4-bit mode */
    lcd_write_byte(0x30 | BACKLIGHT); usleep(5000);
    lcd_pulse_enable(0x30);           usleep(5000);
    lcd_pulse_enable(0x30);           usleep(200);
    lcd_pulse_enable(0x20);           usleep(200);

    lcd_cmd(0x28); /* 4-bit, 2 lines, 5x8 */
    lcd_cmd(0x08); /* display off */
    lcd_cmd(0x01); /* clear */
    usleep(2000);
    lcd_cmd(0x06); /* entry mode: increment */
    lcd_cmd(0x0C); /* display on, cursor off */
}

static void lcd_print(const char *str)
{
    int col = 0;
    while (*str) {
        if (*str == '\n' || col == 16) {
            lcd_cmd(0xC0); /* move to second line */
            col = 0;
            if (*str == '\n') { str++; continue; }
        }
        lcd_char(*str++);
        col++;
    }
}

int main(void)
{
    fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, LCD_ADDR) < 0) {
        perror("ioctl");
        return 1;
    }

    lcd_init();
    lcd_cmd(0x80); /* cursor to start */
    lcd_print("Hello World!");

    close(fd);
    return 0;
}
