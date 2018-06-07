#
# Copyright 2018, Cypress Semiconductor Corporation or a subsidiary of 
 # Cypress Semiconductor Corporation. All Rights Reserved.
 # This software, including source code, documentation and related
 # materials ("Software"), is owned by Cypress Semiconductor Corporation
 # or one of its subsidiaries ("Cypress") and is protected by and subject to
 # worldwide patent protection (United States and foreign),
 # United States copyright laws and international treaty provisions.
 # Therefore, you may use this Software only as provided in the license
 # agreement accompanying the software package from which you
 # obtained this Software ("EULA").
 # If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 # non-transferable license to copy, modify, and compile the Software
 # source code solely for use in connection with Cypress's
 # integrated circuit products. Any reproduction, modification, translation,
 # compilation, or representation of this Software except as specified
 # above is prohibited without the express written permission of Cypress.
 #
 # Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 # EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 # WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 # reserves the right to make changes to the Software without notice. Cypress
 # does not assume any liability arising out of the application or use of the
 # Software or any product or circuit described in the Software. Cypress does
 # not authorize its products for use in any products where a malfunction or
 # failure of the Cypress product may reasonably be expected to result in
 # significant property damage, injury or death ("High Risk Product"). By
 # including Cypress's product in a High Risk Product, the manufacturer
 # of such system or application assumes all risk of such use and in doing
 # so agrees to indemnify Cypress against all liability.
#

NAME := App_AWS_08_thing_shadow

$(NAME)_SOURCES := 08_thing_shadow.c

$(NAME)_SOURCES += device_config.c

$(NAME)_INCLUDES   := .

#WIFI_CONFIG_DCT_H := wifi_config_dct.h

APPLICATION_DCT   := device_dct_config.c

#GLOBAL_DEFINES     += USE_HTTPS
ifneq (,$(findstring USE_HTTPS,$(GLOBAL_DEFINES)))
CERTIFICATE := $(SOURCE_ROOT)resources/certificates/wiced_demo_server_cert.cer
PRIVATE_KEY := $(SOURCE_ROOT)resources/certificates/wiced_demo_server_cert_key.key
endif

$(NAME)_COMPONENTS := protocols/AWS \
                      inputs/gpio_button

$(NAME)_RESOURCES  := apps/aws/iot/rootca.cer

$(NAME)_RESOURCES += images/cypresslogo.png \
                     images/cypresslogo_line.png \
                     images/favicon.ico \
                     images/scan_icon.png \
                     images/wps_icon.png \
                     images/64_0bars.png \
                     images/64_1bars.png \
                     images/64_2bars.png \
                     images/64_3bars.png \
                     images/64_4bars.png \
                     images/64_5bars.png \
                     images/tick.png \
                     images/cross.png \
                     images/lock.png \
                     images/progress.gif \
                     scripts/general_ajax_script.js \
                     scripts/wpad.dat \
                     apps/aws/iot/aws_config.html \
                     config/scan_page_outer.html \
                     styles/buttons.css \
                     styles/border_radius.htc

$(NAME)_COMPONENTS += daemons/HTTP_server \
                      daemons/DNS_redirect \
                      protocols/DNS

VALID_PLATFORMS := BCM943362WCD4 \
                   BCM943362WCD6 \
                   BCM943362WCD8 \
                   BCM943364WCD1 \
                   CYW94343WWCD1_EVB \
                   BCM943438WCD1 \
                   BCM94343WWCD2 \
                   CY8CKIT_062 \
                   NEB1DX* \
                   CYW943907AEVAL1F \
                   CYW9WCD2REFAD2* \
                   CYW9WCD760PINSDAD2 \
                   WW101_*
