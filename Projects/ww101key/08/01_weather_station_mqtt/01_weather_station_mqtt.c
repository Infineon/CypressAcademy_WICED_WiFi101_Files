/* This is the class project for the WICED WiFi WW-101 class */
#include "wiced.h"
#include "u8g_arm.h"
#include "mqtt_api.h"
#include "resources.h"
#include "cJSON.h"

/*******************************************************************************************************/
/* Update this number for the number of the thing that you want to publish to. The default is ww101_00 */
#define MY_THING  0
/*******************************************************************************************************/

/* The highest number thing on the MQTT Broker that you want to subscribe to */
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
#define MQTT_BROKER_ADDRESS                 "amk6m51qrxr2u.iot.us-east-1.amazonaws.com"
#define CLIENT_ID                           "wiced_weather_aws"
#define TOPIC_HEAD							"$aws/things/ww101_"
#define TOPIC_SUBSCRIBE                     "$aws/things/+/shadow/update/documents"
#define TOPIC_GETSUBSCRIBE                  "$aws/things/+/shadow/get/accepted"
#define MQTT_REQUEST_TIMEOUT                (5000)
#define MQTT_DELAY_IN_MILLISECONDS          (1000)
#define MQTT_MAX_RESOURCE_SIZE              (0x7fffffff)
#define MQTT_PUBLISH_RETRY_COUNT            (3)
#define MQTT_SUBSCRIBE_RETRY_COUNT          (3)

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
/* Broker object and function return value */
volatile wiced_mqtt_object_t   mqtt_object;

/* MAC address which will be used as part of the CLIENT_ID to make it unique */
static wiced_mac_t mac;     // WW101 addition
static char macString[20];  // WW101 addition

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

volatile uint8_t dispThing = MY_THING; /* Which thing to display data for on the OLED */

/* IP Address of the local host in IP format */
wiced_ip_address_t ipAddress;

static wiced_ip_address_t                   broker_address;
static wiced_mqtt_event_type_t              expected_event;
static wiced_semaphore_t                    msg_semaphore;
static wiced_mqtt_security_t                security;

/* RTOS global constructs */
static wiced_semaphore_t displaySemaphore;
static wiced_mutex_t i2cMutex;
static wiced_mutex_t pubSubMutex;
static wiced_queue_t pubQueue;
static wiced_timer_t publishTimer;
static wiced_thread_t getWeatherDataThreadHandle;
static wiced_thread_t getCapSenseThreadHandle;
static wiced_thread_t displayThreadHandle;
static wiced_thread_t commandThreadHandle;
static wiced_thread_t publishThreadHandle;

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
void publish30sec(void* arg);
wiced_result_t open_mqtt_connection();
/* Functions from the demo/aws_iot/pub_sub/publisher project used for publishing */
static wiced_result_t wait_for_response( wiced_mqtt_event_type_t event, uint32_t timeout );
static wiced_result_t mqtt_conn_open( wiced_mqtt_object_t mqtt_obj, wiced_ip_address_t *address, wiced_interface_t interface, wiced_mqtt_callback_t callback, wiced_mqtt_security_t *security );
static wiced_result_t mqtt_app_publish( wiced_mqtt_object_t mqtt_obj, uint8_t qos, uint8_t *topic, uint8_t *data, uint32_t data_len );
static wiced_result_t mqtt_app_subscribe( wiced_mqtt_object_t mqtt_obj, char *topic, uint8_t qos );
static wiced_result_t mqtt_conn_close( wiced_mqtt_object_t mqtt_obj );
static wiced_result_t mqtt_connection_event_cb( wiced_mqtt_object_t mqtt_object, wiced_mqtt_event_info_t *event );

