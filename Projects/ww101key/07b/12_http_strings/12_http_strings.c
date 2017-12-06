// Send an HTTP GET to example.com using a socket and streams directly
//
// The message sent and the response from the server are echoed to a UART terminal.
#include "wiced.h"

#define SERVER_NAME     "example.com"
#define SERVER_PORT     (80)

/* Max bytes that we will send/receive to/from example.com */
#define MAX_SEND        (100)
#define MAX_RECEIVE     (2000)

#define TIMEOUT         (500)

void application_start(void)
{
    wiced_result_t      result;
    wiced_ip_address_t  serverAddress;        // address of the HTTP server
    wiced_tcp_socket_t  socket;               // The TCP socket
    wiced_tcp_stream_t  stream;               // The TCP stream

    char message[MAX_SEND];

    wiced_init( );
    wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );

    /* Look up server's address */
    wiced_hostname_lookup( SERVER_NAME, &serverAddress, 5000, WICED_STA_INTERFACE );

    /* Open the connection to the remote server via a socket */
    result = wiced_tcp_create_socket(&socket, WICED_STA_INTERFACE);
    if(result!=WICED_SUCCESS)
    {
        WPRINT_APP_INFO(("Failed to create socket %d\n",result));
        return;
    }

    result = wiced_tcp_bind(&socket,WICED_ANY_PORT);
    if(result!=WICED_SUCCESS)
    {
        WPRINT_APP_INFO(("Failed to bind socket %d\n",result));
        wiced_tcp_delete_socket(&socket);
        return;
    }

    result = wiced_tcp_connect(&socket,&serverAddress,SERVER_PORT,2000); // 2 second timeout
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(( "Failed connect = [%d]\n", result ));
        wiced_tcp_delete_socket(&socket);
        return;
    }

    /* Initialize the TCP stream */
    wiced_tcp_stream_init(&stream, &socket);

    /* Send the HTTP request */
    WPRINT_APP_INFO(("------------------ Client Request -------------------\n"));
    /* Set up and send the start line */
    sprintf(message,"GET / HTTP/1.1\r\n");
    WPRINT_APP_INFO(( "Sending Start Line: %s\n", message ));
    wiced_tcp_stream_write(&stream, message, strlen(message));
    /* Set up and send the header */
    sprintf(message,"Host: %s\r\n",SERVER_NAME);
    WPRINT_APP_INFO(( "Sending Header: %s\n", message ));
    wiced_tcp_stream_write(&stream, message, strlen(message));
    /* Send the header end line */
    sprintf(message,"\r\n");
    wiced_tcp_stream_write(&stream, message, strlen(message));
    WPRINT_APP_INFO(("---------------- End Client Request -----------------\n"));

    /* Force the data to be sent right away even if the packet isn't full yet */
    wiced_tcp_stream_flush(&stream);

    /* Get the response back from the HTTP server */
    char rbuffer[MAX_RECEIVE+1] = {0}; // Initialize with 0's so that we have 0 termination after the data read back
    result = wiced_tcp_stream_read(&stream, rbuffer, MAX_RECEIVE, TIMEOUT); // Read bytes from the buffer - wait up to 500ms for a response
    if((result == WICED_SUCCESS) || (result == WICED_TIMEOUT)) // We need the timeout since we won't get the max number of bytes
    {
        /* Print the response data */
        WPRINT_APP_INFO(("------------------ Server Response ------------------\n"));
        WPRINT_APP_INFO(("%s",rbuffer));
        WPRINT_APP_INFO(("--------------- End of Server Response --------------\n"));
    }
    else
    {
        WPRINT_APP_INFO(("Malformed response\n"));
    }

    /*  Delete the stream and socket */
    wiced_tcp_stream_deinit(&stream);
    wiced_tcp_delete_socket(&socket);
}
