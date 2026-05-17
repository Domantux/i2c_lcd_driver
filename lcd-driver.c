#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "lcd-lithuanian.h"

#define LCD_BUF_SIZE	512
#define LCD_COLS	16
#define LCD_ROWS	2
#define LCD_PAGE_CHARS	(LCD_COLS * LCD_ROWS)

struct lcd_data {
	struct i2c_client *client;
	struct mutex lock;
	bool backlight;
	char msg_buf[LCD_BUF_SIZE];
	size_t msg_len;
	u16 display_chars[LCD_BUF_SIZE];
	int num_display_chars;
	int page;
	int num_pages;
	struct delayed_work page_work;
};

#define LCD_PAGE_DELAY_MS	5000

#define BACKLIGHT	0x08
#define ENABLE		0x04
#define RS		0x01

static int lcd_write_byte(struct i2c_client *client, u8 data)
{
	int ret = i2c_smbus_write_byte(client, data);

	if (ret)
		dev_err(&client->dev, "I2C write failed: %d\n", ret);
	return ret;
}

static int lcd_pulse_enable(struct lcd_data *data, u8 val)
{
	u8 bl = data->backlight ? BACKLIGHT : 0;
	int ret;

	ret = lcd_write_byte(data->client, val | ENABLE | bl);
	if (ret)
		return ret;
	udelay(500);
	ret = lcd_write_byte(data->client, (val & ~ENABLE) | bl);
	udelay(100);
	return ret;
}

static int lcd_send_nibble(struct lcd_data *data, u8 nibble, u8 mode)
{
	return lcd_pulse_enable(data, (nibble & 0xF0) | mode);
}

static int lcd_send_byte(struct lcd_data *data, u8 byte, u8 mode)
{
	int ret;

	ret = lcd_send_nibble(data, byte & 0xF0, mode);
	if (ret)
		return ret;
	return lcd_send_nibble(data, (byte << 4) & 0xF0, mode);
}

static int lcd_cmd(struct lcd_data *data, u8 cmd)
{
	return lcd_send_byte(data, cmd, 0);
}

static int lcd_char(struct lcd_data *data, u8 ch)
{
	return lcd_send_byte(data, ch, RS);
}

static int lcd_init(struct lcd_data *data)
{
	struct i2c_client *client = data->client;
	int ret;

	msleep(50);

	ret = lcd_write_byte(client, 0x30 | BACKLIGHT); msleep(5);
	if (ret) return ret;
	ret = lcd_pulse_enable(data, 0x30);           msleep(5);
	if (ret) return ret;
	ret = lcd_pulse_enable(data, 0x30);           udelay(200);
	if (ret) return ret;
	ret = lcd_pulse_enable(data, 0x20);           udelay(200);
	if (ret) return ret;

	ret = lcd_cmd(data, 0x28); if (ret) return ret;
	ret = lcd_cmd(data, 0x08); if (ret) return ret;
	ret = lcd_cmd(data, 0x01); if (ret) return ret;
	msleep(2);
	ret = lcd_cmd(data, 0x06); if (ret) return ret;
	return lcd_cmd(data, 0x0C);
}

/*
 * Parse UTF-8 input into display_chars array.
 * ASCII printable → stored as-is.
 * Lithuanian 2-byte UTF-8 → stored as LT_CHAR_BASE+ ID.
 * Newlines and other control chars → skipped.
 * Unknown multi-byte → replaced with '?'.
 */
static int lcd_parse_utf8(struct lcd_data *data, const char *buf, size_t len)
{
	int n = 0;
	size_t i = 0;

	while (i < len && n < LCD_BUF_SIZE) {
		u8 b = buf[i];

		if (b < 0x80) {
			/* ASCII */
			if (b >= 0x20) /* printable */
				data->display_chars[n++] = b;
			i++;
		} else if ((b & 0xE0) == 0xC0 && i + 1 < len) {
			/* 2-byte UTF-8 */
			u16 lt = utf8_to_lt_id(b, buf[i + 1]);

			if (lt)
				data->display_chars[n++] = lt;
			else
				data->display_chars[n++] = '?';
			i += 2;
		} else if ((b & 0xF0) == 0xE0) {
			/* 3-byte UTF-8 — skip */
			data->display_chars[n++] = '?';
			i += 3;
		} else if ((b & 0xF8) == 0xF0) {
			/* 4-byte UTF-8 — skip */
			data->display_chars[n++] = '?';
			i += 4;
		} else {
			i++; /* invalid byte, skip */
		}
	}
	data->num_display_chars = n;
	return n;
}

