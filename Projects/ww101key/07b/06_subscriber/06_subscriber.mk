NAME := App_WW101KEY_07b_06_subcriber

$(NAME)_SOURCES := 06_subscriber.c

$(NAME)_COMPONENTS := protocols/MQTT

WIFI_CONFIG_DCT_H := wifi_config_dct.h

$(NAME)_RESOURCES  := apps/aws_iot/rootca.cer \
                      apps/aws_iot/client_subscriber.cer \
                      apps/aws_iot/privkey_subscriber.cer
