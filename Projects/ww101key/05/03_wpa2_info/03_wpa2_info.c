// Attach to a WPA2 network named WW101WPA (this is set in the wifi_config_dct.h file).
//
// Network info is printed to the UART upon connection:
//    local IP address
//    Netmask
//    Gateway IP address
//    IP address for www.cypress.com
//    Local device MAC address
//
// If the connection is successful, the green LED will blink. If not the red LED will blink.
#include "wiced.h"

void printIp(wiced_ip_address_t ipV4address)
{
    WPRINT_APP_INFO(("%d.%d.%d.%d\r\n",
            (int)((ipV4address.ip.v4 >> 24) & 0xFF), (int)((ipV4address.ip.v4 >> 16) & 0xFF),
            (int)((ipV4address.ip.v4 >> 8) & 0xFF),  (int)(ipV4address.ip.v4 & 0xFF)));
}

void application_start( )
{
    /* Variables */   
	wiced_result_t connectResult;
    wiced_bool_t led = WICED_FALSE;
    wiced_ip_address_t ipAddress;
    wiced_mac_t mac;

    wiced_init();	/* Initialize the WICED device */

    connectResult = wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);

    if(connectResult == WICED_SUCCESS)
    {
        /* Turn on LED */
        wiced_gpio_output_high( WICED_LED1 );

        /* Print out network info */
        WPRINT_APP_INFO(("NETWORK INFO\r\n"));
        /* IP address */
        wiced_ip_get_ipv4_address(WICED_STA_INTERFACE, &ipAddress);
        WPRINT_APP_INFO(("IP addr: "));
        printIp(ipAddress);

        /* Netmask */
        wiced_ip_get_netmask(WICED_STA_INTERFACE, &ipAddress);
        WPRINT_APP_INFO(("Netmask: "));
        printIp(ipAddress);

        /* Gateway */
        wiced_ip_get_gateway_address(WICED_STA_INTERFACE, &ipAddress);
        WPRINT_APP_INFO(("Gateway: "));
        printIp(ipAddress);

        /* Cypress.com Address */

        wiced_hostname_lookup("www.cypress.com", &ipAddress, WICED_NEVER_TIMEOUT, WICED_STA_INTERFACE);
        WPRINT_APP_INFO(("Cypress: "));
        printIp(ipAddress);

        /* Device MAC Address */
        wiced_wifi_get_mac_address(&mac);
        WPRINT_APP_INFO(("MAC Address: "));
        WPRINT_APP_INFO(("%X:%X:%X:%X:%X:%X\r\n",
                   mac.octet[0], mac.octet[1], mac.octet[2],
                   mac.octet[3], mac.octet[4], mac.octet[5]));
    }

    while ( 1 )
    {
        /* Blink LED if connection was not successful */
        if (connectResult != WICED_SUCCESS)
        {
            if ( led == WICED_TRUE )
            {
                wiced_gpio_output_low( WICED_LED1 );
                led = WICED_FALSE;
            }
            else
            {
                wiced_gpio_output_high( WICED_LED1 );
                led = WICED_TRUE;
            }
        }

        wiced_rtos_delay_milliseconds(250);
    }
}
