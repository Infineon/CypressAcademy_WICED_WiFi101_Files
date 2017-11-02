NAME := App_WW101KEY_08_01_weather_station_http

$(NAME)_SOURCES := 01_weather_station_http.c

$(NAME)_COMPONENTS := graphics/u8g \
		              utilities/cJSON \
                      protocols/HTTP_client

WIFI_CONFIG_DCT_H := wifi_config_dct.h

$(NAME)_RESOURCES  := apps/ww101key/awskeys/rootca.cer \
                      apps/ww101key/awskeys/client.cer \
                      apps/ww101key/awskeys/privkey.cer