/* Write a custom character bitmap to CGRAM slot (0-7) */
static int lcd_write_cgram(struct lcd_data *data, u8 slot, const u8 *bitmap)
{
	int ret, i;

	ret = lcd_cmd(data, 0x40 | (slot << 3));
	if (ret)
		return ret;
	for (i = 0; i < 8; i++) {
		ret = lcd_char(data, bitmap[i]);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Show a page of display_chars on the LCD.
 * 1. Scan page for Lithuanian chars, assign CGRAM slots (max 8).
 * 2. Upload CGRAM bitmaps.
 * 3. Clear display and write 32 chars (16 per line), padding with spaces.
 */
static void lcd_show_page(struct lcd_data *data, int page)
{
	int start = page * LCD_PAGE_CHARS;
	int end = start + LCD_PAGE_CHARS;
	int i, col;
	u16 ch;

	/* CGRAM slot assignment: lt_id → slot number, or -1 */
	u16 cgram_ids[LT_CGRAM_SLOTS];
	int cgram_count = 0;

	if (end > data->num_display_chars)
		end = data->num_display_chars;

	/* Pass 1: find unique Lithuanian chars on this page */
	for (i = start; i < end; i++) {
		ch = data->display_chars[i];
		if (ch >= LT_CHAR_BASE) {
			int j, found = 0;

			for (j = 0; j < cgram_count; j++) {
				if (cgram_ids[j] == ch) {
					found = 1;
					break;
				}
			}
			if (!found && cgram_count < LT_CGRAM_SLOTS)
				cgram_ids[cgram_count++] = ch;
		}
	}

	/* Pass 2: upload CGRAM bitmaps */
	for (i = 0; i < cgram_count; i++)
		lcd_write_cgram(data, i,
				lt_bitmaps[cgram_ids[i] - LT_CHAR_BASE]);

	/* Clear display and set cursor to home */
	lcd_cmd(data, 0x01);
	msleep(2);
	lcd_cmd(data, 0x80);

	/* Pass 3: write characters */
	col = 0;
	for (i = start; i < start + LCD_PAGE_CHARS; i++) {
		if (col == LCD_COLS) {
			lcd_cmd(data, 0xC0); /* move to line 2 */
			col = 0;
		}

		if (i < end) {
			ch = data->display_chars[i];
			if (ch >= LT_CHAR_BASE) {
				/* find CGRAM slot */
				int j, slot = -1;

				for (j = 0; j < cgram_count; j++) {
					if (cgram_ids[j] == ch) {
						slot = j;
						break;
					}
				}
				if (slot >= 0)
					lcd_char(data, (u8)slot);
				else
					lcd_char(data, lt_fallback(ch));
			} else {
				lcd_char(data, (u8)ch);
			}
		} else {
			lcd_char(data, ' '); /* pad */
		}
		col++;
	}

	data->page = page;
}

static void lcd_page_work_fn(struct work_struct *work)
{
	struct lcd_data *data = container_of(work, struct lcd_data,
					     page_work.work);
	int next;

	if (!mutex_trylock(&data->lock))
		return;

	next = (data->page + 1) % data->num_pages;
	lcd_show_page(data, next);

	if (data->num_pages > 1)
		schedule_delayed_work(&data->page_work,
				      msecs_to_jiffies(LCD_PAGE_DELAY_MS));

	mutex_unlock(&data->lock);
}

static ssize_t message_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct lcd_data *data = dev_get_drvdata(dev);
	size_t len = count;

	/* strip trailing newline from echo/shell */
	if (len > 0 && buf[len - 1] == '\n')
		len--;

	if (len == 0)
		return -EINVAL;

	if (len > LCD_BUF_SIZE - 1)
		len = LCD_BUF_SIZE - 1;

	if (!mutex_trylock(&data->lock))
		return -EBUSY;

	memcpy(data->msg_buf, buf, len);
	data->msg_buf[len] = '\0';
	data->msg_len = len;

	lcd_parse_utf8(data, data->msg_buf, len);
	data->num_pages = (data->num_display_chars + LCD_PAGE_CHARS - 1)
			  / LCD_PAGE_CHARS;
	if (data->num_pages == 0)
		data->num_pages = 1;

	lcd_show_page(data, 0);

	cancel_delayed_work(&data->page_work);
	if (data->num_pages > 1)
		schedule_delayed_work(&data->page_work,
				      msecs_to_jiffies(LCD_PAGE_DELAY_MS));

	mutex_unlock(&data->lock);
	return count;
}

static DEVICE_ATTR_WO(message);

static ssize_t page_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct lcd_data *data = dev_get_drvdata(dev);
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (!mutex_trylock(&data->lock))
		return -EBUSY;

	if (val >= data->num_pages) {
		mutex_unlock(&data->lock);
		return -EINVAL;
	}

	lcd_show_page(data, val);

	cancel_delayed_work(&data->page_work);
	if (data->num_pages > 1)
		schedule_delayed_work(&data->page_work,
				      msecs_to_jiffies(LCD_PAGE_DELAY_MS));

	mutex_unlock(&data->lock);
	return count;
}