/*************** Functions **********************/
/*************** Main application ***************/
void application_start( )
{
    uint32_t        size_out = 0;
    int             sub_retries = 0;
    uint8_t         loop;
    wiced_result_t  ret = WICED_SUCCESS;


	wiced_init();	/* Initialize the WICED device */

    /* Setup Thread Control functions */
     wiced_rtos_init_mutex(&i2cMutex);
     wiced_rtos_init_mutex(&pubSubMutex);
     wiced_rtos_init_semaphore(&displaySemaphore);
     wiced_rtos_init_queue(&pubQueue, NULL, MESSAGE_SIZE, QUEUE_SIZE);
     wiced_rtos_init_semaphore( &msg_semaphore );

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

    /* Start up the MQTT connection to the server */
    /* Get AWS root certificate, client certificate and private key respectively */
    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_rootca_cer, 0, MQTT_MAX_RESOURCE_SIZE, &size_out, (const void **) &security.ca_cert );
    security.ca_cert_len = size_out;

    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_client_cer, 0, MQTT_MAX_RESOURCE_SIZE, &size_out, (const void **) &security.cert );
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\nNot a valid Certificate! Please replace the dummy certificate file 'resources/app/aws_iot/client.cer' with the one got from AWS\n\n" ) );
        return;
    }
    security.cert_len = size_out;

    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_privkey_cer, 0, MQTT_MAX_RESOURCE_SIZE, &size_out, (const void **) &security.key );
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\nNot a valid Private Key! Please replace the dummy private key file 'resources/app/aws_iot/privkey.cer' with the one got from AWS\n\n" ) );
        return;
    }
    security.key_len = size_out;

    /* Disable roaming to other access points */
    wiced_wifi_set_roam_trigger( -99 ); /* -99dBm ie. extremely low signal level */

    /* Bring up the network interface */
    ret = wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "\nNot able to join the requested AP\n\n" ) );
        return;
    }

    /* WW101 addition - get MAC address and save as a string*/
    wiced_wifi_get_mac_address(&mac);
    snprintf(macString, sizeof(macString), "%02X:%02X:%02X:%02X:%02X:%02X",
                       mac.octet[0], mac.octet[1], mac.octet[2],
                       mac.octet[3], mac.octet[4], mac.octet[5]);

    /* Allocate memory for MQTT object*/
    mqtt_object = (wiced_mqtt_object_t) malloc( WICED_MQTT_OBJECT_MEMORY_SIZE_REQUIREMENT );
    if ( mqtt_object == NULL )
    {
        WPRINT_APP_ERROR("Don't have memory to allocate for mqtt object...\n");
        return;
    }

    WPRINT_APP_INFO( ( "Resolving IP address of MQTT broker...\n" ) );
    ret = wiced_hostname_lookup( MQTT_BROKER_ADDRESS, &broker_address, 10000, WICED_STA_INTERFACE );
    WPRINT_APP_INFO(("Resolved Broker IP: %u.%u.%u.%u\n\n", (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 24),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 16),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 8),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 0)));
    if ( ret == WICED_ERROR || broker_address.ip.v4 == 0 )
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

    ret = open_mqtt_connection();

    if(ret == WICED_SUCCESS) /* Start up everything else if the MQTT connection was opened */
    {
        /* Now that the connection is established, get everything else going */
        WPRINT_APP_INFO(("Success\n"));

        /* Subscribe to the update/documents topic for all things using the + wildcard */
        wiced_rtos_lock_mutex(&pubSubMutex);
        WPRINT_APP_INFO(("[MQTT] Subscribing to %s...",TOPIC_SUBSCRIBE));
        do
        {
            ret = mqtt_app_subscribe( mqtt_object, TOPIC_SUBSCRIBE, WICED_MQTT_QOS_DELIVER_AT_MOST_ONCE );
            sub_retries++ ;
        } while ( ( ret != WICED_SUCCESS ) && ( sub_retries < MQTT_SUBSCRIBE_RETRY_COUNT ) );
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
        WPRINT_APP_INFO(("[MQTT] Subscribing to %s...",TOPIC_GETSUBSCRIBE));
        do
        {
            ret = mqtt_app_subscribe( mqtt_object, TOPIC_GETSUBSCRIBE, WICED_MQTT_QOS_DELIVER_AT_MOST_ONCE );
            sub_retries++ ;
        } while ( ( ret != WICED_SUCCESS ) && ( sub_retries < MQTT_SUBSCRIBE_RETRY_COUNT ) );
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
    }

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
		iot_data[MY_THING].temp =     weather_data.temp;
		iot_data[MY_THING].humidity = weather_data.humidity;
		iot_data[MY_THING].light =    weather_data.light;

		/* Look at weather data - only update display if a value has changed*/
		if((tempPrev != iot_data[MY_THING].temp) || (humPrev != iot_data[MY_THING].humidity) || (lightPrev != iot_data[MY_THING].light))
		{
			/* Save the new values as previous for next time around */
		    tempPrev  = iot_data[MY_THING].temp;
		    humPrev   = iot_data[MY_THING].humidity;
		    lightPrev = iot_data[MY_THING].light;

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
				WPRINT_APP_INFO(("Temperature: %.1f\n", iot_data[MY_THING].temp)); /* Print temperature to terminal */
			    /* Publish temperature to the cloud */
				pubCmd[0] = TEMPERATURE_CMD;
				wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
			case 'h': /* Print humidity to terminal and publish */
				WPRINT_APP_INFO(("Humidity: %.1f\t\n", iot_data[MY_THING].humidity)); /* Print humidity to terminal */
			    /* Publish humidity to the cloud */
				pubCmd[0] = HUMIDITY_CMD;
				wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
            case 'l': /* Print light value to terminal and publish */
                WPRINT_APP_INFO(("Light: %.1f\t\n", iot_data[MY_THING].light)); /* Print humidity to terminal */
                /* Publish light value to the cloud */
                pubCmd[0] = LIGHT_CMD;
                wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
                break;
			case 'A': /* Publish Weather Alert ON */
				WPRINT_APP_INFO(("Weather Alert ON\n"));
				iot_data[MY_THING].alert = WICED_TRUE;
			    wiced_rtos_set_semaphore(&displaySemaphore); /* Update display */
	            pubCmd[0] = ALERT_CMD;
				wiced_rtos_push_to_queue(&pubQueue, pubCmd, WICED_WAIT_FOREVER); /* Push value onto queue*/
				break;
			case 'a': /* Publish Weather Alert OFF */
				WPRINT_APP_INFO(("Weather Alert OFF\n"));
				iot_data[MY_THING].alert = WICED_FALSE;
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
        snprintf(topic, sizeof(topic), "%s%02d/shadow/update", TOPIC_HEAD,MY_THING);

        /* Setup the JSON message based on the command */
        switch(command[0])
        {
            case WEATHER_CMD: 	/* publish temperature and humidity */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%.0f}}}", iot_data[MY_THING].temp, iot_data[MY_THING].humidity, iot_data[MY_THING].light);
                break;
            case TEMPERATURE_CMD: 	/* publish temperature */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"temperature\":%.1f} } }", iot_data[MY_THING].temp);
                break;
            case HUMIDITY_CMD: 	/* publish humidity */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"humidity\":%.1f} } }", iot_data[MY_THING].humidity);
                break;
            case LIGHT_CMD:  /* publish light value */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"light\":%.1f} } }", iot_data[MY_THING].light);
                break;
            case ALERT_CMD: /* weather alert */
                if(iot_data[MY_THING].alert)
                {
                    snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"weatherAlert\":true} } }");
                }
                else
                {
                    snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"weatherAlert\":false} } }");
                }
                break;
            case IP_CMD:	/* IP address */
                snprintf(json, sizeof(json), "{\"state\" : {\"reported\" : {\"IPAddress\":\"%s\"} } }", iot_data[MY_THING].ip_str);
                break;
            case GET_CMD:   /* Get starting state of other things */
                snprintf(json, sizeof(json), "{}");
                /* Override the topic to do a get of the specified thing's shadow */
                snprintf(topic, sizeof(topic), "%s%02d/shadow/get", TOPIC_HEAD,command[1]);
                break;
        }

        wiced_rtos_lock_mutex(&pubSubMutex);
        WPRINT_APP_INFO(("[MQTT] Publishing..."));
        pub_retries = 0; // reset retries to 0 before going into the loop so that the next publish after a failure will still work
        do
        {
            ret = mqtt_app_publish( mqtt_object, WICED_MQTT_QOS_DELIVER_AT_MOST_ONCE, (uint8_t*) topic, (uint8_t*) json, strlen( json ) );
            pub_retries++ ;
        } while ( ( ret != WICED_SUCCESS ) && ( pub_retries < MQTT_PUBLISH_RETRY_COUNT ) );
        if ( ret != WICED_SUCCESS )
        {
            WPRINT_APP_INFO(("Publish Failed: Error Code %d\n",ret));
        }
        else
        {
            WPRINT_APP_INFO(("Publish Success\n"));
        }
        wiced_rtos_unlock_mutex(&pubSubMutex);
        wiced_rtos_delay_milliseconds( 100 );
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

