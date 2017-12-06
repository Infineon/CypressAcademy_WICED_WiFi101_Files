/* This is the class project for the WICED WiFi WW-101 class */
#include "wiced.h"
#include "u8g_arm.h"
#include "wiced_tls.h"
#include "http_client.h"
#include "resources.h"
#include "cJSON.h"

/*******************************************************************************************************/
/* Update this number for the number of the thing that you want to post to. The default is ww101_00 */
#define MY_THING  0
/*******************************************************************************************************/

/* The highest number thing that you want to get information on */
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
#define QUEUE_SIZE			(100)

/* Thread parameters */
#define THREAD_BASE_PRIORITY 	(10)
#define THREAD_STACK_SIZE		(8192)

/* Time in milliseconds for automatic post of weather info */
#define TIMER_TIME (30000)

/* HTTP Server and message info */
#define SERVER_HOST        "amk6m51qrxr2u.iot.us-east-1.amazonaws.com"

#define SERVER_PORT        ( 8443 )
#define DNS_TIMEOUT_MS     ( 10000 )
#define CONNECT_TIMEOUT_MS ( 3000 )

/* Maximum certificate size and offset */
#define MAX_CERT_SIZE		(10000)
#define CERT_OFFSET         (0)

/* HTTP command codes used to determine what to post or get */
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
/* HTTP variables */
static http_client_t  client;
static http_request_t request;
static http_client_configuration_info_t client_configuration;
static wiced_tls_identity_t tls_identity;
/* We need three headers - host, content type, and content length */
http_header_field_t header[3];

volatile wiced_result_t        ret = WICED_SUCCESS;

/* Structure to hold data from an IoT device */
typedef struct {
    uint8_t thingNumber;
    char ip_str[16];
    wiced_bool_t alert;
    float temp;
    float humidity;
    float light;
} iot_data_t;

iot_data_t iot_data[40];    /* Array to hold data from all IoT things */
iot_data_t iot_holding;     /* Temporary holding location */

/* I2C for the Analog Coprocessor - used for weather data and for CapSense button values */
const wiced_i2c_device_t i2cPsoc = {
    .port = PLATFORM_ARDUINO_I2C,
    .address = PSOC_ADDRESS,
    .address_width = I2C_ADDRESS_WIDTH_7BIT,
    .speed_mode = I2C_STANDARD_SPEED_MODE
};

volatile wiced_bool_t getAll = WICED_FALSE;         /* Flag to print updates from all things to UART when true */
volatile wiced_bool_t connected = WICED_FALSE;      /* Flag to keep track of connection to the server */
volatile uint8_t dispThing = MY_THING; /* Which thing to display data for on the OLED */

static wiced_ip_address_t ipAddress;        /* IP Address of the local host in IP format */
static wiced_ip_address_t server_address;   /* IP address of the host */

/* RTOS global constructs */
static wiced_semaphore_t displaySemaphore;
static wiced_semaphore_t httpWaitSemaphore;
static wiced_mutex_t i2cMutex;
static wiced_queue_t httpQueue;
static wiced_timer_t postTimer;
static wiced_thread_t getWeatherDataThreadHandle;
static wiced_thread_t getCapSenseThreadHandle;
static wiced_thread_t displayThreadHandle;
static wiced_thread_t commandThreadHandle;
static wiced_thread_t httpThreadHandle;

/*************** Function Prototypes ***************/
/* ISRs */
static void post_button_isr(void* arg);
static void alert_button_isr(void* arg);
/* Threads and timer functions */
static void getWeatherDataThread(wiced_thread_arg_t arg);
static void getCapSenseThread(wiced_thread_arg_t arg);
static void displayThread(wiced_thread_arg_t arg);
static void commandThread(wiced_thread_arg_t arg);
static void httpThread(wiced_thread_arg_t arg);
static void timer30sec(void* arg);
static void http_event_handler( http_client_t* client, http_event_t event, http_response_t* response );

