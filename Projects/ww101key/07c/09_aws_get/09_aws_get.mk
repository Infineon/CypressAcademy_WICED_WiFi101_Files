NAME := apps_ww101key_07c_09_aws_get

$(NAME)_SOURCES    := 09_aws_get.c

$(NAME)_COMPONENTS := protocols/HTTP_client

WIFI_CONFIG_DCT_H := wifi_config_dct.h

$(NAME)_RESOURCES  := apps/ww101/awskeys/rootca.cer \
                      apps/ww101/awskeys/client.cer \
                      apps/ww101/awskeys/privkey.cer