/*************** Function to try opening MQTT connection that fails gracefully after a few attempts ***************/
wiced_result_t open_mqtt_connection()
{
    int             connection_retries = 0;
    wiced_result_t  ret = WICED_SUCCESS;

    WPRINT_APP_INFO(("[MQTT] Opening connection..."));
    wiced_mqtt_init( mqtt_object );
    connection_retries = 0;
    do
    {
        ret = mqtt_conn_open( mqtt_object, &broker_address, WICED_STA_INTERFACE, mqtt_connection_event_cb, &security );
        connection_retries++ ;
    } while ( ( ret != WICED_SUCCESS ) && ( connection_retries < WICED_MQTT_CONNECTION_NUMBER_OF_RETRIES ) );

    if ( ret != WICED_SUCCESS )
    {
        /* If we get here, the MQTT connection could not be opened */
        WPRINT_APP_INFO(("Connection Failed: Error Code %d\n",ret));
        WPRINT_APP_INFO(("[MQTT] Closing connection..."));
        mqtt_conn_close( mqtt_object );
        wiced_rtos_delay_milliseconds( MQTT_DELAY_IN_MILLISECONDS * 2 );
        wiced_rtos_deinit_semaphore( &msg_semaphore );
        WPRINT_APP_INFO(("[MQTT] Deinit connection..."));
        wiced_mqtt_deinit( mqtt_object );
        free( mqtt_object );
        mqtt_object = NULL;
        WPRINT_APP_INFO(("Done\n"));
    }
    return ret;
}

