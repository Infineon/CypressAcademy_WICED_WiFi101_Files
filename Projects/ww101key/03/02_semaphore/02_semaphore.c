// Use a semaphore to communicate between a button interrupt and an LED blink thread.
// The LED state will toggle when the user button is pressed.
#include "wiced.h"

/* Thread parameters */
#define THREAD_PRIORITY 	(10)
#define THREAD_STACK_SIZE	(1024)

static wiced_thread_t ledThreadHandle;
static wiced_semaphore_t semaphoreHandle;

/* Interrupt service routine for the button */
void button_isr(void* arg)
{
	wiced_rtos_set_semaphore(&semaphoreHandle); /* Set the semaphore */
}

/* Define the thread function that will toggle the LED */
void ledThread(wiced_thread_arg_t arg)
{
	static wiced_bool_t led1 = WICED_FALSE;

	while(1)
	{
		/* Check for the semaphore here. If it is not set, then this thread will suspend until the semaphore is set by the button thread */
		wiced_rtos_get_semaphore(&semaphoreHandle, WICED_WAIT_FOREVER);

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
}

void application_start( )
{
	wiced_init();	/* Initialize the WICED device */

	/* Setup the semaphore which will be set by the button interrupt */
    wiced_rtos_init_semaphore(&semaphoreHandle);

	/* Initialize and start LED thread */
    wiced_rtos_create_thread(&ledThreadHandle, THREAD_PRIORITY, "ledThread", ledThread, THREAD_STACK_SIZE, NULL);

	/* Setup button interrupt */
	wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button_isr, NULL);

    /* No while(1) here since everything is done by the new thread. */

}
