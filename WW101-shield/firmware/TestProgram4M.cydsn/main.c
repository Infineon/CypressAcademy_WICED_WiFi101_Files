/*
* This is a test project to test the CYCKIT-032 using a CY8CKIT-044 kit as a baseboard.  
*
* There are four display screens tha are used to test various parts of the shield. 
* The user button on the baseboard is used to switch between screens.
*
* On the main screen, each test will show Pass once the test passes. To perform the tests, the
* user must:
*   1. Touch each CapSense button and press each mechanical button.
*   2. Turn the POT across its full range.
*   3. Cover the ALS (block all light) and then shine a light directly on it.
*
* Once all tests pass, the green LED on the baseboard will turn on.
*
* If the user button is held for more than about 3 seconds, the kit goes into a "bootloader mode"
* which stops updating the screen (to prevent I2C collisions) and allows the USB to I2C bridge on
* the baseboard to be used to bootload the PSoC 4 on the shield.
*
* See the CY8CKIT-032 kit guide for details.
*/
#include "project.h"
#include "regmap.h"
#include "u8g2.h"
#include "testlimits.h"
#include <stdio.h>

volatile dataSet_t pafeDataSet;      /* Data from PSoC 4 on shield read over I2C */
#define PSOC_AFE_I2C (0x42) /* I2C address for PSoC 4 on shield */
// How often to update the screen in milliseconds
#define UPDATE_INTERVAL 200

int updateData=0;       /* Flag set by the systick timer ISR */
u8x8_t u8x8;            /* Structure for the OLED display */
int16 A0,A1,A2;         /* ADC values for the 3 ADC channels - Note: The PSoC 4M ADC range is 0-2.048V, so values above 2.048V will be reported as 2.05V */
char buff[64];          /* Global scratch buffer to hold sprintfs values for the OLED */
int bootloaderMode = 0; /* The baseboard button toggles bootloader mode when held for 3 seconds */

int currentScreen=0; // Initialize the displayed screen selection to the main test screen

/* Success = all buttons + dacValue + (potMin <0.2 && potMax > 2.0) + (alsMin < x && alsMax > y) && humidity && temp */
#define SUCCESS_BUTTON_FLAG (1<<0)
#define SUCCESS_DAC_FLAG (1<<1)
#define SUCCESS_POT_FLAG (1<<2)
#define SUCCESS_ALS_FLAG (1<<3)
#define SUCCESS_HUMIDITY_FLAG (1<<4)
#define SUCCESS_TEMP_FLAG (1<<5)

#define SUCCESS_ALL (SUCCESS_BUTTON_FLAG | SUCCESS_DAC_FLAG | SUCCESS_POT_FLAG | SUCCESS_ALS_FLAG | SUCCESS_HUMIDITY_FLAG | SUCCESS_TEMP_FLAG) 
uint32 success = 0;

/* I2C Timeout */
uint32_t timeout = 0;
#define I2C_TIMEOUT (2000)

/*******************************************************************************
* Function Name: uint8_t u8x8_byte_hw_i2c( u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr )
********************************************************************************
*
* Summary:
*  This is a hardware abstraction layer for the u8x8 library
*
*  Return: Status - Return 1 for a valid case, 0 for an invalid case
*
*  Inputs:
*     *u8x8:    Pointer to a OLED structure
*     msg:      The type of I2C message to send (start, send bytes, or stop))
*     arg_int:  The number of bytes to send
*     *arg_ptr: Pointer to the bytes to send
*
*  Note: No error checking is done on the I2C transactions
********************************************************************************/
uint8_t u8x8_byte_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    uint8_t *data;
    switch(msg)
    {
        case U8X8_MSG_BYTE_SEND:    // Send arg_int bytes from arg_ptr
            data = (uint8_t *)arg_ptr;
            while( arg_int > 0 )
            {
                (void)I2C_I2CMasterWriteByte(*data);
  	            data++;
	            arg_int--;
            }                 
            break;     
        case U8X8_MSG_BYTE_INIT: // Using the HW block so you dont need to initialize
            break;
        case U8X8_MSG_BYTE_SET_DC:
            break;
        case U8X8_MSG_BYTE_START_TRANSFER: // Send an I2C start
            (void)I2C_I2CMasterSendStart(u8x8_GetI2CAddress(u8x8)>>1,I2C_I2C_WRITE_XFER_MODE);
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:   // Send an I2C stop
            (void)I2C_I2CMasterSendStop();
            break;
        default:
            return 0;
    }    
    return 1;
}

