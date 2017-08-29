// Send characters over the UART interface
#include "wiced.h"

volatile wiced_bool_t newPress = WICED_FALSE;

/* Interrupt service routine for the button */
void button_isr(void* arg)
{
	static wiced_bool_t led1 = WICED_FALSE;

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

	newPress = WICED_TRUE; /* Need to send new button count value */
}

/* Main application */
void application_start( )
{
    uint8_t pressCount = 0;
    char    printChar;

	wiced_init();	/* Initialize the WICED device */

    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button_isr, NULL); /* Setup interrupt */

    /* Configure and start the UART. */
    /* Note that WICED_DISABLE_STDIO must be defined in the make file for this to work */
    wiced_uart_config_t uart_config =
    {
        .baud_rate    = 9600,
        .data_width   = DATA_WIDTH_8BIT,
        .parity       = NO_PARITY,
        .stop_bits    = STOP_BITS_1,
        .flow_control = FLOW_CONTROL_DISABLED,
    };
    wiced_uart_init( STDIO_UART, &uart_config, NULL); /* Setup UART */

    while ( 1 )
    {
    	if(newPress)
    	{
    		pressCount ++;		/* Increment counter */
    		if(pressCount > 9)
    		{
    			pressCount = 0;
    		}
    		printChar = pressCount + '0';
    		wiced_uart_transmit_bytes(STDIO_UART, &printChar , 1);
    		newPress = WICED_FALSE;	/* Reset for next press */
    	}
    }
}
