// Use a thread to blink an LED with a 500ms period
#include "wiced.h"

/* Thread parameters */
#define THREAD_PRIORITY 	(10)
#define THREAD_STACK_SIZE	(1024)

static wiced_thread_t ledThreadHandle;

/* Define the thread function that will blink the LED on/off every 500ms */
void ledThread(wiced_thread_arg_t arg)
{
	wiced_bool_t led1 = WICED_FALSE;

	while(1)
	{
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
		wiced_rtos_delay_milliseconds( 250 );
	}
}

void application_start( )
{
	wiced_init();	/* Initialize the WICED device */

	/* Initialize and start a new thread */
    wiced_rtos_create_thread(&ledThreadHandle, THREAD_PRIORITY, "ledThread", ledThread, THREAD_STACK_SIZE, NULL);

    /* No while(1) here since everything is done by the new thread. */
}