/*******************************************************************************
* Function Name: uint8_t psoc_gpio_and_delay_cb( u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr )
********************************************************************************
*
* Summary:
*  This is a callback function used by the u8x8 library. It is used to add
*  a delay using the available PSoC delay functions.
*
*  The delay can be a specified number of milliseconds, 10 microseconds, or 100 nanoseconds
*
*  Return: Status - Return 1 for a valid case, 0 for an invalid case
*
*  Inputs:
*     *u8x8:    Unused but requierd since the u8x8 library call provides it
*     msg:      The type of delay (x milliseconds, 10 microseconds, or 100 nanoseconds)
*     arg_int:  The delay in millisconds (for the mmillisecond delay type)
*     *arg_ptr: Unused but requierd since the u8x8 library call provides it
********************************************************************************/
uint8_t psoc_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;
    switch(msg)
    {
        case U8X8_MSG_GPIO_AND_DELAY_INIT: // No initialization required
            break;
        case U8X8_MSG_DELAY_MILLI:
            CyDelay(arg_int);
            break;
        case U8X8_MSG_DELAY_10MICRO:
            CyDelayUs(10);
         break;
        case U8X8_MSG_DELAY_100NANO:
            CyDelayCycles((CYDEV_BCLK__SYSCLK__HZ/1000000) * 100 - 16); //PSoC system reference guide says ~16 extra cycles 
            break;
        /* We only use I2C in HW so nont of these cases are used
         * If you want to use a software interface or have these pins then you 
         * need to read and write them */
        case U8X8_MSG_GPIO_SPI_CLOCK:
        case U8X8_MSG_GPIO_SPI_DATA:
        case U8X8_MSG_GPIO_CS:
        case U8X8_MSG_GPIO_DC:
        case U8X8_MSG_GPIO_RESET:
        //case U8X8_MSG_GPIO_D0: // Same as SPI_CLOCK    
        //case U8X8_MSG_GPIO_D1: // Same as SPI_DATA
        case U8X8_MSG_GPIO_D2:		
        case U8X8_MSG_GPIO_D3:		
        case U8X8_MSG_GPIO_D4:	
        case U8X8_MSG_GPIO_D5:	
        case U8X8_MSG_GPIO_D6:	
        case U8X8_MSG_GPIO_D7:
        case U8X8_MSG_GPIO_E: 	
        case U8X8_MSG_GPIO_I2C_CLOCK:
        case U8X8_MSG_GPIO_I2C_DATA:
            break;
        default:
            return 0;
    }
    return 1;
}

/*******************************************************************************
* Function Name: SystickISR(void)
********************************************************************************
*
* Summary:
*  This is the callback for the SysTick timer. It is called every 1ms. It is used to
*  update the screen every UPDATE_INTERVAL
********************************************************************************/
void sysTickISR(void)
{
    static int counter=0;
    if(counter++ > UPDATE_INTERVAL)
    {
        updateData = 1;
        counter = 0;
    }
}

/*******************************************************************************
* Function Name: displayDataScreen0(void)
********************************************************************************
*
* Summary:
*  This function displays the voltage in mV from the 3 Arduino header pins (A0,A1,A2)
*
*  Note: The ADC range is 0-2.048V, so values above 2.048V will be reported as 2.05V
********************************************************************************/
void displayDataScreen0(void)
{
    sprintf(buff,"A0=%4d mV",ADC_CountsTo_mVolts(0,A0));
    u8x8_DrawString(&u8x8,0,3,buff);
    
    sprintf(buff,"A1=%4d mV",ADC_CountsTo_mVolts(1,A1));
    u8x8_DrawString(&u8x8,0,4,buff);
    
    sprintf(buff,"A2=%4d mV",ADC_CountsTo_mVolts(2,A2));
    u8x8_DrawString(&u8x8,0,5,buff);
}

