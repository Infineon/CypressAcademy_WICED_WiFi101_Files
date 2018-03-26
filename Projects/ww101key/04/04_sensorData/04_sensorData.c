/*
 * Display sensor data from the analog co-processor on the OLED
 *
 * psocThread reads environment data from the shield across I2C and posts to oledQueue
 * oledThread extracts data from oledQueue and updates the OLED display using I2C
 *
 * i2cMutex is used to ensure the I2C is only accessed by one thread at a time
 */

#include "u8g_arm.h"

#define PSOC_ADDRESS        (0x42)
#define OLED_ADDRESS        (0x3C)

#define TEMPERATURE_REG     (0x07)

#define QUEUE_DEPTH         (10)

#define OLED_WIDTH          (16)

/* Data structure for data from the PSoC shield */
struct env_data_t
{
    float temp;
    float humidity;
    float light;
    float pot;
};

/* Global RTOS resources */
wiced_queue_t oledQueue;        // Send environment data between threads
wiced_mutex_t i2cMutex;         // Ensure only one thread uses I2C at a time
wiced_thread_t oledThread;
wiced_thread_t psocThread;

/* Thread to get data from the shield and post it to the RTOS queue */
void psocFunction( wiced_thread_arg_t arg )
{

    /* I2C variables - Tx and Rx buffers and a message */
    uint8_t             txData[] = { TEMPERATURE_REG };
    struct env_data_t   rxData;

    /* Setup I2C master structure for communicating with the PSoC */
    const wiced_i2c_device_t psoc_i2c =
    {
        .port           = WICED_I2C_2,
        .address        = PSOC_ADDRESS,
        .address_width  = I2C_ADDRESS_WIDTH_7BIT,
        .flags          = 0,
        .speed_mode     = I2C_STANDARD_SPEED_MODE
    };

    /* Initialize PSoC analog co-processor I2C interface and set the offset */
    wiced_i2c_init( &psoc_i2c );
    wiced_i2c_write(&psoc_i2c, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, txData, sizeof(txData));

    while( WICED_TRUE )
    {
        wiced_rtos_lock_mutex( &i2cMutex );         // Grab the I2C bus

        /* Get data from the PSoC shield */
        wiced_i2c_read(&psoc_i2c, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, &rxData, sizeof(rxData));

        wiced_rtos_unlock_mutex( &i2cMutex );       // Release the I2C bus

        /* Send the data to the oledThread. Wait if there is no room in the queue */
        wiced_rtos_push_to_queue( &oledQueue, &rxData, WICED_WAIT_FOREVER );

        wiced_rtos_delay_milliseconds( 500 );       // Update twice per second
    }
}


/* Thread to extract messages from the RTOS queue and update the OLED display */
void oledFunction( wiced_thread_arg_t arg )
{

    struct env_data_t   dataToPrint;

    char                temp_str[20];
    char                humidity_str[20];
    char                light_str[20];
    char                pot_str[20];

    u8g_t               display;

    /* Setup I2C master structure for communicating with the OLED Display */
    wiced_i2c_device_t  display_i2c =
    {
        .port          = WICED_I2C_2,
        .address       = OLED_ADDRESS,
        .address_width = I2C_ADDRESS_WIDTH_7BIT,
        .flags         = 0,
        .speed_mode    = I2C_STANDARD_SPEED_MODE,
    };

    /* Initialize the OLED display */
    u8g_init_wiced_i2c_device( &display_i2c );
    u8g_InitComFn( &display, &u8g_dev_ssd1306_128x64_i2c, u8g_com_hw_i2c_fn );
    u8g_SetFont( &display, u8g_font_unifont );
    u8g_SetFontPosTop( &display );

    while( WICED_TRUE )
    {
        /* Wait for sensor data to be posted to the queue */
        wiced_rtos_pop_from_queue( &oledQueue, &dataToPrint, WICED_WAIT_FOREVER );

        /* Setup Display Strings */
        snprintf( temp_str,     OLED_WIDTH+1, "Temp:     %.1f", dataToPrint.temp );
        snprintf( humidity_str, OLED_WIDTH+1, "Humidity: %.1f", dataToPrint.humidity );
        snprintf( light_str,    OLED_WIDTH+1, "Light:    %.1f", dataToPrint.light );
        snprintf( pot_str,      OLED_WIDTH+1, "Pot:      %.1f", dataToPrint.pot );

        wiced_rtos_lock_mutex( &i2cMutex );         // Grab the I2C bus

        /* Send data to the display */
        u8g_FirstPage( &display );
        do {
            u8g_DrawStr( &display, 0, 5,  temp_str );
            u8g_DrawStr( &display, 0, 20, humidity_str );
            u8g_DrawStr( &display, 0, 35, light_str );
            u8g_DrawStr( &display, 0, 50, pot_str );
        } while( u8g_NextPage( &display ) );

        wiced_rtos_unlock_mutex( &i2cMutex );       // Release the I2C bus
    }
}


void application_start()
{
    /* Initialize the application resources and start the threads */

    /* Application RTOS Resources */
    wiced_rtos_init_queue( &oledQueue, "oledQueue", sizeof( struct env_data_t ), QUEUE_DEPTH );
    wiced_rtos_init_mutex( &i2cMutex );

    /* Application RTOS Resources */
    wiced_rtos_create_thread( &oledThread, 10, "OLED", oledFunction, 1000, NULL );
    wiced_rtos_create_thread( &psocThread, 10, "PSoC", psocFunction, 1000, NULL );

    /* Allow application_start thread (this one) to terminate */
    return;
}
