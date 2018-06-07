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
 * AWS Thing Shadow Demo Application
 *
 * This application demonstrates how you can publish state from WICED and get sync with AWS thing shadow.
 *
 * To demonstrate the app, work through the following steps.
 *  1. Plug the WICED eval board into your computer
 *  2. Open a terminal application and connect to the WICED eval board
 *  3. Build and download the application (to the WICED board)
 *  4. Connect your computer to the soft AP details displayed on the UART terminal.
 *  5. Type the IP address of soft AP in the browser. Update the thing name & select save settings. Upload client certificate &
 *     private key file. After browsing, click on the Upload Certificate & Upload Key buttons.
 *  6. In Wi-Fi setup window, select the Wi-Fi network & give the password & select connect.
 *
 * Once WICED device boots up it will publish current state of LIGHT (LED) that will be OFF on topic
 * $shadow/beta/state/DEMO. same WICED device subscribe to topic $shadow/beta/sync/DEMO to get the latest
 * state and sync with AWS.
 *
 * NOTE : DEMO is the thing name here created in AWS. You can create new thing and change here.
 *
 * After above steps from AWS CLI you can publish and change the state of LIGHT. You can refer below command as \
 * example.
 *
 * aws iot-data --endpoint-url https://data.iot.us-east-1.amazonaws.comupdate-thing-state update-thing-state --thing-name lightbulb
 * --payload "{ \"state\": {\"desired\": { \"status\": \"ON\" } } }" output.txt && output.txt
 *
 * After successful execution of above commands you can check that LED will be turned on in WICED device and publishes
 * same with reported state.
 *
 * You can also change status of LED manually with push button on WICED and WICED board publishes its new state with
 * $shadow/beta/state/DEMO. but AWS thing shadow gives back delta because reported state is overwritten with desired
 * state and it sends delta back. This is something needs to be debugged in AWS shadow part from Amazon.
 **/

#include "wiced.h"
#include "resources.h"
#include "gpio_button.h"
#include "JSON.h"
#include "device_config.h"
#include "wiced_aws.h"
#include "aws_common.h"

/******************************************************
 *                      Macros
 ******************************************************/

#define SHADOW_PUBLISH_MESSAGE_STR_OFF      "{ \"state\": {\"reported\": { \"status\": \"OFF\" } } }"
#define SHADOW_PUBLISH_MESSAGE_STR_ON       "{ \"state\": {\"reported\": { \"status\": \"ON\" } } }"
#define SHADOW_PUBLISH_MESSAGE_STR_OFF_DESIRED_AND_REPORTED     "{ \"state\": {\"desired\": { \"status\": \"OFF\" } ,\"reported\": { \"status\": \"OFF\" } } }"
#define SHADOW_PUBLISH_MESSAGE_STR_ON_DESIRED_AND_REPORTED      "{ \"state\": {\"desired\": { \"status\": \"ON\" } ,\"reported\": { \"status\": \"ON\" } } }"

#define CLIENT_ID                           "wiced_shadow_aws"

#define AWS_IOT_HOST_NAME                   "data.iot.us-east-1.amazonaws.com"
#define AWS_IOT_PEER_COMMON_NAME            "*.iot.us-east-1.amazonaws.com"

#define APP_DELAY_IN_MILLISECONDS          (1000)
#define SHADOW_CERTIFICATES_MAX_SIZE        (0x7fffffff)
#define THING_STATE_TOPIC_STR_BUILDER       "$aws/things/%s/shadow/update"
#define THING_DELTA_TOPIC_STR_BUILDER       "$aws/things/%s/shadow/update/delta"

#define SHADOW_CONNECTION_NUMBER_OF_RETRIES (3)

/******************************************************
 *               Variable Definitions
 ******************************************************/

typedef struct
{
    wiced_aws_event_type_t    expected_event;
    wiced_semaphore_t         msg_semaphore;
    wiced_semaphore_t         wake_semaphore;
    char                      thing_name[32];
    char                      shadow_state_topic[64];
    char                      shadow_delta_topic[64];
    wiced_aws_handle_t        handle;
} my_shadow_app_info_t;

static my_shadow_app_info_t app_info;