/*******************************************************************************
* Function Name: displayDataScreen1(void)
********************************************************************************
*
* Summary:
*   This function displays the most recent values I2C Register Map for temperature,
*   humidity, and illumination. In addition it displays the current values from 
*   analog pins A1(DAC) and A2 (Pot) in volts.
*
*   Note: The PSoC 4M ADC range is 0-2.048V, so values for A1 and A2 above 2.048V will be reported as 2.05V
********************************************************************************/
void displayDataScreen1(void)
{
    sprintf(buff,"Temp=%9.1f C",pafeDataSet.temperature);
    u8x8_DrawString(&u8x8,0,2,buff);
    
    sprintf(buff,"Humidity=%5.1f %%",pafeDataSet.humidity);
    u8x8_DrawString(&u8x8,0,3,buff);

    sprintf(buff,"Illum=%6.0f lux",pafeDataSet.illuminance);
    u8x8_DrawString(&u8x8,0,4,buff);
    
    sprintf(buff,"Pot=   %7.2f V",pafeDataSet.potVal);
    u8x8_DrawString(&u8x8,0,5,buff);

    sprintf(buff,"A2=    %7.2f V",(float)ADC_CountsTo_mVolts(2,A2) / 1000.0);
    u8x8_DrawString(&u8x8,0,6,buff);
    
    sprintf(buff,"A1=    %7.2f V",(float)ADC_CountsTo_mVolts(1,A1) /1000.0);
    u8x8_DrawString(&u8x8,0,7,buff);    
}

/*******************************************************************************
* Function Name: displayDataScreen2(void)
********************************************************************************
*
* Summary:
*   This function displays the I2C values for the button and LED registers.
********************************************************************************/
// This function displays the I2C Register map variable from the LED and Buttons
void displayDataScreen2(void)
{
    sprintf(buff,"Buttons=   %02X",pafeDataSet.buttonVal);
    u8x8_DrawString(&u8x8,0,3,buff);
    
    sprintf(buff,"LEDs=      %02X",pafeDataSet.ledVal);
    u8x8_DrawString(&u8x8,0,4,buff);
    
    
    sprintf(buff,"LED Cntrl= %02X",pafeDataSet.ledControl);
    u8x8_DrawString(&u8x8,0,5,buff);    
}

/*******************************************************************************
* Function Name: displayTestScreen(void)
********************************************************************************
*
* Summary:
*   This function displays the test results screen with Pass or Fail for each item.
*
*   Each row will say Fail until the test sucessfully completes. Some of these 
*   require user input such as touching/pressing all buttons, rotating the pot, etc.
*
*   The test limits for each test can be found in testlimits.h.
********************************************************************************/
void displayTestScreen(void)
{
    sprintf(buff,"Buttons  =  %s",((success&SUCCESS_BUTTON_FLAG)?"Pass":"Fail"));
    u8x8_DrawString(&u8x8,0,2,buff);

    sprintf(buff,"DAC      =  %s",((success&SUCCESS_DAC_FLAG)?"Pass":"Fail"));
    u8x8_DrawString(&u8x8,0,3,buff);

    sprintf(buff,"POT      =  %s",((success&SUCCESS_POT_FLAG)?"Pass":"Fail"));
    u8x8_DrawString(&u8x8,0,4,buff);

    sprintf(buff,"ALS      =  %s",((success&SUCCESS_ALS_FLAG)?"Pass":"Fail"));
    u8x8_DrawString(&u8x8,0,5,buff);

    sprintf(buff,"HUMIDITY =  %s",((success&SUCCESS_HUMIDITY_FLAG)?"Pass":"Fail"));
    u8x8_DrawString(&u8x8,0,6,buff);

    sprintf(buff,"TEMP     =  %s",((success&SUCCESS_TEMP_FLAG)?"Pass":"Fail"));
    u8x8_DrawString(&u8x8,0,7,buff);
}

