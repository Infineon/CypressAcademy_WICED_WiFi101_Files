NAME := apps_ww101key_07c_07_webapi_CtoF

$(NAME)_SOURCES    := 07_webapi_CtoF.c

$(NAME)_COMPONENTS := protocols/HTTP_client \
                      utilities/cJSON \
                      graphics/u8g
                      
WIFI_CONFIG_DCT_H := wifi_config_dct.h
