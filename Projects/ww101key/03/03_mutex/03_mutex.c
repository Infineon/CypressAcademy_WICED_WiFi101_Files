// Use a MUTEX to lock access to an LED.
// Button 1 blinks fast and Button 2 blinks slow.
//
// Without a Mutex, if you hold both buttons the blink rate will vary
//
// With a Mutex the blink rate will retain the value of the first button
// that was pressed.

#include "wiced.h"

// Comment out the following line to see what happens without the mutex
#define USE_MUTEX

/* Thread parameters */
#define THREAD_PRIORITY     (10)
#define THREAD_STACK_SIZE   (1024)

static wiced_thread_t ledThread1Handle;
static wiced_thread_t ledThread2Handle;
#ifdef USE_MUTEX
static wiced_mutex_t MutexHandle;
#endif

/* Define the thread function that will blink LED1 if B1 is pressed */
void ledThread1(wiced_thread_arg_t arg)
{
    while(1)
    {
        #ifdef USE_MUTEX
        wiced_rtos_lock_mutex(&MutexHandle);
        #endif
        while(!wiced_gpio_input_get( WICED_BUTTON1 )) // Loop while button is pressed
        {
            wiced_gpio_output_high( WICED_LED1 );
            wiced_rtos_delay_milliseconds(100);
            wiced_gpio_output_low(WICED_LED1);
            wiced_rtos_delay_milliseconds(100);
        }
        #ifdef USE_MUTEX
        wiced_rtos_unlock_mutex(&MutexHandle);
        #endif
        wiced_rtos_delay_milliseconds(1); // Yield control when button is not pressed
    }
}

/* Define the thread function that will blink LED1 if B2 is pressed */
void ledThread2(wiced_thread_arg_t arg)
{
    while(1)
    {
        #ifdef USE_MUTEX
        wiced_rtos_lock_mutex(&MutexHandle);
        #endif
        while(!wiced_gpio_input_get( WICED_BUTTON2 )) // Loop while button is pressed
        {
            wiced_gpio_output_low( WICED_LED1 );
            wiced_rtos_delay_milliseconds(150);
            wiced_gpio_output_high( WICED_LED1 );
            wiced_rtos_delay_milliseconds(150);
        }
        #ifdef USE_MUTEX
        wiced_rtos_unlock_mutex(&MutexHandle);
        #endif
        wiced_rtos_delay_milliseconds(1); // Yield control when button is not pressed
    }
}

void application_start( )
{
    wiced_init();   /* Initialize the WICED device */

    /* Initialize the Mutex */
    #ifdef USE_MUTEX
    wiced_rtos_init_mutex(&MutexHandle);
    #endif

    /* Initialize and start threads */
    wiced_rtos_create_thread(&ledThread1Handle, THREAD_PRIORITY, "ledThread1", ledThread1, THREAD_STACK_SIZE, NULL);
    wiced_rtos_create_thread(&ledThread2Handle, THREAD_PRIORITY, "ledThread2", ledThread2, THREAD_STACK_SIZE, NULL);

    /* No while(1) here since everything is done by the new threads. */
}
