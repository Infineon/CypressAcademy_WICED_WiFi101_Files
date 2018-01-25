/*
* This is the default program for the PSoC 4 on the shield. It monitors CapSense, reads analog sensors,
* and stores values in I2C registers so that they can be read from a baseboard. It sets a DAC output
* voltage based on an I2C register value. It also maps the mechanical buttons and LEDs to Arduino 
* pins using the SmartIO component.
*
* A bootloader is included. Bootloader mode is entered by holding both mechanical buttons while
* turning the POT by at least 1V.
*
* See the CY8CKIT-032 kit guide for details.
*/
#include "project.h"
#include "regmap.h"
#include <stdbool.h>

/* Uncomment this to run the CapSense tuner. */
//#define ENABLE_TUNER

/* Button State */
#define PRESSED (0)

/* LED States */
#define LEDON   (0)
#define LEDOFF  (1)

/* I2C Read/Write Boundary */
/* Write: 4 bytes for DAC, 1 byte for ledVal, 1 byte for ledControl */
#define RW (6)

/* CapSense LED mode is defined by bit 1 in the led control register */
#define CAPLEDMASK (0x01)

/* Number of ADC channels */
#define NUM_CHAN (4)

/* Constants used to calculate humidity */
/* This is the capacitance of the sensor at 55% RH with 0.1pF resolution */
#define CAPACITANCE_AT_55_RH        (1850)
/* Sensitivity numerator and denominator indicate sensitivity of the sensor */
#define SENSITIVITY_NUMERATOR       (31)
#define SENSITIVITY_DENOMINATOR     (100)
/* Value of reference capacitor.  Note that this value includes the pin capacitance
    and the physical 180pF reference capacitor */
#define CREF                        (1930)
/* Offset Capacitance */
#define COFFSET                     (150)
/* This is raw count equivalent to trace capacitance */
#define OFFSETCOUNT                 (1536)
#define BUFFERSIZE                  (8)
#define READ_WRITE_BOUNDARY         (0)
/* Nominal humidity 55% */
#define NOMINAL_HUMIDITY            (550)
#define HUMIDITY_0_PERCENT          (0)
#define HUMIDITY_100_PERCENT        (1000)
#define HUMIDITY_50                 (500)   

/* Constants for photodiode current calculation */
/* Scale Factor = (VREF / (2048 * 220K)) * 10^9 nA = 2.6633 
   As the TIA produces a negative voltage, the scale factor is made 
   negative */
#define ALS_CURRENT_SCALE_FACTOR_NUMERATOR		(-26633)
#define ALS_CURRENT_SCALE_FACTOR_DENOMINATOR	(10000)

/* Constants for ambient light calculation */
/* Scale Factor = 10000Lx / 3000nA = 3.333 */
#define ALS_LIGHT_SCALE_FACTOR_NUMERATOR		(3333)
#define ALS_LIGHT_SCALE_FACTOR_DENOMINATOR		(1000)

/* Global variables and typedefs */
/* CapSense sensors */
typedef enum {
    B0,
    B1,
    B2,
    B3,
    PROX,
    HUM
} CapSenseWidget;

/* ADC Channels */
typedef enum {
    ALS,
    THERM_REF,
    THERM,
    POT,
} ADC_Chan;  

/* ADC States */
typedef enum {
    RUNNING,
    PROCESS,
    DONE,
} ADC_States;

volatile dataSet_t I2Cbuf;   /* I2C buffer containing the data set */
volatile dataSet_t LocData;  /* Local working copy of the data set */

volatile uint16 capacitance;			    /* Capacitance of the humidity sensor */
volatile uint16 humidity;			    /* Measured humidity */

volatile uint32 adcState = DONE;          /* The ADC states are: RUNNING, PROCESS and DONE */
volatile int16  adcResults[NUM_CHAN];     /* Array to hold raw ADC results */

volatile bool capLedBase = false;        /* Setting for whether the CapSense LEDs are controlled by CapSense or I2C */


