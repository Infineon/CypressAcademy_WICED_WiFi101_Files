/* Connect to httpbin.org using HTTP (non-secure) and send a POST request to /anything */
#include <stdlib.h>
#include "wiced.h"
#include "http_client.h"

#define SERVER_HOST        "httpbin.org"

/* This is the JSON message that we will send */
#define JSON_MSG           "{\"WICED\":\"yes\"}"

/* secure HTTP is port 443 */
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

static char json_len[5]; /* This holds the length of the JSON message as a sting containing the decimal value */

/******************************************************
 *               Function Definitions
 ******************************************************/

void application_start( void )
{
    wiced_ip_address_t  ip_address;
    wiced_result_t      result;

    /* We need three headers - host, content type, and content length */
    http_header_field_t header[3];
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
    /* Header 2 is the content length. In this case, it is the length of the JSON message */
    sprintf(json_len,"%d", strlen(JSON_MSG)); /* Calculate the length of the JSON message and store the value as a string */
    header[2].field        = HTTP_HEADER_CONTENT_LENGTH;
    header[2].field_length = strlen( HTTP_HEADER_CONTENT_LENGTH );
    header[2].value        = json_len; // This holds the length of the JSON message as a sting containing the decimal value
    header[2].value_length = strlen( json_len ); // This is the length of the string that holds the JSON message size. For example, if the JSON is 12 characters, this would be "2" because the string "12" is 2 characters long.

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
    client.peer_cn = NULL; /* If you set hostname, library will make sure subject name in the server certificate is matching with host name you are trying to connect. Pass NULL if you don't want to enable this check */

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

    /* Send a POST to resource /anything */
    http_request_init( &request, &client, HTTP_POST, "/post", HTTP_1_1 );
    http_request_write_header( &request, &header[0], 3 ); // We need 3 headers
    http_request_write_end_header( &request );
    http_request_write( &request, (uint8_t* ) JSON_MSG, strlen(JSON_MSG)); /* Write the content */
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

