// WW101 TCP server that supports multiple connections using  WWEP.
//
// See WW101 lab manual for more information on the custom protocol WWEP.
#include "wiced.h"
#include "linked_list.h" //usr the WICED linked list library (libraries/utilities/linked_list)

#define TCP_SERVER_LISTEN_PORT              (27708)
#define TCP_SERVER_THREAD_PRIORITY          (WICED_DEFAULT_LIBRARY_PRIORITY)
#define TCP_SERVER_STACK_SIZE               (6200)

static wiced_result_t client_connected_callback   ( wiced_tcp_socket_t* socket, void* arg );
static wiced_result_t client_disconnected_callback( wiced_tcp_socket_t* socket, void* arg );
static wiced_result_t received_data_callback      ( wiced_tcp_socket_t* socket, void* arg );

////////////////////// Database  /////////////

/// In order to save space and make the program simpler I have a linked list of all
// of the writes to the database.
//
// Read:
// When I get a read from the remote server I just look  through the linked list to
// find if that has deviceId/regId combination has ever been written and either return it
// or a f
//
// Write:
// When I get a write I look through the linked list to find that deviceId/regId combination
// if yes then overwrite.. otherwise add it to the list
//

// the dbEntry is the structure that is stored in the linked list.
typedef struct dbEntry {
	uint32_t deviceId;
	uint32_t regId;
	uint32_t value;
} dbEntry_t;

linked_list_t db;

// initialize the database
void dbStart()
{
	linked_list_init(&db);
}

// This function is  used by the linked_list library to compare two dbEntries for equality
// e.g when searching for a dbEntry
wiced_bool_t dbCompare( linked_list_node_t* node_to_compare, void* user_data )
{
	// if the device ID and regId are the same
	dbEntry_t *p1 = (dbEntry_t *)node_to_compare->data;
	dbEntry_t *p2 = (dbEntry_t *)user_data;

	if(p1->deviceId == p2->deviceId && p1->regId == p2->regId)
		return WICED_TRUE;
	return WICED_FALSE;
}

// dbFind:
// Search the database for specific deviceId/regId combination
dbEntry_t *dbFind(dbEntry_t *find)
{
	linked_list_node_t *found;

	if(linked_list_find_node( &db, dbCompare, (void*) find, &found ) == WICED_SUCCESS)
		return found->data;
	else
		return NULL;
}

// dbSetValue
// searches the database and newValue is not found then it inserts it or
// overwrite the value
//
void dbSetValue(dbEntry_t *newValue)
{
	dbEntry_t *found = dbFind(newValue);
	if(found) // if it is already in the database
	{
		found->value = newValue->value;
	}
	else // add it to the linked list
	{
		linked_list_node_t *newNode = (linked_list_node_t *)malloc(sizeof(linked_list_node_t));
		newNode->data = newValue;
		linked_list_insert_node_at_front( &db, newNode );
	}
}

//////////////// End of Database ////////////////

// Globals for the tcp/ip communication system
static void tcp_server_thread_main(uint32_t arg);
static wiced_thread_t      tcp_thread;

static const wiced_ip_setting_t ip_settings =
{
		INITIALISER_IPV4_ADDRESS( .ip_address, MAKE_IPV4_ADDRESS( 198,51,  100,  3 ) ),
		INITIALISER_IPV4_ADDRESS( .netmask,    MAKE_IPV4_ADDRESS( 255,255,255,  0 ) ),
		INITIALISER_IPV4_ADDRESS( .gateway,    MAKE_IPV4_ADDRESS( 198,51,  100,  1 ) ),
};

// To make things easier while I was on the airplane I setup the server so that it
// could be a soft AP or a station. If it is set up as a station, then the server will
// connect to an existing wireless access point using client settings from the wifi_config_dct.h
// file. If it is NOT setup as a station, then the server starts an access point using the soft AP
// settings from the wifi_config_dct.h file.
//
#define USE_STA 		(0)
#define USE_AP  		(1)
#define USE_ETHERNET 	(2)

// The options for the NETWORK_TYPE define are:
//     USE_STA (connect to an existing Wi-Fi access point as specified in the DCT)
//     USE_AP  (create an access point as specified in the DCT that the clients can connect to)
//     USE_ETHERNET (connect via an ethernet cable)
#define NETWORK_TYPE USE_STA

#if (NETWORK_TYPE == USE_STA)
#define INTERFACE WICED_STA_INTERFACE
#define DHCP_MODE WICED_USE_STATIC_IP
#endif