static wiced_aws_thing_security_info_t my_shadow_security_creds =
{
    /* Read security credentials either from DCT or 'resources' */
    .private_key        = NULL,
    .key_length         = 0,
    .certificate        = NULL,
    .certificate_length = 0,
};

static wiced_aws_endpoint_info_t my_shadow_aws_iot_endpoint =
{
    .transport           = WICED_AWS_TRANSPORT_MQTT_NATIVE,

    .uri                 = "a38td4ke8seeky.iot.us-east-1.amazonaws.com",
    .ip_addr             = {0},
    .port                = WICED_AWS_IOT_DEFAULT_MQTT_PORT,
    .root_ca_certificate = NULL,
    .root_ca_length      = 0,
    .peer_common_name    = NULL,
};

static wiced_aws_thing_info_t my_shadow_aws_config = {
    .name            = NULL,
    .credentials     = &my_shadow_security_creds,
};

static char* led_status        = "OFF";
static char  req_led_status[8] = "OFF";

/******************************************************
 *               Static Function Definitions
 ******************************************************/

// static wiced_result_t wait_for_response( wiced_aws_event_type_t event, uint32_t timeout )
// {
//     if ( wiced_rtos_get_semaphore( &app_info.msg_semaphore, timeout ) != WICED_SUCCESS )
//     {
//         return WICED_ERROR;
//     }
//     else
//     {
//         if ( event != app_info.expected_event )
//         {
//             return WICED_ERROR;
//         }
//     }
//     return WICED_SUCCESS;
// }

static wiced_result_t parse_json_shadow_status( wiced_json_object_t * json_object )
{
    if(strncmp(json_object->object_string, "status", sizeof("status")-1) == 0)
    {
        if(json_object->value_length > 0 && json_object->value_length < sizeof(req_led_status)-1)
        {
            memcpy(req_led_status, json_object->value, json_object->value_length);
            req_led_status[json_object->value_length] = '\0';
        }
    }

    return WICED_SUCCESS;
}

static void my_shadow_aws_callback( wiced_aws_handle_t aws, wiced_aws_event_type_t event, wiced_aws_callback_data_t* data )
{
    if( !aws || !data || ( aws != app_info.handle ) )
        return;

    switch ( event )
    {
        case WICED_AWS_EVENT_CONNECTED:
        case WICED_AWS_EVENT_DISCONNECTED:
        case WICED_AWS_EVENT_PUBLISHED:
        case WICED_AWS_EVENT_SUBSCRIBED:
        case WICED_AWS_EVENT_UNSUBSCRIBED:
            break;

        case WICED_AWS_EVENT_PAYLOAD_RECEIVED:
        {
            wiced_result_t ret = WICED_ERROR;
            wiced_aws_callback_message_t* message = NULL;
            if( !data )
                return;

            message = &data->message;

            WPRINT_APP_INFO( ("[Shadow] Received Payload [ Topic: %.*s ] :\n====\n%.*s\n====\n",
                (int) message->topic_length, message->topic, (int) message->data_length, message->data ) );

            ret = wiced_JSON_parser( (const char*)message->data , message->data_length );
            if(ret == WICED_SUCCESS)
            {
                WPRINT_APP_INFO(("[Shadow] LED State %s[current] ---> %s[requested]\n", led_status, req_led_status ));
                if ( strcasecmp( req_led_status, led_status ) == 0 )
                {
                    break;
                }
                else
                {
                    wiced_rtos_set_semaphore( &app_info.wake_semaphore );
                }
            }
            break;
        }
        default:
            break;
    }
}

/*
 * Handles key events
 */
static void setpoint_control_keypad_handler( void *arg )
{
    WPRINT_APP_INFO(("[Shadow] Button is pressed\n"));
    wiced_rtos_set_semaphore( &app_info.wake_semaphore );
}

