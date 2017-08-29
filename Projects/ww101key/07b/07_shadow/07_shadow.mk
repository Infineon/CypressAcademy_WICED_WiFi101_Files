NAME := App_WW101KEY_07b_07_shadow

$(NAME)_SOURCES := 07_shadow.c

$(NAME)_SOURCES += aws_config.c \
                   aws_common.c

$(NAME)_INCLUDES   := .

WIFI_CONFIG_DCT_H := wifi_config_dct.h
APPLICATION_DCT := aws_config_dct.c

#GLOBAL_DEFINES     += USE_HTTPS
ifneq (,$(findstring USE_HTTPS,$(GLOBAL_DEFINES)))
CERTIFICATE := $(SOURCE_ROOT)resources/certificates/brcm_demo_server_cert.cer
PRIVATE_KEY := $(SOURCE_ROOT)resources/certificates/brcm_demo_server_cert_key.key
endif

$(NAME)_COMPONENTS := protocols/MQTT \
                      inputs/gpio_button \
                      libraries/utilities/JSON_parser

$(NAME)_RESOURCES  := apps/aws_iot/rootca.cer

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
                     apps/aws_iot/aws_config.html \
                     config/scan_page_outer.html \
                     styles/buttons.css \
                     styles/border_radius.htc

$(NAME)_COMPONENTS += daemons/HTTP_server \
                      daemons/DNS_redirect \
                      protocols/DNS