#if (NETWORK_TYPE == USE_AP)
#define INTERFACE WICED_AP_INTERFACE
#define DHCP_MODE WICED_USE_INTERNAL_DHCP_SERVER
#endif

#if (NETWORK_TYPE == USE_ETHERNET)
#define INTERFACE WICED_ETHERNET_INTERFACE
#define DHCP_MODE WICED_USE_STATIC_IP
#endif


// Main application thread which is started by the RTOS after boot
void application_start(void)
{

	wiced_init( );

	wiced_network_up( INTERFACE, DHCP_MODE, &ip_settings );

	// I created all of the server code in a separate thread to make it easier to put the server
	// and client together in one application.

	wiced_rtos_create_thread(&tcp_thread, TCP_SERVER_THREAD_PRIORITY, "Server TCP Server", tcp_server_thread_main, TCP_SERVER_STACK_SIZE, 0);

}

wiced_tcp_server_t tcp_server;

void printStatus()
{

	linked_list_node_t* current;

	linked_list_get_front_node( &tcp_server.socket_list, &current);

	while(current)
	{
		wiced_tcp_socket_t *socket;

		wiced_tcp_server_socket_t *serverSocket;
		serverSocket = (wiced_tcp_server_socket_t *) current->data;
		socket = &serverSocket->socket;


		wiced_socket_state_t ss;
		wiced_tcp_get_socket_state( socket, &ss);

		switch(ss)
		{
		case   WICED_SOCKET_CLOSED: WPRINT_APP_INFO(("Status:closed\n")); break;
		case   WICED_SOCKET_CLOSING: WPRINT_APP_INFO(("Status:closing\n")); break;
		case   WICED_SOCKET_CONNECTING: WPRINT_APP_INFO(("Status:connecting\n")); break;
		case   WICED_SOCKET_CONNECTED: WPRINT_APP_INFO(("Status:connected\n")); break;
		case   WICED_SOCKET_DATA_PENDING: WPRINT_APP_INFO(("Status:data pending\n")); break;
		case   WICED_SOCKET_LISTEN: WPRINT_APP_INFO(("Status:listen\n")); break;
		case   WICED_SOCKET_ERROR: WPRINT_APP_INFO(("Status:error\n")); break;
		}

		current = current->next;
	}

}

static void tcp_server_thread_main(uint32_t arg)
{


	// 5 Connections maximum at a time
	wiced_tcp_server_start(&tcp_server,INTERFACE,TCP_SERVER_LISTEN_PORT,5, client_connected_callback, received_data_callback, client_disconnected_callback, NULL );

	WPRINT_APP_INFO(("IP\t\tPort\tC\tDEVICE\tREGID\tVALUE\tDBSIZE\n"));
	WPRINT_APP_INFO(("----------------------------------------------------------------------\n"));

	char receiveChar;
	uint32_t expected_data_size=1;

	while(1)
	{
		wiced_uart_receive_bytes( STDIO_UART, &receiveChar, &expected_data_size, WICED_NEVER_TIMEOUT );
		switch(receiveChar)
		{
		case 'p':
			printStatus();
			break;
		case '?':
			WPRINT_APP_INFO(("p: print status of server sockets\n"));
			break;

		}
	}
}


static wiced_result_t client_connected_callback( wiced_tcp_socket_t* socket, void* arg )
{

	UNUSED_PARAMETER( arg );

	/* Accept connection request */
	wiced_result_t result;
	result = wiced_tcp_server_accept( 	&tcp_server, socket );
	if( result == WICED_SUCCESS )
	{
		return WICED_SUCCESS;
	}
	return WICED_ERROR;
}

static wiced_result_t client_disconnected_callback( wiced_tcp_socket_t* socket, void* arg )
{
	UNUSED_PARAMETER( arg );

	// There are two ways to get here
	// 1. You disconnected the socket ... then this calls back for you to listen again (WICED_SOCKET_CLOSED)
	// 2. The client disconnected (in which case the state is WICED_SOCKET_CLOSING

	wiced_socket_state_t ss;
	wiced_tcp_get_socket_state( socket, &ss);

 /*
 // Debug prints
    switch(ss)
    {
    case   WICED_SOCKET_CLOSED: WPRINT_APP_INFO(("closed\n")); break;
    case   WICED_SOCKET_CLOSING: WPRINT_APP_INFO(("closing\n")); break;
    case   WICED_SOCKET_CONNECTING: WPRINT_APP_INFO(("connecting\n")); break;
    case   WICED_SOCKET_CONNECTED: WPRINT_APP_INFO(("connected\n")); break;
    case   WICED_SOCKET_DATA_PENDING: WPRINT_APP_INFO(("data pending\n")); break;
    case   WICED_SOCKET_LISTEN: WPRINT_APP_INFO(("listen\n")); break;
    case   WICED_SOCKET_ERROR: WPRINT_APP_INFO(("error\n")); break;
    }
 */


	if(ss == WICED_SOCKET_CLOSED)
	{
		wiced_tcp_listen( socket, TCP_SERVER_LISTEN_PORT );
		return WICED_SUCCESS;
	}

	wiced_tcp_disconnect(socket);

	return WICED_SUCCESS;
}