/* Function prototypes */
/* Interrupt Service Routines */
CY_ISR_PROTO(ADC_ISR_Callback);
void SysTickISRCallback(void);
/* Humidity calculation */
__inline uint16 CalculateCapacitance(uint16 rawCounts, uint16 refsensorCounts);
__inline uint16 CalculateHumidity(uint16 capacitance);
/*Processing function for each major system */
void processButtons(void);
void processCapSense(void);
void processADC(void);
void processDAC(void);
void processI2C(void);

/*******************************************************************************
* Function Name: int  main( void )
********************************************************************************/
int main(void)
{
    uint32  i;
    
    CyGlobalIntEnable; /* Enable global interrupts. */

    EZI2C_Start();
    #ifdef ENABLE_TUNER
    EZI2C_EzI2CSetBuffer1(sizeof(CapSense_dsRam), sizeof(CapSense_dsRam),(uint8 *)&CapSense_dsRam);        
    #else
    EZI2C_EzI2CSetBuffer1(sizeof(I2Cbuf), RW, (void *) &I2Cbuf);     
    #endif   
    
    SmartIO_Start();    
    VDAC_Start();
    PVref_ALS_Start();
    Opamp_ALS1_Start();
    Opamp_ALS2_Start();
    PVref_Therm_Start();
    Opamp_Therm_Start();    
    ADC_Start();
    ADC_IRQ_Enable();
    
    CapSense_Start();   
    /* Over-ride IDAC values for buttons but keep auto for Prox and Humidity */
    CapSense_BUTTON0_IDAC_MOD0_VALUE =          7u;
    CapSense_BUTTON0_SNS0_IDAC_COMP0_VALUE =    6u;
    CapSense_BUTTON1_IDAC_MOD0_VALUE =          7u;
    CapSense_BUTTON1_SNS0_IDAC_COMP0_VALUE =    7u;
    CapSense_BUTTON2_IDAC_MOD0_VALUE =          9u;
    CapSense_BUTTON2_SNS0_IDAC_COMP0_VALUE =    7u;
    CapSense_BUTTON3_IDAC_MOD0_VALUE =          9u;
    CapSense_BUTTON3_SNS0_IDAC_COMP0_VALUE =    8u;
    /* Setup first widget and run the scan */    
    CapSense_SetupWidget(CapSense_BUTTON0_WDGT_ID);
    CapSense_Scan();      
    
    /* Start SysTick Timer to give a 1ms interrupt */
    CySysTickStart();
    /* Find unused callback slot and assign the callback. */
    for (i = 0u; i < CY_SYS_SYST_NUM_OF_CALLBACKS; ++i)
    {
        if (CySysTickGetCallback(i) == NULL)
        {
            /* Set callback */
            CySysTickSetCallback(i, SysTickISRCallback);
            break;
        }
    }
    
    /* Initialize I2C and local data registers to 0's */
    I2Cbuf.dacVal = 0.0;
    I2Cbuf.ledVal = 0x00;
    I2Cbuf.ledControl = 0x00;
    I2Cbuf.buttonVal = 0x00;
    I2Cbuf.temperature = 0.0;
    I2Cbuf.humidity = 0.0;
    I2Cbuf.illuminance = 0.0;
    I2Cbuf.potVal = 0.0;
    
    LocData.dacVal = 0.0;
    LocData.ledVal = 0x00;
    LocData.ledControl = 0x00;
    LocData.buttonVal = 0x00;
    LocData.temperature = 0.0;
    LocData.humidity = 0.0;
    LocData.illuminance = 0.0;
    LocData.potVal = 0.0;
    
    for(;;)
    {
        processButtons();  /* Mechanical buttons and bootloader entry */
        processCapSense(); /* CapSense Scanning */
        processDAC();      /* VDAC output voltage setting */
        processADC();      /* Process ADC results after each scan completes */
        processI2C();      /* Copy date between I2C registers and local operating registers */
    }
} /* End of main */

