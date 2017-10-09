/* Use the JSON_parser library */
#include <wiced.h>
#include <JSON.h>
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
// This is the JSON data to parse. Because the 'C' format of the data harder to read with
// the embedded quotes and broken over lines, it is replicated here in the comment to be easier
// to understand.
//
// { "i2cleds" : { "1":"on", "2":"off", "3":"on", "4":"on"}, "gpioleds" : { "1":"off", "2":"on"}}
//
const char json_data[] =
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
// is the address for the sensor hub / psoc AFE
//
const wiced_i2c_device_t dev = {
        .port = WICED_I2C_2,                                // I2C instance 2 on the WICED device platform
        .address = PSOC_SENSOR_HUB_I2C_ADDRESS,             // I2C peripheral address 0x42
        .address_width = I2C_ADDRESS_WIDTH_7BIT,            // I2C we have seven bit addresses
        .speed_mode = I2C_STANDARD_SPEED_MODE               // Traditional (100 Khz) speed
};

//
// The parsed state of the I2C LEDs
//
uint8_t i2cleds = 0 ;

//
// The parsed state of the GPIO LEDs
//
uint8_t gpioleds = 0 ;

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

//
// This function is called during the parsing of the JSON text.  It is called when a
// complete item is parsed.
//
wiced_result_t jsonCallback(wiced_json_object_t *obj_p)
{
    //
    // This conditional ensures that
    // - We have a parent object
    // - The length of the name of the parent object is 7
    // - The parent object is named i2cleds
    // - The current object name length is one character
    // - The current object type is JSON_STRING_TYPE
    // - The current object value is "on"
    //
    if (obj_p->parent_object != NULL && obj_p->parent_object->object_string_length == 7 &&
            strncmp(obj_p->parent_object->object_string, "i2cleds", 7) == 0 &&
            obj_p->object_string_length == 1 && obj_p->value_type == JSON_STRING_TYPE && strncmp(obj_p->value, "on", 2) == 0)
    {
        switch(obj_p->object_string[0])
        {
        case '1':
            i2cleds |= 0x01 ;
            break ;

        case '2':
            i2cleds |= 0x02 ;
            break ;

        case '3':
            i2cleds |= 0x04 ;
            break ;

        case '4':
            i2cleds |= 0x08 ;
            break ;
        }
    }
    //
    // This conditional ensures that
    // - We have a parent object
    // - The length of the name of the parent object is 8
    // - The parent object is named gpioleds
    // - The current object name length is one character
    // - The current object type is JSON_STRING_TYPE
    // - The current object value is "on"
    //
    else if (obj_p->parent_object != NULL && obj_p->parent_object->object_string_length == 8 &&
            strncmp(obj_p->parent_object->object_string, "gpioleds", 8) == 0 &&
            obj_p->object_string_length == 1 && obj_p->value_type == JSON_STRING_TYPE && strncmp(obj_p->value, "on", 2) == 0)
    {
        switch(obj_p->object_string[0])
        {
        case '1':
            gpioleds |= 0x01 ;
            break ;

        case '2':
            gpioleds |= 0x02 ;
            break ;
        }
    }
    return WICED_SUCCESS ;
}

void application_start()
{
    wiced_result_t result ;

    //
    // Print a message about starting and show what we are actually parsing
    //
    WPRINT_APP_INFO(("Starting JSON_parser Example Program\n")) ;
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
    wiced_JSON_parser_register_callback(jsonCallback) ;
    result = wiced_JSON_parser(json_data, sizeof(json_data) - 1) ;
    if (result != WICED_SUCCESS)
    {
        //
        // If this message is printed, you are playing with the JSON data and have
        // changed it in a way that makes the data invalid and therefore it cannot be parsed.
        //
        WPRINT_APP_INFO(("The JSON was not valid and could not be parsed.\n")) ;
    }
    else
    {
        setI2Cleds(i2cleds) ;
        setGPIOleds(gpioleds) ;
    }
}
