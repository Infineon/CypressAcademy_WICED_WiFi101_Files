/* This is the class project for the WICED WiFi WW-101 class */
#include "wiced.h"
#include "u8g_arm.h"
#include "wiced_aws.h"
#include "aws_common.h"
#include "resources.h"
#include "cJSON.h"

/*******************************************************************************************************/
/* Update this number for the number of the thing that you want to publish to. The default is ww101_00 */
/* Thing number must be 2 digits */
#define MY_THING_NUMBER  00
/*******************************************************************************************************/

/* The highest number thing on the Broker that you want to subscribe to */
#define MAX_THING 39

/* I2C port to use. If the platform already defines it, use that, otherwise default to WICED_I2C_2 */
#ifndef PLATFORM_ARDUINO_I2C
#define PLATFORM_ARDUINO_I2C ( WICED_I2C_2 )
#endif

/* I2C device addresses */
#define PSOC_ADDRESS (0x42)
#define DISP_ADDRESS (0x3C)

/* I2C Offset registers for CapSense buttons and start of weather data */
#define BUTTON_OFFSET_REG       (0x06)
#define WEATHER_OFFSET_REG      (0x07)

/* CapSense Buttons */
#define B0_MASK  (0x01)
#define B1_MASK  (0x02)
#define B2_MASK  (0x04)
#define B3_MASK  (0x08)
#define ALL_MASK (0x0F)

/* Queue configuration */
#define MESSAGE_SIZE		(4)
#define QUEUE_SIZE			(50)

/* Thread parameters */
#define THREAD_BASE_PRIORITY 	(10)
#define THREAD_STACK_SIZE		(8192)

/* Time in milliseconds for automatic publish of weather info */
#define TIMER_TIME (30000)

/* Broker info */
#define AWS_BROKER_ADDRESS                  "amk6m51qrxr2u.iot.us-east-1.amazonaws.com"
#define THING_NAME_BASE                     "ww101_"
#define TOPIC_HEAD							"$aws/things/ww101_"
#define TOPIC_SUBSCRIBE                     "$aws/things/+/shadow/update/documents"
#define TOPIC_GETSUBSCRIBE                  "$aws/things/+/shadow/get/accepted"
#define CERTIFICATES_MAX_SIZE               (0x7fffffff)
#define AWS_RETRY_COUNT                     (5)
#define TIMEOUT                             (2000)

/* This generates the full thing name as a string */
#define STR1(NUM) #NUM
#define STR2(NUM) STR1(NUM)
#define THING_NAME THING_NAME_BASE STR2(MY_THING_NUMBER)

/* Publish commands */
typedef enum command {
	WEATHER_CMD,
	TEMPERATURE_CMD,
	HUMIDITY_CMD,
	LIGHT_CMD,
	ALERT_CMD,
	IP_CMD,
	GET_CMD,
} CMD;

/*************** Global Variables *****************/
/* Structure to hold data from an IoT device */
typedef struct {
    uint8_t thingNumber;
    char ip_str[16];
    wiced_bool_t alert;
    float temp;
    float humidity;
    float light;
} iot_data_t;

/* Array to hold data from all IoT things */
iot_data_t iot_data[40];

/* I2C for the Analog Coprocessor - used for weather data and for CapSense button values */
const wiced_i2c_device_t i2cPsoc = {
    .port          = PLATFORM_ARDUINO_I2C,
    .address = PSOC_ADDRESS,
    .address_width = I2C_ADDRESS_WIDTH_7BIT,
    .speed_mode = I2C_STANDARD_SPEED_MODE
};

volatile wiced_bool_t printAll = WICED_FALSE;       /* Flag to print updates from all things to UART when true */

volatile uint8_t dispThing = MY_THING_NUMBER; /* Which thing to display data for on the OLED */

/* IP Address of the local host in IP format */
wiced_ip_address_t ipAddress;

/* AWS server variables */
static wiced_aws_thing_security_info_t aws_security_creds =
{
    .private_key         = NULL,
    .key_length          = 0,
    .certificate         = NULL,
    .certificate_length  = 0,
};

static wiced_aws_endpoint_info_t aws_iot_endpoint = {
    .transport           = WICED_AWS_TRANSPORT_MQTT_NATIVE,
    .uri                 = AWS_BROKER_ADDRESS,
    .peer_common_name    = NULL,
    .ip_addr             = {0},
    .port                = WICED_AWS_IOT_DEFAULT_MQTT_PORT,
    .root_ca_certificate = NULL,
    .root_ca_length      = 0,
};

static wiced_aws_thing_info_t aws_config = {
    .name            = THING_NAME,
    .credentials     = &aws_security_creds,
};

static volatile wiced_aws_handle_t aws_handle;
static volatile wiced_bool_t       is_connected = WICED_FALSE;


