// When the button on the base board is pressed, I2C is used to read
// the temperature, humidity, light, and PWM values from the analog
// co-processor on the shield board. The values are printed to the UART
#include "wiced.h"

/* I2C port to use. If the platform already defines it, use that, otherwise default to WICED_I2C_2 */
#ifndef PLATFORM_ARDUINO_I2C
#define PLATFORM_ARDUINO_I2C ( WICED_I2C_2 )
#endif

#define I2C_ADDRESS (0x42)

#define TEMPERATURE_REG 0x07

volatile wiced_bool_t buttonPress = WICED_FALSE;

/* Interrupt service routine for the button */
void button_isr(void* arg)
{
	buttonPress = WICED_TRUE;
}

/* Main application */
void application_start( )
{
	wiced_init();	/* Initialize the WICED device */

    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button_isr, NULL); /* Setup interrupt */

    /* Setup I2C master */
    const wiced_i2c_device_t i2cDevice = {
    	.port = PLATFORM_ARDUINO_I2C,
		.address = I2C_ADDRESS,
		.address_width = I2C_ADDRESS_WIDTH_7BIT,
		.speed_mode = I2C_STANDARD_SPEED_MODE
    };

    wiced_i2c_init(&i2cDevice);

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
    wiced_i2c_write(&i2cDevice, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, tx_buffer, sizeof(tx_buffer));

    while ( 1 )
    {
    	if(buttonPress)
    	{
    	    wiced_i2c_read(&i2cDevice, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, &rx_buffer, sizeof(rx_buffer));
    		WPRINT_APP_INFO(("Temperature: %.1f\t Humidity: %.1f\t Light: %.1f\t POT: %.1f\n", rx_buffer.temp, rx_buffer.humidity, rx_buffer.light, rx_buffer.pot)); /* Print data to terminal */

    		buttonPress = WICED_FALSE; /* Reset flag for next button press */
    	}
    }
}
