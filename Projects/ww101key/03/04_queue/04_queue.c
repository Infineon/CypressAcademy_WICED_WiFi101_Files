// Use a queue to send a message to an LED blink thread indicating how many times to blink.
//
// Each time the button is pressed, the blink count increases by one. Even if you press the
// button while the previous set of blinks is occurring, it will still work since the queue is
// 10 values deep. If you press the button more than 10 times while the LED is still blinking from
// a previous press,  the LED will not blink for any press after the 10th one but the blink
// count will still increment.
#include "wiced.h"

/* Thread parameters */
#define THREAD_PRIORITY 	(10)
#define THREAD_STACK_SIZE	(1024)
/* The queue messages will be 4 bytes each (a 32 bit integer) */
#define MESSAGE_SIZE		(4)
#define QUEUE_SIZE			(10)

static wiced_queue_t queueHandle;
static wiced_thread_t ledThreadHandle;

wiced_bool_t buttonFlag = WICED_FALSE;

/* Interrupt service routine for the button */
void button_isr(void* arg)
{
	static uint32_t blinks = 0;

	blinks ++;
	/* We will use WICED_NO_WAIT here so that the ISR won't lock execution if the queue is full */
	/* In addition, WICED_WAIT_FOREVER is not allowed inside an ISR */
	wiced_rtos_push_to_queue(&queueHandle, &blinks, WICED_NO_WAIT); /* Push value onto queue*/
}

/* Define the thread function that will toggle the LED */
void ledThread(wiced_thread_arg_t arg)
{
	static uint32_t message;

	while(1)
	{
		uint32_t i; /* Loop Counter */

		/* If not empty, pull the value off the queue. If empty, this will
		 * suspend the thread until a value is available. */
		wiced_rtos_pop_from_queue(&queueHandle, &message, WICED_WAIT_FOREVER);

		/* Blink LED1 the specified number of times */
		for(i=0; i < message; i++)
		{
			wiced_gpio_output_high( WICED_LED1 );
			wiced_rtos_delay_milliseconds(250);
			wiced_gpio_output_low( WICED_LED1 );
			wiced_rtos_delay_milliseconds(250);
		}
		// Wait 1 second between sets of blinks
		wiced_rtos_delay_milliseconds(1000);
	}
}

void application_start( )
{
	wiced_init();	/* Initialize the WICED device */

	/* Setup button interrupt */
	wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button_isr, NULL);

    /* Initialize the queue */
	wiced_rtos_init_queue(&queueHandle, "blinkQueue", MESSAGE_SIZE, QUEUE_SIZE); /* Setup the semaphore which will be set by the button interrupt */

	/* Initialize and start LED thread */
    wiced_rtos_create_thread(&ledThreadHandle, THREAD_PRIORITY, "ledThread", ledThread, THREAD_STACK_SIZE, NULL);

    /* No while(1) here since everything is done by the new thread. */
}