/* RTOS global constructs */
static wiced_semaphore_t displaySemaphore;
static wiced_semaphore_t connectSemaphore;
static wiced_mutex_t i2cMutex;
static wiced_mutex_t pubSubMutex;
static wiced_queue_t pubQueue;
static wiced_timer_t publishTimer;
static wiced_thread_t getWeatherDataThreadHandle;
static wiced_thread_t getCapSenseThreadHandle;
static wiced_thread_t displayThreadHandle;
static wiced_thread_t commandThreadHandle;
static wiced_thread_t publishThreadHandle;
static wiced_thread_t awsConnectThreadHandle;

/*************** Function Prototypes ***************/
/* ISRs */
void publish_button_isr(void* arg);
void alert_button_isr(void* arg);
void print_thing_info(uint8_t thingNumber);
/* Threads and timer functions */
void getWeatherDataThread(wiced_thread_arg_t arg);
void getCapSenseThread(wiced_thread_arg_t arg);
void displayThread(wiced_thread_arg_t arg);
void commandThread(wiced_thread_arg_t arg);
void publishThread(wiced_thread_arg_t arg);
void  awsConnectThread(wiced_thread_arg_t arg);
void publish30sec(void* arg);
/* Functions from the demo/aws/iot/pub_sub/publisher project */
static wiced_result_t get_aws_credentials_from_resources( void );
static void aws_callback( wiced_aws_handle_t aws, wiced_aws_event_type_t event, wiced_aws_callback_data_t* data );

/*************** Functions **********************/
/*************** Main application ***************/
void application_start( )
{
    int                 sub_retries = 0;
    uint8_t             loop;
    wiced_result_t      ret = WICED_SUCCESS;

	wiced_init();	/* Initialize the WICED device */

    /* Setup Thread Control functions */
     wiced_rtos_init_mutex(&i2cMutex);
     wiced_rtos_init_mutex(&pubSubMutex);
     wiced_rtos_init_semaphore(&displaySemaphore);
     wiced_rtos_init_semaphore(&connectSemaphore);
     wiced_rtos_init_queue(&pubQueue, NULL, MESSAGE_SIZE, QUEUE_SIZE);

	/* Initialize the I2C to read from the PSoC
	 * 2 different threads will use this - reading weather data, and reading CapSense */
	wiced_i2c_init(&i2cPsoc);

	/* Initialize all of the thing data structures */
	for(loop = 0; loop <= MAX_THING; loop++)
	{
	    iot_data[loop].thingNumber = loop;
	    snprintf(iot_data[loop].ip_str, sizeof(iot_data[loop].ip_str), "0.0.0.0");
        iot_data[loop].alert = WICED_FALSE;
	    iot_data[loop].temp = 0.0;
        iot_data[loop].humidity = 0.0;
        iot_data[loop].light = 0.0;
	}

    /* Start threads that interact with the shield (PSoC and OLED) */
    wiced_rtos_create_thread(&getCapSenseThreadHandle, THREAD_BASE_PRIORITY+2, NULL, getCapSenseThread, THREAD_STACK_SIZE, NULL);
	wiced_rtos_create_thread(&getWeatherDataThreadHandle, THREAD_BASE_PRIORITY+2, NULL, getWeatherDataThread, THREAD_STACK_SIZE, NULL);
    wiced_rtos_create_thread(&displayThreadHandle, THREAD_BASE_PRIORITY+4, NULL, displayThread, THREAD_STACK_SIZE, NULL);

    /* Get AWS root certificate, client certificate and private key respectively */
    ret = get_aws_credentials_from_resources();
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ("[Application/AWS] Error fetching credentials from resources\n" ) );
        return;
    }

    /* Disable roaming to other access points */
    wiced_wifi_set_roam_trigger( -99 ); /* -99dBm ie. extremely low signal level */

    /* Bring up the network interface */
    ret = wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "\nNot able to join the requested AP\n\n" ) );
        return;
    }

    /* Get IP Address for MyThing and save in the Thing data structure */
    wiced_ip_get_ipv4_address(WICED_STA_INTERFACE, &ipAddress);
    snprintf(iot_data[MY_THING_NUMBER].ip_str, sizeof(iot_data[MY_THING_NUMBER].ip_str), "%d.%d.%d.%d",
                   (int)((ipAddress.ip.v4 >> 24) & 0xFF), (int)((ipAddress.ip.v4 >> 16) & 0xFF),
                   (int)((ipAddress.ip.v4 >> 8) & 0xFF),  (int)(ipAddress.ip.v4 & 0xFF));

    /* Update the display so that the IP address is shown */
    wiced_rtos_set_semaphore(&displaySemaphore);

    /* Initialize the AWS connection and register the callback */
    ret = wiced_aws_init( &aws_config , aws_callback );
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "[Application/AWS] Failed to Initialize AWS library\n\n" ) );
        return;
    }

    aws_handle = (wiced_aws_handle_t)wiced_aws_create_endpoint(&aws_iot_endpoint);
    if( !aws_handle )
    {
        WPRINT_APP_INFO( ( "[Application/AWS] Failed to create AWS connection handle\n\n" ) );
        return;
    }

    /* Connect to AWS */
    wiced_rtos_create_thread(&awsConnectThreadHandle, THREAD_BASE_PRIORITY+5, NULL, awsConnectThread, THREAD_STACK_SIZE, NULL);
    wiced_rtos_set_semaphore(&connectSemaphore);
    //GJL
