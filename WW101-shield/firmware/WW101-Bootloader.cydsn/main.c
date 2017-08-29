/* ========================================
 *
 * Copyright Cypress Semiconductor, 2017
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF Cypress Semiconductor.
 *
 * ========================================
*/
#include <project.h>

int main()
{
    /* Place your initialization/startup code here (e.g. MyInst_Start()) */

    CyGlobalIntEnable;

    /* Blink LEDs */
    PWM_1_Start();
    PWM_2_Start();

    /* Start bootloader communication */
    Bootloader_Start();

    for(;;)
    {
        /* Place your application code here. */
    }
}

/* [] END OF FILE */
