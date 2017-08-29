// Use a pin interrupt to toggle the LED on every button press
#include "wiced.h"

/* Interrupt service routine for the button */
void button_isr(void* arg)
{
	static wiced_bool_t led1 = WICED_FALSE;

	/* Toggle LED1 */
	if ( led1 == WICED_TRUE )
	{
		wiced_gpio_output_low( WICED_LED1 );
		led1 = WICED_FALSE;
	}
	else
	{
		wiced_gpio_output_high( WICED_LED1 );
		led1 = WICED_TRUE;
	}
}

/* Main application */
void application_start( )
{
    wiced_init();	/* Initialize the WICED device */

    //The button is initialized in platform.c. If it wasn't you would need:
    //wiced_gpio_init( WICED_BUTTON1, INPUT_PULL_UP );

    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button_isr, NULL); /* Setup interrupt */

    /* No main loop code required - the interrupt handles everything */
}