//    WPRINT_APP_INFO(("[Application/AWS] Opening connection...\n"));
//    ret = wiced_aws_connect(aws_handle);
//    if ( ret != WICED_SUCCESS )
//    {
//        WPRINT_APP_INFO(("[Application/AWS] Connect Failed\r\n"));
//        return;
//    }

    while(is_connected == WICED_FALSE)
    {
        /* Wait until connection is up. This is set in the aws callback */
        wiced_rtos_delay_milliseconds(TIMEOUT);
        WPRINT_APP_INFO(("[Application/AWS] Waiting For Connection\r\n"));
    }

    /* Subscribe to the update/documents topic for all things using the + wildcard */
    wiced_rtos_lock_mutex(&pubSubMutex);
    WPRINT_APP_INFO(("[Application/AWS] Subscribing to %s...",TOPIC_SUBSCRIBE));
    do
    {
        ret = wiced_aws_subscribe( aws_handle, TOPIC_SUBSCRIBE, WICED_AWS_QOS_ATMOST_ONCE);
        sub_retries++ ;
    } while ( ( ret != WICED_SUCCESS ) && ( sub_retries < AWS_RETRY_COUNT ) );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("Subscribe Failed: Error Code %d\n",ret));
    }
    else
    {
        WPRINT_APP_INFO(("Subscribe Success\n"));
    }
    wiced_rtos_unlock_mutex(&pubSubMutex);

    /* Subscribe to the get/accepted topic for all things using the + wildcard */
    wiced_rtos_lock_mutex(&pubSubMutex);
    WPRINT_APP_INFO(("[Application/AWS] Subscribing to %s...",TOPIC_GETSUBSCRIBE));
    do
    {
        ret = wiced_aws_subscribe( aws_handle, TOPIC_GETSUBSCRIBE, WICED_AWS_QOS_ATMOST_ONCE);
        sub_retries++ ;
    } while ( ( ret != WICED_SUCCESS ) && ( sub_retries < AWS_RETRY_COUNT ) );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("Subscribe Failed: Error Code %d\n",ret));
    }
    else
    {
        WPRINT_APP_INFO(("Subscribe Success\n"));
    }
    wiced_rtos_unlock_mutex(&pubSubMutex);

    /* Start the publish thread */
    /* This has to be done after the subscriptions are done so that we can get an initial state from all things */
    wiced_rtos_create_thread(&publishThreadHandle, THREAD_BASE_PRIORITY+1, NULL, publishThread, THREAD_STACK_SIZE, NULL);

    /* Wait and then start command thread last so that help info is displayed at the bottom of the terminal window */
    wiced_rtos_delay_milliseconds(5500);
    wiced_rtos_create_thread(&commandThreadHandle, THREAD_BASE_PRIORITY+3, NULL, commandThread, THREAD_STACK_SIZE, NULL);

    /* Start timer to publish weather data every 30 seconds */
    wiced_rtos_init_timer(&publishTimer, TIMER_TIME, publish30sec, NULL);
    wiced_rtos_start_timer(&publishTimer);

    /* Setup interrupts for the 2 mechanical buttons */
    wiced_gpio_input_irq_enable(WICED_BUTTON2, IRQ_TRIGGER_FALLING_EDGE, publish_button_isr, NULL);
    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, alert_button_isr, NULL);


    /* No while(1) here since everything is done by the new threads. */
    return;
}


/*************** Weather Publish Button ISR ***************/
/* When button is pressed, we publish the weather data */
void publish_button_isr(void* arg)
{
     char pubCmd[4]; /* Command pushed onto the queue to determine what to publish */
     pubCmd[0] = WEATHER_CMD;
     /* Note - only WICED_NO_WAIT is supported in an ISR so if the queue is full we won't publish */
     wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_NO_WAIT); /* Push value onto queue*/
}


/*************** Weather Alert Button ISR ***************/
/* When button is pressed, we toggle the weather alert and publish it */
void alert_button_isr(void* arg)
{
    char pubCmd[4]; /* Command pushed onto the queue to determine what to publish */

    if(iot_data[MY_THING_NUMBER].alert == WICED_TRUE)
     {
        iot_data[MY_THING_NUMBER].alert = WICED_FALSE;
     }
     else
     {
         iot_data[MY_THING_NUMBER].alert = WICED_TRUE;
     }

     /* Set a semaphore for the OLED to update the display */
     wiced_rtos_set_semaphore(&displaySemaphore);
     /* Publish the alert */
     pubCmd[0] = ALERT_CMD;
     /* Note - only WICED_NO_WAIT is supported in an ISR so if the queue is full we won't publish */
     wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_NO_WAIT); /* Push value onto queue*/
}


