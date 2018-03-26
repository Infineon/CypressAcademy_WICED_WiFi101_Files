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
 */

#include "wiced.h"
#include "mqtt_api.h"
#include "resources.h"
#include "gpio_button.h"
#include "aws_config.h"

#pragma once

#ifdef __cplusplus
extern "C" {
#endif


/******************************************************
 *                      Macros
 ******************************************************/

#define AWS_IOT_HOST_NAME                   "amk6m51qrxr2u.iot.us-east-1.amazonaws.com"
#define AWS_IOT_PEER_COMMON_NAME            "*.iot.us-east-1.amazonaws.com"

#define MQTT_REQUEST_TIMEOUT                (5000)
#define MQTT_DELAY_IN_MILLISECONDS          (1000)
#define MQTT_MAX_RESOURCE_SIZE              (0x7fffffff)
#define THING_STATE_TOPIC_STR_BUILDER       "$aws/things/%s/shadow/update"
#define THING_DELTA_TOPIC_STR_BUILDER       "$aws/things/%s/shadow/update/delta"

/******************************************************
 *                    Constants
 ******************************************************/

/******************************************************
 *                   Enumerations
 ******************************************************/

/******************************************************
 *                 Type Definitions
 ******************************************************/

/******************************************************
 *                    Structures
 ******************************************************/

/******************************************************
 *                 Global Variables
 ******************************************************/

typedef struct aws_app_info_s
{
    wiced_mqtt_event_type_t   expected_event;
    wiced_semaphore_t         msg_semaphore;
    wiced_semaphore_t         wake_semaphore;
    char                      thing_name[32];
    char                      shadow_state_topic[64];
    char                      shadow_delta_topic[64];
    char                      mqtt_client_id[64];
    wiced_mqtt_object_t       mqtt_object;
} aws_app_info_t;

/******************************************************
 *               Function Declarations
 ******************************************************/
wiced_result_t aws_app_init( aws_app_info_t *app_info );

/*
 * Publish (send) WICED_MESSAGE_STR to WICED_TOPIC and wait for 5 seconds to receive a PUBCOMP (as it is QoS=2).
 */
wiced_result_t aws_mqtt_app_publish( wiced_mqtt_object_t mqtt_obj, uint8_t qos, uint8_t *topic, uint8_t *data, uint32_t data_len );

/*
 * Open a connection and wait for MQTT_REQUEST_TIMEOUT period to receive a connection open OK event
 */
wiced_result_t aws_mqtt_conn_open( wiced_mqtt_object_t mqtt_obj, wiced_mqtt_callback_t callback );

/*
 * Close a connection and wait for 5 seconds to receive a connection close OK event
 */
wiced_result_t aws_mqtt_conn_close( wiced_mqtt_object_t mqtt_obj );

/*
 * Subscribe to WICED_TOPIC and wait for 5 seconds to receive an ACM.
 */
wiced_result_t aws_mqtt_app_subscribe( wiced_mqtt_object_t mqtt_obj, char *topic, uint8_t qos );

/*
 * Unsubscribe from WICED_TOPIC and wait for 10 seconds to receive an ACM.
 */
wiced_result_t aws_mqtt_app_unsubscribe( wiced_mqtt_object_t mqtt_obj, char *topic );

wiced_result_t mqtt_connection_event_cb( wiced_mqtt_object_t mqtt_object, wiced_mqtt_event_info_t *event );

#ifdef __cplusplus
} /* extern "C" */
#endif
