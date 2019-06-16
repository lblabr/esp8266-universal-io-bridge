#include "util.h"
#include "sys_string.h"
#include "uart.h"
#include "ota.h"
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

string_t logbuffer =
{
	.size = 0x3fffeb2c - 0x3fffe000 - 16,
	.length = 0,
	.buffer = (char *)0x3fffe000,
};

int attr_used __errno;

void espconn_init(void);
void espconn_init(void)
{
}

void reset(void)
{
	system_restart();
}

attr_const const char *yesno(bool value)
{
	if(!value)
		return("no");

	return("yes");
}

attr_const const char *onoff(bool value)
{
	if(!value)
		return("off");

	return("on");
}

unsigned int log_from_flash_no_format(const char *data)
{
	unsigned int length;

	length = flash_to_dram(true, data, string_buffer_nonconst(&flash_dram), string_size(&flash_dram));
	string_setlength(&flash_dram, length);

	if(config_flags_match(flag_log_to_uart))
		uart_send_string(0, &flash_dram);

	if(config_flags_match(flag_log_to_buffer))
	{
		if((unsigned int)(string_length(&logbuffer) + length) >= (unsigned int)string_size(&logbuffer))
			string_clear(&logbuffer);

		string_append_string(&logbuffer, &flash_dram);
	}

	return(length);
}

unsigned int log_from_flash(const char *fmt_in_flash, ...)
{
	va_list ap;
	int length;
	char fmt_in_dram[128];

	flash_to_dram(true, fmt_in_flash, fmt_in_dram, sizeof(fmt_in_dram));

	va_start(ap, fmt_in_flash);
	length = vsnprintf(string_buffer_nonconst(&flash_dram), string_size(&flash_dram), fmt_in_dram, ap);
	va_end(ap);

	if(length < 0)
		return(0);

	string_setlength(&flash_dram, length);

	if(config_flags_match(flag_log_to_uart))
		uart_send_string(0, &flash_dram);

	if(config_flags_match(flag_log_to_buffer))
	{
		if((string_length(&logbuffer) + length) >= string_size(&logbuffer))
			string_clear(&logbuffer);

		string_append_string(&logbuffer, &flash_dram);
	}

	return(length);
}

iram void logchar(char c)
{
	if(config_flags_match(flag_log_to_uart))
	{
		uart_send(0, c);
		uart_flush(0);
	}

	if(config_flags_match(flag_log_to_buffer))
	{
		if((logbuffer.length + 1) >= logbuffer.size)
			string_clear(&logbuffer);

		string_append_char(&logbuffer, c);
	}
}

void msleep(int msec)
{
	while(msec-- > 0)
	{
		system_soft_wdt_feed();
		os_delay_us(1000);
	}
}

attr_pure ip_addr_t ip_addr(const char *src)
{
	ip_addr_to_bytes_t ip_addr_to_bytes;

	sscanf(src, "%3hhu.%3hhu.%3hhu.%3hhu",
			&ip_addr_to_bytes.byte[0],
			&ip_addr_to_bytes.byte[1],
			&ip_addr_to_bytes.byte[2],
			&ip_addr_to_bytes.byte[3]);

	return(ip_addr_to_bytes.ip_addr);
}

// missing from libc

void *_malloc_r(struct _reent *r, size_t sz)
{
	extern void* pvPortMalloc(size_t sz, const char *, unsigned, bool);
	return(pvPortMalloc(sz, "", 0, false));
}

void *_calloc_r(struct _reent *r, size_t a, size_t b)
{
	extern void* pvPortCalloc(size_t count,size_t size,const char *,unsigned);
	return(pvPortCalloc(a, b, "", 0));
}

void _free_r(struct _reent *r, void *x)
{
	extern void vPortFree (void *p, const char *, unsigned);
	return(vPortFree(x, "", 0));
}

void *_realloc_r(struct _reent *r, void *x, size_t sz)
{
	extern void* pvPortRealloc (void *p, size_t n, const char *, unsigned);
	return(pvPortRealloc(x, sz, "", 0));
}