/*************** Print Thing Info ***************/
/* Print information for the given thing */
void print_thing_info(uint8_t thingNumber)
{
    WPRINT_APP_INFO(("Thing: ww101_%02d   IP: %15s   Alert: %d   Temperature: %4.1f   Humidity: %4.1f   Light: %5.0f \n",
            thingNumber,
            iot_data[thingNumber].ip_str,
            iot_data[thingNumber].alert,
            iot_data[thingNumber].temp,
            iot_data[thingNumber].humidity,
            iot_data[thingNumber].light));
}


/*************** Weather Data Acquisition Thread ***************/
/* Thread to read temperature, humidity, and light from the PSoC analog Co-processor */
void getWeatherDataThread(wiced_thread_arg_t arg)
{
    /* Weather data from the PSoC Analog Co-processor  */
    struct {
        float temp;
        float humidity;
        float light;
    } __attribute__((packed)) weather_data;

    /* Variables to remember previous values */
	static float tempPrev = 0;
	static float humPrev = 0;
	static float lightPrev = 0;

    /* Buffer to set the offset */
    uint8_t offset[] = {WEATHER_OFFSET_REG};

	while(1)
	{
	    /* Get I2C data - use a Mutex to prevent conflicts */
	    wiced_rtos_lock_mutex(&i2cMutex);
	    wiced_i2c_write(&i2cPsoc, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, offset, sizeof(offset)); /* Set the offset */
        wiced_i2c_read(&i2cPsoc, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, &weather_data, sizeof(weather_data)); /* Get data */
		wiced_rtos_unlock_mutex(&i2cMutex);

		/* Copy weather data into my thing's data structure */
		iot_data[MY_THING_NUMBER].temp =     weather_data.temp;
		iot_data[MY_THING_NUMBER].humidity = weather_data.humidity;
		iot_data[MY_THING_NUMBER].light =    weather_data.light;

		/* Look at weather data - only update display if a value has changed*/
		if((tempPrev != iot_data[MY_THING_NUMBER].temp) || (humPrev != iot_data[MY_THING_NUMBER].humidity) || (lightPrev != iot_data[MY_THING_NUMBER].light))
		{
			/* Save the new values as previous for next time around */
		    tempPrev  = iot_data[MY_THING_NUMBER].temp;
		    humPrev   = iot_data[MY_THING_NUMBER].humidity;
		    lightPrev = iot_data[MY_THING_NUMBER].light;

		    /* Set a semaphore for the OLED to update the display */
			wiced_rtos_set_semaphore(&displaySemaphore);
		}

		/* Wait 500 milliseconds */
		wiced_rtos_delay_milliseconds( 500 );
	}
}


/*************** CapSense Button Monitor Thread ***************/
/* Thread to read CapSense button values */
void getCapSenseThread(wiced_thread_arg_t arg)
{
    uint8_t CapSenseValues = 0;
    wiced_bool_t buttonPressed = WICED_FALSE;

    /* Buffer to set the offset */
    uint8_t offset = BUTTON_OFFSET_REG;

    while(1)
    {
        /* Wait 100 milliseconds */
         wiced_rtos_delay_milliseconds( 100 );

        /* Get I2C data - use a Mutex to prevent conflicts */
        wiced_rtos_lock_mutex(&i2cMutex);
        wiced_i2c_write(&i2cPsoc, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, &offset, sizeof(offset)); /* Set the offset */
        wiced_i2c_read(&i2cPsoc, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, &CapSenseValues, sizeof(CapSenseValues)); /* Get data */
        wiced_rtos_unlock_mutex(&i2cMutex);

        /* Look for CapSense button presses */
        if(buttonPressed == WICED_FALSE) /* Only look for new button presses */
        {
            if((CapSenseValues & B0_MASK) == B0_MASK) /* Button 0 goes to the local thing's screen */
            {
                buttonPressed = WICED_TRUE;
                dispThing = MY_THING_NUMBER;
                wiced_rtos_set_semaphore(&displaySemaphore);
            }
            if((CapSenseValues & B1_MASK) == B1_MASK) /* Button 1 goes to next thing's screen */
            {
                buttonPressed = WICED_TRUE;
                if(dispThing == 0) /* Handle wrap-around case */
                {
                    dispThing = MAX_THING;
                }
                else
                {
                    dispThing--;
                }
                wiced_rtos_set_semaphore(&displaySemaphore);
            }
            if((CapSenseValues & B2_MASK) == B2_MASK) /* Button 2 goes to previous thing's screen */
            {
                buttonPressed = WICED_TRUE;
                dispThing++;
                if(dispThing > MAX_THING) /* Handle wrap-around case */
                {
                    dispThing = 0;
                }
                wiced_rtos_set_semaphore(&displaySemaphore);
            }
            if((CapSenseValues & B3_MASK) == B3_MASK) /* Button 3 increments by 10 things */
            {
                buttonPressed = WICED_TRUE;
                dispThing += 10;
                if(dispThing > MAX_THING) /* Handle wrap-around case */
                {
                    dispThing -= (MAX_THING + 1);
                }
                wiced_rtos_set_semaphore(&displaySemaphore);
            }
        }
        if(((CapSenseValues | ~ALL_MASK) & ALL_MASK) == 0) /* All buttons released */
        {
            buttonPressed = WICED_FALSE;
        }
    }
}