/*******************************************************************************
* Function Name: void SysTick( void )
********************************************************************************
*
* Summary:
*  This is the SysTick Timer callback ISR. It is called every 1ms and performs 2 functions:
*  1. It starts a new ADC conversion every 100ms be setting adcState to RUNNING.
*  2. It resets the CapSense interrupt line every 2nd time it is called.
*     Since the CapSense interrupt may be asserted at any time, this guarantees a pulse
*     on the CapSense interrupt output pin between 1ms and 2ms.
********************************************************************************/
/* 1ms SysTick ISR */
/* This is used to start a new ADC conversion every 100ms
   and to turn off the CapSense interrupt line */
void SysTickISRCallback(void)
{
    static uint8 ADCcounter = 0;
    static uint8 CScounter = 0;
    
    ADCcounter++;
    if(ADCcounter > 99)
    {
        ADCcounter = 0;
        if(adcState == DONE)
        {
            ADC_StartConvert();
            adcState = RUNNING;
        }
    }
    
    /* Read CS Interrupt pin state and turn off after 2nd tick */
    if(CSINTR_Read() == 1)
    {
        CScounter++;
        if(CScounter > 1)
        {
            CSINTR_Write(0);
            CScounter = 0;
        }
    }
}

/*******************************************************************************
* Function Name: void ADC_ISR_Callback( void )
********************************************************************************
*
* Summary:
*  This function is called each time the ADC finishes a full set of conversions.
*  It copies the results to an array and sets the adcState to PROCESS. This causes
*  the processADC funciton to calculate values from the latest results.
********************************************************************************/
/* ADC converstion is done - capture all ADC values */
CY_ISR(ADC_ISR_Callback)
{
    uint8 i;
    
    for(i = 0; i < NUM_CHAN; i++)
    {
        adcResults[i] = ADC_GetResult16(i);        
    }
    adcState = PROCESS;    /* Set ADC state to process results */
}

/*******************************************************************************
* Function Name: void processButtons( void )
********************************************************************************
*
* Summary:
*  This function looks at the state of each mechanical button and sets the I2C
*  register bits appropriatly.
*
*  It also looks for bootloader entry which required both buttons held down while
*  the POT is moved by more than 1V.
*******************************************************************************/
void processButtons(void)
{
    /* Set min at 3.3 and max at 0.0 to guarantee they are beyond the actual range */
    static float32 potMin = 3.3;
    static float32 potMax = 0.0;
    
    /* Read and update mechanical button state */
    if(MB1_Read() == PRESSED)
    {
        LocData.buttonVal |= (BVAL_MB1_MASK);
    }
    else
    {
       LocData.buttonVal &= (~BVAL_MB1_MASK);
    }
    if(MB2_Read() == PRESSED)
    {
        LocData.buttonVal |= (BVAL_MB2_MASK);
    }
    else
    {
        LocData.buttonVal &= (~BVAL_MB2_MASK);
    }
    
    /* Update CapSense buttons if set to base board control */
    if(capLedBase == true)
    {
        CBLED0_Write(!(LocData.ledVal & BVAL_B0_MASK));
        CBLED1_Write(!(LocData.ledVal & BVAL_B1_MASK));
        CBLED2_Write(!(LocData.ledVal & BVAL_B2_MASK));
        CBLED3_Write(!(LocData.ledVal & BVAL_B3_MASK));
    }
    
    /* Look for bootloader entry - both mechanical buttons held down and
       POT rotated at least 1V */
    if((MB1_Read() == PRESSED) && (MB2_Read() == PRESSED))
    {
        /* Check pot reading compared to stored min/max */
        if(LocData.potVal < potMin)
        {
            potMin = LocData.potVal;
        }
        if(LocData.potVal > potMax)
        {
            potMax = LocData.potVal;
        }
        if((potMax - potMin) > 1.0) /* Pot moved more than 1V, time to bootload */
        {        
            Bootloadable_Load();
        }
    }
    else /* Buttons not both pressed - reset POT range values */
    {
        potMin = 3.3;
        potMax = 0.0;
    }
}

