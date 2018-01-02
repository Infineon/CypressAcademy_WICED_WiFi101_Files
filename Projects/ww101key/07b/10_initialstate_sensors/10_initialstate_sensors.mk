NAME := apps_ww101key_07b_10_initialstate_sensors

$(NAME)_SOURCES    := 10_initialstate_sensors.c

$(NAME)_COMPONENTS := protocols/HTTP_client

WIFI_CONFIG_DCT_H := wifi_config_dct.h