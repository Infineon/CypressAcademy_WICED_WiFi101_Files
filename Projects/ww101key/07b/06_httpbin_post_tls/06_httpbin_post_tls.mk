NAME := apps_ww101key_07b_06_httpbin_post_tls

$(NAME)_SOURCES    := 06_httpbin_post_tls.c

$(NAME)_COMPONENTS := protocols/HTTP_client

WIFI_CONFIG_DCT_H := wifi_config_dct.h

$(NAME)_RESOURCES  := apps/ww101/httpbin/ca_certificate.cer