/*************** OLED Display Thread ***************/
/* Thread to display data on the OLED */
void displayThread(wiced_thread_arg_t arg)
{
    /* Strings to hold the results to print */
    char thing_str[25];
    char temp_str[25];
    char humidity_str[25];
    char light_str[25];

	/* Initialize the OLED display */
	wiced_i2c_device_t display_i2c =
    {
        .port          = PLATFORM_ARDUINO_I2C,
        .address       = DISP_ADDRESS,
        .address_width = I2C_ADDRESS_WIDTH_7BIT,
        .flags         = 0,
        .speed_mode    = I2C_STANDARD_SPEED_MODE,
    };

    u8g_t display;

    u8g_init_wiced_i2c_device(&display_i2c);

    u8g_InitComFn(&display, &u8g_dev_ssd1306_128x64_i2c, u8g_com_hw_i2c_fn);
    u8g_SetFont(&display, u8g_font_unifont);
    u8g_SetFontPosTop(&display);

	while(1)
	{
		/* Wait until new data is ready to display */
		wiced_rtos_get_semaphore(&displaySemaphore, WICED_WAIT_FOREVER);

		/* Setup Display Strings */
		if(iot_data[dispThing].alert)
		{
			snprintf(thing_str, sizeof(thing_str),    "ww101_%02d *ALERT*", dispThing);
		} else {
			snprintf(thing_str, sizeof(thing_str),    "ww101_%02d", dispThing);
		}
		snprintf(temp_str,      sizeof(temp_str),     "Temp:     %.1f", iot_data[dispThing].temp);
		snprintf(humidity_str,  sizeof(humidity_str), "Humidity: %.1f", iot_data[dispThing].humidity);
        snprintf(light_str,     sizeof(light_str),    "Light:    %.0f", iot_data[dispThing].light);

		/* Send data to the display */
		u8g_FirstPage(&display);
		wiced_rtos_lock_mutex(&i2cMutex);
		do {
			u8g_DrawStr(&display, 0, 2,  thing_str);
			u8g_DrawStr(&display, 0, 14, iot_data[dispThing].ip_str);
			u8g_DrawStr(&display, 0, 26, temp_str);
			u8g_DrawStr(&display, 0, 38, humidity_str);
			u8g_DrawStr(&display, 0, 50, light_str);
		} while (u8g_NextPage(&display));
		wiced_rtos_unlock_mutex(&i2cMutex);
	}
}


