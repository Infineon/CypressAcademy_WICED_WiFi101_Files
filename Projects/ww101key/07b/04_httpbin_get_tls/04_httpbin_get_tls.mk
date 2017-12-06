NAME := apps_ww101key_07c_04_httpbin_get_tls

$(NAME)_SOURCES    := 04_httpbin_get_tls.c

$(NAME)_COMPONENTS := protocols/HTTP_client

WIFI_CONFIG_DCT_H := wifi_config_dct.h

CERTIFICATE := $(SOURCE_ROOT)resources/apps/ww101/httpbin/ca_certificate.cer
