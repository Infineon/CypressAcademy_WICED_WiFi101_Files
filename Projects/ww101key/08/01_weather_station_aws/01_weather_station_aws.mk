NAME := App_WW101KEY_08_01_weather_station_aws

$(NAME)_SOURCES := 01_weather_station_aws.c

$(NAME)_COMPONENTS := graphics/u8g \
		              utilities/cJSON \
                      protocols/AWS

WIFI_CONFIG_DCT_H := wifi_config_dct.h

$(NAME)_RESOURCES  := apps/ww101/awskeys/rootca.cer \
                      apps/ww101/awskeys/client.cer \
                      apps/ww101/awskeys/privkey.cer
