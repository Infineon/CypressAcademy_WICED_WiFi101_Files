/* Use the cJSON parser library */
#include <wiced.h>
#include <cJSON.h>
#include <stdint.h>

const char *jsonString = "{\"state\" : {\"reported\" : {\"temperature\":25.4} } }";

void application_start()
{
    float temperatureValue;

    cJSON *root =        cJSON_Parse(jsonString);                        // Read the JSON
    cJSON *state =       cJSON_GetObjectItem(root,"state");              // Search for the key "state"
    cJSON *reported =    cJSON_GetObjectItem(state,"reported");          // Search for the key "reported" under state
    cJSON *temperature = cJSON_GetObjectItem(reported,"temperature");    // Search for the key "temperature" under reported

    temperatureValue = temperature->valuedouble; // Get the floating point value associated with the key temperature

    WPRINT_APP_INFO(("Temperature: %.1f\n", temperatureValue)) ; // Print to the screen
}
