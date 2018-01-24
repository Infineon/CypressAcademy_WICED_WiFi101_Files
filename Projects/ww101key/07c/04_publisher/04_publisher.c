/*
 * Broadcom Proprietary and Confidential. Copyright 2016 Broadcom
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 */

/** @file
 *
 * A rudimentary AWS IOT publisher application which demonstrates how to connect to
 * AWS IOT cloud (MQTT Broker) and publish MQTT messages for a given topic.
 *
 * This application publishes "LIGHT ON" or "LIGHT OFF" message for each button press
 * to topic "WICED_BULB" with QOS1. Button used is WICED_BUTTON1 on the WICED board.
 * If we press the button first time it publishes "LIGHT OFF" and if we press next the same button
 * it will publish "LIGHT ON".
 *
 * To run the app, work through the following steps.
 *  1. Modify Wi-Fi configuration settings CLIENT_AP_SSID and CLIENT_AP_PASSPHRASE in wifi_config_dct.h to match your router settings.
 *  2. Update the AWS MQTT broker address (MQTT_BROKER_ADDRESS) if needed.
 *  3. Make sure AWS Root Certifcate 'resources/apps/aws_iot/rootca.cer' is up to date while building the app.
 *  4. Copy client certificate and private key for the given AWS IOT user in resources/apps/aws_iot folder.
 *     Ensure that valid client certificates and private keys are provided for the AWS IOT user in resources/apps/aws_iot folder.
 *  5. Build and run this application.
 *  6. Run another application which subscribes to the same topic.
 *  7. Press button WICED_BUTTON1 to publish the messages "LIGHT ON" or LIGHT OFF" alternatively and check if
 *     it reaches subscriber app running on the other WICED board (which can be anywhere but connected to internet)
 *
 */

#include "wiced.h"
#include "mqtt_api.h"
#include "resources.h"

/******************************************************
 *                      Macros
 ******************************************************/
#define MQTT_BROKER_ADDRESS                 "amk6m51qrxr2u.iot.us-east-1.amazonaws.com"
//#define MQTT_BROKER_ADDRESS                 "data.iot.us-east-1.amazonaws.com"

#define WICED_TOPIC                         "GJL_TestTopic"
#define CLIENT_ID                           "wiced_publisher_aws_GJL"
#define MQTT_REQUEST_TIMEOUT                (5000)
#define MQTT_DELAY_IN_MILLISECONDS          (1000)
#define MQTT_MAX_RESOURCE_SIZE              (0x7fffffff)
#define MQTT_PUBLISH_RETRY_COUNT            (3)
#define MSG_ON                              "LIGHT ON"
#define MSG_OFF                             "LIGHT OFF"

/******************************************************
 *               Variable Definitions
 ******************************************************/
static wiced_ip_address_t                   broker_address;
static wiced_mqtt_event_type_t              expected_event;
static wiced_semaphore_t                    msg_semaphore;
static wiced_semaphore_t                    wake_semaphore;
static wiced_mqtt_security_t                security;
static uint8_t                              pub_in_progress = 0;

/******************************************************
 *               Static Function Definitions
 ******************************************************/
static void publish_callback( void* arg )
{
    if(pub_in_progress == 0)
    {
        pub_in_progress = 1;
        wiced_rtos_set_semaphore( &wake_semaphore );
    }
}

/*
 * A blocking call to an expected event.
 */
