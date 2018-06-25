// Client to send information to a server using a custom protocol (WWEP).
// See the WW101 lab manual for more information on WWEP.
//
// The message sent and the response from the server are echoed to a UART terminal.
//
// This version can use either the unsecured port or the secure TLS port
#include "wiced.h"
#include "wiced_tls.h"


#define TCP_CLIENT_STACK_SIZE 	(16384)
#define SECURE_SERVER_PORT 		(40508)
#define SERVER_PORT             (27708)


static wiced_ip_address_t serverAddress; 		// address of the WWEP server
static wiced_semaphore_t button1_semaphore;     // Semaphore unlocks sending of data after button presses
static wiced_semaphore_t button2_semaphore;     // Semaphore unlocks sending of data after button presses

static wiced_thread_t button1Thread;
static wiced_thread_t button2Thread;
static uint16_t myDeviceId; 					// A checksum of the MAC address
static wiced_mac_t myMac;

// This function is called by the RTOS when the button is pressed
// It just unlocks the button thread semaphore
void button1_isr(void *arg)
{
    wiced_rtos_set_semaphore(&button1_semaphore);
}

// This function is called by the RTOS when the button is pressed
// It just unlocks the button thread semaphore
void button2_isr(void *arg)
{
    wiced_rtos_set_semaphore(&button2_semaphore);
}



// sendDataSecure:
// This function opens a TLS socket connection to the WWEP server
// then sends the state of the LED and gets the response
// The input data is 0=Off, 1=On
void sendDataSecure(int data)
{
    wiced_tcp_socket_t socket;                      // The TCP socket
    wiced_tls_context_t tls_context;
    platform_dct_security_t* dct_security = NULL;

    wiced_tcp_stream_t stream;						// The TCP stream
    char sendMessage[12];
    wiced_result_t result;

    // Open the connection to the remote server via a socket
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

    /* Lock the DCT to allow us to access the certificate and key */
     result = wiced_dct_read_lock( (void**) &dct_security, WICED_FALSE, DCT_SECURITY_SECTION, 0, sizeof( *dct_security ) );
     if ( result != WICED_SUCCESS )
     {
         WPRINT_APP_INFO(("Unable to lock DCT to read certificate\n"));
         wiced_tcp_delete_socket(&socket);
         return;
     }

     result = wiced_tls_init_root_ca_certificates( dct_security->certificate, strlen( dct_security->certificate ) );
     if ( result != WICED_SUCCESS )
     {
         WPRINT_APP_INFO(( "Unable to initialize Root Certificate = [%d]\n", result ));
         wiced_tcp_delete_socket(&socket);
         return;
     }

     result = wiced_tls_init_context( &tls_context, NULL, NULL );
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(( "Unable to initialize Context. Error = [%d]\n", result ));
        wiced_tcp_delete_socket(&socket);
        return;
    }

    result =  wiced_tcp_enable_tls( &socket, &tls_context );
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(( "Start TLS Failed. Error = [%d]\n", result ));
        wiced_tls_deinit_context(&tls_context);
        wiced_tcp_delete_socket(&socket);
        return;
    }

    result = wiced_tcp_connect(&socket,&serverAddress,SECURE_SERVER_PORT,2000); // 2 second timeout
    if ( result != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(( "Failed connect = [%d]\n", result ));
        wiced_tls_deinit_context(&tls_context);
        wiced_tcp_delete_socket(&socket);
        return;
    }

    // Format the data per the specification in section 6
    sprintf(sendMessage,"W%04X%02X%04X",myDeviceId,5,data); // 5 is the register from the lab manual
    WPRINT_APP_INFO(("Sent Secure Message=%s\n",sendMessage)); // echo the message so that the user can see something

    // Initialize the TCP stream
    wiced_tcp_stream_init(&stream, &socket);

    // Send the data via the stream
    wiced_tcp_stream_write(&stream, sendMessage, strlen(sendMessage));
    // Force the data to be sent right away even if the packet isn't full yet
    wiced_tcp_stream_flush(&stream);

    // Get the response back from the WWEP server
    char rbuffer[12] = {0}; // The first 11 bytes of the buffer will be sent by the server. Byte 12 will stay 0 to null terminate the string
    result = wiced_tcp_stream_read(&stream, rbuffer, 11, 500); // Read 11 bytes from the buffer - wait up to 500ms for a response
    if(result == WICED_SUCCESS)
    {
        WPRINT_APP_INFO(("Server Response=%s\n",rbuffer));
    }
    else
    {
        WPRINT_APP_INFO(("Malformed response\n"));
    }

    // Delete the stream and socket
    wiced_tls_deinit_context(&tls_context);
    wiced_tcp_stream_deinit(&stream);
    wiced_tcp_delete_socket(&socket);
}