/*************** UART Command Interface Thread ***************/
/* Tread to handle UART command input/output */
void commandThread(wiced_thread_arg_t arg)
{
	#define ESC (0x1B)
	char    receiveChar;
	char	sendChar[10];
	uint32_t expected_data_size = 1;
	uint8_t loop;

	char pubCmd[4]; /* Command pushed onto the queue to determine what to publish */

	WPRINT_APP_INFO(("\n******************************************\n"));
	WPRINT_APP_INFO(("Enter '?' for a list of available commands\n"));
	WPRINT_APP_INFO(("******************************************\n"));

	while(1)
	{
	    if ( wiced_uart_receive_bytes( STDIO_UART, &receiveChar, &expected_data_size, WICED_NEVER_TIMEOUT ) == WICED_SUCCESS )
		{
			 /* If we get here then a character has been received */
			switch(receiveChar)
			{
			case '?':
				WPRINT_APP_INFO(("Commands:\n"));
				WPRINT_APP_INFO(("\tt - Print temperature and publish\n"));
				WPRINT_APP_INFO(("\th - Print humidity and publish\n"));
                WPRINT_APP_INFO(("\tl - Print light value and publish\n"));
				WPRINT_APP_INFO(("\tA - Publish weather alert ON\n"));
				WPRINT_APP_INFO(("\ta - Publish weather alert OFF\n"));
				WPRINT_APP_INFO(("\tP - Turn printing of messages from all things ON\n"));
				WPRINT_APP_INFO(("\tp - Turn printing of messages from all things OFF\n"));
				WPRINT_APP_INFO(("\tx - Print the current known state of the data from all things\n"));
				WPRINT_APP_INFO(("\tc - Clear the terminal and set the cursor to the upper left corner\n"));
				WPRINT_APP_INFO(("\t? - Print the list of commands\n"));
				break;
			case 't': /* Print temperature to terminal and publish */
				WPRINT_APP_INFO(("Temperature: %.1f\n", iot_data[MY_THING_NUMBER].temp)); /* Print temperature to terminal */
			    /* Publish temperature to the cloud */
				pubCmd[0] = TEMPERATURE_CMD;
				wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
			case 'h': /* Print humidity to terminal and publish */
				WPRINT_APP_INFO(("Humidity: %.1f\t\n", iot_data[MY_THING_NUMBER].humidity)); /* Print humidity to terminal */
			    /* Publish humidity to the cloud */
				pubCmd[0] = HUMIDITY_CMD;
				wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
            case 'l': /* Print light value to terminal and publish */
                WPRINT_APP_INFO(("Light: %.1f\t\n", iot_data[MY_THING_NUMBER].light)); /* Print humidity to terminal */
                /* Publish light value to the cloud */
                pubCmd[0] = LIGHT_CMD;
                wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
                break;
			case 'A': /* Publish Weather Alert ON */
				WPRINT_APP_INFO(("Weather Alert ON\n"));
				iot_data[MY_THING_NUMBER].alert = WICED_TRUE;
			    wiced_rtos_set_semaphore(&displaySemaphore); /* Update display */
	            pubCmd[0] = ALERT_CMD;
				wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
			case 'a': /* Publish Weather Alert OFF */
				WPRINT_APP_INFO(("Weather Alert OFF\n"));
				iot_data[MY_THING_NUMBER].alert = WICED_FALSE;
                wiced_rtos_set_semaphore(&displaySemaphore); /* Update display */
				pubCmd[0] = ALERT_CMD;
				wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
			case 'P': /* Turn on printing of updates to all things */
			    WPRINT_APP_INFO(("Thing Updates ON\n"));
			    printAll = WICED_TRUE;
				break;
            case 'p': /* Turn off printing of updates to all things */
                WPRINT_APP_INFO(("Thing Updates OFF\n"));
                printAll = WICED_FALSE;
                break;
            case 'x': /* Print current state of all things */
                for(loop = 0; loop <= MAX_THING; loop++)
                {
                    print_thing_info(loop);
                }
                break;
			case 'c':	/* Send VT100 clear screen code ESC[2J and move cursor to upper left corner with ESC[H */
				sendChar[0] = ESC;
				sendChar[1] = '[';
				sendChar[2] = '2';
				sendChar[3] = 'J';
				sendChar[4] = ESC;
				sendChar[5] = '[';
				sendChar[6] = 'H';
	    		wiced_uart_transmit_bytes(STDIO_UART, &sendChar , 7);
				break;
			}
		}

		/* Wait 100ms between looking for characters */
		wiced_rtos_delay_milliseconds( 100 );
	}
}


/*************** Publish Thread ***************/
/* Thread to publish data to the cloud */
void publishThread(wiced_thread_arg_t arg)
{
    int             pub_retries = 0;
    uint8_t         thingNumber;
    wiced_result_t  ret;

	char json[100] = "TEST";	  /* json message to send */

	uint8_t pubCmd[4]; /* Command pushed ONTO the queue to determine what to publish */

    uint8_t command[4]; /* Value popped FROM the queue to determine what to publish */

    /* Set the default topic to publish updates for my thing. */
    char topic[50];

	/* Publish the IP address to the server one time */
	pubCmd[0] = IP_CMD;
	wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/

	/* Push a shadow/get command to the publish queue for all things to get initial state */
    for(thingNumber=0; thingNumber <= MAX_THING; thingNumber++)
    {
        pubCmd[0] = GET_CMD;
        pubCmd[1] = thingNumber; /* 2nd byte of the message will be the thing number */
        wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
    }

    while ( 1 )
    {
        /* Wait until a publish is requested */
        wiced_rtos_pop_from_queue(&pubQueue, &command, WICED_WAIT_FOREVER);

        /* Set the topic for an update of my thing */
        snprintf(topic, sizeof(topic), "%s%02d/shadow/update", TOPIC_HEAD,MY_THING_NUMBER);

        /* Setup the JSON message based on the command */
        switch(command[0])
        {
            case WEATHER_CMD: 	/* publish temperature and humidity */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%.0f}}}", iot_data[MY_THING_NUMBER].temp, iot_data[MY_THING_NUMBER].humidity, iot_data[MY_THING_NUMBER].light);
                break;
            case TEMPERATURE_CMD: 	/* publish temperature */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"temperature\":%.1f} } }", iot_data[MY_THING_NUMBER].temp);
                break;
            case HUMIDITY_CMD: 	/* publish humidity */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"humidity\":%.1f} } }", iot_data[MY_THING_NUMBER].humidity);
                break;
            case LIGHT_CMD:  /* publish light value */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"light\":%.1f} } }", iot_data[MY_THING_NUMBER].light);
                break;
            case ALERT_CMD: /* weather alert */
                if(iot_data[MY_THING_NUMBER].alert)
                {
                    snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"weatherAlert\":true} } }");
                }
                else
                {
                    snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"weatherAlert\":false} } }");
                }
                break;
            case IP_CMD:	/* IP address */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"IPAddress\":\"%s\"} } }", iot_data[MY_THING_NUMBER].ip_str);
                break;
            case GET_CMD:   /* Get starting state of other things */
                snprintf(json, sizeof(json), "{}");
                /* Override the topic to do a get of the specified thing's shadow */
                snprintf(topic, sizeof(topic), "%s%02d/shadow/get", TOPIC_HEAD,command[1]);
                break;
        }

        if(is_connected)
        {
            wiced_rtos_lock_mutex(&pubSubMutex);
            WPRINT_APP_INFO(("[Application/AWS] Publishing..."));
            pub_retries = 0; // reset retries to 0 before going into the loop so that the next publish after a failure will still work
            do
            {
                ret = wiced_aws_publish( aws_handle, topic, (uint8_t*) json, strlen( json ), WICED_AWS_QOS_ATMOST_ONCE );
                pub_retries++ ;
            } while ( ( ret != WICED_SUCCESS ) && ( pub_retries < AWS_RETRY_COUNT ) );
            if ( ret != WICED_SUCCESS )
            {
                WPRINT_APP_INFO(("Publish Failed: Error Code %d\n",ret));
            }
            else
            {
                WPRINT_APP_INFO(("Publish Success\n"));
            }
            wiced_rtos_unlock_mutex(&pubSubMutex);
        }
        wiced_rtos_delay_milliseconds( 100 );
    }
}


