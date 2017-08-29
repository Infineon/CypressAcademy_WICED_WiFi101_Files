// Use a PWM to adjust the brightness of  LED1
#include "wiced.h"

// Use one of the following defines depending on if you are using the shield board
#define PWM_PIN WICED_PWM_5 // For shield board
//#define PWM_PIN WICED_PWM_4 // For base board red LED. Also need to change PWM_4 mux in platform.c from PIN_GPIO_15 to PIN_PWM_3

void application_start( )
{
    float duty_cycle = 50.0;

    wiced_init();	/* Initialize the WICED device */

    wiced_gpio_deinit(WICED_LED1); // For base board red LED
    wiced_gpio_deinit(WICED_LED2); // For shield

    while ( 1 )
    {
        wiced_pwm_init(PWM_PIN, 1000, duty_cycle);
        wiced_pwm_start(PWM_PIN);
        duty_cycle += 1.0;

        if(duty_cycle > 100.0)
        {
        	duty_cycle = 0.0;
        }
        wiced_rtos_delay_milliseconds( 10 );
    }
}
