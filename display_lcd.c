#include "display.h"
#include "display_lcd.h"
#include "io.h"
#include "config.h"
#include "dispatch.h"

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
} unicode_map_t;

typedef struct
{
	unsigned int	unicode;
	unsigned int	internal;
	unsigned int	pattern[8];
} udg_map_t;

enum
{
	display_text_width =	20,
	display_text_height =	4,
};

enum
{
	cmd_clear_screen =		0b00000001,
	cmd_home =				0b00000011,
	cmd_cursor_config =		0b00000110, // cursor move direction = LTR / no display shift
	cmd_display_config =	0b00001100, // display on, cursor off, blink off
	cmd_off_off_off =		0b00001000,	//	display off, cursor off, blink off
	cmd_on_off_off =		0b00001100,	//	display on, cursor off, blink off
	cmd_special_reset =		0b00110000,	//	datalength = 8, 2 lines, font = 5x8
	cmd_special_nibble =	0b00100000,	//	datalength = 4, 1 lines, font = 5x8
	cmd_nibble_mode =		0b00101000,	//	datalength = 4, 2 lines, font = 5x8
	cmd_byte_mode =			0b00111000,	//	datalength = 8, 2 lines, font = 5x8
	cmd_set_udg_ptr =		0b01000000,
	cmd_set_ram_ptr =		0b10000000,
};

enum
{
	mapeof = 0xffffffff,
};

roflash static const unicode_map_t unicode_map[] =
{
	{	0x005c,	'/'		},	//	backslash -> /
	{	0x007e,	'-'		},	//	~ -> -
	{	0x00a5,	0x5c	},	//	¥
	{	0x2192, 0x7e	},	//	→ 
	{	0x2190, 0x7f	},	//	← 
	{	0x00b0, 0xdf	},	//	°
	{	0x03b1, 0xe0	},	//	α
	{	0x00e4, 0xe1	},	//	ä
	{	0x03b2, 0xe2	},	//	β
	{	0x03b5, 0xe3	},	//	ε
	{	0x00b5, 0xe4	},	//	µ
	{	0x03bc, 0xe4	},	//	μ
	{	0x03c3, 0xe5	},	//	σ
	{	0x03c1, 0xe6	},	//	ρ
	{	0x00a2, 0xec	},	//	¢
	{	0x00f1, 0xee	},	//	ñ
	{	0x00f6, 0xef	},	//	ö
	{	0x03b8, 0xf2	},	//	θ
	{	0x221e, 0xf3	},	//	∞
	{	0x03a9, 0xf4	},	//	Ω
	{	0x00fc, 0xf5	},	//	ü
	{	0x03a3, 0xf6	},	//	Σ
	{	0x03c0, 0xf7	},	//	π
	{	0x00f7, 0xfd	},	//	÷
	{	0x258b,	0xff	},	//	▋
	{	mapeof,	0x00	},	//	EOF
};

