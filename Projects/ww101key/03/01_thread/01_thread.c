// Use a thread to blink an LED with a 500ms period
#include "wiced.h"

/* Thread parameters */
#define THREAD_PRIORITY 	(10)
#define THREAD_STACK_SIZE	(1024)

static wiced_thread_t ledThreadHandle;

/* This function will print out the max amount of stack a thread has used */
/* This is ThreadX specific code so it will only work for ThreadX RTOS */
uint32_t maxStackUsage(TX_THREAD *thread)
{
    uint8_t *end =   thread->tx_thread_stack_end;
    uint8_t *start = thread->tx_thread_stack_start;
    while(start < end)
    {
        if(*start != 0xEF)
        {
            return end-start;
        }
        start++;
    }
    return 0;
}

/* Define the thread function that will blink the LED on/off every 500ms */
void ledThread(wiced_thread_arg_t arg)
{
	while(1)
	{
        /* LED OFF for the shield (LED ON if using the baseboard by itself) */
        wiced_gpio_output_low( WICED_LED1 );
        wiced_rtos_delay_milliseconds( 250 );
        /* LED ON for the shield (LED OFF if using the baseboard by itself) */
        wiced_gpio_output_high( WICED_LED1 );
        wiced_rtos_delay_milliseconds( 250 );
	}
}

void application_start( )
{
	wiced_init();	/* Initialize the WICED device */

	/* Initialize and start a new thread */
    wiced_rtos_create_thread(&ledThreadHandle, THREAD_PRIORITY, "ledThread", ledThread, THREAD_STACK_SIZE, NULL);

    /* Check to see the thread's max stack usage and print it every 5 seconds */
    while(1)
    {
        WPRINT_APP_INFO(("Max Stack: %d\n",(int) maxStackUsage((TX_THREAD*) &ledThreadHandle)));
        wiced_rtos_delay_milliseconds(5000);
    }
}