/*************** Functions **********************/
/*************** Main application ***************/
void application_start( )
{
    uint32_t        size_root = 0;
    uint32_t        size_cert = 0;
    uint32_t        size_key  = 0;
    uint8_t         loop;

    /* Header 0 is the Host header */
    header[0].field        = HTTP_HEADER_HOST;
    header[0].field_length = strlen( HTTP_HEADER_HOST );
    header[0].value        = SERVER_HOST;
    header[0].value_length = strlen( SERVER_HOST );

    /* Header 1 is the content type (JSON) */
    header[1].field        =  HTTP_HEADER_CONTENT_TYPE;
    header[1].field_length = strlen( HTTP_HEADER_CONTENT_TYPE );
    header[1].value        = "application/json";
    header[1].value_length = strlen( "application/json" );

    /* Header 3 is the application content length. This will be set when we need it later since it changes. */

	wiced_init();	/* Initialize the WICED device */

    /* Setup Thread Control functions */
     wiced_rtos_init_mutex(&i2cMutex);
     wiced_rtos_init_semaphore(&displaySemaphore);
     wiced_rtos_init_semaphore(&httpWaitSemaphore);
     wiced_rtos_init_queue(&httpQueue, NULL, MESSAGE_SIZE, QUEUE_SIZE);

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

    /* Initialize the root CA Certificate so that our thing can validate the AWS server is authentic */
    char * root_ca_cert;
    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_rootca_cer, CERT_OFFSET, MAX_CERT_SIZE, &size_root, (const void **) &root_ca_cert );
    ret = wiced_tls_init_root_ca_certificates( root_ca_cert, size_root );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "Error: Root CA certificate failed to initialize: %u\n", ret) );
        return;
    }

    /* Initialize the local certificate and private key so the AWS server can validate our thing */
    char * client_cert;
    char * client_privkey;
    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_client_cer, CERT_OFFSET, MAX_CERT_SIZE, &size_cert, (const void **) &client_cert );
    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_privkey_cer, CERT_OFFSET, MAX_CERT_SIZE, &size_key, (const void **) &client_privkey );
    ret = wiced_tls_init_identity(&tls_identity, client_privkey, size_key, (const uint8_t*) client_cert, size_cert );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "Error: Local certificate/key identity failed to initialize: %u\n", ret) );
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

    WPRINT_APP_INFO( ( "Resolving IP address of HTTP server...\n" ) );
    ret = wiced_hostname_lookup( SERVER_HOST, &server_address, 10000, WICED_STA_INTERFACE );
    WPRINT_APP_INFO(("Resolved Broker IP: %u.%u.%u.%u\n\n", (uint8_t)(GET_IPV4_ADDRESS(server_address) >> 24),
                    (uint8_t)(GET_IPV4_ADDRESS(server_address) >> 16),
                    (uint8_t)(GET_IPV4_ADDRESS(server_address) >> 8),
                    (uint8_t)(GET_IPV4_ADDRESS(server_address) >> 0)));
    if ( ret == WICED_ERROR || server_address.ip.v4 == 0 )
    {
        WPRINT_APP_INFO(("Error in resolving DNS\n"));
        return;
    }

    /* Get IP Address for MyThing and save in the Thing data structure */
    wiced_ip_get_ipv4_address(WICED_STA_INTERFACE, &ipAddress);
    snprintf(iot_data[MY_THING].ip_str, sizeof(iot_data[MY_THING].ip_str), "%d.%d.%d.%d",
                   (int)((ipAddress.ip.v4 >> 24) & 0xFF), (int)((ipAddress.ip.v4 >> 16) & 0xFF),
                   (int)((ipAddress.ip.v4 >> 8) & 0xFF),  (int)(ipAddress.ip.v4 & 0xFF));

    /* Update the display so that the IP address is shown */
    wiced_rtos_set_semaphore(&displaySemaphore);

    /* Start the HTTP thread */
    wiced_rtos_create_thread(&httpThreadHandle, THREAD_BASE_PRIORITY+1, NULL, httpThread, THREAD_STACK_SIZE, NULL);

    /* Wait and then start command thread last so that help info is displayed at the bottom of the terminal window */
    wiced_rtos_delay_milliseconds(200);
    wiced_rtos_create_thread(&commandThreadHandle, THREAD_BASE_PRIORITY+3, NULL, commandThread, THREAD_STACK_SIZE, NULL);

    /* Start timer to post weather data every 30 seconds */
    wiced_rtos_init_timer(&postTimer, TIMER_TIME, timer30sec, NULL);
    wiced_rtos_start_timer(&postTimer);

    /* Setup interrupts for the 2 mechanical buttons */
    wiced_gpio_input_irq_enable(WICED_BUTTON2, IRQ_TRIGGER_FALLING_EDGE, post_button_isr, NULL);
    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, alert_button_isr, NULL);

    /* No while(1) here since everything is done by the new threads. */
    return;
}


