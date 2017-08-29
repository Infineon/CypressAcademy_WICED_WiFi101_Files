// Attach to the standard WPA2 network (WW101WPA).
//
// Sending '0' from a terminal will cause the DCT to be updated to connect to WW101WPA
// Sending '1' from a terminal will cause the DCT to be updated to connect to WW101WPA_SWITCH
// Sending 'p' from a terminal will print the values currently stored in the DCT
//
// The selected network is saved in the DCT (in Flash) so that the choice is
// remembered for successive reboots of the system.
//
#include "wiced.h"

void print_network_info( )
{
	platform_dct_wifi_config_t*  wifi_config;

	// Get a copy of the WIFT config from the DCT into RAM
	wiced_dct_read_lock((void**) &wifi_config, WICED_FALSE, DCT_WIFI_CONFIG_SECTION, 0, sizeof(platform_dct_wifi_config_t));

	// Print info
	WPRINT_APP_INFO(("SSID = %s\n\r",wifi_config->stored_ap_list[0].details.SSID.value));
	WPRINT_APP_INFO(("Security = %d\n\r",wifi_config->stored_ap_list[0].details.security));
	WPRINT_APP_INFO(("Passphrase = %s\n\r",wifi_config->stored_ap_list[0].security_key));

	// Free RAM buffer
	wiced_dct_read_unlock(wifi_config, WICED_FALSE);
}

void update_network_info(char* ssid, char* passphrase, wiced_security_t security)
{
	wiced_result_t res;
	platform_dct_wifi_config_t*  wifi_config;

	// Print requested network
	WPRINT_APP_INFO(("New SSID = %s\n\r",ssid));

	// Take down the network before modifying parameters
	wiced_network_down(WICED_STA_INTERFACE);

	// Get a copy of the WIFT config from the DCT into RAM
	wiced_dct_read_lock((void**) &wifi_config, WICED_TRUE, DCT_WIFI_CONFIG_SECTION, 0, sizeof(platform_dct_wifi_config_t));

	// Update parameters
	strcpy((char *) wifi_config->stored_ap_list[0].details.SSID.value, ssid);
	wifi_config->stored_ap_list[0].details.SSID.length = strlen(ssid);
	strcpy((char *) wifi_config->stored_ap_list[0].security_key, passphrase);
	wifi_config->stored_ap_list[0].security_key_length = strlen(passphrase);
	wifi_config->stored_ap_list[0].details.security = security;

	// Write updated parameters to the DCT
	res = wiced_dct_write((const void *) wifi_config, DCT_WIFI_CONFIG_SECTION, 0, sizeof(platform_dct_wifi_config_t));
	if(res == WICED_SUCCESS)
	{
		WPRINT_APP_INFO(("DCT write SUCCEEDED\n\r"));
	}
	else
	{
		WPRINT_APP_INFO(("DCT write FAILED\n\r"));
	}

	// Free RAM buffer
	wiced_dct_read_unlock(wifi_config, WICED_TRUE);

	// Print network info from the DCT
	print_network_info();

	// Restart the network
    wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);
}

void application_start( )
{
    /* Variables */   
	char    receiveChar;
    uint32_t expected_data_size = 1;

    wiced_init();	/* Initialize the WICED device */

    wiced_network_up(WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL);

    while ( 1 )
    {
        if ( wiced_uart_receive_bytes( STDIO_UART, &receiveChar, &expected_data_size, WICED_NEVER_TIMEOUT ) == WICED_SUCCESS )
        {
            switch(receiveChar)
            {
            	case 'p': // Print Network Info
            		print_network_info();
            		break;
            	case '1': // Switch to Alternate Network
            		update_network_info("WW101WPA_SWITCH", "cypresswicedwifi101s", WICED_SECURITY_WPA2_AES_PSK);
            		break;
            	case '0': // Switch to WPA Network
            		update_network_info("WW101WPA", "cypresswicedwifi101", WICED_SECURITY_WPA2_AES_PSK);
            		break;
        	}
        }
    }
}
