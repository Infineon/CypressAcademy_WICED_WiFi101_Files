/* Use Neutrino to convert C to F */
/* Press MB1 to read temperature and run the conversion. Values are displayed on the OLED */
#include <stdlib.h>
#include "wiced.h"
#include "wiced_tls.h"
#include "http_client.h"
#include "cJSON.h"
#include "u8g_arm.h"

#define SERVER_HOST        "neutrinoapi.com"

#define SERVER_PORT        ( 443 )
#define DNS_TIMEOUT_MS     ( 10000 )
#define CONNECT_TIMEOUT_MS ( 3000 )

/* I2C port to use. If the platform already defines it, use that, otherwise default to WICED_I2C_2 */
#ifndef PLATFORM_ARDUINO_I2C
#define PLATFORM_ARDUINO_I2C ( WICED_I2C_2 )
#endif

#define PSOC_ADDRESS (0x42)
#define TEMPERATURE_REG 0x07
#define OLED_ADDRESS (0x3C)

static void  event_handler( http_client_t* client, http_event_t event, http_response_t* response );
static void  print_data   ( char* data, uint32_t length );
static void  update_display(char* line1, char* line2);

static const char root_ca_certificate[] =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIF2DCCA8CgAwIBAgIQTKr5yttjb+Af907YWwOGnTANBgkqhkiG9w0BAQwFADCB\n"
        "hTELMAkGA1UEBhMCR0IxGzAZBgNVBAgTEkdyZWF0ZXIgTWFuY2hlc3RlcjEQMA4G\n"
        "A1UEBxMHU2FsZm9yZDEaMBgGA1UEChMRQ09NT0RPIENBIExpbWl0ZWQxKzApBgNV\n"
        "BAMTIkNPTU9ETyBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMTE5\n"
        "MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBhTELMAkGA1UEBhMCR0IxGzAZBgNVBAgT\n"
        "EkdyZWF0ZXIgTWFuY2hlc3RlcjEQMA4GA1UEBxMHU2FsZm9yZDEaMBgGA1UEChMR\n"
        "Q09NT0RPIENBIExpbWl0ZWQxKzApBgNVBAMTIkNPTU9ETyBSU0EgQ2VydGlmaWNh\n"
        "dGlvbiBBdXRob3JpdHkwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQCR\n"
        "6FSS0gpWsawNJN3Fz0RndJkrN6N9I3AAcbxT38T6KhKPS38QVr2fcHK3YX/JSw8X\n"
        "pz3jsARh7v8Rl8f0hj4K+j5c+ZPmNHrZFGvnnLOFoIJ6dq9xkNfs/Q36nGz637CC\n"
        "9BR++b7Epi9Pf5l/tfxnQ3K9DADWietrLNPtj5gcFKt+5eNu/Nio5JIk2kNrYrhV\n"
        "/erBvGy2i/MOjZrkm2xpmfh4SDBF1a3hDTxFYPwyllEnvGfDyi62a+pGx8cgoLEf\n"
        "Zd5ICLqkTqnyg0Y3hOvozIFIQ2dOciqbXL1MGyiKXCJ7tKuY2e7gUYPDCUZObT6Z\n"
        "+pUX2nwzV0E8jVHtC7ZcryxjGt9XyD+86V3Em69FmeKjWiS0uqlWPc9vqv9JWL7w\n"
        "qP/0uK3pN/u6uPQLOvnoQ0IeidiEyxPx2bvhiWC4jChWrBQdnArncevPDt09qZah\n"
        "SL0896+1DSJMwBGB7FY79tOi4lu3sgQiUpWAk2nojkxl8ZEDLXB0AuqLZxUpaVIC\n"
        "u9ffUGpVRr+goyhhf3DQw6KqLCGqR84onAZFdr+CGCe01a60y1Dma/RMhnEw6abf\n"
        "Fobg2P9A3fvQQoh/ozM6LlweQRGBY84YcWsr7KaKtzFcOmpH4MN5WdYgGq/yapiq\n"
        "crxXStJLnbsQ/LBMQeXtHT1eKJ2czL+zUdqnR+WEUwIDAQABo0IwQDAdBgNVHQ4E\n"
        "FgQUu69+Aj36pvE8hI6t7jiY7NkyMtQwDgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB\n"
        "/wQFMAMBAf8wDQYJKoZIhvcNAQEMBQADggIBAArx1UaEt65Ru2yyTUEUAJNMnMvl\n"
        "wFTPoCWOAvn9sKIN9SCYPBMtrFaisNZ+EZLpLrqeLppysb0ZRGxhNaKatBYSaVqM\n"
        "4dc+pBroLwP0rmEdEBsqpIt6xf4FpuHA1sj+nq6PK7o9mfjYcwlYRm6mnPTXJ9OV\n"
        "2jeDchzTc+CiR5kDOF3VSXkAKRzH7JsgHAckaVd4sjn8OoSgtZx8jb8uk2Intzna\n"
        "FxiuvTwJaP+EmzzV1gsD41eeFPfR60/IvYcjt7ZJQ3mFXLrrkguhxuhoqEwWsRqZ\n"
        "CuhTLJK7oQkYdQxlqHvLI7cawiiFwxv/0Cti76R7CZGYZ4wUAc1oBmpjIXUDgIiK\n"
        "boHGhfKppC3n9KUkEEeDys30jXlYsQab5xoq2Z0B15R97QNKyvDb6KkBPvVWmcke\n"
        "jkk9u+UJueBPSZI9FoJAzMxZxuY67RIuaTxslbH9qh17f4a+Hg4yRvv7E491f0yL\n"
        "S0Zj/gA0QHDBw7mh3aZw4gSzQbzpgJHqZJx64SIDqZxubw5lT2yHh17zbqD5daWb\n"
        "QOhTsiedSrnAdyGN/4fy3ryM7xfft0kL0fJuMAsaDk527RH89elWsn2/x20Kk4yl\n"
        "0MC2Hb46TpSi125sC8KKfPog88Tk5c0NqMuRkrF8hey1FGlmDoLnzc7ILaZRfyHB\n"
        "NVOFBkpdn627G190\n"
        "-----END CERTIFICATE-----\n";

