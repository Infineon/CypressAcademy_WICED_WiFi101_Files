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
 *
 * A rudimentary AWS IOT Subscriber application which demonstrates how to connect to
 * AWS IOT cloud and subscribe MQTT messages for a given topic.
 * The application uses Wiced AWS library APIs.
 *
 * This application subscribes to topic "WICED_BULB" with Qos0 and sets the LED1 to ON or OFF based on
 * the received message "LIGHT ON" or LIGHT OFF" respectively.
 *
 * The application should provide typical AWS IoT configuration like:
 *  i.   Subscriber's 'Thing' name( Your device's logical name as per AWS IoT console)
 *  ii.  Subscriber's AWS Security credentials - Private Key & Certificate.
 *  iii. AWS IoT Root CA certificate
 *  iv.  Subscriber's AWS endpoint( typically unique for a single account )
 *         For example - "a38tdxxxxxxxx.iot.us-east-1.amazonaws.com"
 *         Note: This app can also be used to connect & subscribe directly to AWS MQTT broker.
 *         For example - "data.iot.us-east-1.amazonaws.com" is valid MQTT Broker URI.
 *         Use this if you don't know your AWS endpoint URI.
 *
 * To run the app, work through the following steps:
 *
 *  1. Modify Wi-Fi configuration settings CLIENT_AP_SSID and CLIENT_AP_PASSPHRASE in wifi_config_dct.h
 *     to match your router settings.
 *
 *  2. Fill the AWS-IoT Thing information(Name, Private-key, Certificate) and
 *     AWS-IoT Endpoint information(AWS Endpoint URI, IP-address, port etc. ).
 *     See wiced_aws.h for more details.
 *
 *     One can also provide Security credentials by copying relevant Client Certificates/Private-Keys/Root-CA
 *     @ resources/apps/aws/iot/ folder. Ensure that latest & valid certificates & private-keys are provided
 *     for this device.
 *
 *  3. Make sure resource entries are added in subscriber.mk file.
 *
 *     $(NAME)_RESOURCES  := apps/aws/iot/rootca.cer \
 *                           apps/aws/iot/subscriber/privkey.cer \
 *                           apps/aws/iot/subscriber/client.cer
 *
 *
 *  4. Build and run this application.
 *
 *  5. Run another application which publishes to the same topic from anywhere but connected to internet.
 *
 *  6. Publish the messages "LIGHT ON" or LIGHT OFF" alternatively from the publisher app and check if
 *     LED1 gets set accordingly.
 *
 */

#include "wiced.h"
#include "wiced_aws.h"
#include "aws_common.h"

#include "resources.h"

/******************************************************
 *                      Macros
 ******************************************************/

#define APPLICATION_DELAY_IN_MILLISECONDS           (1000)

#define SUBSCRIBER_CERTIFICATES_MAX_SIZE            (0x7fffffff)

/* Use these URIs if this subscriber's AWS IoT Endpoint is not known */
#define AWS_IOT_MQTT_BROKER_ADDRESS                 "data.iot.us-east-1.amazonaws.com"
#define AWS_IOT_MQTT_BROKER_PEER_COMMON_NAME        "*.iot.us-east-1.amazonaws.com"

#define WICED_TOPIC                                 "KEY_TestTopic"
#define MSG_ON                                      "LIGHT ON"
#define MSG_OFF                                     "LIGHT OFF"
#define APPLICATION_SUBSCRIBE_RETRY_COUNT           (5)

/******************************************************
 *               Variable Definitions
 ******************************************************/

static wiced_aws_thing_security_info_t my_subscriber_security_creds =
{
    .private_key         = NULL,
    .key_length          = 0,
    .certificate         = NULL,
    .certificate_length  = 0,
};

static wiced_aws_endpoint_info_t my_subscriber_aws_iot_endpoint = {
    .transport           = WICED_AWS_TRANSPORT_MQTT_NATIVE,
    .uri                 = "amk6m51qrxr2u.iot.us-east-1.amazonaws.com",
    .peer_common_name    = NULL,
    .ip_addr             = {0},
    .port                = WICED_AWS_IOT_DEFAULT_MQTT_PORT,
    .root_ca_certificate = NULL,
    .root_ca_length      = 0,
};