// This typedef is used to hold a function pointer for the current
// display selection and a subtitle to dispaly for that screen
typedef struct DisplayFunction {
    void (*fcn)(void); // Pointer to the function to call
    char *title;       // Subtitle to display
} DisplayFunction;

// Create a function pointer and subtitle for each of the four display screens
DisplayFunction displayFunctions[] = { 
    {displayTestScreen,"Test"},
    {displayDataScreen1,"Values"},
    {displayDataScreen0,"Base ADC"},
    {displayDataScreen2,"Buttons"}
};

#define NUM_SCREENS (sizeof(displayFunctions)/sizeof(DisplayFunction))

/*******************************************************************************
* Function Name: switchScreen(void)
********************************************************************************
*
* Summary:
*   This function displays the main title, subtitle (centered) and then calls
//  the selected display function using the displayFunctions function pointer array.
********************************************************************************/
void switchScreen(void)
{
    int offset = (16-strlen(displayFunctions[currentScreen].title))/2; // center subtitle on a 16 character wide screen
    u8x8_ClearDisplay(&u8x8);
    u8x8_DrawString(&u8x8,1,0,"PSoC AFE Shield");
    u8x8_DrawString(&u8x8,offset,1,displayFunctions[currentScreen].title);
   
    (*displayFunctions[currentScreen].fcn)();   
}

/*******************************************************************************
* Function Name: handleBaseSwitch(void)
********************************************************************************
*
* Summary:
*   This function is the ISR for the timer on the base board which starts and runs when
*   the user button is held down on the baseboard. Once the timer expires, it toggles
*   "bootloader mode" which allows the shield to use the I2C-USB bridge on the 044 kit
*   to allow I2C bootloading of the shield. It is necessary to disable OLED updates from
*   the 044 kit so that there is no I2C traffic interfering with the bootload process from
*   the USB-I2C bridge to the PSoC on the shield.
********************************************************************************/
void handleBaseSwitch(void)
{
    Timer_ClearInterrupt(Timer_INTR_MASK_TC);
    bootloaderMode = !bootloaderMode;
    Red_Write(bootloaderMode);
}

/*******************************************************************************
* Function Name: handleBaseSwitch(void)
********************************************************************************
*
* Summary:
*   This function is the ISR for the input connected to D11. This is driven by
*   MB2 on the shield. An ISR is used to turn LED2 on the shieild on/off based
*   on the button state.
********************************************************************************/
void handleShieldButton(void)
{
    // Look at mechanical Button2 from shield and drive LED2
    // (Button1 and LED1 are done in hardware)
    D11_Write(!D12_Read());
    D12_ClearInterrupt();
}