// sendData:
// This function opens a socket connection to the WWEP server
// then sends the state of the LED and gets the response
// The input data is 0=Off, 1=On
void sendData(int data)
{
    wiced_tcp_socket_t socket;                      // The TCP socket

    wiced_tcp_stream_t stream;                      // The TCP stream
    char sendMessage[12];
    wiced_result_t result;

    // Open the connection to the remote server via a socket
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

    // Format the data per the specification in section 6
    sprintf(sendMessage,"W%04X%02X%04X",myDeviceId,5,data); // 5 is the register from the lab manual
    WPRINT_APP_INFO(("Sent Message=%s\n",sendMessage)); // echo the message so that the user can see something

    // Initialize the TCP stream
    wiced_tcp_stream_init(&stream, &socket);

    // Send the data via the stream
    wiced_tcp_stream_write(&stream, sendMessage, strlen(sendMessage));
    // Force the data to be sent right away even if the packet isn't full yet
    wiced_tcp_stream_flush(&stream);

    // Get the response back from the WWEP server
    char rbuffer[12] = {0}; // The first 11 bytes of the buffer will be sent by the server. Byte 12 will stay 0 to null terminate the string
    result = wiced_tcp_stream_read(&stream, rbuffer, 11, 500); // Read 11 bytes from the buffer - wait up to 500ms for a response
    if(result == WICED_SUCCESS)
    {
        WPRINT_APP_INFO(("Server Response=%s\n",rbuffer));
    }
    else
    {
        WPRINT_APP_INFO(("Malformed response\n"));
    }

    // Delete the stream and socket
    wiced_tcp_stream_deinit(&stream);
    wiced_tcp_delete_socket(&socket);
}


// buttonThreadMain:
// This function is the thread that waits for button presses and then sends the
// data via the sendData function
//
// This is done as a separate thread to make the code easier to copy to a later program.
void button1ThreadMain()
{
    // Setup the Semaphore and Button Interrupt
    wiced_rtos_init_semaphore(&button1_semaphore); // the semaphore unlocks when the user presses the button
    wiced_gpio_input_irq_enable(WICED_BUTTON1, IRQ_TRIGGER_FALLING_EDGE, button1_isr, NULL); // call the ISR when the button is pressed

    WPRINT_APP_INFO(("Starting Button 1 Loop\n"));

    // Main Loop: wait for semaphore.. then send the data
    while(1)
    {
        wiced_rtos_get_semaphore(&button1_semaphore,WICED_WAIT_FOREVER);
        wiced_gpio_output_low( WICED_LED1 );
        sendData(0);
        wiced_rtos_get_semaphore(&button1_semaphore,WICED_WAIT_FOREVER);
        sendData(1);
        wiced_gpio_output_high( WICED_LED1 );
    }
}

// buttonThreadMain:
// This function is the thread that waits for button presses and then sends the
// data via the sendData function
//
// This is done as a separate thread to make the code easier to copy to a later program.
void button2ThreadMain()
{
    // Setup the Semaphore and Button Interrupt
    wiced_rtos_init_semaphore(&button2_semaphore); // the semaphore unlocks when the user presses the button
    wiced_gpio_input_irq_enable(WICED_BUTTON2, IRQ_TRIGGER_FALLING_EDGE, button2_isr, NULL); // call the ISR when the button is pressed

    WPRINT_APP_INFO(("Starting Button 2 Loop\n"));

    // Main Loop: wait for semaphore.. then send the data
    while(1)
    {
        wiced_rtos_get_semaphore(&button2_semaphore,WICED_WAIT_FOREVER);
        wiced_gpio_output_low( WICED_LED2 );
        sendDataSecure(0);
        wiced_rtos_get_semaphore(&button2_semaphore,WICED_WAIT_FOREVER);
        sendDataSecure(1);
        wiced_gpio_output_high( WICED_LED2 );
    }
}

void application_start(void)
{

    wiced_result_t result;
    wiced_init( );
    wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );


    // Find the MAC address and calculate 16 bit checksum
     wiced_wifi_get_mac_address(&myMac);
     myDeviceId = myMac.octet[0] + myMac.octet[1] + myMac.octet[2] + myMac.octet[3] + myMac.octet[4] + myMac.octet[5];

     // Use DNS to find the address.. if you can't look it up after 5 seconds then hard code it.
     WPRINT_APP_INFO(("DNS Lookup wwep.ww101.cypress.com\n"));
     result = wiced_hostname_lookup( "wwep.ww101.cypress.com", &serverAddress, 5000, WICED_STA_INTERFACE );
     //result = WICED_ERROR;

     if ( result == WICED_ERROR || serverAddress.ip.v4 == 0 )
     {
         WPRINT_APP_INFO(("Error in resolving DNS using hard coded address\n"));
         SET_IPV4_ADDRESS( serverAddress, MAKE_IPV4_ADDRESS( 198,51,  100,  3 ) );
     }
     else
     {
         WPRINT_APP_INFO(("wwep.ww101.cypress.com IP : %u.%u.%u.%u\n\n", (uint8_t)(GET_IPV4_ADDRESS(serverAddress) >> 24),
                 (uint8_t)(GET_IPV4_ADDRESS(serverAddress) >> 16),
                 (uint8_t)(GET_IPV4_ADDRESS(serverAddress) >> 8),
                 (uint8_t)(GET_IPV4_ADDRESS(serverAddress) >> 0)));
     }

     wiced_rtos_create_thread(&button1Thread, WICED_DEFAULT_LIBRARY_PRIORITY, "Button 1 Thread", button1ThreadMain, TCP_CLIENT_STACK_SIZE, 0);
     wiced_rtos_create_thread(&button2Thread, WICED_DEFAULT_LIBRARY_PRIORITY, "Button 2 Thread", button2ThreadMain, TCP_CLIENT_STACK_SIZE, 0);
}
