/* Use the JSON_parser library */
#include <wiced.h>
#include <JSON.h>
#include <stdint.h>

const char *jsonString = "{\"state\" : {\"reported\" : {\"temperature\":25.4} } }";

float temperatureValue;
char  temperatureString[10];

/* This function is called during the parsing of the JSON text.  It is called when a
   complete item is parsed. */
wiced_result_t jsonCallback(wiced_json_object_t *obj_p)
{
    /* This conditional ensures that the path is state: reported: temperature and the value is a number */
    if( (obj_p->parent_object != NULL) &&
        (obj_p->parent_object->parent_object != NULL) &&
        (strncmp(obj_p->parent_object->parent_object->object_string, "state", strlen("state")) == 0) &&
        (strncmp(obj_p->parent_object->object_string, "reported", strlen("reported")) == 0) &&
        (strncmp(obj_p->object_string, "temperature", strlen("temperature")) == 0) &&
        (obj_p->value_type == JSON_NUMBER_TYPE)
      )
    {
        snprintf(temperatureString, obj_p->value_length+1, "%s", obj_p->value);
        temperatureValue = atof(temperatureString);
    }
    return WICED_SUCCESS;
}

void application_start()
{
    wiced_JSON_parser_register_callback(jsonCallback);          // Register the callback function
    wiced_JSON_parser(jsonString, strlen(jsonString));          // Send the JSON to be parsed
    WPRINT_APP_INFO(("Temperature: %.1f\n", temperatureValue)); // Print temperature to the screen
}