/*******************************************************************************
* Function Name: testParameters(void)
********************************************************************************
*
* Summary:
*   This function checks each test to see if it has passed. If so, it sets the 
*   proper success regiter bit so that the main screen indicates Pass for that test.
*   If all tests have passed, it will turn on the Green LED on the base board.
********************************************************************************/
void testParameters(void)
{
    float scratchFloat;
    static uint8 success_seen_buttons = 0;
    static float success_max_pot = TEST_LIMIT_POT_MIN;
    static float success_min_pot = TEST_LIMIT_POT_MAX;
    static float success_max_als = TEST_LIMIT_ALS_MIN;
    static float success_min_als = TEST_LIMIT_ALS_MAX;
        
    /*********** DAC **********/
    // Only check if the curent screen is the main screen (which means the DAC is set to
    // a fixed value of TEST_LIMIT_DAC). If so, then compare the expected value to the 
    // measured value and set the success bit if it is within the test limit.
    if(currentScreen == 0)
    {
        scratchFloat = TEST_LIMIT_DAC - (float)ADC_CountsTo_mVolts(1,A1) / 1000.0;        
        if(scratchFloat > -TEST_LIMIT_DAC_MAX_DIFF && scratchFloat < TEST_LIMIT_DAC_MAX_DIFF)
        {
            success |= SUCCESS_DAC_FLAG;
        }
    }
        
    /********** Ambient Light Sensor **********/
    // Check for values above the max limit and below the min limit. If the ALS
    // has been above the max and below the min limit at some point, set
    // the success bit.
    if(pafeDataSet.illuminance > success_max_als)
    {
        success_max_als = pafeDataSet.illuminance;
    }        
    if(pafeDataSet.illuminance < success_min_als)
    {
        success_min_als = pafeDataSet.illuminance;
    }    
    if(success_max_als > TEST_LIMIT_ALS_MAX && success_min_als < TEST_LIMIT_ALS_MIN)
    {
        success |= SUCCESS_ALS_FLAG;
    }
            
    /********** Buttons (CapSense, Proximity, and Mechanical) **********/
    // Set bit for any button that is currently active
    // If all buttons have been pressed at some point, set the success bit
    success_seen_buttons = success_seen_buttons | pafeDataSet.buttonVal;
    if(success_seen_buttons == BVAL_ALL_MASK)
    {
        success |= SUCCESS_BUTTON_FLAG;
    }
        
    /********** Humidity **********/
    // If the humidity is within the acceptable range, set the success bit
    if(pafeDataSet.humidity > TEST_LIMIT_HUMIDITY_MIN && pafeDataSet.humidity < TEST_LIMIT_HUMIDITY_MAX)
    {
        success |= SUCCESS_HUMIDITY_FLAG;
    }
        
    /********** Temperature **********/
    // If the temperature is within the acceptable range, set the success bit
    if(pafeDataSet.temperature > TEST_LIMIT_TEMP_MIN && pafeDataSet.temperature < TEST_LIMIT_TEMP_MAX) {
        success |= SUCCESS_TEMP_FLAG;
    }
            
    /********** Potentiometer **********/
    // Check for potentiometer values above the max limit and below the min limit. If the
    // reading has been above the max and below the min limit at some point, set
    // the success bit.
    scratchFloat = (float)A2/1000.0;
    if(scratchFloat > success_max_pot)
    {
        success_max_pot = scratchFloat;
    }   
    if(scratchFloat < success_min_pot)
    {
        success_min_pot = scratchFloat;
    }   
    if(success_min_pot < TEST_LIMIT_POT_MIN && success_max_pot > TEST_LIMIT_POT_MAX)
    {
        success |= SUCCESS_POT_FLAG;
    }
        
    /********** All tests **********/
    // If all of the tests have passed, turn on the Green LED on the base board
    if(success == SUCCESS_ALL)
    {
        Green_Write(0);
    }
}