/*******************************************************************************
* Function Name: void processCapsense( void )
********************************************************************************
*
* Summary:
*  This function steps through each capSense sensor one by one and captures its state.
*
*  For the humidity and humidity reference capacitors, the raw counts are stored 
*  and then the humidity is calculated.
*******************************************************************************/
void processCapSense(void)
{
    static uint8  state = B0;               /* CapSense sensor state machine to cycle through sensors */
    static uint16 humidityRawCounts;        /* Raw count from CapSense Component for the humidity sensor */
    static uint16 humidityRefRawCounts;     /* Raw count from CapSense Component for the Reference capacitor */
    static uint8  buttonValPrev = 0x00;     /* Previous CapSense button state */
    
    if(!CapSense_IsBusy())
    {
        switch(state) {
            case B0: /* Process Button 0, Scan Button 1 */
                CapSense_ProcessWidget(CapSense_BUTTON0_WDGT_ID);
                if(CapSense_IsWidgetActive(CapSense_BUTTON0_WDGT_ID))
                {
                    if(capLedBase == false)
                    {
                        CBLED0_Write(LEDON);
                        
                    }      
                    LocData.buttonVal |= (BVAL_B0_MASK);
                }
                else
                {
                    if(capLedBase == false)
                    {
                        CBLED0_Write(LEDOFF);
                    }
                    LocData.buttonVal &= (~BVAL_B0_MASK);
                }
                CapSense_SetupWidget(CapSense_BUTTON1_WDGT_ID);
                state++;
                break;
            case B1: /* Process Button 1, Scan Button 2 */
                CapSense_ProcessWidget(CapSense_BUTTON1_WDGT_ID);
                if(CapSense_IsWidgetActive(CapSense_BUTTON1_WDGT_ID))
                {
                    if(capLedBase == false)
                    {
                        CBLED1_Write(LEDON);
                    }
                    LocData.buttonVal |= (BVAL_B1_MASK);
                }
                else
                {
                    if(capLedBase == false)
                    {
                        CBLED1_Write(LEDOFF);
                    }
                    LocData.buttonVal &= (~BVAL_B1_MASK);
                }
                
                CapSense_SetupWidget(CapSense_BUTTON2_WDGT_ID);
                state++;                
                break;
            case B2: /* Process Button 2, Scan Button 3 */
                CapSense_ProcessWidget(CapSense_BUTTON2_WDGT_ID);
                if(CapSense_IsWidgetActive(CapSense_BUTTON2_WDGT_ID))
                {
                    if(capLedBase == false)
                    {
                        CBLED2_Write(LEDON);
                    }
                    LocData.buttonVal |= (BVAL_B2_MASK);
                }
                else
                {
                    if(capLedBase == false)
                    {
                        CBLED2_Write(LEDOFF);
                    }
                    LocData.buttonVal &= (~BVAL_B2_MASK);
                }
                CapSense_SetupWidget(CapSense_BUTTON3_WDGT_ID);
                state++;
                break;
            case B3: /* Process Button 3, Scan Proximity */
                CapSense_ProcessWidget(CapSense_BUTTON3_WDGT_ID);
                if(CapSense_IsWidgetActive(CapSense_BUTTON3_WDGT_ID))
                {
                    if(capLedBase == false)
                    {
                        CBLED3_Write(LEDON);
                    }
                    LocData.buttonVal |= (BVAL_B3_MASK);
                }
                else
                {
                    if(capLedBase == false)
                    {
                        CBLED3_Write(LEDOFF);
                    }
                    LocData.buttonVal &= (~BVAL_B3_MASK);
                }
                
                /* Now that butons have all been processed, set interrupt state */
                if((LocData.buttonVal & BVAL_ALLB_MASK) != buttonValPrev) /* At least 1 CapSense button state changed */
                {
                    CSINTR_Write(1);
                    buttonValPrev = (LocData.buttonVal & BVAL_ALLB_MASK);
                }
                
                /* Setup Proximity scan */
                CapSense_SetupWidget(CapSense_PROXIMITY0_WDGT_ID);
                state++;
                break;      
            case PROX: /* Process Proximity, Scan Humidity */
                CapSense_ProcessWidget(CapSense_PROXIMITY0_WDGT_ID);
                if(CapSense_IsWidgetActive(CapSense_PROXIMITY0_WDGT_ID))
                {
                    PROXLED_Write(LEDON);
                    LocData.buttonVal |= (BVAL_PROX_MASK);
                }
                else
                {
                    PROXLED_Write(LEDOFF);
                    LocData.buttonVal &= (~BVAL_PROX_MASK);
                }
                CapSense_SetupWidget(CapSense_HUMIDITY_WDGT_ID);
                state++;
                break;
            case HUM: /* Process Humidity, Scan Button 0  and go back to start of loop */                
                humidityRawCounts =    CapSense_HUMIDITY_SNS0_RAW0_VALUE; 
                humidityRefRawCounts = CapSense_HUMIDITY_SNS1_RAW0_VALUE;
                /* Convert raw counts to capacitance */
                capacitance = CalculateCapacitance(humidityRawCounts, humidityRefRawCounts);
                /* Calculate humidity */
                humidity = CalculateHumidity(capacitance); 
                LocData.humidity = ((float32)(humidity))/10.0;
                CapSense_SetupWidget(CapSense_BUTTON0_WDGT_ID);
                state=0;
                break;
        } /* End of CapSense Switch statement */
        #ifdef ENABLE_TUNER
        CapSense_RunTuner();
        #endif
        CapSense_Scan();
    }
}