static wiced_semaphore_t httpWait;
static wiced_semaphore_t buttonWait;

static http_client_t  client;
static http_request_t request;
static http_client_configuration_info_t client_configuration;
static char resourceOptions[200];  // This holds the options string to be sent
http_header_field_t header[1]; // Array of headers

u8g_t display;

char tempStrC[15];  /* String for temperature in C */
char tempStrF[15];  /* String for temperature in F */

/******************************************************
 *               Function Definitions
 ******************************************************/
/* Interrupt service routine for the button */
void button_isr(void* arg)
{
     wiced_rtos_set_semaphore(&buttonWait);
}

void application_start( void )
{
    wiced_ip_address_t  ip_address;
    wiced_result_t      result;

    /* Header 0 is the Host header */
    header[0].field        = HTTP_HEADER_HOST;
    header[0].field_length = strlen( HTTP_HEADER_HOST );
    header[0].value        = SERVER_HOST;
    header[0].value_length = strlen( SERVER_HOST );

    wiced_init( );

    wiced_rtos_init_semaphore(&httpWait);
    wiced_rtos_init_semaphore(&buttonWait);

    wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);

    WPRINT_APP_INFO( ( "Resolving IP address of %s\n", SERVER_HOST ) );
    wiced_hostname_lookup( SERVER_HOST, &ip_address, DNS_TIMEOUT_MS, WICED_STA_INTERFACE );
    WPRINT_APP_INFO( ( "%s is at %u.%u.%u.%u\n", SERVER_HOST,
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 24),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 16),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 8),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 0) ) );

    /* Initialize the root CA certificate */
    result = wiced_tls_init_root_ca_certificates( root_ca_certificate, strlen(root_ca_certificate) );
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "Error: Root CA certificate failed to initialize: %u\n", result) );
        return;
    }

    http_client_init( &client, WICED_STA_INTERFACE, event_handler, NULL );

    /* configure HTTP client parameters */
    client_configuration.flag = (http_client_configuration_flags_t)(HTTP_CLIENT_CONFIG_FLAG_SERVER_NAME | HTTP_CLIENT_CONFIG_FLAG_MAX_FRAGMENT_LEN);
    client_configuration.server_name = (uint8_t*) SERVER_HOST;
    client_configuration.max_fragment_length = TLS_FRAGMENT_LENGTH_1024;
    http_client_configure(&client, &client_configuration);

    /* If you set hostname, library will make sure subject name in the server certificate is matching with host name you are trying to connect. pass NULL if you don't want to enable this check */
    client.peer_cn = NULL;

    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button_isr, NULL); /* Setup interrupt */

    /* Setup I2C master for reading temperature from PSoC */
    const wiced_i2c_device_t i2cDevice = {
        .port = PLATFORM_ARDUINO_I2C,
        .address = PSOC_ADDRESS,
        .address_width = I2C_ADDRESS_WIDTH_7BIT,
        .speed_mode = I2C_STANDARD_SPEED_MODE
    };

    wiced_i2c_init(&i2cDevice);

    /* Tx buffer is used to set the offset */
    uint8_t tx_buffer[] = {TEMPERATURE_REG};

    /* Rx buffer is used to get temperature as a float */
    struct {
        float temp;
    } rx_buffer;

    /* Initialize offset */
    wiced_i2c_write(&i2cDevice, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, tx_buffer, sizeof(tx_buffer));

    /* Setup I2C master to send value to the display */
    /* Initialize the OLED display */
    wiced_i2c_device_t display_i2c =
    {
        .port          = PLATFORM_ARDUINO_I2C,
        .address       = OLED_ADDRESS,
        .address_width = I2C_ADDRESS_WIDTH_7BIT,
        .flags         = 0,
        .speed_mode    = I2C_STANDARD_SPEED_MODE,

    };

    u8g_init_wiced_i2c_device(&display_i2c);

    u8g_InitComFn(&display, &u8g_dev_ssd1306_128x64_i2c, u8g_com_hw_i2c_fn);
    u8g_SetFont(&display, u8g_font_unifont);
    u8g_SetFontPosTop(&display);

    WPRINT_APP_INFO( ( "Press WICED_BUTTON1 to convert Temperature Data\n" ) );

    /* Display ready message on OLED */
    update_display("Press Button", "to Update");

    while(1)
    {
        /* Wait for a button press */
        wiced_rtos_get_semaphore(&buttonWait, WICED_WAIT_FOREVER);

        /* Read temperature from shield over I2C */
        wiced_i2c_read(&i2cDevice, WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG, &rx_buffer, sizeof(rx_buffer));
        WPRINT_APP_INFO(("Temperature: %.1f\n", rx_buffer.temp)); /* Print temperature to terminal */
        /* Display ready message on OLED */
        update_display("Updating","");

        /* Save temperature in C for line 1 of display */
        snprintf(tempStrC, sizeof(tempStrC), "C: %.1f",rx_buffer.temp); /* Format temperature in C for the OLED */

        /* Connect to the server */
        WPRINT_APP_INFO( ( "Connecting to %s\n", SERVER_HOST ) );
        if ( ( result = http_client_connect( &client, (const wiced_ip_address_t*)&ip_address, SERVER_PORT, HTTP_USE_TLS, CONNECT_TIMEOUT_MS ) ) != WICED_SUCCESS )
        {
            WPRINT_APP_INFO( ( "Error: failed to connect to server: %u\n", result) );
            return;
        }
        WPRINT_APP_INFO( ( "Connected\n" ) );

        /* Setup options to send to the server including the value to be converted*/
        sprintf(resourceOptions,"/convert?from-type=C&to-type=F&user-id=wicedwifi101&api-key=kyM2OWa22SZ1B5PGE7DvjSi67sPMXHTNXXENVut8JvmjkjMo&from-value=%.1f", rx_buffer.temp);

        /* Setup the POST request */
        http_request_init( &request, &client, HTTP_POST, resourceOptions, HTTP_1_1 );
        http_request_write_header( &request, &header[0], 1 );
        http_request_write_end_header( &request );
        http_request_flush( &request );

        /* Wait for request to complete */
        wiced_rtos_get_semaphore(&httpWait, WICED_WAIT_FOREVER);

        /* Close connection */
        http_client_disconnect(&client);
    }
}