static wiced_result_t get_aws_credentials_from_dct( void )
{
    wiced_result_t ret  = WICED_ERROR;
    uint32_t size_out   = 0;
    platform_dct_security_t* dct_security = NULL;
    wiced_aws_thing_security_info_t* security = &my_shadow_security_creds;
    uint8_t** root_ca_certificate = &my_shadow_aws_iot_endpoint.root_ca_certificate;

    if( security->certificate && security->private_key &&  (*root_ca_certificate) )
    {
        WPRINT_APP_INFO(("[Shadow] Security Credentials already set(not NULL). Abort Reading from DCT...\n"));
        return WICED_SUCCESS;
    }

    /* Get AWS Root CA certificate filename: 'rootca.cer' located @ resources/apps/aws/iot folder */
    resource_get_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_rootca_cer, 0, SHADOW_CERTIFICATES_MAX_SIZE, &size_out, (const void **)root_ca_certificate);
    if( ret != WICED_SUCCESS )
    {
        return ret;
    }

    if( size_out < 64 )
    {
        WPRINT_APP_INFO( ( "[Shadow] Invalid Root CA Certificate! Replace the dummy certificate with AWS one[<YOUR_WICED_SDK>/resources/app/aws/iot/'rootca.cer']\n\n" ) );
        resource_free_readonly_buffer( &resources_apps_DIR_aws_DIR_iot_DIR_rootca_cer, (const void *)*root_ca_certificate );

       return WICED_ERROR;
    }

    my_shadow_aws_iot_endpoint.root_ca_length = size_out;

    /* Reading Security credentials of the device from DCT section */

    WPRINT_APP_INFO(( "[Shadow] Reading Device's certificate and private key from DCT...\n" ));

    /* Lock the DCT to allow us to access the certificate and key */
    ret = wiced_dct_read_lock( (void**) &dct_security, WICED_FALSE, DCT_SECURITY_SECTION, 0, sizeof( *dct_security ) );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("[Shadow] Unable to lock DCT to read certificate\n"));
        return ret;
    }

    security->certificate           = (uint8_t *)dct_security->certificate;
    security->certificate_length    = strlen( dct_security->certificate );
    security->private_key           = (uint8_t *)dct_security->private_key;
    security->key_length            = strlen( dct_security->private_key );

    /* Finished accessing the certificates */
    wiced_dct_read_unlock( dct_security, WICED_FALSE );

    return WICED_SUCCESS;
}

static wiced_result_t build_shadow_topics(void)
{
    wiced_result_t ret = WICED_ERROR;
    aws_config_dct_t *aws_app_dct = NULL;

    ret = wiced_dct_read_lock( (void**) &aws_app_dct, WICED_FALSE, DCT_APP_SECTION, 0, sizeof( aws_config_dct_t ) );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("[Shadow] Unable to lock DCT to read certificate\n"));
        return ret;
    }

    strncpy( app_info.thing_name, aws_app_dct->thing_name, sizeof(app_info.thing_name) - 1 );
    snprintf( app_info.shadow_state_topic, sizeof(app_info.shadow_state_topic), THING_STATE_TOPIC_STR_BUILDER, app_info.thing_name );
    snprintf( app_info.shadow_delta_topic, sizeof(app_info.shadow_delta_topic), THING_DELTA_TOPIC_STR_BUILDER, app_info.thing_name );

    printf("[Shadow] Thing Name: %s\n", app_info.thing_name );
    printf("[Shadow] Shadow State Topic: %s\n", app_info.shadow_state_topic);
    printf("[Shadow] Shadow Delta Topic: %s\n", app_info.shadow_delta_topic);

    /* Finished accessing the AWS APP DCT */
    ret = wiced_dct_read_unlock( aws_app_dct, WICED_FALSE );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(( "[Shadow] DCT Read Unlock Failed. Error = [%d]\n", ret ));
        return ret;
    }

    return WICED_SUCCESS;
}

/******************************************************
 *               Function Definitions
 ******************************************************/

