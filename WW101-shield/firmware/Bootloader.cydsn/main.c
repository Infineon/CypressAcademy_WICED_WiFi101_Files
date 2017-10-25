/*
* This is the I2C bootloader that is included in the Shield projet.
*
* While in bootloader mode, LED1 and LED2 will flash in an alternating pattern at about 1Hz.
*
* Bootloader mode can only be exited by either reseting the PSoC 4 on the shield or by
* bootloading new firmware.
*
* See the CY8CKIT-032 kit guide for details.
*/
#include <project.h>

int main()
{
    /* Place your initialization/startup code here (e.g. MyInst_Start()) */

    CyGlobalIntEnable;

    /* Blink LEDs */
    SmartIO_Start();
    PWM_1_Start();
    
    /* Start bootloader communication */
    Bootloader_Start();

    for(;;)
    {
        /* We never get here since the bootloader starts first. */
    }
}

/* [] END OF FILE */