/**************************************************************************************/
/* Functions copied from the demo/aws_iot/pub_sub/publisher application */
/*
 * A blocking call to an expected event.
 */
static wiced_result_t wait_for_response( wiced_mqtt_event_type_t event, uint32_t timeout )
{
    if ( wiced_rtos_get_semaphore( &msg_semaphore, timeout ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    else
    {
        if ( event != expected_event )
        {
            return WICED_ERROR;
        }
    }
    return WICED_SUCCESS;
}


/*
 * Open a connection and wait for MQTT_REQUEST_TIMEOUT period to receive a connection open OK event
 */
static wiced_result_t mqtt_conn_open( wiced_mqtt_object_t mqtt_obj, wiced_ip_address_t *address, wiced_interface_t interface, wiced_mqtt_callback_t callback, wiced_mqtt_security_t *security )
{
    wiced_mqtt_pkt_connect_t conninfo;
    wiced_result_t        ret = WICED_SUCCESS;

    char id_plus_mac[sizeof(macString)+sizeof(CLIENT_ID)];  // WW101 addition

    memset( &conninfo, 0, sizeof( conninfo ) );
    conninfo.port_number = 0;
    conninfo.mqtt_version = WICED_MQTT_PROTOCOL_VER4;
    conninfo.clean_session = 1;
    snprintf(id_plus_mac, sizeof(id_plus_mac), "%s_%s", macString, (uint8_t*) CLIENT_ID); // WW101 addition
    conninfo.client_id = (uint8_t*) id_plus_mac; // WW101 modified
    conninfo.keep_alive = 5;
    conninfo.password = NULL;
    conninfo.username = NULL;
    conninfo.peer_cn = (uint8_t*) "*.iot.us-east-1.amazonaws.com";
    ret = wiced_mqtt_connect( mqtt_obj, address, interface, callback, security, &conninfo );
    if ( ret != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}


/*
 * Publish (send) message to WICED_TOPIC and wait for 5 seconds to receive a PUBCOMP (as it is QoS=2).
 */
static wiced_result_t mqtt_app_publish( wiced_mqtt_object_t mqtt_obj, uint8_t qos, uint8_t *topic, uint8_t *data, uint32_t data_len )
{
    wiced_mqtt_msgid_t pktid;

    pktid = wiced_mqtt_publish( mqtt_obj, topic, data, data_len, qos );

    if ( pktid == 0 )
    {
        return WICED_ERROR;
    }

    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_PUBLISHED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}


/*
 * Subscribe to WICED_TOPIC and wait for 5 seconds to receive an ACM.
 */
static wiced_result_t mqtt_app_subscribe( wiced_mqtt_object_t mqtt_obj, char *topic, uint8_t qos )
{
    wiced_mqtt_msgid_t pktid;
    pktid = wiced_mqtt_subscribe( mqtt_obj, topic, qos );
    if ( pktid == 0 )
    {
        return WICED_ERROR;
    }
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_SUBCRIBED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}


/*
 * Close a connection and wait for 5 seconds to receive a connection close OK event
 */
static wiced_result_t mqtt_conn_close( wiced_mqtt_object_t mqtt_obj )
{
    if ( wiced_mqtt_disconnect( mqtt_obj ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_DISCONNECTED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}


/*
 * Call back function to handle connection events.
 */
static wiced_result_t mqtt_connection_event_cb( wiced_mqtt_object_t mqtt_object, wiced_mqtt_event_info_t *event )
{
    uint thingNumber;           /* The number of the thing that published a message */
    wiced_mqtt_topic_msg_t msg; /* The message from the thing */
    char topicStr[50] = {0};    /* String to copy the topic into */
    char pubType[20] =  {0};    /* String to compare to the publish type */

    switch ( event->type )
    {
        case WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS:
        case WICED_MQTT_EVENT_TYPE_DISCONNECTED:
        case WICED_MQTT_EVENT_TYPE_PUBLISHED:
        case WICED_MQTT_EVENT_TYPE_SUBCRIBED:
        case WICED_MQTT_EVENT_TYPE_UNSUBSCRIBED:
        {
            expected_event = event->type;
            wiced_rtos_set_semaphore( &msg_semaphore );
        }
            break;
        case WICED_MQTT_EVENT_TYPE_PUBLISH_MSG_RECEIVED:
            msg = event->data.pub_recvd;
            /* Copy the message to a null terminated string */
            memcpy(topicStr, msg.topic, msg.topic_len);
            topicStr[msg.topic_len+1] = 0; /* Add termination */

            /* Scan the topic to see if it is one of the things we are interested in */
            sscanf(topicStr, "$aws/things/ww101_%2u/shadow/%19s", &thingNumber, pubType);
            /* Check to see if it is an initial get of the values of other things */
            if(strcmp(pubType,"get/accepted") == 0)
            {
                if(thingNumber != MY_THING) /* Only do the rest if it isn't the local thing */
                {
                    /* Parse JSON message for the weather station data */
                    cJSON *root = cJSON_Parse((char*) msg.data);
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
                    iot_data[thingNumber].alert = (wiced_bool_t) cJSON_GetObjectItem(reported,"weatherAlert")->type;
                    cJSON_Delete(root);
                }
            }
            /* Check to see if it is an update published by another thing */
            if(strcmp(pubType,"update/documents") == 0)
            {
                if(thingNumber != MY_THING) /* Only do the rest if it isn't the local thing */
                {
                    /* Parse JSON message for the weather station data */
                    cJSON *root = cJSON_Parse((char*) msg.data);
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
                    iot_data[thingNumber].alert = (wiced_bool_t) cJSON_GetObjectItem(reported,"weatherAlert")->type;
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
        default:
            break;
    }
    return WICED_SUCCESS;
}
