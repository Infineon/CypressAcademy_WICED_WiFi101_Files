/* Connect to httpbin.org and send GET requests to /anything and /html */
#include <stdlib.h>
#include "wiced.h"
#include "http_client.h"

#define SERVER_HOST        "httpbin.org"

/* non-secure HTTP is port 80 */
#define SERVER_PORT        ( 80 )
#define DNS_TIMEOUT_MS     ( 10000 )
#define CONNECT_TIMEOUT_MS ( 3000 )

static void  event_handler( http_client_t* client, http_event_t event, http_response_t* response );
static void  print_data   ( char* data, uint32_t length );

static wiced_semaphore_t httpWait;

static http_client_t  client;
static http_request_t request;
static http_client_configuration_info_t client_configuration;
static wiced_bool_t connected = WICED_FALSE;

/******************************************************
 *               Function Definitions
 ******************************************************/

void application_start( void )
{
    wiced_ip_address_t  ip_address;
    wiced_result_t      result;
    /* We need only 1 header to specify the host */
    http_header_field_t header[1];

    /* Header 0 is the Host header */
    header[0].field        = HTTP_HEADER_HOST;
    header[0].field_length = strlen( HTTP_HEADER_HOST );
    header[0].value        = SERVER_HOST;
    header[0].value_length = strlen( SERVER_HOST );

    wiced_init( );

    /* This semaphore will be used to wait for one request to finish before re-initializing and starting the next one */
    wiced_rtos_init_semaphore(&httpWait);

    wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);

    WPRINT_APP_INFO( ( "Resolving IP address of %s\n", SERVER_HOST ) );
    wiced_hostname_lookup( SERVER_HOST, &ip_address, DNS_TIMEOUT_MS, WICED_STA_INTERFACE );
    WPRINT_APP_INFO( ( "%s is at %u.%u.%u.%u\n", SERVER_HOST,
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 24),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 16),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 8),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 0) ) );

    /* Configure HTTP client parameters */
    client_configuration.flag = (http_client_configuration_flags_t)(HTTP_CLIENT_CONFIG_FLAG_SERVER_NAME | HTTP_CLIENT_CONFIG_FLAG_MAX_FRAGMENT_LEN);
    client_configuration.server_name = (uint8_t*)SERVER_HOST;

    /* Initialize and configure client */
    http_client_init( &client, WICED_STA_INTERFACE, event_handler, NULL );
    http_client_configure(&client, &client_configuration);

    /* Connect to the server */
    if ( ( result = http_client_connect( &client, (const wiced_ip_address_t*)&ip_address, SERVER_PORT, HTTP_NO_SECURITY, CONNECT_TIMEOUT_MS ) ) == WICED_SUCCESS )
    {
        connected = WICED_TRUE;
        WPRINT_APP_INFO( ( "Connected to %s\n", SERVER_HOST ) );
    }
    else
    {
        WPRINT_APP_INFO( ( "Error: failed to connect to server: %u\n", result) );
        return; /* Connection failed - exit program */
    }

    /* Send a GET to resource /html */
    http_request_init( &request, &client, HTTP_GET, "/html", HTTP_1_1 );
    http_request_write_header( &request, &header[0], 1 ); // Header 1 is Host
    http_request_write_end_header( &request );
    http_request_flush( &request );

    wiced_rtos_get_semaphore(&httpWait, WICED_WAIT_FOREVER); /* Wait for this request to complete before going on */

    /* Uncomment this section if you want to wait until the server disconnects before going on */
//    int loop = 0;
//    while(connected)
//    {
//        WPRINT_APP_INFO( ( "Still connected after %d sec\n", loop ) );
//        wiced_rtos_delay_milliseconds(1000);
//        loop ++;
//    }

    /* Check to see if the server has disconnected. If so, we need to re-connect */
    /* Usually the server won't disconnect between requests since the requests happen quickly. */
    if(!connected)
    {
        if ( ( result = http_client_connect( &client, (const wiced_ip_address_t*)&ip_address, SERVER_PORT, HTTP_NO_SECURITY, CONNECT_TIMEOUT_MS ) ) == WICED_SUCCESS )
        {
            connected = WICED_TRUE;
            WPRINT_APP_INFO( ( "Connected to %s\n", SERVER_HOST ) );
        }
        else
        {
            WPRINT_APP_INFO( ( "Error: failed to connect to server: %u\n", result) );
            return; /* Connection failed - exit program */
        }
    }

    /* Send a GET to resource /anything */
    /* We will re-use the same request structure from the last request */
    http_request_init( &request, &client, HTTP_GET, "/anything", HTTP_1_1 );
    http_request_write_header( &request, &header[0], 1 ); // Header 1 is Host
    http_request_write_end_header( &request );
    http_request_flush( &request );

    wiced_rtos_get_semaphore(&httpWait, WICED_WAIT_FOREVER); /* Wait for this request to complete before going on */
    /* Disconnect from the server and deinit since we are done now */
    http_client_disconnect( &client );
    http_client_deinit( &client );
}


/* This is the callback event for HTTP events */
static void event_handler( http_client_t* client, http_event_t event, http_response_t* response )
{
    static uint8_t count = 1; // Keep track of how many responses we have received

    switch( event )
    {
        case HTTP_CONNECTED:
            /* This state is never called */
            break;

        /* This is called when we are disconnected by the server */
        case HTTP_DISCONNECTED:
        {
            connected = WICED_FALSE;
            http_client_disconnect( client ); /* Need to keep client connection state synchronized with the server */
            WPRINT_APP_INFO(( "Disconnected from %s\n", SERVER_HOST ));
            break;
        }

        /* This is called when new data is received (header, or payload) */
        case HTTP_DATA_RECEIVED:
        {
            WPRINT_APP_INFO( ( "------------------ Received response: %d ------------------\n", count ) );

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
               WPRINT_APP_INFO( ("\n------------------ End Response %d ------------------\n", count ) );
               http_request_deinit( (http_request_t*) &(response->request) );
               wiced_rtos_set_semaphore(&httpWait); // Set semaphore that allows the next request to start
               count++;
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

