// When the button on the base board is pressed, send a character over the I2C bus to
// the shield board. This is used by the processor on the shield to toggle through
// the four LEDs on the shield.
#include "wiced.h"

/* I2C port to use. If the platform already defines it, use that, otherwise default to WICED_I2C_2 */
#ifndef PLATFORM_ARDUINO_I2C
#define PLATFORM_ARDUINO_I2C ( WICED_I2C_2 )
#endif


#define I2C_ADDRESS  (0x42)

/* I2C register locations */
#define CONTROL_REG (0x05)
#define LED_REG     (0x04)

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

    /* Setup transmit buffer */
    /* We will always write an offset and then a single value, so we need 2 bytes in the buffer */
    uint8_t tx_buffer[] = {0, 0};

    /* Write a value of 0x01 to the control register to enable control of the CapSense LEDs over I2C */
    tx_buffer[0] = CONTROL_REG;
    tx_buffer[1] = 0x01;
	wiced_i2c_write(&i2cDevice, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, tx_buffer, sizeof(tx_buffer));

	tx_buffer[0] = LED_REG; /* Set offset for the LED register */

    while ( 1 )
    {
    	if(buttonPress)
    	{
    		/* Send new I2C data */
    	    wiced_i2c_write(&i2cDevice, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, tx_buffer, sizeof(tx_buffer));
    		tx_buffer[1] = tx_buffer[1] << 1; /* Shift to the next LED */
    		if (tx_buffer[1] > 0x08) /* Reset after turning on LED3 */
    		{
    			tx_buffer[1] = 0x01;
    		}

    		buttonPress = WICED_FALSE; /* Reset flag for next button press */
    	}
    }
}
