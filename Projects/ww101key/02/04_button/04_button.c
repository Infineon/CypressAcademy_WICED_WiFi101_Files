// Monitor button on the base board and set the LED based on the button's state
#include "wiced.h"

void application_start( )
{
    wiced_bool_t button1_pressed;

    wiced_init();	/* Initialize the WICED device */

    //The button is initialized in platform.c. If it wasn't you would need:
    //wiced_gpio_init( WICED_BUTTON1, INPUT_PULL_UP );

    while ( 1 )
    {
        /* Read the state of Button 1 */
        button1_pressed = wiced_gpio_input_get( WICED_BUTTON1 ) ? WICED_FALSE : WICED_TRUE;  /* The button is active low */

        if ( button1_pressed == WICED_TRUE )
        {   /* LED ON for the shield (LED OFF if using the baseboard by itself) */
            wiced_gpio_output_high( WICED_LED1 );
        }
        else
        {   /* LED OFF for the shield (LED ON if using the baseboard by itself) */
        	wiced_gpio_output_low( WICED_LED1 );
        }
    }
}
