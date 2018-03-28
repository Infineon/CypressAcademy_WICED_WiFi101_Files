/*
 * Copyright 2018, Cypress Semiconductor Corporation or a subsidiary of 
 * Cypress Semiconductor Corporation. All Rights Reserved.
 * 
 * This software, associated documentation and materials ("Software"),
 * is owned by Cypress Semiconductor Corporation
 * or one of its subsidiaries ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products. Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 */

/** @file
 * This file contains common APP and MQTT functionalities which can be used across AWS IOT applications
 */

#include "aws_common.h"

#ifndef WICED_LED2
#define WICED_LED2  WICED_LED1
#endif


static aws_app_info_t *aws_app_info;
static wiced_ip_address_t broker_address;

static wiced_result_t wait_for_response( wiced_mqtt_event_type_t event, uint32_t timeout )
{
    if ( wiced_rtos_get_semaphore( &aws_app_info->msg_semaphore, timeout ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    else
    {
        if ( event != aws_app_info->expected_event )
        {
            return WICED_ERROR;
        }
    }
    return WICED_SUCCESS;
}


/******************************************************
 *               Function Definitions
 ******************************************************/
wiced_result_t aws_app_init( aws_app_info_t *app_info )
{
    wiced_result_t ret = WICED_SUCCESS;
    aws_config_dct_t *aws_app_dct = NULL;

    aws_app_info = app_info;

    wiced_init( );

    /* Disable roaming to other access points */
    wiced_wifi_set_roam_trigger( -99 ); /* -99dBm ie. extremely low signal level */

    WPRINT_APP_INFO((" Please wait, connecting to network...\n"));
    WPRINT_APP_INFO(("(To return to SSID console screen, hold USER switch for 5 seconds during RESET to clear DCT configuration)\n"));
    wiced_rtos_delay_milliseconds( 1000 );

    for ( int i = 0; i < 25; i++ )
    {
        wiced_gpio_output_high( WICED_LED2 );
        wiced_rtos_delay_milliseconds( 100 );
        wiced_gpio_output_low( WICED_LED2 );
        wiced_rtos_delay_milliseconds( 100 );

        if ( !wiced_gpio_input_get( WICED_BUTTON1 ) )
        {
            wiced_rtos_delay_milliseconds( 5000 );

            if ( !wiced_gpio_input_get( WICED_BUTTON1 ) )
            {
                aws_config_dct_t aws_dct =
                {
                    .is_configured = WICED_FALSE,
                    .thing_name = AWS_DEFAULT_THING_NAME
                };

                wiced_gpio_output_high( WICED_LED1 );
                WPRINT_APP_INFO(( "DCT clearing start\n" ));
                wiced_dct_write( &aws_dct, DCT_APP_SECTION, 0, sizeof( aws_config_dct_t ) );
                wiced_rtos_delay_milliseconds( 1000 );
                wiced_gpio_output_low( WICED_LED1 );
                WPRINT_APP_INFO(( "DCT clearing end\n" ));

                break;
            }
        }
    }

    /* Configure the device */
    ret = aws_configure_device();
    if ( ret != WICED_ALREADY_INITIALIZED )
    {
        WPRINT_APP_INFO(("Restarting the device...\n"));
        wiced_framework_reboot();
        return ret;
    }

    /* Bringup the network interface */
    wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );

    wiced_hostname_lookup( AWS_IOT_HOST_NAME, &broker_address, 10000, WICED_STA_INTERFACE );

    WPRINT_APP_INFO(("[MQTT] Connecting to broker %u.%u.%u.%u ...\n\n", (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 24),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 16),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 8),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 0)));

    wiced_rtos_init_semaphore( &aws_app_info->msg_semaphore );
    wiced_rtos_init_semaphore( &aws_app_info->wake_semaphore );

    /* ------------- Read thing-name from DCT ------------- */
    ret = wiced_dct_read_lock( (void**) &aws_app_dct, WICED_FALSE, DCT_APP_SECTION, 0, sizeof( aws_config_dct_t ) );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("Unable to lock DCT to read certificate\n"));
        return ret;
    }

    strncpy(app_info->thing_name, aws_app_dct->thing_name, sizeof(app_info->thing_name)-1);
    snprintf(app_info->shadow_state_topic, sizeof(app_info->shadow_state_topic), THING_STATE_TOPIC_STR_BUILDER, app_info->thing_name);
    snprintf(app_info->shadow_delta_topic, sizeof(app_info->shadow_delta_topic), THING_DELTA_TOPIC_STR_BUILDER, app_info->thing_name);

    printf("Thing Name: %s\n", aws_app_dct->thing_name);
    printf("Shadow State Topic: %s\n", app_info->shadow_state_topic);
    printf("Shadow Delta Topic: %s\n", app_info->shadow_delta_topic);

    /* Finished accessing the AWS APP DCT */
    ret = wiced_dct_read_unlock( aws_app_dct, WICED_FALSE );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(( "DCT Read Unlock Failed. Error = [%d]\n", ret ));
        return ret;
    }
    /* ---------------------------------------------------- */

    /* Allocate memory for MQTT object*/
    app_info->mqtt_object = (wiced_mqtt_object_t) malloc( WICED_MQTT_OBJECT_MEMORY_SIZE_REQUIREMENT );
    if ( app_info->mqtt_object == NULL )
    {
        WPRINT_APP_ERROR(("Dont have memory to allocate for mqtt object...\n"));
        return ret;
    }

    wiced_mqtt_init( app_info->mqtt_object );

    return ret;
}

