/* Use the cJSON parser library */
#include <wiced.h>
#include <cJSON.h>
#include <stdint.h>

//
// Defines for the I2C connection
//

//
// This is the 7 bit I2C address for the PSoC based analog front end
// sensor hub
//
#define PSOC_SENSOR_HUB_I2C_ADDRESS             (0x42)

//
// This register is the control register for the LEDs that sit above
// the CapSense buttons.  If this register contains 1, the LEDs are
// controlled by the data registers.  If this register contains 0,
// the LEDs are controlled by the CapSense buttons
//
#define PSOC_SENSOR_HUB_LED_CONTROL             (0x05)

//
// If the control register above contains 1, this register contains the
// state of the LEDs.  Bit zero is the LED state for button 0.  Bit one is the
// LED state for button 1.  Bit two is the LED state for button 2, and bit three
// is the LED state for button 3.
//
#define PSOC_SENSOR_HUB_LED_DATA                (0x04)

//
// This is the JSON data to parse. Because the 'C' format of the data is harder to read with
// the embedded quotes and broken over lines, it is replicated here in the comment to be easier
// to understand.
//
// { "i2cleds" : { "1":"on", "2":"off", "3":"on", "4":"on"}, "gpioleds" : { "1":"off", "2":"on"}}
//
const char *json_data =
        "{"
        "   \"i2cleds\" :"
        "      {"
        "         \"1\": \"on\","
        "         \"2\": \"off\","
        "         \"3\": \"on\","
        "         \"4\": \"off\""
        "       },"
        "   \"gpioleds\" :"
        "      {"
        "        \"1\": \"on\","
        "        \"2\": \"on\""
        "      }"
        "}" ;


//
// Data array for writing EZ-I2C formatted data.  The first byte
// is the address and the second byte is the data.  We initialize
// this array to write a 1 to address 5 which enables I2C register
// control of the LEDs instead of the CapSense button control which
// is default.
//
uint8_t data[] = { PSOC_SENSOR_HUB_LED_CONTROL, 0x01 } ;

//
// The flags for each I2C transaction.  We do simple read or write
// transactions so we just start and stop during each transaction.
//
const uint16_t flags = WICED_I2C_START_FLAG | WICED_I2C_STOP_FLAG ;

//
// The data structure used to define a specific I2C target we are addressing.  This
// is the address for the sensor hub / PSoC AFE
//
const wiced_i2c_device_t dev = {
        .port = WICED_I2C_2,                                // I2C instance 2 on the WICED device platform
        .address = PSOC_SENSOR_HUB_I2C_ADDRESS,             // I2C peripheral address 0x42
        .address_width = I2C_ADDRESS_WIDTH_7BIT,            // I2C we have seven bit addresses
        .speed_mode = I2C_STANDARD_SPEED_MODE               // Traditional (100 Khz) speed
};

//
// Process the JSON objects and extract the information about the state of the
// I2C LEDs.  This information is expected to be stored in an object with the
// tag i2cleds.  Within this object there should be a field per led based on the
// led number.  The value of this field should be "on" if the LED is to be turned on.
// Any other value of this field results in the LED being off.
//
static uint8_t processI2C(cJSON *root_p)
{
    uint8_t ret = 0 ;
    cJSON *i2cobj_p, *led_p ;

    i2cobj_p = cJSON_GetObjectItem(root_p, "i2cleds") ;
    if (i2cobj_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds structure")) ;
        return ret ;
    }

    led_p = cJSON_GetObjectItem(i2cobj_p, "1") ;
    if (led_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds/1 data item")) ;
        return ret ;
    }

    if (strcmp(led_p->valuestring, "on") == 0)
        ret |= 0x01 ;

    led_p = cJSON_GetObjectItem(i2cobj_p, "2") ;
    if (led_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds/1 data item")) ;
        return ret ;
    }

    if (strcmp(led_p->valuestring, "on") == 0)
        ret |= 0x02 ;

    led_p = cJSON_GetObjectItem(i2cobj_p, "3") ;
    if (led_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds/1 data item")) ;
        return ret ;
    }

    if (strcmp(led_p->valuestring, "on") == 0)
        ret |= 0x04 ;

    led_p = cJSON_GetObjectItem(i2cobj_p, "3") ;
    if (led_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds/1 data item")) ;
        return ret ;
    }

    if (strcmp(led_p->valuestring, "on") == 0)
        ret |= 0x08 ;

    return ret ;
}