static wiced_result_t wait_for_response( wiced_mqtt_event_type_t event, uint32_t timeout )
{
    if ( wiced_rtos_get_semaphore( &msg_semaphore, timeout ) != WICED_SUCCESS )
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
    conninfo.peer_cn = (uint8_t*) "*.iot.us-east-1.amazonaws.com";
    ret = wiced_mqtt_connect( mqtt_obj, address, interface, callback, security, &conninfo );
    if ( ret != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}

/*
 * Publish (send) message to WICED_TOPIC and wait for 5 seconds to receive a PUBCOMP (as it is QoS=2).
 */
static wiced_result_t mqtt_app_publish( wiced_mqtt_object_t mqtt_obj, uint8_t qos, uint8_t *topic, uint8_t *data, uint32_t data_len )
{
    wiced_mqtt_msgid_t pktid;

    pktid = wiced_mqtt_publish( mqtt_obj, topic, data, data_len, qos );

    if ( pktid == 0 )
    {
        return WICED_ERROR;
    }

    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_PUBLISHED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
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
    if ( wait_for_response( WICED_MQTT_EVENT_TYPE_DISCONNECTED, MQTT_REQUEST_TIMEOUT ) != WICED_SUCCESS )
    {
        return WICED_ERROR;
    }
    return WICED_SUCCESS;
}

/*
 * Call back function to handle connection events.
 */
static wiced_result_t mqtt_connection_event_cb( wiced_mqtt_object_t mqtt_object, wiced_mqtt_event_info_t *event )
{
    switch ( event->type )
    {
        case WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS:
        case WICED_MQTT_EVENT_TYPE_DISCONNECTED:
        case WICED_MQTT_EVENT_TYPE_PUBLISHED:
        case WICED_MQTT_EVENT_TYPE_SUBCRIBED:
        case WICED_MQTT_EVENT_TYPE_UNSUBSCRIBED:
        {
            expected_event = event->type;
            wiced_rtos_set_semaphore( &msg_semaphore );
        }
            break;
        case WICED_MQTT_EVENT_TYPE_PUBLISH_MSG_RECEIVED:
        default:
            break;
    }
    return WICED_SUCCESS;
}

/******************************************************
 *               Function Definitions
 ******************************************************/
void application_start( void )
{
    wiced_mqtt_object_t   mqtt_object;
    wiced_result_t        ret = WICED_SUCCESS;
    uint32_t              size_out = 0;
    int                   connection_retries = 0;
    int                   retries = 0;
    int                   count = 0;
    char*                 msg = MSG_OFF;

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

    /* Bring up the network interface */
    ret = wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );
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

    WPRINT_APP_INFO( ( "Resolving IP address of MQTT broker...\n" ) );
    ret = wiced_hostname_lookup( MQTT_BROKER_ADDRESS, &broker_address, 10000 , WICED_STA_INTERFACE);
    WPRINT_APP_INFO(("Resolved Broker IP: %u.%u.%u.%u\n\n", (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 24),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 16),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 8),
                    (uint8_t)(GET_IPV4_ADDRESS(broker_address) >> 0)));
    if ( ret == WICED_ERROR || broker_address.ip.v4 == 0 )
    {
        WPRINT_APP_INFO(("Error in resolving DNS\n"));
        return;
    }

    wiced_rtos_init_semaphore( &wake_semaphore );
    wiced_mqtt_init( mqtt_object );
    wiced_rtos_init_semaphore( &msg_semaphore );

    do
    {
        WPRINT_APP_INFO(("[MQTT] Opening connection..."));
        do
        {
            ret = mqtt_conn_open( mqtt_object, &broker_address, WICED_STA_INTERFACE, mqtt_connection_event_cb, &security );
            connection_retries++ ;
        } while ( ( ret != WICED_SUCCESS ) && ( connection_retries < WICED_MQTT_CONNECTION_NUMBER_OF_RETRIES ) );

        if ( ret != WICED_SUCCESS )
        {
            WPRINT_APP_INFO(("Failed\n"));
            break;
        }
        WPRINT_APP_INFO(("Success\n"));
        /* configure push button to publish a message */
        wiced_gpio_input_irq_enable( WICED_BUTTON1, IRQ_TRIGGER_RISING_EDGE, publish_callback, NULL );

        while ( 1 )
        {
            wiced_rtos_get_semaphore( &wake_semaphore, WICED_NEVER_TIMEOUT );
            if ( pub_in_progress == 1 )
            {
                WPRINT_APP_INFO(("[MQTT] Publishing..."));
                if ( count % 2 )
                {
                    msg = MSG_ON;
                }
                else
                {
                    msg = MSG_OFF;
                }
                retries = 0; // reset retries to 0 before going into the loop so that the next publish after a failure will still work
                do
                {
                    ret = mqtt_app_publish( mqtt_object, WICED_MQTT_QOS_DELIVER_AT_LEAST_ONCE, (uint8_t*) WICED_TOPIC, (uint8_t*) msg, strlen( msg ) );
                    retries++ ;
                } while ( ( ret != WICED_SUCCESS ) && ( retries < MQTT_PUBLISH_RETRY_COUNT ) );
                if ( ret != WICED_SUCCESS )
                {
                    WPRINT_APP_INFO((" Failed\n"));
                    break;
                }
                else
                {
                    WPRINT_APP_INFO((" Success\n"));
                }

                pub_in_progress = 0;
                count++ ;
            }

            wiced_rtos_delay_milliseconds( 100 );
        }

        pub_in_progress = 0; // Reset flag if we got a failure so that another button push is needed after a failre

        WPRINT_APP_INFO(("[MQTT] Closing connection..."));
        mqtt_conn_close( mqtt_object );

        wiced_rtos_delay_milliseconds( MQTT_DELAY_IN_MILLISECONDS * 2 );
    } while ( 1 );

    wiced_rtos_deinit_semaphore( &msg_semaphore );
    WPRINT_APP_INFO(("[MQTT] Deinit connection...\n"));
    ret = wiced_mqtt_deinit( mqtt_object );
    wiced_rtos_deinit_semaphore( &wake_semaphore );
    free( mqtt_object );
    mqtt_object = NULL;

    return;
}
