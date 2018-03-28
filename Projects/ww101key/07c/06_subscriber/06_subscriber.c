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

#include "wiced.h"
#include "mqtt_api.h"
#include "resources.h"

/** @file
 * A rudimentary AWS IOT subscriber application which demonstrates how to connect to AWS IOT cloud
 * (MQTT Broker) and subscribe for a given topic and receive the published messages.
 *
 * This application subscribes to topic "WICED_BULB" with Qos0 and sets the LED1 to ON or OFF based on
 * the received message "LIGHT ON" or LIGHT OFF" respectively.
 *
 * To run the app, work through the following steps.
 *  1. Modify Wi-Fi configuration settings CLIENT_AP_SSID and CLIENT_AP_PASSPHRASE in wifi_config_dct.h to match your router settings.
 *  2. Update the AWS MQTT broker address (MQTT_BROKER_ADDRESS) if needed.
 *  3. Make sure AWS Root Certifcate 'resources/apps/aws_iot/rootca.cer' is up to date while building the app.
 *  4. Copy client certificate and private key for the given AWS IOT user in resources/apps/aws_iot folder.
 *     Ensure that valid client certificates and private keys are provided for the AWS IOT user in resources/apps/aws_iot folder.
 *  5. Build and run this application.
 *  6. Run another application which publishes to the same topic from anywhere but connected to internet.
 *  7. Publish the messages "LIGHT ON" or LIGHT OFF" alternatively from the publisher app and check if
 *     LED1 gets set accordingly.
 */

/******************************************************
 *                      Macros
 ******************************************************/
#define MQTT_BROKER_ADDRESS                 "amk6m51qrxr2u.iot.us-east-1.amazonaws.com"
#define MQTT_BROKER_PEER_COMMON_NAME        "*.iot.us-east-1.amazonaws.com"
#define WICED_TOPIC                         "KEY_TestTopic"
/* The CLIENT_ID is the AWS Thing Name */
#define CLIENT_ID                           "KEY_TestThing"
#define MQTT_REQUEST_TIMEOUT                (5000)
#define MQTT_DELAY_IN_MILLISECONDS          (1000)
#define MQTT_MAX_RESOURCE_SIZE              (0x7fffffff)
#define MQTT_SUBSCRIBE_RETRY_COUNT          (3)

/******************************************************
 *               Variable Definitions
 ******************************************************/
static wiced_ip_address_t                    broker_address;
static wiced_mqtt_event_type_t               expected_event;
static wiced_semaphore_t                     semaphore;
static wiced_mqtt_security_t                 security;
static wiced_bool_t                          is_connected = WICED_FALSE;
static wiced_bool_t                          is_subscribed = WICED_FALSE;

/******************************************************
 *               Static Function Definitions
 ******************************************************/
/*
 * Call back function to handle connection events.
 */
static wiced_result_t mqtt_connection_event_cb( wiced_mqtt_object_t mqtt_object, wiced_mqtt_event_info_t *event )
{
    switch ( event->type )
    {
        case WICED_MQTT_EVENT_TYPE_DISCONNECTED:
            is_connected = WICED_FALSE;
        case WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS:
        case WICED_MQTT_EVENT_TYPE_PUBLISHED:
        case WICED_MQTT_EVENT_TYPE_SUBCRIBED:
        case WICED_MQTT_EVENT_TYPE_UNSUBSCRIBED:
        {
            expected_event = event->type;
            wiced_rtos_set_semaphore( &semaphore );
        }
            break;
        case WICED_MQTT_EVENT_TYPE_PUBLISH_MSG_RECEIVED:
        {
            wiced_mqtt_topic_msg_t msg = event->data.pub_recvd;

            if ( !strncmp( (char*) msg.data, "LIGHT ON", msg.data_len ) )
            {
                wiced_gpio_output_high( WICED_LED1 );
                WPRINT_APP_INFO(( "light on\n" ));
            }
            else
            {
                wiced_gpio_output_low( WICED_LED1 );
                WPRINT_APP_INFO(( "light off\n" ));
            }
        }
            break;
        default:
            break;
    }
    return WICED_SUCCESS;
}