static wiced_aws_thing_info_t my_subscriber_aws_config = {
    .name                = "KEY_TestThing_Sub",
    .credentials         = &my_subscriber_security_creds,
};

static wiced_bool_t                          is_connected = WICED_FALSE;
static wiced_bool_t                          is_subscribed = WICED_FALSE;
static wiced_aws_handle_t my_app_aws_handle;

/******************************************************
 *               Static Function Definitions
 ******************************************************/

/*
 * Call back function to handle AWS events.
 */
static void my_subscriber_aws_callback( wiced_aws_handle_t aws, wiced_aws_event_type_t event, wiced_aws_callback_data_t* data )
{
    if( !aws || !data || (aws != my_app_aws_handle) )
    {
        WPRINT_APP_INFO( ("[Application/AWS] Invalid AWS callback params") );
        return;
    }

    switch ( event )
    {
        case WICED_AWS_EVENT_CONNECTED:
        {
            if( data->connection.status == WICED_SUCCESS )
            {
                is_connected = WICED_TRUE;
            }
            break;
        }

        case WICED_AWS_EVENT_DISCONNECTED:
        {
            if( data->disconnection.status == WICED_SUCCESS )
            {
                is_connected = WICED_FALSE;
            }
            break;
        }

        case WICED_AWS_EVENT_PUBLISHED:
        case WICED_AWS_EVENT_SUBSCRIBED:
        case WICED_AWS_EVENT_UNSUBSCRIBED:
            break;
        case WICED_AWS_EVENT_PAYLOAD_RECEIVED:
        {
            WPRINT_APP_INFO( ("[Application/AWS] Payload Received[ Topic: %.*s ]:\n", (int)data->message.topic_length, data->message.topic ) );
            if ( !strncmp( (char*) data->message.data, "LIGHT ON", data->message.data_length ) )
            {
                wiced_gpio_output_high( WICED_LED1 );
                WPRINT_APP_INFO(( "light on\n" ));
            }
            else
            {
                wiced_gpio_output_low( WICED_LED1 );
                WPRINT_APP_INFO(( "light off\n" ));
            }

            break;
        }

        default:
            break;
    }
}

static wiced_result_t get_aws_credentials_from_resources( void )
{
    uint32_t size_out = 0;
    wiced_result_t result = WICED_ERROR;

    wiced_aws_thing_security_info_t* security = &my_subscriber_security_creds;
    uint8_t** root_ca_certificate = &my_subscriber_aws_iot_endpoint.root_ca_certificate;

    if( security->certificate && security->private_key && (*root_ca_certificate) )
    {
        WPRINT_APP_INFO(("\n[Application/AWS] Security Credentials already set(not NULL). Abort Reading from Resources...\n"));
        return WICED_SUCCESS;
    }

    /* Get AWS Root CA certificate filename: 'rootca.cer' located @ resources/apps/aws/iot folder */
    result = resource_get_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_rootca_cer, 0, SUBSCRIBER_CERTIFICATES_MAX_SIZE, &size_out, (const void **) root_ca_certificate);
    if( result != WICED_SUCCESS )
    {
        goto _fail_aws_certificate;
    }
    if( size_out < 64 )
    {
        WPRINT_APP_INFO( ( "\n[Application/AWS] Invalid Root CA Certificate! Replace the dummy certificate with AWS one[<YOUR_WICED_SDK>/resources/app/aws/iot/'rootca.cer']\n\n" ) );
        resource_free_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_rootca_cer, (const void *)*root_ca_certificate );
        goto _fail_aws_certificate;
    }

    my_subscriber_aws_iot_endpoint.root_ca_length = size_out;

    /* Get Subscriber's Certificate filename: 'client.cer' located @ resources/apps/aws/iot/subscriber folder */
    result = resource_get_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_subscriber_DIR_client_cer, 0, SUBSCRIBER_CERTIFICATES_MAX_SIZE, &size_out, (const void **) &security->certificate );
    if( result != WICED_SUCCESS )
    {
        goto _fail_client_certificate;
    }
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\n[Application/AWS] Invalid Device Certificate! Replace the dummy certificate with AWS one[<YOUR_WICED_SDK>/resources/app/aws/iot/subscriber/'client.cer']\n\n" ) );
        resource_free_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_subscriber_DIR_client_cer, (const void *)security->certificate );
        goto _fail_client_certificate;
    }

    security->certificate_length = size_out;

    /* Get Subscriber's Private Key filename: 'privkey.cer' located @ resources/apps/aws/iot/subscriber folder */
    result = resource_get_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_subscriber_DIR_privkey_cer, 0, SUBSCRIBER_CERTIFICATES_MAX_SIZE, &size_out, (const void **) &security->private_key );
    if( result != WICED_SUCCESS )
    {
        goto _fail_private_key;
    }
    if(size_out < 64)
    {
        WPRINT_APP_INFO( ( "\n[Application/AWS] Invalid Device Private-Key! Replace the dummy Private-key with AWS one[<YOUR_WICED_SDK>/resources/app/aws/iot/subscriber/'privkey.cer'\n\n" ) );
        resource_free_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_subscriber_DIR_privkey_cer, (const void *)security->private_key );
        goto _fail_private_key;
    }
    security->key_length = size_out;

    return WICED_SUCCESS;