/*************** Weather Post Button ISR ***************/
/* When button is pressed, we post the weather data */
void post_button_isr(void* arg)
{
     char httpCmd[4]; /* Command pushed onto the queue to determine what to post */
     httpCmd[0] = WEATHER_CMD;
     /* Note - only WICED_NO_WAIT is supported in an ISR so if the queue is full we won't post */
     wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT); /* Push value onto queue*/
}


/*************** Weather Alert Button ISR ***************/
/* When button is pressed, we toggle the weather alert and post it */
void alert_button_isr(void* arg)
{
    char httpCmd[4]; /* Command pushed onto the queue to determine what to post */

    if(iot_data[MY_THING].alert == WICED_TRUE)
     {
        iot_data[MY_THING].alert = WICED_FALSE;
     }
     else
     {
         iot_data[MY_THING].alert = WICED_TRUE;
     }

     /* Set a semaphore for the OLED to update the display */
     wiced_rtos_set_semaphore(&displaySemaphore);
     /* Post the alert */
     httpCmd[0] = ALERT_CMD;
     /* Note - only WICED_NO_WAIT is supported in an ISR so if the queue is full we won't post */
     wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT); /* Push value onto queue*/
}


/*************** Weather Data Acquisition Thread ***************/
/* Thread to read temperature, humidity, and light from the PSoC analog Co-processor */
void getWeatherDataThread(wiced_thread_arg_t arg)
{
    /* Weather data from the PSoC Analog Coprocessor  */
    struct {
        float temp;
        float humidity;
        float light;
    } __attribute__((packed)) weather_data;

    /* Variables to remember previous values */
	float tempPrev = 0;
	float humPrev = 0;
	float lightPrev = 0;

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
		iot_data[MY_THING].temp =     weather_data.temp;
		iot_data[MY_THING].humidity = weather_data.humidity;
		iot_data[MY_THING].light =    weather_data.light;

		/* Look at weather data - only update display if a value has changed*/
		if((tempPrev != iot_data[MY_THING].temp) || (humPrev != iot_data[MY_THING].humidity) || (lightPrev != iot_data[MY_THING].light))
		{
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

    /* Command to request HTTP data */
    char httpCmd[4];
    httpCmd[0] = GET_CMD;

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
                dispThing = MY_THING;
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
                httpCmd[1] = dispThing;
                wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT); /* Get latest data for this thing */
            }
            if((CapSenseValues & B2_MASK) == B2_MASK) /* Button 2 goes to previous thing's screen */
            {
                buttonPressed = WICED_TRUE;
                dispThing++;
                if(dispThing > MAX_THING) /* Handle wrap-around case */
                {
                    dispThing = 0;
                }
                httpCmd[1] = dispThing;
                wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT); /* Get latest data for this thing */
            }
            if((CapSenseValues & B3_MASK) == B3_MASK) /* Button 3 increments by 10 things */
            {
                buttonPressed = WICED_TRUE;
                dispThing += 10;
                if(dispThing > MAX_THING) /* Handle wrap-around case */
                {
                    dispThing -= (MAX_THING + 1);
                }
                httpCmd[1] = dispThing;
                wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT); /* Get latest data for this thing */
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

	char httpCmd[4]; /* Command pushed onto the queue to determine what to post or get */

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
				WPRINT_APP_INFO(("\tt - Print temperature and post\n"));
				WPRINT_APP_INFO(("\th - Print humidity and post\n"));
                WPRINT_APP_INFO(("\tl - Print light value and post\n"));
				WPRINT_APP_INFO(("\tA - Publish weather alert ON\n"));
				WPRINT_APP_INFO(("\ta - Publish weather alert OFF\n"));
                WPRINT_APP_INFO(("\tP - Turn getting data from all things every 30 sec ON\n"));
                WPRINT_APP_INFO(("\tp - Turn getting data from all things every 30 sec OFF\n"));
                WPRINT_APP_INFO(("\tx - Print the current known state of the data from all things\n"));
				WPRINT_APP_INFO(("\tc - Clear the terminal and set the cursor to the upper left corner\n"));
				WPRINT_APP_INFO(("\t? - Print the list of commands\n"));
				break;
			case 't': /* Print temperature to terminal and post */
				WPRINT_APP_INFO(("Temperature: %.1f\n", iot_data[MY_THING].temp)); /* Print temperature to terminal */
			    /* Post temperature to the cloud */
				httpCmd[0] = TEMPERATURE_CMD;
				wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
			case 'h': /* Print humidity to terminal and post */
				WPRINT_APP_INFO(("Humidity: %.1f\t\n", iot_data[MY_THING].humidity)); /* Print humidity to terminal */
			    /* Post humidity to the cloud */
				httpCmd[0] = HUMIDITY_CMD;
				wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
            case 'l': /* Print light value to terminal and post */
                WPRINT_APP_INFO(("Light: %.1f\t\n", iot_data[MY_THING].light)); /* Print humidity to terminal */
                /* Post light value to the cloud */
                httpCmd[0] = LIGHT_CMD;
                wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
                break;
			case 'A': /* Post Weather Alert ON */
				WPRINT_APP_INFO(("Weather Alert ON\n"));
				iot_data[MY_THING].alert = WICED_TRUE;
			    wiced_rtos_set_semaphore(&displaySemaphore); /* Update display */
	            httpCmd[0] = ALERT_CMD;
				wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
			case 'a': /* Post Weather Alert OFF */
				WPRINT_APP_INFO(("Weather Alert OFF\n"));
				iot_data[MY_THING].alert = WICED_FALSE;
                wiced_rtos_set_semaphore(&displaySemaphore); /* Update display */
				httpCmd[0] = ALERT_CMD;
				wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
            case 'P': /* Turn on getting data from all things every 30 sec */
                WPRINT_APP_INFO(("Thing Updates ON\n"));
                getAll = WICED_TRUE;
                break;
            case 'p': /* Turn off getting data from all things every 30 sec */
                WPRINT_APP_INFO(("Thing Updates OFF\n"));
                getAll = WICED_FALSE;
                break;
			case 'x': /* Print current state of all things */
                for(loop = 0; loop <= MAX_THING; loop++)
                {
                    WPRINT_APP_INFO(("Thing: ww101_%02d   IP: %15s   Alert: %d   Temperature: %4.1f   Humidity: %4.1f   Light: %5.0f \n",
                            loop,
                            iot_data[loop].ip_str,
                            iot_data[loop].alert,
                            iot_data[loop].temp,
                            iot_data[loop].humidity,
                            iot_data[loop].light));
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


/*************** HTTP Thread ***************/
/* Thread to post and get data to/from the cloud */
void httpThread(wiced_thread_arg_t arg)
{
	char json[100] = "TEST";	  /* JSON message to send */
	static char json_size[5];     /* This holds the size of the JSON message as a decimal value */

	uint8_t httpCmd[4]; /* Command pushed ONTO the queue to determine what to post/get */

    uint8_t command[4]; /* Value popped FROM the queue to determine what to post/get */

    /* HTTP method and resource name */
    http_method_t method;
    char resource[50];

    /* Define HTTP client configuration parameters */
    client_configuration.flag = (http_client_configuration_flags_t)(HTTP_CLIENT_CONFIG_FLAG_SERVER_NAME | HTTP_CLIENT_CONFIG_FLAG_MAX_FRAGMENT_LEN);
    client_configuration.server_name = (uint8_t*)SERVER_HOST;
    client_configuration.max_fragment_length = TLS_FRAGMENT_LENGTH_1024;
    /* If you set hostname, library will make sure subject name in the server certificate is matching with host name you are trying to connect. Pass NULL if you don't want to enable this check */
    client.peer_cn = NULL;

    /* Initialize and configure the HTTP Client */
    http_client_init( &client, WICED_STA_INTERFACE, http_event_handler, &tls_identity );
    http_client_configure(&client, &client_configuration);

	/* Post the IP address to the server one time */
	httpCmd[0] = IP_CMD;
	wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/

    while ( 1 )
    {
        /* Wait until an HTTP event is requested */
        wiced_rtos_pop_from_queue(&httpQueue, &command, WICED_WAIT_FOREVER);

        /* Assume that we are doing a post of the local thing's shadow */
        method = HTTP_POST; // http method
        snprintf(resource, sizeof(resource), "/things/ww101_%02d/shadow", MY_THING); // http resource path
        iot_holding.thingNumber = MY_THING; // set thing number in temporary IoT holding structure

        /* Setup the JSON message based on the command */
        switch(command[0])
        {
            case WEATHER_CMD: 	/* post temperature and humidity */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%.0f}}}", iot_data[MY_THING].temp, iot_data[MY_THING].humidity, iot_data[MY_THING].light);
                break;
            case TEMPERATURE_CMD: 	/* post temperature */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"temperature\":%.1f} } }", iot_data[MY_THING].temp);
                break;
            case HUMIDITY_CMD: 	/* post humidity */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"humidity\":%.1f} } }", iot_data[MY_THING].humidity);
                break;
            case LIGHT_CMD:  /* post light value */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"light\":%.1f} } }", iot_data[MY_THING].light);
                break;
            case ALERT_CMD: /* post weather alert */
                if(iot_data[MY_THING].alert)
                {
                    snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"weatherAlert\":true} } }");
                }
                else
                {
                    snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"weatherAlert\":false} } }");
                }
                break;
            case IP_CMD:	/* post IP address */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"IPAddress\":\"%s\"} } }", iot_data[MY_THING].ip_str);
                break;
            case GET_CMD:   /* Get state of other things */
                snprintf(json, sizeof(json), "{}");
                /* Override the method and resource path to the specified thing's shadow */
                method = HTTP_GET;
                snprintf(resource, sizeof(resource), "/things/ww101_%02d/shadow", command[1]);
                iot_holding.thingNumber = command[1]; // Update thingNumber in the IoT holding structure
                break;
        }

        /* Connect to the server */
        if(!connected)
        {
            /*  Need to de-init and re-init client in case the server has disconnected us */
            http_client_deinit( &client );
            http_client_init( &client, WICED_STA_INTERFACE, event_handler, NULL );
            http_client_configure(&client, &client_configuration);
            WPRINT_APP_INFO( ( "Connecting to %s\n", SERVER_HOST ) );
            if ( ( ret = http_client_connect( &client, (const wiced_ip_address_t*)&server_address, SERVER_PORT, HTTP_USE_TLS, CONNECT_TIMEOUT_MS ) ) != WICED_SUCCESS )
            {
                WPRINT_APP_INFO( ( "Error: failed to connect to server: %u\n", ret) );
                return;
            }
        }

        http_request_init( &request, &client, method, resource, HTTP_1_1 );
        /* Update headers based on the command and send body if it is a post */
        if(command[0] == GET_CMD)
        {
            /* Write headers */
            http_request_write_header( &request, &header[0], 1 ); // Header 1 is Host
            http_request_write_end_header( &request );
        }
        else /* All other commands do a post */
        {
            /* Find content length and write that value to the 3rd header */
            sprintf(json_size,"%d",strlen(json));
            header[2].field        =  HTTP_HEADER_CONTENT_LENGTH;
            header[2].field_length = strlen( HTTP_HEADER_CONTENT_LENGTH );
            header[2].value        = json_size;
            header[2].value_length = strlen( json_size );
            /* Write headers */
            http_request_write_header( &request, &header[0], 3 ); // Header 1 is Host, 2 is content type, 3 is content length
            http_request_write_end_header( &request );
            /* Write body */
            http_request_write( &request, (uint8_t* ) json, strlen(json));
        }
        http_request_flush( &request );

        wiced_rtos_get_semaphore(&httpWaitSemaphore, WICED_WAIT_FOREVER); /* Wait for this request to complete before going on */
        /* Now that we are done, copy data from the temporary holding structure to the correct things IoT
         * structure. If the thing number is the local thing, do nothing since we already have the data. */
        if(iot_holding.thingNumber != MY_THING)
        {
            strcpy(iot_data[iot_holding.thingNumber].ip_str, iot_holding.ip_str);
            iot_data[iot_holding.thingNumber].temp = iot_holding.temp;
            iot_data[iot_holding.thingNumber].humidity = iot_holding.humidity;
            iot_data[iot_holding.thingNumber].light = iot_holding.light;
            iot_data[iot_holding.thingNumber].alert = iot_holding.alert;
            /* Update the display if this is the thing we are currently displaying */
            if(iot_holding.thingNumber == dispThing)
            {
                wiced_rtos_set_semaphore(&displaySemaphore);
            }
        }
    }
}


