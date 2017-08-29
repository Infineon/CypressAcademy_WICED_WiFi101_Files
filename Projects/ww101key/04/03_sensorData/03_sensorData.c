/* Display sensor data from the analog co-processor on the OLED */
#include "u8g_arm.h"

#define TEMPERATURE_REG 0x07

void application_start()
{

	/* Initialize the OLED display */
	wiced_i2c_device_t display_i2c =
    {
        .port          = WICED_I2C_2,
        .address       = 0x3C,
        .address_width = I2C_ADDRESS_WIDTH_7BIT,
        .flags         = 0,
        .speed_mode    = I2C_STANDARD_SPEED_MODE,

    };

    u8g_t display;

    u8g_init_wiced_i2c_device(&display_i2c);

    u8g_InitComFn(&display, &u8g_dev_ssd1306_128x64_i2c, u8g_com_hw_i2c_fn);
    u8g_SetFont(&display, u8g_font_unifont);
    u8g_SetFontPosTop(&display);

    /* Initialize PSoC analog co-processor I2C interface and set the offset */
    const wiced_i2c_device_t psoc_i2c = {
    	.port 			= WICED_I2C_2,
		.address 		= 0x42,
		.address_width 	= I2C_ADDRESS_WIDTH_7BIT,
		.speed_mode 	= I2C_STANDARD_SPEED_MODE
    };

    wiced_i2c_init(&psoc_i2c);

    /* Tx buffer is used to set the offset */
    uint8_t tx_buffer[] = {TEMPERATURE_REG};

    /* Rx buffer is used to get temperature, humidity, light, and POT data - 4 bytes each */
    struct {
    	float temp;
		float humidity;
		float light;
		float pot;
    } rx_buffer;

    /* Initialize offset */
    wiced_i2c_write(&psoc_i2c, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, tx_buffer, sizeof(tx_buffer));


    /* Strings to hold the results */
    char temp_str[25];
    char humidity_str[25];
    char light_str[25];
    char pot_str[25];

	while(1)
	{
		/* Get data from the PSoC */
        wiced_i2c_read(&psoc_i2c, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, &rx_buffer, sizeof(rx_buffer));

		/* Setup Display Strings */
		snprintf(temp_str,     sizeof(temp_str),     "Temp:     %.1f", rx_buffer.temp);
		snprintf(humidity_str, sizeof(humidity_str), "Humidity: %.1f", rx_buffer.humidity);
		snprintf(light_str,    sizeof(light_str),    "Light:    %.1f", rx_buffer.light);
		snprintf(pot_str,      sizeof(pot_str),      "Pot:      %.1f", rx_buffer.pot);

		/* Send data to the display */
		u8g_FirstPage(&display);
		do {
			u8g_DrawStr(&display, 0, 5,  temp_str);
			u8g_DrawStr(&display, 0, 20, humidity_str);
			u8g_DrawStr(&display, 0, 35, light_str);
			u8g_DrawStr(&display, 0, 50, pot_str);
		} while (u8g_NextPage(&display));

		wiced_rtos_delay_milliseconds(500);
	}
}
