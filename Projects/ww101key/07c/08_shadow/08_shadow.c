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
#include "mqtt_api.h"
#include "resources.h"
#include "gpio_button.h"
#include "JSON.h"
#include "aws_config.h"
#include "aws_common.h"

/******************************************************
 *                      Macros
 ******************************************************/
#define SHADOW_PUBLISH_MESSAGE_STR_OFF      "{ \"state\": {\"reported\": { \"status\": \"OFF\" } } }"
#define SHADOW_PUBLISH_MESSAGE_STR_ON       "{ \"state\": {\"reported\": { \"status\": \"ON\" } } }"
#define SHADOW_PUBLISH_MESSAGE_STR_OFF_DESIRED_AND_REPORTED     "{ \"state\": {\"desired\": { \"status\": \"OFF\" } ,\"reported\": { \"status\": \"OFF\" } } }"
#define SHADOW_PUBLISH_MESSAGE_STR_ON_DESIRED_AND_REPORTED      "{ \"state\": {\"desired\": { \"status\": \"ON\" } ,\"reported\": { \"status\": \"ON\" } } }"

#define CLIENT_ID                           "KEY_TestThing"

/******************************************************
 *               Variable Definitions
 ******************************************************/
static aws_app_info_t  app_info =
{
    .mqtt_client_id = CLIENT_ID
};

static char* led_status = "OFF";
static char  req_led_status[8] = "OFF";

/******************************************************
 *               Static Function Definitions
 ******************************************************/
static wiced_result_t parse_json_shadow_status(wiced_json_object_t * json_object )
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

/*
 * Call back function to handle connection events.
 */
wiced_result_t mqtt_connection_event_cb( wiced_mqtt_object_t mqtt_object, wiced_mqtt_event_info_t *event )
{
    wiced_result_t ret = WICED_SUCCESS;

    switch ( event->type )
    {
        case WICED_MQTT_EVENT_TYPE_CONNECT_REQ_STATUS:
        case WICED_MQTT_EVENT_TYPE_DISCONNECTED:
        case WICED_MQTT_EVENT_TYPE_PUBLISHED:
        case WICED_MQTT_EVENT_TYPE_SUBCRIBED:
        case WICED_MQTT_EVENT_TYPE_UNSUBSCRIBED:
        {
            app_info.expected_event = event->type;
            wiced_rtos_set_semaphore( &app_info.msg_semaphore );
            break;
        }

        case WICED_MQTT_EVENT_TYPE_PUBLISH_MSG_RECEIVED:
        {
            wiced_mqtt_topic_msg_t msg = event->data.pub_recvd;
            WPRINT_APP_INFO(("Received %.*s  for TOPIC : %.*s\n\n", (int) msg.data_len, msg.data, (int) msg.topic_len, msg.topic));

            ret = wiced_JSON_parser( (const char*)msg.data , msg.data_len );
            if(ret == WICED_SUCCESS)
            {
                WPRINT_APP_INFO(("Requested LED State[%s] Current LED State [%s]\n", req_led_status, led_status));
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
    return WICED_SUCCESS;
}

/*
 * Handles key events
 */
static void setpoint_control_keypad_handler( void *arg )
{
    WPRINT_APP_INFO(("Button is pressed\n"));
    wiced_rtos_set_semaphore( &app_info.wake_semaphore );
}

/******************************************************
 *               Function Definitions
 ******************************************************/
void application_start( void )
{
    wiced_result_t   ret = WICED_SUCCESS;
    int              connection_retries = 0;

    ret = aws_app_init(&app_info);

    wiced_JSON_parser_register_callback(parse_json_shadow_status);

    wiced_gpio_input_irq_enable( WICED_BUTTON1, IRQ_TRIGGER_RISING_EDGE, setpoint_control_keypad_handler, NULL );

    do
    {
        ret = aws_mqtt_conn_open( app_info.mqtt_object, mqtt_connection_event_cb );
        connection_retries++ ;
    } while ( ( ret != WICED_SUCCESS ) && ( connection_retries < WICED_MQTT_CONNECTION_NUMBER_OF_RETRIES ) );

    aws_mqtt_app_publish( app_info.mqtt_object, WICED_MQTT_QOS_DELIVER_AT_LEAST_ONCE, (uint8_t*)app_info.shadow_state_topic, (uint8_t*)SHADOW_PUBLISH_MESSAGE_STR_OFF ,sizeof(SHADOW_PUBLISH_MESSAGE_STR_OFF) );

    wiced_rtos_delay_milliseconds( MQTT_DELAY_IN_MILLISECONDS * 2 );

    aws_mqtt_app_subscribe( app_info.mqtt_object, app_info.shadow_delta_topic , WICED_MQTT_QOS_DELIVER_AT_MOST_ONCE );

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
            aws_mqtt_app_publish( app_info.mqtt_object, WICED_MQTT_QOS_DELIVER_AT_LEAST_ONCE, (uint8_t*)app_info.shadow_state_topic, (uint8_t*)SHADOW_PUBLISH_MESSAGE_STR_OFF_DESIRED_AND_REPORTED ,sizeof(SHADOW_PUBLISH_MESSAGE_STR_OFF_DESIRED_AND_REPORTED) );
        }
        else
        {
            wiced_gpio_output_high( WICED_LED1 );
            led_status = "ON";
            strcpy(req_led_status, led_status);
            aws_mqtt_app_publish( app_info.mqtt_object, WICED_MQTT_QOS_DELIVER_AT_LEAST_ONCE, (uint8_t*)app_info.shadow_state_topic, (uint8_t*)SHADOW_PUBLISH_MESSAGE_STR_ON_DESIRED_AND_REPORTED ,sizeof(SHADOW_PUBLISH_MESSAGE_STR_ON_DESIRED_AND_REPORTED) );
        }
    }

    aws_mqtt_app_unsubscribe( app_info.mqtt_object, app_info.shadow_delta_topic );

    aws_mqtt_conn_close( app_info.mqtt_object );

    wiced_rtos_deinit_semaphore( &app_info.msg_semaphore );
    ret = wiced_mqtt_deinit( app_info.mqtt_object );
    free( app_info.mqtt_object );
    app_info.mqtt_object = NULL;

    return;
}
