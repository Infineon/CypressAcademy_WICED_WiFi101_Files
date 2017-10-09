#include "project.h"
#include "regmap.h"
#include <stdbool.h>

/* Use this to run the tuner. */
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
#define CAPACITANCE_AT_55_RH        (1800)
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

uint16 capacitance;			    /* Capacitance of the humidity sensor */
uint16 humidity;			    /* Measured humidity */

uint32 adcState = DONE;          /* The ADC states are: RUNNING, PROCESS and DONE */
int16  adcResults[NUM_CHAN];     /* Array to hold raw ADC results */

bool capLedBase = false;        /* Setting for whether the CapSense LEDs are controlled by CapSense or I2C */


/* Function prototypes */
/* Interrupt Service Routines */
CY_ISR_PROTO(BL_ISR);
CY_ISR_PROTO(ADC_ISR_Callback);
void SysTickISRCallback(void);
/* Humidity calculation */
__inline uint16 CalculateCapacitance(uint16 rawCounts, uint16 refsensorCounts);
__inline uint16 CalculateHumidity(uint16 capacitance);
/*CapSense */
void processCapSense(void);

/*******************************************************************************
* Function Name: int  main( void )
********************************************************************************/
int main(void)
{
    /* Local variables */
    bool    BootloadCountFlag = false;
    uint8   interruptState = 0;   /* Variable to store the status returned by CyEnterCriticalSection() */
    int32   dacValPrev = 0;
    float32 dacVal;
    int32   dacCode;
    int16   alsCurrent; /* Variable to store ALS current */
    int16   illum16; /* Illuminance as a 16 bit value (lux) */
    int16   thermistorResistance; /* Variables for temperature calculation */
    int16   temp16; /* Temperature expressed as a 16 bit integer in 1/100th of a degree */
    uint32  i;

    CyGlobalIntEnable; /* Enable global interrupts. */

    BL_INT_StartEx(BL_ISR); /* Interrupt to start the bootloader */ 
    
    /* This starts both master and slave but only one will be used at at time */

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
    
    for(;;)
    {
        /* Look for bootloader entry - both mechanical buttons held down for 2 seconds */
        if((MB1_Read() == PRESSED) && (MB2_Read() == PRESSED))
        {
            if(BootloadCountFlag == false)
            {
                BootloadTimer_Start();
            }
            BootloadCountFlag = true;
        }
        else if(BootloadCountFlag == true)
        {
            BootloadCountFlag = false;
            BootloadTimer_Stop();
            BootloadTimer_WriteCounter(0);
        }

        /* CapSense Scanning */
        processCapSense();
        
        /* Read and update mechanical button state */
        if(MB1_Read() == PRESSED)
        {
            LocData.buttonVal |= (BVAL_MB1_MASK);
        }
        else
        {
           LocData.buttonVal &= (~BVAL_MB1_MASK);
        }
        if(MB1_Read() == PRESSED)
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
        
    } /* End of main infinite loop */
} /* End of main */

/*******************************************************************************
* Function Name: void BL_ISR( void )
********************************************************************************/
/* ISR for the bootloader timer */
/* If we get here it is time to launch the bootloader */
CY_ISR(BL_ISR)
{
    BootloadTimer_ClearInterrupt(BootloadTimer_INTR_MASK_TC);
    Bootloadable_Load();
}

/*******************************************************************************
* Function Name: void SysTick( void )
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
* Function Name: void procesCapsense( void )
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
                if(LocData.buttonVal != buttonValPrev) /* At least 1 button state changed */
                {
                    CSINTR_Write(1);
                    buttonValPrev = LocData.buttonVal;
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
