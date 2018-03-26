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

NAME := App_WW101KEY_07c_06_subcriber

$(NAME)_SOURCES := 06_subscriber.c

$(NAME)_COMPONENTS := protocols/MQTT

WIFI_CONFIG_DCT_H := wifi_config_dct.h

$(NAME)_RESOURCES  := apps/aws_iot/rootca.cer \
                      apps/aws_iot/client.cer \
                      apps/aws_iot/privkey.cer

# To support Low memory platforms, disabling components which are not required
GLOBAL_DEFINES += WICED_CONFIG_DISABLE_SSL_SERVER \
                  WICED_CONFIG_DISABLE_DTLS \
                  WICED_CONFIG_DISABLE_ENTERPRISE_SECURITY \
                  WICED_CONFIG_DISABLE_DES \
                  WICED_CONFIG_DISABLE_ADVANCED_SECURITY_CURVES

VALID_PLATFORMS := BCM943362WCD4 \
                   BCM943362WCD6 \
                   BCM943362WCD8 \
                   BCM943364WCD1 \
                   BCM94343WWCD1 \
                   BCM943438WCD1 \
                   BCM94343WWCD2 \
                   CY8CKIT_062 \
                   NEB1DX* \
                   CYW9MCU7X9N364 \
                   CYW943907AEVAL1F \
                   CYW9WCD2REFAD* \
                   WW101_*

ifeq ($(PLATFORM),$(filter $(PLATFORM), CYW9MCU7X9N364))
GLOBAL_DEFINES += PLATFORM_HEAP_SIZE=34*1024
USE_LIBC_PRINTF     := 0
endif
