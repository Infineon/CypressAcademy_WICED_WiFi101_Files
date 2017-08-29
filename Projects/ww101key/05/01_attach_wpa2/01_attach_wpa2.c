// Attach to a WPA2 network named WW101WPA (this is set in the wifi_config_dct.h file).
//
// If the connection is successful, LED1 will be on. If not the LED1 will blink.
#include "wiced.h"

void application_start( )
{
    /* Variables */   
	wiced_result_t connectResult;
    wiced_bool_t led = WICED_FALSE;

    wiced_init();	/* Initialize the WICED device */

    connectResult = wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);

    if(connectResult == WICED_SUCCESS) /* Turn LED on if connection was successful */
    {
        wiced_gpio_output_high( WICED_LED1 );
    }

    while ( 1 )
    {
        /* Blink LED if connection was not successful */
        if (connectResult != WICED_SUCCESS)
        {
            if ( led == WICED_TRUE )
            {
                wiced_gpio_output_low( WICED_LED1 );
                led = WICED_FALSE;
            }
            else
            {
                wiced_gpio_output_high( WICED_LED1 );
                led = WICED_TRUE;
            }
        }
        wiced_rtos_delay_milliseconds(250);
    }
}