/*************** HTTP event handler callback ***************/
/*
 * Call back function to handle http events.
 */
static void http_event_handler( http_client_t* client, http_event_t event, http_response_t* response )
{
    switch( event )
    {
        case HTTP_CONNECTED:
            connected = WICED_TRUE;
            break;

        case HTTP_DISCONNECTED:
        {
            WPRINT_APP_INFO(( "Disconnected\n"));
            connected = WICED_FALSE;
            break;
        }

        case HTTP_DATA_RECEIVED:
        {
            /* Parse the JSON response data and put in a temporary holding place */
            /* The data will be moved to the appropriate location once the response is complete */
            if(iot_holding.thingNumber != MY_THING) /* Only parse the payload for other things */
            {
                cJSON *root = cJSON_Parse((char*) response->payload);
                cJSON *state = cJSON_GetObjectItem(root,"state");
                cJSON *reported = cJSON_GetObjectItem(state,"reported");
                cJSON *ipValue = cJSON_GetObjectItem(reported,"IPAddress");
                if(ipValue->type == cJSON_String) /* Make sure we have a string */
                {
                    snprintf(iot_holding.ip_str, sizeof(iot_holding.ip_str), cJSON_GetObjectItem(reported,"IPAddress")->valuestring);
                }
                iot_holding.temp = (float) cJSON_GetObjectItem(reported,"temperature")->valuedouble;
                iot_holding.humidity = (float) cJSON_GetObjectItem(reported,"humidity")->valuedouble;
                iot_holding.light = (float) cJSON_GetObjectItem(reported,"light")->valuedouble;
                iot_holding.alert = (wiced_bool_t) cJSON_GetObjectItem(reported,"weatherAlert")->type;
                cJSON_Delete(root);
            }

            /* This is the end of the response, so we will clean up and set the semaphore for the calling thread to go on */
            if(response->remaining_length == 0)
            {
               http_request_deinit( (http_request_t*) &(response->request) );
               wiced_rtos_set_semaphore(&httpWaitSemaphore); // Set semaphore to tell calling thread that this response is complete
            }
            break;
        }
        default:
        break;
    }
}


/*************** 30 second Timer ***************/
/*
 * 1. Post weather data every 30sec from the local thing.
 *
 * 2. Get data for all things if that function is enabled.
 *
 * 3. Get the weather data for the currently
 *    displayed thing if it isn't the local thing.
 */
void timer30sec(void* arg)
{
    uint8_t loop;

    char httpCmd[4]; /* Command pushed onto the queue to determine what to post */
    httpCmd[0] = WEATHER_CMD;
    /* Must use WICED_NO_WAIT here because waiting is not allowed in a timer - if the queue is full we wont post */
    wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT); /* Send weather data for local thing */

    /* Get data for all things if enabled otherwise get the displayed thing if it isn't the local thing */
    if(getAll)
    {
        httpCmd[0] = GET_CMD;
        for(loop = 0; loop <= MAX_THING; loop++)
            {
                httpCmd[1] = loop;
                wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT);
            }
    }
    else if(dispThing != MY_THING)
    {
        httpCmd[0] = GET_CMD;
        httpCmd[1] = dispThing;
        wiced_rtos_push_to_queue(&httpQueue, httpCmd, WICED_NO_WAIT);
    }
}