/*
 * A blocking call to an expected event.
 */
static wiced_result_t mqtt_wait_for( wiced_mqtt_event_type_t event, uint32_t timeout )
{
    if ( wiced_rtos_get_semaphore( &semaphore, timeout ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    else
    {
        if ( event != expected_event )
        {
            return WICED_ERROR;
        }
    }
    return WICED_SUCCESS;
}

/*
 * Open a connection and wait for MQTT_REQUEST_TIMEOUT period to receive a connection open OK event
 */
static wiced_result_t mqtt_conn_open( wiced_mqtt_object_t mqtt_obj, wiced_ip_address_t *address, wiced_interface_t interface, wiced_mqtt_callback_t callback, wiced_mqtt_security_t *security )
{
    wiced_mqtt_pkt_connect_t conninfo;
    wiced_result_t ret = WICED_SUCCESS;

    memset( &conninfo, 0, sizeof( conninfo ) );
    conninfo.port_number = 0;
    conninfo.mqtt_version = WICED_MQTT_PROTOCOL_VER4;
    conninfo.clean_session = 1;
    conninfo.client_id = (uint8_t*) CLIENT_ID;
    conninfo.keep_alive = 5;
    conninfo.password = NULL;
    conninfo.username = NULL;
    conninfo.peer_cn = (uint8_t*) MQTT_BROKER_PEER_COMMON_NAME;

    ret = wiced_mqtt_connect( mqtt_obj, address, interface, callback, security, &conninfo );
    if ( ret != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    if ( mqtt_wait_for( WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}

/*
 * Close a connection and wait for 5 seconds to receive a connection close OK event
 */
static wiced_result_t mqtt_conn_close( wiced_mqtt_object_t mqtt_obj )
{
    if ( wiced_mqtt_disconnect( mqtt_obj ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    if ( mqtt_wait_for( WICED_MQTT_EVENT_TYPE_DISCONNECTED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}

/*
 * Subscribe to WICED_TOPIC and wait for 5 seconds to receive an ACM.
 */
static wiced_result_t mqtt_app_subscribe( wiced_mqtt_object_t mqtt_obj, char *topic, uint8_t qos )
{
    wiced_mqtt_msgid_t pktid;
    pktid = wiced_mqtt_subscribe( mqtt_obj, topic, qos );
    if ( pktid == 0 )
    {
        return WICED_ERROR;
    }
    if ( mqtt_wait_for( WICED_MQTT_EVENT_TYPE_SUBCRIBED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}

/******************************************************
 *               Function Definitions
 ******************************************************/
void application_start( void )
{
    static wiced_mqtt_object_t   mqtt_object;
    wiced_result_t               ret = WICED_SUCCESS;
    int                          connection_retries = 0;
    int                          retries = 0;
    uint32_t                     size_out = 0;

    wiced_init( );

    /* Get AWS root certificate, client certificate and private key respectively */
    resource_get_readonly_buffer( &resources_apps_DIR_aws_iot_DIR_rootca_cer, 0, MQTT_MAX_RESOURCE_SIZE, &size_out, (const void **) &security.ca_cert );
    security.ca_cert_len = size_out;

    resource_get_readonly_buffer( &resources_apps_DIR_aws_iot_DIR_client_cer, 0, MQTT_MAX_RESOURCE_SIZE, &size_out, (const void **) &security.cert );
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\nNot a valid Certificate! Please replace the dummy certificate file 'resources/app/aws_iot/client.cer' with the one got from AWS\n\n" ) );
        return;
    }
    security.cert_len = size_out;

    resource_get_readonly_buffer( &resources_apps_DIR_aws_iot_DIR_privkey_cer, 0, MQTT_MAX_RESOURCE_SIZE, &size_out, (const void **) &security.key );
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\nNot a valid Private Key! Please replace the dummy private key file 'resources/app/aws_iot/privkey.cer' with the one got from AWS\n\n" ) );
        return;
    }
    security.key_len = size_out;

    /* Disable roaming to other access points */
    wiced_wifi_set_roam_trigger( -99 ); /* -99dBm ie. extremely low signal level */

    /* Bringup the network interface */
    wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "\nNot able to join the requested AP\n\n" ) );
        return;
    }

    /* Allocate memory for MQTT object*/
    mqtt_object = (wiced_mqtt_object_t) malloc( WICED_MQTT_OBJECT_MEMORY_SIZE_REQUIREMENT );
    if ( mqtt_object == NULL )
    {
        WPRINT_APP_ERROR("Dont have memory to allocate for mqtt object...\n");
        return;
    }

    WPRINT_APP_INFO( ( "Resolving IP address of MQTT broker\n" ) );
    ret = wiced_hostname_lookup( MQTT_BROKER_ADDRESS, &broker_address, 10000, WICED_STA_INTERFACE );
    WPRINT_APP_INFO(("Resolved Broker IP: %u.%u.%u.%u ...\n\n", (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 24),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 16),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 8),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 0)));
    if ( ret == WICED_ERROR || broker_address.ip.v4 == 0 )
    {
        WPRINT_APP_INFO(("Error in resolving DNS\n"));
        return;
    }

    wiced_mqtt_init( mqtt_object );
    wiced_rtos_init_semaphore( &semaphore );
    do
    {
        is_connected = WICED_FALSE;
        is_subscribed = WICED_FALSE;
        connection_retries = 0;
        retries = 0;

        WPRINT_APP_INFO(("[MQTT] Opening connection..."));
        do
        {
            ret = mqtt_conn_open( mqtt_object, &broker_address, WICED_STA_INTERFACE, mqtt_connection_event_cb, &security );
            connection_retries++ ;
        } while ( ( ret != WICED_SUCCESS ) && ( connection_retries < WICED_MQTT_CONNECTION_NUMBER_OF_RETRIES ) );
        if ( ret != WICED_SUCCESS )
        {
            WPRINT_APP_INFO((" Failed\n"));
            wiced_rtos_delay_milliseconds( MQTT_DELAY_IN_MILLISECONDS * 5 );
            continue;
        }
        else
        {
            is_connected = WICED_TRUE;
            WPRINT_APP_INFO((" Success\n"));

        }

        WPRINT_APP_INFO(("[MQTT] Subscribing..."));

        do
        {
            ret = mqtt_app_subscribe( mqtt_object, WICED_TOPIC, WICED_MQTT_QOS_DELIVER_AT_MOST_ONCE );
            retries++ ;
        } while ( ( ret != WICED_SUCCESS ) && ( retries < MQTT_SUBSCRIBE_RETRY_COUNT ) );
        if ( ret != WICED_SUCCESS )
        {
            WPRINT_APP_INFO((" Failed\n"));
            is_subscribed = WICED_FALSE;
        }
        else
        {
        WPRINT_APP_INFO(("Success...\n"));
        is_subscribed = WICED_TRUE;
        }
        /* Wait in a loop*/
        while ( 1 )
        {
            if ( is_connected == WICED_FALSE || is_subscribed == WICED_FALSE )
            {
                break;
            }
            wiced_rtos_delay_milliseconds( MQTT_DELAY_IN_MILLISECONDS * 2 );
        }
        WPRINT_APP_INFO(("[MQTT] Closing connection...\n"));
        mqtt_conn_close( mqtt_object );
    } while ( 1 );

    wiced_rtos_deinit_semaphore( &semaphore );
    WPRINT_APP_INFO(("[MQTT] Deinit connection...\n"));
    ret = wiced_mqtt_deinit( mqtt_object );
    free( mqtt_object );
    mqtt_object = NULL;

    return;
}

