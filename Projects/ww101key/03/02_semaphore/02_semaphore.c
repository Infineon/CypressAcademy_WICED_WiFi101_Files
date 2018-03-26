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
	while(1)
	{
        /* LED OFF for the shield (LED ON if using the baseboard by itself) */
        wiced_gpio_output_low( WICED_LED1 );
        wiced_rtos_get_semaphore(&semaphoreHandle, WICED_WAIT_FOREVER);
        /* LED ON for the shield (LED OFF if using the baseboard by itself) */
        wiced_gpio_output_high( WICED_LED1 );
        wiced_rtos_get_semaphore(&semaphoreHandle, WICED_WAIT_FOREVER);
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