roflash static const udg_map_t udg_map[] =
{
	{
		0x00e8,	0,	//	è
		{
			0b00001000,
			0b00000100,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x00e9,	1,	//	é
		{
			0b00000100,
			0b00001000,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x00eb,	2,	//	ë
		{
			0b00001010,
			0b00000000,
			0b00001110,
			0b00010001,
			0b00011111,
			0b00010000,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x00f4,	3,	//	ô
		{
			0b00000100,
			0b00001010,
			0b00001110,
			0b00010001,
			0b00010001,
			0b00010001,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x03b4,	4,	//	δ
		{
			0b00001111,
			0b00001000,
			0b00000100,
			0b00001110,
			0b00010001,
			0b00010001,
			0b00001110,
			0b00000000,
		}
	},
	{
		0x03bb,	5,	//	λ
		{
			0b00000000,
			0b00010000,
			0b00011000,
			0b00001100,
			0b00001010,
			0b00010001,
			0b00010001,
			0b00000000,
		}
	},
	{
		0x00a9,	6,	//	©
		{
			0b00000100,
			0b00001010,
			0b00010111,
			0b00011001,
			0b00010111,
			0b00001010,
			0b00000100,
			0b00000000,
		}
	},
	{
		0x20ac,	7,	//	€
		{
			0b00000011,
			0b00000100,
			0b00011111,
			0b00000100,
			0b00011111,
			0b00000100,
			0b00000011,
			0b00000000,
		}
	},
	{
		mapeof,	0,	//	EOF
		{
		}
	}
};

roflash static const unsigned int ram_offsets[4] =
{
	0	+	0,
	0	+	64,
	20	+	0,
	20	+	64
};

_Static_assert(display_buffer_size > (sizeof(int) * io_lcd_size), "display buffer too small");

static bool display_inited = false;
static bool display_logmode;
static bool nibble_mode;
static bool display_disable_text;
static int bl_io;
static int bl_pin;
static int lcd_io;
static int *lcd_pin = (int *)(void *)display_buffer; // this memory is guaranteed int aligned
static unsigned int pin_mask;
static unsigned int display_x, display_y;
static unsigned int display_picture_load_flash_sector;

static unsigned int attr_result_used bit_to_pin(unsigned int value, unsigned int src_bitindex, unsigned int function)
{
	unsigned int pin = 0;

	if(value & (1 << src_bitindex))
		pin = 1;

	return(pin << lcd_pin[function]);
}

static bool attr_result_used send_byte_raw(unsigned int byte, bool data)
{
	unsigned int pins = 0;

	io_set_mask((string_t *)0, lcd_io, pin_mask, 0);

	pins |= bit_to_pin(!!data, 0, io_lcd_rs);
	pins |= bit_to_pin(0, 0, io_lcd_rw);
	pins |= bit_to_pin(byte, 0, io_lcd_d0);
	pins |= bit_to_pin(byte, 1, io_lcd_d1);
	pins |= bit_to_pin(byte, 2, io_lcd_d2);
	pins |= bit_to_pin(byte, 3, io_lcd_d3);
	pins |= bit_to_pin(byte, 4, io_lcd_d4);
	pins |= bit_to_pin(byte, 5, io_lcd_d5);
	pins |= bit_to_pin(byte, 6, io_lcd_d6);
	pins |= bit_to_pin(byte, 7, io_lcd_d7);
	pins |= bit_to_pin(1, 0, io_lcd_e);

	io_set_mask((string_t *)0, lcd_io, pin_mask, pins);

	return(true);
}

static bool attr_result_used send_byte(unsigned int byte, bool data)
{
	if(nibble_mode)
	{
		if(!send_byte_raw((byte & 0xf0) << 0, data))
			return(false);

		if(!send_byte_raw((byte & 0x0f) << 4, data))
			return(false);

		return(true);
	}

	return(send_byte_raw(byte, data));
}

static bool attr_result_used text_send(unsigned int byte)
{
	display_x++;

	if((display_x >= display_text_width) || (display_y >= display_text_height))
		return(true);

	if(!send_byte(byte, true))
		return(false);

	return(true);
}

static bool attr_result_used text_goto(int x, int y)
{
	if(x >= 0)
		display_x = x;

	if(y >= 0)
		display_y = y;

	if((display_x >= display_text_width) || (display_y >= display_text_height))
		return(true);

	if(!send_byte(cmd_set_ram_ptr | (ram_offsets[display_y] + display_x), false))
		return(false);

	return(true);
}

static bool attr_result_used text_newline(void)
{
	unsigned int x, y;

	if(display_logmode)
	{
		y = (display_y + 1) % display_text_height;
		if(!text_goto(0, y))
			return(false);
	}
	else
		y = display_y + 1;

	if(display_y < display_text_height)
		for(x = display_x; x < display_text_width; x++)
			if(!text_send(' '))
				return(false);

	if(!text_goto(0, y))
		return(false);

	return(true);
}

static bool attr_result_used udg_init(void)
{
	const udg_map_t *udg_map_ptr;
	unsigned int byte;

	if(!send_byte(cmd_set_udg_ptr, false))
		return(false);

	msleep(2);

	for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
		for(byte = 0; byte < 8; byte++)
			if(!send_byte(udg_map_ptr->pattern[byte], true))
				return(false);

	return(true);
}

bool display_lcd_init(void)
{
	io_config_pin_entry_t *pin_config;
	int io, pin;

	bl_io	= -1;
	bl_pin	= -1;
	lcd_io	= -1;
	pin_mask = 0;

	for(pin = 0; pin < io_lcd_size; pin++)
		lcd_pin[pin] = -1;

	for(io = 0; io < io_id_size; io++)
	{
		for(pin = 0; pin < max_pins_per_io; pin++)
		{
			pin_config = &io_config[io][pin];

			if(pin_config->mode == io_pin_lcd)
			{
				if(pin_config->shared.lcd.pin_use == io_lcd_bl)
				{
					bl_io = io;
					bl_pin = pin;
				}
				else
				{
					if((lcd_io >= 0) && (io != lcd_io))
					{
						log("* lcd: pins must be on same device\n");
						continue;
					}

					lcd_io = io;
					lcd_pin[pin_config->shared.lcd.pin_use] = pin;
					pin_mask |= 1 << pin;
				}
			}
		}
	}

	if(lcd_io < 0)
		return(false);

	if(lcd_pin[io_lcd_rs] < 0)
		return(false);

	if(lcd_pin[io_lcd_e] < 0)
		return(false);

	if(lcd_pin[io_lcd_d4] < 0)
		return(false);

	if(lcd_pin[io_lcd_d5] < 0)
		return(false);

	if(lcd_pin[io_lcd_d6] < 0)
		return(false);

	if(lcd_pin[io_lcd_d7] < 0)
		return(false);

	nibble_mode = false;

	if(lcd_pin[io_lcd_d0] < 0)
		nibble_mode = true;

	if(lcd_pin[io_lcd_d1] < 0)
		nibble_mode = true;

	if(lcd_pin[io_lcd_d2] < 0)
		nibble_mode = true;

	if(lcd_pin[io_lcd_d3] < 0)
		nibble_mode = true;

	// robust initialisation sequence,
	// from http://web.alfredstate.edu/weimandn/lcd/lcd_initialization/lcd_initialization_index.html

	for(pin = 3; pin > 0; pin--)
	{
		if(!send_byte_raw(cmd_special_reset, false))	// 3 x special "reset" command, low nibble ignored
			return(false);

		msleep(5);
	}

	if(nibble_mode)
	{
		if(!send_byte_raw(cmd_special_nibble, false))	// set 4 bit mode, low nibble ignored
			return(false);

		msleep(2);

		if(!send_byte(cmd_nibble_mode, false))			// set 4 bit mode / two lines / 5x8 font
			return(false);
	}
	else
		if(!send_byte(cmd_byte_mode, false))			// set 8 bit mode / two lines / 5x8 font
			return(false);

	if(!send_byte(cmd_clear_screen, false))				// clear screen
		return(false);

	msleep(2);

	if(!send_byte(cmd_cursor_config, false))			// cursor move direction = LTR / no display shift
		return(false);

	if(!send_byte(cmd_display_config, false))			// display on, cursor off, blink off
		return(false);

	if(!udg_init())
		return(false);

	display_inited = true;

	return(display_lcd_bright(1));
}

bool display_lcd_begin(int slot, bool logmode)
{
	if(display_disable_text)
		return(true);

	if(!display_inited)
	{
		log("! display lcd not display_inited\n");
		return(false);
	}

	display_logmode = logmode;

	if(!text_goto(0, 0))
		return(false);

	return(true);
}

bool display_lcd_output(unsigned int unicode)
{
	const unicode_map_t *unicode_map_ptr;
	const udg_map_t *udg_map_ptr;

	if(display_disable_text)
		return(true);

	if(unicode == '\n')
		return(text_newline());

	if((display_y < display_text_height) && (display_x < display_text_width))
	{
		for(unicode_map_ptr = unicode_map; unicode_map_ptr->unicode != mapeof; unicode_map_ptr++)
			if(unicode_map_ptr->unicode == unicode)
			{
				unicode = unicode_map_ptr->internal;
				if(!text_send(unicode))
					return(false);
				return(true);
			}

		for(udg_map_ptr = udg_map; udg_map_ptr->unicode != mapeof; udg_map_ptr++)
			if((udg_map_ptr->unicode == unicode))
			{
				unicode = udg_map_ptr->internal;
				if(!text_send(unicode))
					return(false);
				return(true);
			}

		if((unicode < ' ') || (unicode > '}'))
			unicode = ' ';

		if(!text_send(unicode))
			return(false);

		if((display_y == (display_text_height - 1)) && (display_x == display_text_width)) // workaround for bug in some LCD controllers that need last row/column to be sent twice
		{
			if(!text_goto(display_x - 1, -1))
				return(false);

			if(!text_send(unicode))
				return(false);
		}
	}

	return(true);
}

bool display_lcd_end(void)
{
	if(display_disable_text)
		return(false);

	while(display_y < display_text_height)
		if(!text_newline())
			break;

	return(true);
}

bool display_lcd_bright(int brightness)
{
	unsigned int max_value, value;

	if((brightness < 0) || (brightness > 4))
		return(false);

	if(!send_byte((brightness > 0) ? cmd_on_off_off : cmd_off_off_off, false))
		return(false);

	if((bl_io >= 0) && (bl_pin >= 0))
	{
		max_value = io_pin_max_value(bl_io, bl_pin);

		if(brightness == 0)
			value = 0;
		else
			value = max_value >> ((4 - brightness) << 1);

		io_write_pin((string_t *)0, bl_io, bl_pin, value);
	}

	return(true);
}

bool display_lcd_picture_load(unsigned int picture_load_index)
{
	display_picture_load_flash_sector = (picture_load_index ? PICTURE_FLASH_OFFSET_1 : PICTURE_FLASH_OFFSET_0) / SPI_FLASH_SEC_SIZE;

	if(string_size(&flash_sector_buffer) < SPI_FLASH_SEC_SIZE)
	{
		logf("display lcd: load picture: sector buffer too small: %u\n", flash_sector_buffer_use);
		return(false);
	}

	return(true);
}

bool display_lcd_layer_select(unsigned int layer)
{
	static const char pbm_header[] = "P4\n20 16\n";
	bool success = false;
	unsigned int row, column;
	unsigned int udg, udg_line, udg_bit, udg_value;
	unsigned int byte_offset, bit_offset;
	const uint8_t *bitmap;

	if(layer == 0)
	{
		if(!send_byte(cmd_clear_screen, false))
			return(false);

		if(!udg_init())
			return(false);

		display_disable_text = false;
		return(true);
	}

	display_disable_text = true;

	if((flash_sector_buffer_use != fsb_free) && (flash_sector_buffer_use != fsb_config_cache))
	{
		logf("display lcd: load picture: flash buffer not free, used by: %u\n", flash_sector_buffer_use);
		return(false);
	}

	flash_sector_buffer_use = fsb_display_picture;

	if(spi_flash_read(display_picture_load_flash_sector * SPI_FLASH_SEC_SIZE, string_buffer_nonconst(&flash_sector_buffer), SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK)
	{
		logf("display lcd: load picture: failed to read sector: 0x%x\n", display_picture_load_flash_sector);
		goto error;
	}

	string_setlength(&flash_sector_buffer, sizeof(pbm_header) - 1);

	if(!string_match_cstr(&flash_sector_buffer, pbm_header))
	{
		logf("display lcd: show picture: invalid image header: %s\n", string_to_cstr(&flash_sector_buffer));
		goto error;
	}

	string_setlength(&flash_sector_buffer, SPI_FLASH_SEC_SIZE);
	bitmap = (const uint8_t *)string_buffer(&flash_sector_buffer) + (sizeof(pbm_header) - 1);

	if(!send_byte(cmd_set_udg_ptr, false))
		goto error;

	msleep(2);

	for(udg = 0; udg < 8; udg++)
	{
		for(udg_line = 0; udg_line < 8; udg_line++)
		{
			udg_value = 0;

			row = ((udg / 4) * 8) + udg_line;

			for(udg_bit = 0; udg_bit < 5; udg_bit++)
			{
				column = ((udg % 4) * 5) + udg_bit;

				byte_offset = ((24 / 8) * row) + (column / 8);
				bit_offset = column % 8;

				if(bitmap[byte_offset] & (1 << (7 - bit_offset)))
					udg_value |= (1 << (4 - udg_bit));
			}

			msleep(1);

			if(!send_byte(udg_value, true))
				goto error;
		}
	}

	if(!send_byte(cmd_clear_screen, false))
		return(false);

	for(row = 0; row < 2; row++)
	{
		if(!send_byte(cmd_set_ram_ptr | (ram_offsets[row + 1] + 8), false))
			return(false);

		msleep(2);

		for(column = 0; column < 4; column++)
			if(!send_byte((row * 4) + column, true))
				goto error;
	}

	if(!text_goto(0, 0))
		goto error;

	success = true;

error:
	flash_sector_buffer_use = fsb_free;
	if(!text_goto(-1, -1))
		return(false);

	return(success);
}
