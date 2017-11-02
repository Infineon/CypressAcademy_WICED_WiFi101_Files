NAME := App_WW101KEY_08_01_weather_station_mqtt

$(NAME)_SOURCES := 01_weather_station_mqtt.c

$(NAME)_COMPONENTS := graphics/u8g \
		              utilities/cJSON \
                      protocols/MQTT

WIFI_CONFIG_DCT_H := wifi_config_dct.h

$(NAME)_RESOURCES  := apps/aws_iot/rootca.cer \
                      apps/aws_iot/client.cer \
                      apps/aws_iot/privkey.cer