//
// Process the JSON objects and extract the information about the state of the
// GPIO LEDs.  This information is expected to be stored in an object with the
// tag gpioleds.  Within this object there should be a field per led based on the
// led number.  The value of this field should be "on" if the LED is to be turned on.
// Any other value of this field results in the LED being off.
//
static uint8_t processGPIO(cJSON *root_p)
{
    uint8_t ret = 0 ;
    cJSON *i2cobj_p, *led_p ;

    i2cobj_p = cJSON_GetObjectItem(root_p, "gpioleds") ;
    if (i2cobj_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds structure")) ;
        return ret ;
    }

    led_p = cJSON_GetObjectItem(i2cobj_p, "1") ;
    if (led_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds/1 data item")) ;
        return ret ;
    }

    if (strcmp(led_p->valuestring, "on") == 0)
        ret |= 0x01 ;

    led_p = cJSON_GetObjectItem(i2cobj_p, "2") ;
    if (led_p == NULL)
    {
        WPRINT_APP_INFO(("Invalid JSON Object format - could not find the i2cleds/1 data item")) ;
        return ret ;
    }

    if (strcmp(led_p->valuestring, "on") == 0)
        ret |= 0x02 ;

    return ret ;
}

//
// This function sets the state of the I2C LEDs based on the state value given.  This state
// value is written directly to the I2C LED state register.  Each of the four lower bits
// of this value maps to an LED on the board.
//
static void setI2Cleds(uint8_t state)
{
    //
    // Actually write the ledstate data via I2C to the register (4) that
    // controls the LED lights.
    //
    data[0] = PSOC_SENSOR_HUB_LED_DATA ;
    data[1] = state ;

    if (wiced_i2c_write(&dev, flags, data, sizeof(data)) != WICED_SUCCESS)
    {
        WPRINT_APP_INFO(("I2C Write Failed\n")) ;
    }
}

//
// This function sets the state of the GPIO controlled LEDs based on the state value given.
// The lower two bits of the state control the state of the two LEDs connected to GPIOs.
//
static void setGPIOleds(uint8_t state)
{
    if (state & 0x01)
        wiced_gpio_output_high(WICED_LED1) ;

    if (state & 0x02)
        wiced_gpio_output_high(WICED_LED2) ;
}

void application_start()
{
    uint8_t leds = 0 ;
    cJSON *root  ;

    //
    // Print a message about starting and show what we are actually parsing
    //
    WPRINT_APP_INFO(("Starting cJSON Example Program\n")) ;
    WPRINT_APP_INFO(("Parsing JSON: '%s'\n", json_data)) ;

    //
    // Initialize the I2C port
    //
    wiced_i2c_init(&dev) ;

    //
    // Write the initial I2C data which is basically 0x05, 0x01.  This programs
    // a value of 1 into register 5 which give us I2C register control over the
    // LEDs.
    //
    if (wiced_i2c_write(&dev, flags, data, sizeof(data)) != WICED_SUCCESS)
    {
        WPRINT_APP_INFO(("I2C Write Failed\n")) ;
    }

    //
    // Parse the JSON data
    //
    root = cJSON_Parse(json_data);
    if (root == NULL)
    {
        //
        // If this message is printed, you are playing with the JSON data and have
        // changed it in a way that makes the data invalid and therefore it cannot be parsed.
        //
        WPRINT_APP_INFO(("The JSON was not valid and could not be parsed.\n")) ;
    }
    else
    {
        //
        // Get the data for the i2c LEDS from the JSON
        //
        leds = processI2C(root) ;

        //
        // Set the I2C LEDS based on the data parsed
        //
        setI2Cleds(leds) ;

        //
        // Get the data for the GPIO LEDS from the JSON
        //
        leds = processGPIO(root) ;

        //
        // Set the GPIO LEDS based on the data parsed
        //
        setGPIOleds(leds) ;

    }
}