static ssize_t page_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct lcd_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d/%d\n", data->page, data->num_pages);
}

static DEVICE_ATTR_RW(page);

static ssize_t backlight_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct lcd_data *data = dev_get_drvdata(dev);
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (!mutex_trylock(&data->lock))
		return -EBUSY;

	data->backlight = val ? true : false;
	lcd_write_byte(data->client, data->backlight ? BACKLIGHT : 0);

	mutex_unlock(&data->lock);
	return count;
}

static DEVICE_ATTR_WO(backlight);

static int lcd_probe(struct i2c_client *client)
{
	struct lcd_data *data;
	int ret;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->backlight = true;
	data->num_pages = 1;
	mutex_init(&data->lock);
	INIT_DELAYED_WORK(&data->page_work, lcd_page_work_fn);
	i2c_set_clientdata(client, data);

	ret = lcd_init(data);
	if (ret) {
		dev_err(&client->dev, "LCD init failed: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&client->dev, &dev_attr_message);
	if (ret) {
		dev_err(&client->dev, "Failed to create sysfs entry: %d\n", ret);
		return ret;
	}

	ret = device_create_file(&client->dev, &dev_attr_page);
	if (ret) {
		dev_err(&client->dev, "Failed to create page sysfs entry: %d\n", ret);
		device_remove_file(&client->dev, &dev_attr_message);
		return ret;
	}

	ret = device_create_file(&client->dev, &dev_attr_backlight);
	if (ret) {
		dev_err(&client->dev, "Failed to create backlight sysfs entry: %d\n", ret);
		device_remove_file(&client->dev, &dev_attr_page);
		device_remove_file(&client->dev, &dev_attr_message);
		return ret;
	}

	dev_info(&client->dev, "LCD driver loaded\n");
	return 0;
}

static void lcd_remove(struct i2c_client *client)
{
	struct lcd_data *data = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&data->page_work);
	device_remove_file(&client->dev, &dev_attr_backlight);
	device_remove_file(&client->dev, &dev_attr_page);
	device_remove_file(&client->dev, &dev_attr_message);
	lcd_cmd(data, 0x01);
	dev_info(&client->dev, "LCD driver removed\n");
}

static const struct of_device_id lcd_of_match[] = {
	{ .compatible = "custom,i2c-lcd" },
	{ }
};
MODULE_DEVICE_TABLE(of, lcd_of_match);

static const struct i2c_device_id lcd_id[] = {
	{ "i2c-lcd", 0 },
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
module_i2c_driver(lcd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Domantas Racys");
MODULE_DESCRIPTION("I2C LCD driver for HD44780 via PCF8574");
