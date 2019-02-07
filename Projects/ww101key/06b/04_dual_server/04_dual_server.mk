NAME := App_WW101KEY_06b_04_dual_server

$(NAME)_SOURCES := 04_dual_server.c database.c

#GLOBAL_DEFINES     += RX_PACKET_POOL_SIZE=6
#GLOBAL_DEFINES     += TX_PACKET_POOL_SIZE=6

WIFI_CONFIG_DCT_H := wifi_config_dct.h

CERTIFICATE := $(SOURCE_ROOT)resources/certificates/wwep_cert.pem
PRIVATE_KEY := $(SOURCE_ROOT)resources/certificates/wwep_privkey.pem