/*******************************************************************************
* Function Name: main(void)
********************************************************************************
*
* Summary:
*   This is the main program loop.
********************************************************************************/
int main(void)
{   
    CyGlobalIntEnable; /* Enable global interrupts. */
	
    ADC_Start();
    ADC_StartConvert();
    I2C_Start();
    CySysTickStart();
    CySysTickSetCallback(0,sysTickISR);
   
    // Start ISR for the timer and regiser the callback function for when the timer expires.
    SWISR_StartEx(handleBaseSwitch);
    Timer_Start();
    
    // Start ISR for the button input from the shield to drive the corresponding LED on the shield
    D12ISR_StartEx(handleShieldButton);
    
    // Initialize the U8 Display
    u8x8_Setup(&u8x8, u8x8_d_ssd1306_128x64_noname, u8x8_cad_ssd13xx_i2c, u8x8_byte_hw_i2c, psoc_gpio_and_delay_cb);
    u8x8_InitDisplay(&u8x8);  
    u8x8_SetPowerSave(&u8x8,0);
    u8x8_SetFont(&u8x8,u8x8_font_amstrad_cpc_extended_f);
    u8x8_RefreshDisplay(&u8x8);
    
    // This structure is used to send the EZI2C register address + the desired DAC value to the PSoC
    typedef CY_PACKED struct SendBuff {
        uint8 address;
        float dacValue;
    } CY_PACKED_ATTR SendBuff_t;
    
    SendBuff_t sendBuff;
    sendBuff.address = 0; // Start in the first byte of the I2C register map
    sendBuff.dacValue = TEST_LIMIT_DAC_MIN; // Start the DAC at the minimum of the range
   
    // Set the PSoC AFE EZI2C Read Pointer to 0
    I2C_I2CMasterSendStart(PSOC_AFE_I2C,I2C_I2C_WRITE_XFER_MODE);
    I2C_I2CMasterWriteByte(0x00);
    I2C_I2CMasterSendStop();
        
    switchScreen(); // Display the screen for the first time - this will display the main test screen
    
    for(;;)
    {
        /* Feed the watchdog timer - this is used to reset after 1 sec if the I2C bus is hung */
        CySysWatchdogFeed(CY_SYS_WDT_COUNTER0);
        
        // Variables to hold the current and previous state of the button on the baseboard
        static int b1Prev=1;
        int b1State;
        
        // Read the values of the Analog Arduino Pins if a conversion has completed
        /* Note: The PSoC 4M ADC range is 0-2.048V, so values above 2.048V will be reported as 2.05V */
        if(ADC_IsEndConversion(ADC_RETURN_STATUS)) // The three Arduino A inputs are connected to the channel 0,1,2
        {
            A0 = ADC_GetResult16(0);
            A1 = ADC_GetResult16(1);
            A2 = ADC_GetResult16(2);            
        }
        
        // Handle the user button to switch screens on the display
        b1State = SW1_Read();
        if(b1State == 0)
        {
            if(b1Prev == 1)
            {
                b1Prev = 0;
                currentScreen = (currentScreen + 1) % NUM_SCREENS;
                switchScreen();
            }
        }
        else
        {
            b1Prev = 1;
        }
        
        // Read the PSoC AFE Shield I2C Register Map
        I2C_I2CMasterReadBuf(PSOC_AFE_I2C,(uint8 *)&pafeDataSet,sizeof(pafeDataSet),I2C_I2C_MODE_COMPLETE_XFER);
        timeout = 0;
        while (0u == (I2C_I2CMasterStatus() & I2C_I2C_MSTAT_RD_CMPLT))
        {
            /* Wait  until I2C completes  - timeout after I2C_TIMEOUT ms */        
            CyDelay(1);
            timeout++;
            if(timeout > I2C_TIMEOUT)
            {
                break;
            }
        }
        
        // If the SysTick ISR has set the updateData flag then update the DAC value & the Display
        if(updateData)
        {
            updateData = 0;
            (*displayFunctions[currentScreen].fcn)(); // Update the display
            
            // Update DAC Value
            if(currentScreen == 0) // If you are on the main test screen set the DAC to the test limit value
            {
                sendBuff.dacValue = TEST_LIMIT_DAC;
            }
            else // If you are not on the main test screen, then increment the DAC by 100mV
            {
                 sendBuff.dacValue = sendBuff.dacValue + 0.1;   
            }
            if(sendBuff.dacValue > TEST_LIMIT_DAC_MAX) // Handle wrap-around when we go past the max limit
            {
                sendBuff.dacValue = TEST_LIMIT_DAC_MIN;
            }
        
            // Send the updated DAC value to the PSoC 4 on the shield
            I2C_I2CMasterWriteBuf(PSOC_AFE_I2C,(uint8 *)&sendBuff,sizeof(sendBuff),I2C_I2C_MODE_COMPLETE_XFER);
            timeout = 0;
            while (0u == (I2C_I2CMasterStatus() & I2C_I2C_MSTAT_WR_CMPLT))
            {
                /* Wait  until I2C completes  - timeout after I2C_TIMEOUT ms */        
                CyDelay(1);
                timeout++;
                if(timeout > I2C_TIMEOUT)
                {
                    break;
                }      
            }           
        }
        
        /* Check all of the test limits and set success bits as appropriate */
        testParameters();
        
        // Disable all I2C traffic so PSoc AFE can bootload.
        // Once we are here, the only way out is to hold the button for 3 
        // seconds to get the timer to expire again or power cycle the kit.
        while(bootloaderMode) 
        {
            CySysWatchdogFeed(CY_SYS_WDT_COUNTER0); /* Feed watchdog timer */
            CyDelay(100);
            Red_Write(~Red_Read());
            
        }     
    }
}