_fail_private_key:
    resource_free_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_subscriber_DIR_client_cer, (const void *)security->certificate );
_fail_client_certificate:
    resource_free_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_rootca_cer, (const void *)*root_ca_certificate );
_fail_aws_certificate:
    return WICED_ERROR;
}

/******************************************************
 *               Function Definitions
 ******************************************************/

void application_start( void )
{
    int retries = 0;
    wiced_aws_handle_t aws_connection = 0;
    wiced_result_t ret = WICED_SUCCESS;

    wiced_init( );

    /* Bring up the network interface */
    ret = wiced_network_up( WICED_AWS_DEFAULT_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "[Application/AWS] Not able to join the requested AP\n\n" ) );
        return;
    }

    ret = get_aws_credentials_from_resources();
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ("[Application/AWS] Error fetching credentials from resources\n" ) );
        return;
    }

    ret = wiced_aws_init( &my_subscriber_aws_config, my_subscriber_aws_callback );
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "[Application/AWS] Failed to Initialize AWS library\n" ) );
        return;
    }

    aws_connection = (wiced_aws_handle_t)wiced_aws_create_endpoint(&my_subscriber_aws_iot_endpoint);
    if( !aws_connection )
    {
        WPRINT_APP_INFO( ( "[Application/AWS] Failed to create AWS connection handle\n" ) );
        return;
    }

    my_app_aws_handle = aws_connection;

    WPRINT_APP_INFO(("[Application/AWS] Opening connection...\n"));
    ret = wiced_aws_connect(aws_connection);
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("[Application/AWS] Connect Failed\r\n"));
        wiced_rtos_delay_milliseconds( APPLICATION_DELAY_IN_MILLISECONDS * 5 );
        return;
    }

    wiced_rtos_delay_milliseconds(2000);

    WPRINT_APP_INFO(("[Application/AWS] Subscribing[ Topic %.*s ] ...",(int)strlen(WICED_TOPIC), WICED_TOPIC ) );
    do
    {
        ret = wiced_aws_subscribe( aws_connection, WICED_TOPIC, WICED_AWS_QOS_ATMOST_ONCE);
        retries++ ;
    } while ( ( ret != WICED_SUCCESS ) && ( retries < APPLICATION_SUBSCRIBE_RETRY_COUNT ) );
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
        wiced_rtos_delay_milliseconds( APPLICATION_DELAY_IN_MILLISECONDS * 2 );
    }

    WPRINT_APP_INFO(("[Application/AWS] Closing connection...\r\n"));
    wiced_aws_disconnect( aws_connection );

    WPRINT_APP_INFO(("[Application/AWS] Deinitializing AWS library...\r\n"));
    ret = wiced_aws_deinit( );

    return;
}
