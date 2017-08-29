/*
 * Copyright 2017, Cypress Semiconductor Corporation or a subsidiary of 
 * Cypress Semiconductor Corporation. All Rights Reserved.
 * 
 * This software, associated documentation and materials ("Software"),
 * is owned by Cypress Semiconductor Corporation
 * or one of its subsidiaries ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products. Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 */

/** @file
 *
 * U8G Library HelloWorld Sample Application
 *
 * Features demonstrated
 *  - Basic use of u8g library for i2c displays
 *  - Wiced I2C communication protocol
 *
 * On startup this demo:
 *  - Displays "Hello World!" on an attached display
 *
 * Application Instructions
 *   For 128x64 ssd1306 OLED displays:
 *     Attach the display to the WICED Eval board such that:
 *       - VCC goes to 3.3V power (J5 pin 5)
 *       - GND goes to ground     (J5 pin 2)
 *       - SCL goes to I2C_1_SCL  (J5 pin 1)
 *       - SDA goes to I2C_1_SDA  (J5 pin 3)
 *       (Pin headers/numbers apply to P201 Eval board)
 *     Build, download, and run the application. The display
 *     should show Hello World!
 *
 *   For other displays:
 *     Modify the wiced_i2c_device_t struct below for your
 *     specific device.
 *     Modify arg 2 of u8g_InitComFn() in application_start()
 *     to reflect the type of display being used. The u8g library
 *     supports many different types of displays; you can look
 *     through the various u8g_dev_* files for I2C constructors.
 *     Attach, build, download, and run as described above.
 *
 */

#include "u8g_arm.h"

/******************************************************
 *                      Macros
 ******************************************************/

/******************************************************
 *                    Constants
 ******************************************************/

/******************************************************
 *                   Enumerations
 ******************************************************/

/******************************************************
 *                 Type Definitions
 ******************************************************/

/******************************************************
 *                    Structures
 ******************************************************/

/******************************************************
 *               Function Declarations
 ******************************************************/

/******************************************************
 *               Variables Definitions
 ******************************************************/

/******************************************************
 *               Function Definitions
 ******************************************************/

void draw(u8g_t* u8g)
{
    u8g_SetFont(u8g, u8g_font_unifont);
    u8g_SetFontPosTop(u8g);
    u8g_DrawStr(u8g, 0, 10, "Hello World!");
}

void application_start()
{
    wiced_i2c_device_t oled_display =
    {
        .port          = WICED_I2C_2,
        .address       = 0x3C,
        .address_width = I2C_ADDRESS_WIDTH_7BIT,
        .flags         = 0,
        .speed_mode    = I2C_STANDARD_SPEED_MODE,
    };
    u8g_t u8g;

    u8g_init_wiced_i2c_device(&oled_display);
    u8g_InitComFn(&u8g, &u8g_dev_ssd1306_128x64_i2c, u8g_com_hw_i2c_fn);

	while(1)
	{
		u8g_FirstPage(&u8g);
		do {
			draw(&u8g);
		} while (u8g_NextPage(&u8g));
	}
}