/*************** Thread to connect to AWS whenever the semaphore is set ***************/
void awsConnectThread(wiced_thread_arg_t arg)
{
    wiced_result_t ret;

    while(1)
    {
        wiced_rtos_get_semaphore(&connectSemaphore, WICED_NEVER_TIMEOUT);
        WPRINT_APP_INFO(("[Application/AWS] Opening connection...\n"));
        ret = wiced_aws_connect(aws_handle);
        if ( ret != WICED_SUCCESS )
        {
            WPRINT_APP_INFO(("[Application/AWS] Connect Failed\r\n"));
            return;
        }
    }
}

/*************** Timer to publish weather data every 30sec ***************/
void publish30sec(void* arg)
{
	char pubCmd[4]; /* Command pushed onto the queue to determine what to publish */
	pubCmd[0] = WEATHER_CMD;
	/* Must use WICED_NO_WAIT here because waiting is not allowed in a timer - if the queue is full we wont publish */
	wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_NO_WAIT); /* Push value onto queue*/
}


/*************** Function to load certificates ***************/
static wiced_result_t get_aws_credentials_from_resources( void )
{
    uint32_t size_out = 0;
    wiced_result_t result = WICED_ERROR;

    wiced_aws_thing_security_info_t* security = &aws_security_creds;
    uint8_t** root_ca_certificate = &aws_iot_endpoint.root_ca_certificate;

    if( security->certificate && security->private_key && (*root_ca_certificate) )
    {
        WPRINT_APP_INFO(("\n[Application/AWS] Security Credentials already set(not NULL). Abort Reading from Resources...\n"));
        return WICED_SUCCESS;
    }

    /* Get AWS Root CA certificate filename: 'rootca.cer' */
    result = resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_rootca_cer, 0, CERTIFICATES_MAX_SIZE, &size_out, (const void **) root_ca_certificate);
    if( result != WICED_SUCCESS )
    {
        goto _fail_aws_certificate;
    }
    if( size_out < 64 )
    {
        WPRINT_APP_INFO( ( "\n[Application/AWS] Invalid Root CA Certificate!\n\n" ) );
        resource_free_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_rootca_cer, (const void *)*root_ca_certificate );
        goto _fail_aws_certificate;
    }

    aws_iot_endpoint.root_ca_length = size_out;

    /* Get Publisher's Certificate filename: 'client.cer' */
    result = resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_client_cer, 0, CERTIFICATES_MAX_SIZE, &size_out, (const void **) &security->certificate );
    if( result != WICED_SUCCESS )
    {
        goto _fail_client_certificate;
    }
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\n[Application/AWS] Invalid Device Certificate!\n\n" ) );
        resource_free_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_client_cer, (const void *)security->certificate );
        goto _fail_client_certificate;
    }

    security->certificate_length = size_out;

    /* Get Publisher's Private Key filename: 'privkey.cer' located @ resources/apps/aws/iot/publisher folder */
    result = resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_privkey_cer, 0, CERTIFICATES_MAX_SIZE, &size_out, (const void **) &security->private_key );
    if( result != WICED_SUCCESS )
    {
        goto _fail_private_key;
    }
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\n[Application/AWS] Invalid Device Private-Key!\n\n" ) );
        resource_free_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_privkey_cer, (const void *)security->private_key );
        goto _fail_private_key;
    }
    security->key_length = size_out;

    return WICED_SUCCESS;

_fail_private_key:
    resource_free_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_client_cer, (const void *)security->certificate );
