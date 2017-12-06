/* Connect to AWS and get a thing's shadow */
#include <stdlib.h>
#include "wiced.h"
#include "wiced_tls.h"
#include "http_client.h"
#include "resources.h"

/* The thing to get the shadow from */
#define THING "ww101_00"
/* The resource (URI) to get the shadow from */
#define RESOURCE_PATH "/things/"THING"/shadow"

#define SERVER_HOST        "amk6m51qrxr2u.iot.us-east-1.amazonaws.com"

/* AWS uses 8443 instead of 443 for HTTPS to IoT devices */
#define SERVER_PORT        ( 8443 )
#define DNS_TIMEOUT_MS     ( 10000 )
#define CONNECT_TIMEOUT_MS ( 3000 )
#define TOTAL_REQUESTS     ( 5 )

static void  event_handler( http_client_t* client, http_event_t event, http_response_t* response );
static void  print_data   ( char* data, uint32_t length );

static wiced_semaphore_t httpWait;

static http_client_t  client;
static http_request_t requests[TOTAL_REQUESTS];
static http_client_configuration_info_t client_configuration;
static wiced_tls_identity_t tls_identity;

/******************************************************
 *               Function Definitions
 ******************************************************/

void application_start( void )
{
    uint32_t        size_root = 0;
    uint32_t        size_cert = 0;
    uint32_t        size_key  = 0;

    wiced_ip_address_t  ip_address;
    wiced_result_t      result;
    // We need three headers - host, content type, and content length
    http_header_field_t header[3];

    /* Header 0 is the Host header */
    header[0].field        = HTTP_HEADER_HOST;
    header[0].field_length = strlen( HTTP_HEADER_HOST );
    header[0].value        = SERVER_HOST;
    header[0].value_length = strlen( SERVER_HOST );

    /* Header 1 is the content type (JSON) */
    header[1].field        =  "connection: ";
    header[1].field_length = strlen( "connection: " );
    header[1].value        = "close";
    header[1].value_length = strlen( "close" );

    /* Header 3 is the application content length. This will be set when we need it later since it changes. */

    wiced_init( );

    wiced_rtos_init_semaphore(&httpWait);

    wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);

    WPRINT_APP_INFO( ( "Resolving IP address of %s\n", SERVER_HOST ) );
    wiced_hostname_lookup( SERVER_HOST, &ip_address, DNS_TIMEOUT_MS, WICED_STA_INTERFACE );
    WPRINT_APP_INFO( ( "%s is at %u.%u.%u.%u\n", SERVER_HOST,
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 24),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 16),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 8),
                                                 (uint8_t)(GET_IPV4_ADDRESS(ip_address) >> 0) ) );

    /* Initialize the root CA certificate */
    char * root_ca_cert;
    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_rootca_cer, 0, 10000, &size_root, (const void **) &root_ca_cert );
    result = wiced_tls_init_root_ca_certificates( root_ca_cert, size_root );
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "Error: Root CA certificate failed to initialize: %u\n", result) );
        return;
    }

    /* Initialize the local certificate and private key so the AWS server can validate our thing */
    char * client_cert;
    char * client_privkey;
    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_client_cer, 0, 10000, &size_cert, (const void **) &client_cert );
    resource_get_readonly_buffer( &resources_apps_DIR_ww101_DIR_awskeys_DIR_privkey_cer, 0, 10000, &size_key, (const void **) &client_privkey );
    result = wiced_tls_init_identity(&tls_identity, client_privkey, size_key, (const uint8_t*) client_cert, size_cert );
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "Error: Local certificate/key identity failed to initialize: %u\n", result) );
        return;
    }

    http_client_init( &client, WICED_STA_INTERFACE, event_handler, &tls_identity );
    WPRINT_APP_INFO( ( "Connecting to %s\n", SERVER_HOST ) );

    /* configure HTTP client parameters */
    client_configuration.flag = (http_client_configuration_flags_t)(HTTP_CLIENT_CONFIG_FLAG_SERVER_NAME | HTTP_CLIENT_CONFIG_FLAG_MAX_FRAGMENT_LEN);
    client_configuration.server_name = (uint8_t*)SERVER_HOST;
    client_configuration.max_fragment_length = TLS_FRAGMENT_LENGTH_1024;
    http_client_configure(&client, &client_configuration);

    /* if you set hostname, library will make sure subject name in the server certificate is matching with host name you are trying to connect. pass NULL if you don't want to enable this check */
    client.peer_cn = NULL;

    if ( ( result = http_client_connect( &client, (const wiced_ip_address_t*)&ip_address, SERVER_PORT, HTTP_USE_TLS, CONNECT_TIMEOUT_MS ) ) != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "Error: failed to connect to server: %u\n", result) );
        return;
    }

    WPRINT_APP_INFO( ( "Connected\n" ) );

    /* Send a GET to thing ww101_00 */
        http_request_init( &requests[0], &client, HTTP_GET, RESOURCE_PATH, HTTP_1_1 );
        http_request_write_header( &requests[0], &header[0], 2 ); // Header 1 is Host
        http_request_write_end_header( &requests[0] );
        http_request_flush( &requests[0] );

        wiced_rtos_get_semaphore(&httpWait, WICED_WAIT_FOREVER); /* Wait for this request to complete before going on */
}

/* This is the callback event for HTTP events */
static void event_handler( http_client_t* client, http_event_t event, http_response_t* response )
{
    static uint8_t count = 1; // Keep track of how many responses we have received

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
            WPRINT_APP_INFO( ( "------------------ Received response: %d ------------------\n", count ) );

            WPRINT_APP_INFO( ( "header len = %d, payload len = %d\n ", response->response_hdr_length, response->payload_data_length ) );


            /* Print Response Header */
            if(response->response_hdr != NULL)
            {
                WPRINT_APP_INFO( ( "----- Response Header: -----\n " ) );
                print_data( (char*) response->response_hdr, response->response_hdr_length );
            }

            /* Print Response Payload  */
            WPRINT_APP_INFO( ("\n----- Response Payload: -----\n" ) );

            // The next line doesn't work because of a WICED bug in determining payload_data_length - it looks for "Content-Length: " (case sensitive) but AWS responds with "content-length: "
            //print_data( (char*) response->payload, response->payload_data_length );
            //Because of the bug, we need to parse the length manually from the header
            http_header_field_t length_header;
            length_header.field        = "content-length: ";
            length_header.field_length = strlen("content-length: ");
            length_header.value        = NULL;
            length_header.value_length = 0;
            http_parse_header( response->response_hdr, response->response_hdr_length, &length_header, 1 ); // Parse headers to get the content length
            print_data( (char*) response->payload, atoi(length_header.value) );

            if(response->remaining_length == 0)
            {
               WPRINT_APP_INFO( ("\n------------------ End Response %d ------------------\n", count ) );
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