static wiced_result_t received_data_callback( wiced_tcp_socket_t* socket, void* arg )
{

	uint16_t        request_length;
	uint16_t        available_data_length;
	wiced_packet_t* temp_packet = NULL;

	if(wiced_tcp_receive( socket, &temp_packet, 500 ) == WICED_SUCCESS) // get the data from the client
	{
		char *rbuffer;
		dbEntry_t receive;
		char commandId;
		int err=1;
		char returnMessage[15];

		// get the pointer to the packet sent by the client and the data
		wiced_packet_get_data( temp_packet, 0, (uint8_t**) &rbuffer, &request_length, &available_data_length );
		sscanf(rbuffer,"%c%4x%2x%4x",&commandId,(unsigned int *)&receive.deviceId,(unsigned int *)&receive.regId,(unsigned int *)&receive.value);
		wiced_packet_delete( temp_packet ); // free the packet

		if(request_length >= 11 && request_length <= 13  ) //11 if no end 12 if CR 13 if CRLF
		{
			dbEntry_t *newDbEntry;
			switch(commandId)
			{
			case 'R': // they sent a Read command
				newDbEntry = dbFind(&receive); // look through the database to find a previous write of the deviceId/regId
				if(newDbEntry)
				{
					err=0;
					sprintf(returnMessage,"A%04X%02X%04X",(unsigned int)newDbEntry->deviceId,(unsigned int)newDbEntry->regId,(unsigned int)newDbEntry->value);
				}
				else
					err = 1;
				break;
			case 'W': // they sent a Write command
				sprintf(returnMessage,"A%04X%02X%04X",(unsigned int)receive.deviceId,(unsigned int)receive.regId,(unsigned int)receive.value);
				dbEntry_t *newDB;
				newDB = malloc(sizeof(dbEntry_t)); // make a new entry to put in the database
				memcpy(newDB,&receive,sizeof(dbEntry_t)); // copy the received data into the new entry
				dbSetValue(newDB); // save it.
				err = 0;
				break;
			default: // if they don't send a legal command then it is an error
				err = 1;
				break;
			}
		}

		// Print IP address of the client (peer) that sent the data and print to terminal
		uint32_t 		peerAddressV4;
		peerAddressV4 = (*socket).socket.nx_tcp_socket_connect_ip.nxd_ip_address.v4;
		WPRINT_APP_INFO(("%u.%u.%u.%u\t",
				(uint8_t)(peerAddressV4 >> 24),
				(uint8_t)(peerAddressV4 >> 16),
				(uint8_t)(peerAddressV4 >> 8),
				(uint8_t)(peerAddressV4 >> 0)));

		// Print the port that the peer connected from
		uint16_t	    peerPort;
		peerPort = (*socket).socket.nx_tcp_socket_connect_port;
		WPRINT_APP_INFO(("%d\t",peerPort));

		// Print the data that was sent by the client (peer)
		if(err)
		{
			strcpy(returnMessage,"X");
			WPRINT_APP_INFO(("X length=%d\n",available_data_length));
		}
		else
		{
			uint32_t count;
			linked_list_get_count(&db,&count);
			WPRINT_APP_INFO(("%c\t%4X\t%2X\t%4X\t%d\n",commandId,(unsigned int)receive.deviceId,(unsigned int)receive.regId,(unsigned int)receive.value,(int)count));
		}

		// send response packet
		wiced_packet_t* tx_packet;
		uint8_t *tx_data;
		wiced_packet_create_tcp(socket, strlen(returnMessage), &tx_packet, (uint8_t**)&tx_data, &available_data_length);
		memcpy(tx_data, returnMessage, strlen(returnMessage));
		wiced_packet_set_data_end(tx_packet, (uint8_t*)&tx_data[strlen(returnMessage)]);
		wiced_tcp_send_packet(socket, tx_packet);
		wiced_packet_delete(tx_packet);

		wiced_tcp_server_disconnect_socket(&tcp_server,socket);

	}
	return WICED_SUCCESS;
}