_fail_client_certificate:
    resource_free_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_rootca_cer, (const void *)*root_ca_certificate );
_fail_aws_certificate:
    return WICED_ERROR;
}


/*************** Callback function to handle AWS events ***************/
static void aws_callback( wiced_aws_handle_t aws, wiced_aws_event_type_t event, wiced_aws_callback_data_t* data )
{
    uint thingNumber;           /* The number of the thing that published a message */
    char topicStr[50] = {0};    /* String to copy the topic into */
    char pubType[20] =  {0};    /* String to compare to the publish type */

    if( !aws || !data || (aws != aws_handle) )
    {
        WPRINT_APP_INFO( ("[Application/AWS] Invalid AWS callback params\n") );
        return;
    }

    switch ( event )
    {
        case WICED_AWS_EVENT_CONNECTED:
        {
            if( data->connection.status == WICED_SUCCESS )
            {
                is_connected = WICED_TRUE;
            }
            break;
        }

        case WICED_AWS_EVENT_DISCONNECTED:
        {
            if( data->disconnection.status == WICED_SUCCESS )
            {
                is_connected = WICED_FALSE;
                /* Disconnect on our end and then set semaphore to reconnect to the server */
                wiced_aws_disconnect(aws_handle);
                wiced_rtos_set_semaphore(&connectSemaphore);
            }
            break;
        }

        case WICED_AWS_EVENT_PUBLISHED:
        case WICED_AWS_EVENT_SUBSCRIBED:
        case WICED_AWS_EVENT_UNSUBSCRIBED:
            break;

        case WICED_AWS_EVENT_PAYLOAD_RECEIVED:
        {
            /* Copy the message to a null terminated string */
            memcpy(topicStr, data->message.topic, data->message.topic_length);
            topicStr[data->message.topic_length+1] = 0; /* Add termination */

            /* Scan the topic to see if it is one of the things we are interested in */
            sscanf(topicStr, "$aws/things/ww101_%2u/shadow/%19s", &thingNumber, pubType);
            /* Check to see if it is an initial get of the values of other things */
            if(strcmp(pubType,"get/accepted") == 0)
            {
                if(thingNumber != MY_THING_NUMBER) /* Only do the rest if it isn't the local thing */
                {
                    /* Parse JSON message for the weather station data */
                    cJSON *root = cJSON_Parse((char*) data->message.data);
                    cJSON *state = cJSON_GetObjectItem(root,"state");
                    cJSON *reported = cJSON_GetObjectItem(state,"reported");
                    cJSON *ipValue = cJSON_GetObjectItem(reported,"IPAddress");
                    if(ipValue->type == cJSON_String) /* Make sure we have a string */
                    {
                        strcpy(iot_data[thingNumber].ip_str, ipValue->valuestring);
                    }
                    iot_data[thingNumber].temp = (float) cJSON_GetObjectItem(reported,"temperature")->valuedouble;
                    iot_data[thingNumber].humidity = (float) cJSON_GetObjectItem(reported,"humidity")->valuedouble;
                    iot_data[thingNumber].light = (float) cJSON_GetObjectItem(reported,"light")->valuedouble;
                    iot_data[thingNumber].alert = (wiced_bool_t) cJSON_GetObjectItem(reported,"weatherAlert")->valueint;
                    cJSON_Delete(root);
                }
            }
            /* Check to see if it is an update published by another thing */
            if(strcmp(pubType,"update/documents") == 0)
            {
                if(thingNumber != MY_THING_NUMBER) /* Only do the rest if it isn't the local thing */
                {
                    /* Parse JSON message for the weather station data */
                    cJSON *root = cJSON_Parse((char*) data->message.data);
                    cJSON *current = cJSON_GetObjectItem(root,"current");
                    cJSON *state = cJSON_GetObjectItem(current,"state");
                    cJSON *reported = cJSON_GetObjectItem(state,"reported");
                    cJSON *ipValue = cJSON_GetObjectItem(reported,"IPAddress");
                    if(ipValue->type == cJSON_String) /* Make sure we have a string */
                    {
                        strcpy(iot_data[thingNumber].ip_str, ipValue->valuestring);
                    }
                    iot_data[thingNumber].temp = (float) cJSON_GetObjectItem(reported,"temperature")->valuedouble;
                    iot_data[thingNumber].humidity = (float) cJSON_GetObjectItem(reported,"humidity")->valuedouble;
                    iot_data[thingNumber].light = (float) cJSON_GetObjectItem(reported,"light")->valuedouble;
                    iot_data[thingNumber].alert = (wiced_bool_t) cJSON_GetObjectItem(reported,"weatherAlert")->valueint;

                    cJSON_Delete(root);

                    /* If printing of updates is enabled, print the new info */
                    if(printAll)
                    {
                        print_thing_info(thingNumber);
                    }

                    /* Update the display if we are displaying this thing's data */
                    if(thingNumber == dispThing)
                    {
                        wiced_rtos_set_semaphore(&displaySemaphore);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}