/* This is the callback event for HTTP events */
static void event_handler( http_client_t* client, http_event_t event, http_response_t* response )
{
    switch( event )
    {
        case HTTP_CONNECTED:
            WPRINT_APP_INFO(( "Connected to %s\n", SERVER_HOST ));
            break;

        case HTTP_DISCONNECTED:
        {
            WPRINT_APP_INFO(( "Disconnected from %s\n", SERVER_HOST ));
            break;
        }
        case HTTP_DATA_RECEIVED:
        {
            WPRINT_APP_INFO( ( "------------------ Received response ------------------\n" ) );

            /* Print Response Header */
            if(response->response_hdr != NULL)
            {
                WPRINT_APP_INFO( ( "----- Response Header: -----\n " ) );
                print_data( (char*) response->response_hdr, response->response_hdr_length );
            }

            /* Print Response Payload  */
            WPRINT_APP_INFO( ("\n----- Response Payload: -----\n" ) );
            print_data( (char*) response->payload, response->payload_data_length );

            if(response->remaining_length == 0)
            {
               WPRINT_APP_INFO( ("\n------------------ End Response ------------------\n" ) );
               http_request_deinit( (http_request_t*) &(response->request) );
               wiced_rtos_set_semaphore(&httpWait); // Set semaphore to flag that this request is done

               /* Parse temperature value in F from JSON (it is returned as a string) and display on OLED*/
               cJSON *root =   cJSON_Parse((const char*)response->payload);
               snprintf(tempStrF, sizeof(tempStrF), "F: %s",cJSON_GetObjectItem(root,"result")->valuestring);
               cJSON_Delete(root);
               update_display(tempStrC, tempStrF);
            }
            break;
        }
        default:
        break;
    }
}

/* Helper function to print data in the response to the UART */
static void print_data( char* data, uint32_t length )
{
    uint32_t a;

    for ( a = 0; a < length; a++ )
    {
        WPRINT_APP_INFO( ( "%c", data[a] ) );
    }
}

/* Helper function to display the temperature on the OLED */
/* The row to show the text at on the display is the argument pos */
static void update_display(char* line1, char* line2)
{
    /* Send data to the display */
    u8g_FirstPage(&display);
    do {
        u8g_DrawStr(&display, 0, 5,  line1);
        u8g_DrawStr(&display, 0, 20,  line2);
    } while (u8g_NextPage(&display));
}