/*
 * Open a connection and wait for MQTT_REQUEST_TIMEOUT period to receive a connection open OK event
 */
wiced_result_t aws_mqtt_conn_open( wiced_mqtt_object_t mqtt_obj, wiced_mqtt_callback_t callback )
{
    wiced_mqtt_pkt_connect_t  conninfo;
    wiced_mqtt_security_t     security;
    wiced_result_t            ret = WICED_SUCCESS;
    platform_dct_security_t*  dct_security = NULL;
    wiced_interface_t         interface = WICED_STA_INTERFACE;
    uint32_t                  size_out;

    /* Read security parameters from DCT */
    resource_get_readonly_buffer( &resources_apps_DIR_aws_iot_DIR_rootca_cer, 0, MQTT_MAX_RESOURCE_SIZE, &size_out, (const void **) &security.ca_cert );
    security.ca_cert_len = size_out;

    /* Lock the DCT to allow us to access the certificate and key */
    WPRINT_APP_INFO(( "Reading the certificate and private key from DCT...\n" ));
    ret = wiced_dct_read_lock( (void**) &dct_security, WICED_FALSE, DCT_SECURITY_SECTION, 0, sizeof( *dct_security ) );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("Unable to lock DCT to read certificate\n"));
        return ret;
    }
    security.cert = dct_security->certificate;
    security.cert_len = strlen( dct_security->certificate );
    security.key = dct_security->private_key;
    security.key_len = strlen( dct_security->private_key );

    memset( &conninfo, 0, sizeof( conninfo ) );
    conninfo.port_number = 0;
    conninfo.mqtt_version = WICED_MQTT_PROTOCOL_VER4;
    conninfo.clean_session = 1;
    conninfo.client_id = (uint8_t*) aws_app_info->mqtt_client_id;
    conninfo.keep_alive = 5;
    conninfo.password = NULL;
    conninfo.username = NULL;
    conninfo.peer_cn = (uint8_t*) AWS_IOT_PEER_COMMON_NAME;

    WPRINT_APP_INFO(("[MQTT] Connecting to MQTT Broker...\n"));
    ret = wiced_mqtt_connect( mqtt_obj, &broker_address, interface, callback, &security, &conninfo );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_ERROR(("[MQTT] Failed to connect MQTT Broker\n"));
        ret = WICED_ERROR;
        goto _AWS_MQTT_CONN_OPEN_END;
    }

    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        WPRINT_APP_ERROR(("[MQTT] Failed to connect MQTT Broker\n"));
        ret = WICED_ERROR;
        goto _AWS_MQTT_CONN_OPEN_END;
    }
    WPRINT_APP_INFO(("[MQTT] Successfully connected MQTT Broker\n"));

_AWS_MQTT_CONN_OPEN_END:

    /* Finished accessing the certificates */
    wiced_dct_read_unlock( dct_security, WICED_FALSE );

    return ret;
}

/*
 * Close a connection and wait for 5 seconds to receive a connection close OK event
 */
wiced_result_t aws_mqtt_conn_close( wiced_mqtt_object_t mqtt_obj )
{
    if ( wiced_mqtt_disconnect( mqtt_obj ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_DISCONNECTED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}

/*
 * Publish (send) WICED_MESSAGE_STR to WICED_TOPIC and wait for 5 seconds to receive a PUBCOMP (as it is QoS=2).
 */
wiced_result_t aws_mqtt_app_publish( wiced_mqtt_object_t mqtt_obj, uint8_t qos, uint8_t *topic, uint8_t *data, uint32_t data_len )
{
    wiced_mqtt_msgid_t pktid;
    pktid = wiced_mqtt_publish( mqtt_obj, topic, data, data_len, qos );

    if ( pktid == 0 )
    {
        WPRINT_APP_ERROR(("Publish FAILED for topic [%s]\n", topic));
        return WICED_ERROR;
    }

    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_PUBLISHED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        WPRINT_APP_ERROR(("Publish FAILED for topic [%s]\n", topic));
        return WICED_ERROR;
    }
    WPRINT_APP_INFO(("Publish SUCCEEDED for topic [%s]\n", topic));
    return WICED_SUCCESS;
}

/*
 * Subscribe to WICED_TOPIC and wait for 5 seconds to receive an ACM.
 */
wiced_result_t aws_mqtt_app_subscribe( wiced_mqtt_object_t mqtt_obj, char *topic, uint8_t qos )
{
    wiced_mqtt_msgid_t pktid;
    pktid = wiced_mqtt_subscribe( mqtt_obj, topic, qos );
    if ( pktid == 0 )
    {
        WPRINT_APP_ERROR(("Subscribe FAILED for topic [%s]\n", topic));
        return WICED_ERROR;
    }
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_SUBCRIBED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        WPRINT_APP_ERROR(("Subscribe FAILED for topic [%s]\n", topic));
        return WICED_ERROR;
    }
    WPRINT_APP_INFO(("Subscribe SUCCEEDED for topic [%s]\n", topic));
    return WICED_SUCCESS;
}

/*
 * Unsubscribe from WICED_TOPIC and wait for 10 seconds to receive an ACM.
 */
wiced_result_t aws_mqtt_app_unsubscribe( wiced_mqtt_object_t mqtt_obj, char *topic )
{
    wiced_mqtt_msgid_t pktid;
    pktid = wiced_mqtt_unsubscribe( mqtt_obj, topic );

    if ( pktid == 0 )
    {
        return WICED_ERROR;
    }
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_UNSUBSCRIBED, MQTT_REQUEST_TIMEOUT * 2 ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}
