#include "init.h"

#include "util.h"
#include "sys_string.h"
#include "io.h"
#include "stats.h"
#include "sys_time.h"
#include "dispatch.h"
#include "sequencer.h"
#include "io_gpio.h"

#include <stdint.h>
#include <stdbool.h>

static void user_init2(void);
void user_init(void);

static const partition_item_t partition_items[] =
{
	{	SYSTEM_PARTITION_RF_CAL, 				RFCAL_OFFSET,				RFCAL_SIZE,				},
	{	SYSTEM_PARTITION_PHY_DATA,				PHYDATA_OFFSET,				PHYDATA_SIZE,			},
	{	SYSTEM_PARTITION_SYSTEM_PARAMETER,		SYSTEM_CONFIG_OFFSET,		SYSTEM_CONFIG_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 0,	USER_CONFIG_OFFSET,			USER_CONFIG_SIZE,		},
#if IMAGE_OTA == 0
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 1,	OFFSET_IRAM_PLAIN,			SIZE_IRAM_PLAIN,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 2,	OFFSET_IROM_PLAIN,			SIZE_IROM_PLAIN,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 3,	SEQUENCER_FLASH_OFFSET,		SEQUENCER_FLASH_SIZE,	},
#else
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 1,	OFFSET_OTA_BOOT,			SIZE_OTA_BOOT,			},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 2,	OFFSET_OTA_RBOOT_CFG,		SIZE_OTA_RBOOT_CFG,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 3,	OFFSET_OTA_IMG_0,			SIZE_OTA_IMG,			},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 4,	OFFSET_OTA_IMG_1,			SIZE_OTA_IMG,			},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 5,	SEQUENCER_FLASH_OFFSET_0,	SEQUENCER_FLASH_SIZE,	},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 6,	SEQUENCER_FLASH_OFFSET_1,	SEQUENCER_FLASH_SIZE,	},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 7,	PICTURE_FLASH_OFFSET_0,		PICTURE_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 8,	PICTURE_FLASH_OFFSET_1,		PICTURE_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 9,	FONT_FLASH_OFFSET_0,		FONT_FLASH_SIZE,		},
	{	SYSTEM_PARTITION_CUSTOMER_BEGIN + 10,	FONT_FLASH_OFFSET_1,		FONT_FLASH_SIZE,		},
#endif
};

void user_spi_flash_dio_to_qio_pre_init(void);
iram void user_spi_flash_dio_to_qio_pre_init(void)
{
}

void user_pre_init(void);
iram void user_pre_init(void)
{
	stat_flags.user_pre_init_called = 1;
	stat_flags.user_pre_init_success = system_partition_table_regist(partition_items, sizeof(partition_items) / sizeof(*partition_items), FLASH_SIZE_SDK);
}

uint32_t user_iram_memory_is_enabled(void);
iram attr_const uint32_t user_iram_memory_is_enabled(void)
{
	return(0);
}

void user_init(void)
{
	// don't declare stack variables here, they will get overwritten

	register uint32_t *paint;
	volatile uint32_t sp;

	stat_stack_sp_initial = &sp;

	for(paint = (typeof(paint))stack_top; (paint < (typeof(paint))stack_bottom) && (paint < &sp); paint++)
	{
		*paint = stack_paint_magic;
		stat_stack_painted += 4;
	}

	system_set_os_print(0);
	dispatch_init1();
	config_init();
	uart_init();
	uart_set_initial(0);
	uart_set_initial(1);
	os_install_putc1(&logchar);
	system_set_os_print(1);

	if(config_flags_match(flag_wlan_power_save))
		wifi_set_sleep_type(MODEM_SLEEP_T);
	else
		wifi_set_sleep_type(NONE_SLEEP_T);

	system_init_done_cb(user_init2);
}

static void user_init2(void)
{
	stat_heap_min = stat_heap_max = xPortGetFreeHeapSize();

	dispatch_init2();

	if(config_flags_match(flag_cpu_high_speed))
		system_update_cpu_freq(160);
	else
		system_update_cpu_freq(80);

	if(!wlan_init_from_config())
		wlan_init_start_recovery();
	application_init();
	time_init();
	io_init();

	log("* boot done\n");

	if(config_flags_match(flag_auto_sequencer))
		sequencer_start(0, 1);
}

