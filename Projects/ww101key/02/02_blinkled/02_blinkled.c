/* Blink LED1 on the base board with a frequency of 2 Hz */
#include "wiced.h"

void application_start( )
{
    wiced_init();	/* Initialize the WICED device */

    //The LED is initialized in platform.c. If it was not, you would need the following:
    //wiced_gpio_init(WICED_LED1, OUTPUT_PUSH_PULL); */

    while ( 1 )
    {
		/* LED OFF for the shield (LED ON if using the baseboard by itself) */
    	wiced_gpio_output_low( WICED_LED1 );
    	wiced_rtos_delay_milliseconds( 250 );
		/* LED ON for the shield (LED OFF if using the baseboard by itself) */
    	wiced_gpio_output_high( WICED_LED1 );
        wiced_rtos_delay_milliseconds( 250 );
    }
}