void application_start( void )
{
    wiced_aws_handle_t aws_shadow_handle;
    wiced_result_t   ret = WICED_SUCCESS;

    wiced_init( );

    /* Checks if User has triggered the DCT reset, if Yes - start the configuration routine */
    ret = aws_configure_device();
    if ( ret != WICED_ALREADY_INITIALIZED )
    {
        WPRINT_APP_INFO(("[Shadow] Restarting the device...\n"));
        wiced_framework_reboot();
        return;
    }

    /* Bringup the network interface */
    wiced_network_up( WICED_STA_INTERFACE, WICED_USE_EXTERNAL_DHCP_SERVER, NULL );

    ret = get_aws_credentials_from_dct();
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("[Shadow] Failed to get Security credentials for AWS IoT\n"));
        return;
    }

    wiced_rtos_init_semaphore( &app_info.msg_semaphore );
    wiced_rtos_init_semaphore( &app_info.wake_semaphore );

    ret = wiced_aws_init( &my_shadow_aws_config, my_shadow_aws_callback );
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("[Shadow] Failed to Initialize Wiced AWS library\n"));
        return;
    }

    ret = build_shadow_topics();
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("[Shadow] Error while building Shadow topics\n"));
        return;
    }

    aws_shadow_handle = (wiced_aws_handle_t)wiced_aws_create_endpoint(&my_shadow_aws_iot_endpoint);
    if( !aws_shadow_handle )
    {
        WPRINT_APP_INFO( ( "[Shadow] Failed to create AWS connection handle\n" ) );
        return;
    }

    app_info.handle = aws_shadow_handle;

    wiced_JSON_parser_register_callback(parse_json_shadow_status);

    wiced_gpio_input_irq_enable( WICED_BUTTON1, IRQ_TRIGGER_RISING_EDGE, setpoint_control_keypad_handler, NULL );

    ret = wiced_aws_connect( aws_shadow_handle );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "[Shadow] Failed to Connect to AWS IoT\n" ) );
        return;
    }

    wiced_rtos_delay_milliseconds( APP_DELAY_IN_MILLISECONDS * 5 );

    ret = wiced_aws_publish( aws_shadow_handle, app_info.shadow_state_topic,
            (uint8_t*)SHADOW_PUBLISH_MESSAGE_STR_OFF ,sizeof(SHADOW_PUBLISH_MESSAGE_STR_OFF), WICED_AWS_QOS_ATLEAST_ONCE );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "[Shadow] Failed to Publish Shadow state message\n" ) );
        return;
    }

    wiced_rtos_delay_milliseconds( APP_DELAY_IN_MILLISECONDS * 2 );

    ret = wiced_aws_subscribe( aws_shadow_handle, app_info.shadow_delta_topic, WICED_AWS_QOS_ATMOST_ONCE );
    if ( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO( ( "[Shadow] Failed to Subscribe to Shadow delta\n" ) );
        return;
    }
    while ( 1 )
    {
        /* Wait forever on wake semaphore until the wake button is pressed */
        wiced_rtos_get_semaphore( &app_info.wake_semaphore, WICED_NEVER_TIMEOUT );

        /* Toggle the LED */
        if ( strcasecmp( led_status, "ON" ) == 0 )
        {
            wiced_gpio_output_low( WICED_LED1 );
            led_status = "OFF";
            strcpy(req_led_status, led_status);
            wiced_aws_publish( aws_shadow_handle, app_info.shadow_state_topic,
                        (uint8_t *)SHADOW_PUBLISH_MESSAGE_STR_OFF_DESIRED_AND_REPORTED, sizeof(SHADOW_PUBLISH_MESSAGE_STR_OFF_DESIRED_AND_REPORTED),
                        WICED_AWS_QOS_ATLEAST_ONCE );
        }
        else
        {
            wiced_gpio_output_high( WICED_LED1 );
            led_status = "ON";
            strcpy(req_led_status, led_status);
            wiced_aws_publish( aws_shadow_handle, app_info.shadow_state_topic, (uint8_t *)SHADOW_PUBLISH_MESSAGE_STR_ON_DESIRED_AND_REPORTED ,
                        sizeof(SHADOW_PUBLISH_MESSAGE_STR_ON_DESIRED_AND_REPORTED), WICED_AWS_QOS_ATLEAST_ONCE );
        }
    }

    wiced_aws_unsubscribe( aws_shadow_handle, app_info.shadow_delta_topic );

    wiced_aws_disconnect( aws_shadow_handle );

    wiced_rtos_deinit_semaphore( &app_info.msg_semaphore );
    wiced_rtos_deinit_semaphore( &app_info.wake_semaphore );

    ret = wiced_aws_deinit( );
    if( ret != WICED_SUCCESS )
    {
        WPRINT_APP_INFO(("[Shadow] Failed to DeInitialize Wiced AWS library\n"));
    }

    app_info.handle = 0;

    return;
}