static void wlan_init(config_wlan_mode_t wlan_mode, const string_t *ssid, const string_t *password, unsigned int channel)
{
	struct station_config cconf;
	struct softap_config saconf;

	switch(wlan_mode)
	{
		case(config_wlan_mode_client):
		{
			if((wifi_get_opmode() != STATION_MODE) ||
					!wifi_station_get_config(&cconf) ||
					!wifi_station_get_auto_connect() ||
					!string_match_cstr(ssid, cconf.ssid) ||
					!string_match_cstr(password, cconf.password))
			{
				memset(&cconf, 0, sizeof(cconf));
				strecpy(cconf.ssid, string_buffer(ssid), sizeof(cconf.ssid));
				strecpy(cconf.password, string_buffer(password), sizeof(cconf.password));
				cconf.bssid_set = 0;

				wifi_station_disconnect();
				wifi_set_opmode(STATION_MODE);
				wifi_station_set_config(&cconf);
				wifi_station_connect();
				wifi_station_set_auto_connect(1);
			}

			break;
		}

		default:
		{
			memset(&saconf, 0, sizeof(saconf));

			strecpy(saconf.ssid, string_buffer(ssid), sizeof(saconf.ssid));
			strecpy(saconf.password, string_buffer(password), sizeof(saconf.password));

			saconf.ssid_len = strlen(saconf.ssid);
			saconf.channel = channel;
			saconf.authmode = AUTH_WPA_WPA2_PSK;
			saconf.ssid_hidden = 0;
			saconf.max_connection = 1;
			saconf.beacon_interval = 100;

			wifi_station_disconnect();
			wifi_set_opmode_current(SOFTAP_MODE);
			wifi_softap_set_config_current(&saconf);

			break;
		}
	}
}

bool wlan_init_from_config(void)
{
	unsigned int mode_int;
	config_wlan_mode_t mode;
	string_new(, ssid, 64);
	string_new(, password, 64);
	unsigned int channel;

	if(config_get_uint("wlan.mode", &mode_int, -1, -1))
		mode = (config_wlan_mode_t)mode_int;
	else
		mode = config_wlan_mode_client;

	channel = -1;

	switch(mode)
	{
		case(config_wlan_mode_client):
		{
			if(!config_get_string("wlan.client.ssid", &ssid, -1, -1) ||
					!config_get_string("wlan.client.passwd", &password, -1, -1))
				return(false);

			break;
		}

		case(config_wlan_mode_ap):
		{
			if(!config_get_string("wlan.ap.ssid", &ssid, -1, -1) ||
					!config_get_string("wlan.ap.passwd", &password, -1, -1) ||
					!config_get_uint("wlan.ap.channel", &channel, -1, -1))
				return(false);

			break;
		}

		default:
		{
			return(false);
		}
	}

	stat_flags.wlan_recovery_mode_active = 0;
	wlan_init(mode, &ssid, &password, channel);
	return(true);
}

void wlan_init_start_recovery(void)
{
	config_flag_change_nosave(flag_log_to_uart, true);
	config_flag_change_nosave(flag_log_to_buffer, true);
	config_flag_change_nosave(flag_cmd_from_uart, true);

	uart_invert(0, uart_dir_tx, false);
	uart_invert(0, uart_dir_rx, false);
	uart_loopback(0, false);

	string_init(static, wlan_default_ssid, "esp");
	string_init(static, wlan_default_password, "espespesp");

	stat_flags.wlan_recovery_mode_active = 1;

	wlan_init(config_wlan_mode_ap, &wlan_default_ssid, &wlan_default_password, 1);

	log("* WLAN CAN'T CONNECT, entering recovery mode. *\n"
				"  now, to configure wlan parameters\n"
				"  - EITHER associate to SSID \"esp\" using passwd \"espespesp\"\n"
				"      and then connect to 192.168.4.1:22 using telnet or browser\n"
				"  - OR connect to UART\n"
				"  - THEN issue these commands:\n"
				"      wcc <ssid> <passwd>\n"
				"      wm client\n"
				"  after that, issue a reset command to restore temporarily changed flags.\n");
}