/*******************************************************************************
* Function Name: void processADC( void )
********************************************************************************
*
* Summary:
*  This function calculates all values from the ADC each time a scan completes.
*  The variable adcState is set to PROCESS by the ADC ISR each time a scan finishes.
*  Once processing is done, the adcState variable is set to DONE so that the
*  next scan can start.
*
* The values calculated are: Ambient light, temperature, and POT voltage.
*******************************************************************************/
void processADC(void)
{
    int16   alsCurrent; /* Variable to store ALS current */
    int16   illum16; /* Illuminance as a 16 bit value (lux) */
    int16   thermistorResistance; /* Variables for temperature calculation */
    int16   temp16; /* Temperature expressed as a 16 bit integer in 1/100th of a degree */

    /* Process ADC results that were captured in the interrupt */
    if(adcState == PROCESS)
    {
        /* ALS */
        /* Calculate the photodiode current */
		alsCurrent = (adcResults[ALS] * ALS_CURRENT_SCALE_FACTOR_NUMERATOR)/ALS_CURRENT_SCALE_FACTOR_DENOMINATOR; 
		
		/* If the calculated current is negative, limit it to zero */
		if(alsCurrent < 0)
		{
			alsCurrent = 0;
		}
		
		/* Calculate the light illuminance */
        illum16 = (alsCurrent * ALS_LIGHT_SCALE_FACTOR_NUMERATOR)/ALS_LIGHT_SCALE_FACTOR_DENOMINATOR;
		LocData.illuminance = (float32)(illum16);
        
        /* Thermistor */
        /* Calculate thermistor resistance */
        thermistorResistance = Thermistor_GetResistance(adcResults[THERM_REF], adcResults[THERM]);           
                       
        /* Calculate temperature in degree Celsius using the Component API */
        temp16 = Thermistor_GetTemperature(thermistorResistance);
        /* Convert tempearture to a float */
        LocData.temperature = ((float32)(temp16))/100.0;
        
        /* POT */
        LocData.potVal = ADC_CountsTo_Volts(POT, adcResults[POT]);
        
        adcState = DONE;
    }
}

/*******************************************************************************
* Function Name: void processDAC( void )
********************************************************************************
*
* Summary:
*  This function sets the DAC output voltage based on the value copied from the I2C register.
*******************************************************************************/
void processDAC(void)
{
    static int32   dacValPrev = 0;
    float32 dacVal;
    int32   dacCode;

    /* Set VDAC value if it has changed */
    dacVal = LocData.dacVal;
    if(dacValPrev != dacVal)
    {
        dacValPrev = dacVal;
        // DAC range is 2.4V, Valid inputs are -4096 to 4094
        dacCode = (int32)(((dacVal * 8192.0)/2.4) - 4096.0);
        if (dacCode < -4096)
        {
            dacCode = -4096;
        }
        VDAC_SetValue(VDAC_SaturateTwosComp(dacCode));
    }
}

/*******************************************************************************
* Function Name: void processI2C( void )
********************************************************************************
*
* Summary:
*  This function copies date from/to the I2C registers to local values that are
*  used in the rest of the firmware. The values are:
*  From I2C: Desired DAC Voltage, LED states (if LEDs are under baseboard control), LED control register
*  To I2C:   Button Values, Temperature, Humidity, Ambient Light, POT Voltage
*******************************************************************************/
void processI2C(void)
{
    uint8   interruptState;   /* Variable to store the status returned by CyEnterCriticalSection() */
    
    /* Update I2C registers to/from local copy if it isn't busy */
    /* Enter critical section to check if I2C bus is busy or not */
    interruptState = CyEnterCriticalSection();
    if(!(EZI2C_EzI2CGetActivity() & EZI2C_EZI2C_STATUS_BUSY))
    {
        /* Get values that are written by the master */
        LocData.dacVal = I2Cbuf.dacVal;
        LocData.ledVal = I2Cbuf.ledVal;
        LocData.ledControl = I2Cbuf.ledControl;
        capLedBase = LocData.ledControl & CAPLEDMASK;
        /* Send values that are updated by the slave */
        I2Cbuf.buttonVal = LocData.buttonVal;
        I2Cbuf.temperature = LocData.temperature;
        I2Cbuf.humidity = LocData.humidity;
        I2Cbuf.illuminance = LocData.illuminance;
        I2Cbuf.potVal = LocData.potVal;
    }
    CyExitCriticalSection(interruptState); 
}

/*******************************************************************************
* Function Name: __inline uint16 CalculateCapacitance(uint16 RawCounts, uint16 RefsensorCounts)
********************************************************************************
*
* Summary:
*  This function calculates capacitance from raw count.
*
* Parameters:
*  uint16 RawCounts - Raw count corresponding to Humidity sensor
*  uint16 RefsensorCounts - Raw count corresponding to Reference capacitor
*
* Return:
*  Capacitance of the Humidity sensor
*
* Side Effects:
*   None
*******************************************************************************/
__inline uint16 CalculateCapacitance(uint16 rawCounts, uint16 refsensorCounts)
{
    return (uint16)((float32)(rawCounts - OFFSETCOUNT) * (CREF - COFFSET) / (float32)(refsensorCounts - OFFSETCOUNT));   
}

/*******************************************************************************
* Function Name: __inline uint16 CalculateHumidity(uint16 Capacitance)
********************************************************************************
*
* Summary:
*  This function calculates humidity from capacitance

* Parameters:
*  uint16 Capacitance - Capacitance of the humidity sensor
*
* Return:
*  Calculated Humidity value
*
* Side Effects:
*   None
*******************************************************************************/
__inline uint16 CalculateHumidity(uint16 capacitance)
{
    int16 humidity;
    int16 delta;
    
    /* Find capacitance difference from nominal capacitance at 55% RH */
    delta = capacitance - CAPACITANCE_AT_55_RH;
    
    /* Calculate humidity from capacitance difference and sensor sensitivity */
    humidity = ((delta * SENSITIVITY_DENOMINATOR) / SENSITIVITY_NUMERATOR) + NOMINAL_HUMIDITY;
    
    /* If humidity is less than zero, limit it to 0; If humidity is greater than 1000 (100%), limit to 1000 */
    humidity = (humidity < HUMIDITY_0_PERCENT) ? HUMIDITY_0_PERCENT : (humidity > HUMIDITY_100_PERCENT) ? HUMIDITY_100_PERCENT : humidity;
    
    /* Return Humidity value */
    return humidity;
}

/* [] END OF FILE */
