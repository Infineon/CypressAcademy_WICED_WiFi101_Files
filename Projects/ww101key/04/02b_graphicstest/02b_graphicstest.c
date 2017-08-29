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
 * U8G Library GraphicsTest Sample Application
 *
 * Features demonstrated
 *  - U8G graphics library functionality
 *  - Wiced I2C communication protocol with software Repeat-Start
 *
 * On startup this demo:
 *  - Runs a third-party GraphicsTest (from Arduino)
 *  - Cycles through 9 different images/animations
 *
 * Application Instructions
 *   For 128x64 ssd1306 OLED displays:
 *     Attach the display to the WICED Eval board such that:
 *       - VCC goes to 3.3V power (J5 pin 5)
 *       - GND goes to ground     (J5 pin 2)
 *       - SCL goes to I2C_0_SCL  (J16 pin 15)
 *       - SDA goes to I2C_0_SDA  (J16 pin 13)
 *       (Pin headers/numbers apply to P201 Eval board)
 *     Build, download, and run the application. The display
 *     should cycle through 9 different screens with various
 *     shapes and text.
 *
 *   For other displays:
 *
 *    a) Modify the wiced_i2c_device_t struct below for your
 *     specific device, note that the I2C interface should be set as below:
 *
 *      .port          = WICED_I2C_2 - for BCM43907WAE_1 platforms
 *      .port          = WICED_I2C_1 - for BCM43909WCD1 (WICED Eval) platforms
 *
 *    b) Modify arg 2 of u8g_InitComFn() in application_start()
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

void u8g_prepare(u8g_t* u8g)
{
    u8g_SetFont(u8g, u8g_font_6x10);
    u8g_SetFontRefHeightExtendedText(u8g);
    u8g_SetDefaultForegroundColor(u8g);
    u8g_SetFontPosTop(u8g);
}

void u8g_box_frame(u8g_t* u8g, uint8_t a)
{
    u8g_DrawStr(u8g, 0, 0, "drawBox");
    u8g_DrawBox(u8g, 5,10,20,10);
    u8g_DrawBox(u8g, 10+a,15,30,7);
    u8g_DrawStr(u8g,  0, 30, "drawFrame");
    u8g_DrawFrame(u8g, 5,10+30,20,10);
    u8g_DrawFrame(u8g, 10+a,15+30,30,7);
}

void u8g_disc_circle(u8g_t* u8g, uint8_t a)
{
    u8g_DrawStr(u8g,  0, 0, "drawDisc");
    u8g_DrawDisc(u8g, 10,18,9, 0xFF);
    u8g_DrawDisc(u8g, 24+a,16,7, 0xFF);
    u8g_DrawStr(u8g,  0, 30, "drawCircle");
    u8g_DrawCircle(u8g, 10,18+30,9, 0xFF);
    u8g_DrawCircle(u8g, 24+a,16+30,7, 0xFF);
}

void u8g_r_frame(u8g_t* u8g, uint8_t a)
{
    u8g_DrawStr(u8g,  0, 0, "drawRFrame/Box");
    u8g_DrawRFrame(u8g, 5, 10,40,30, a+1);
    u8g_DrawRBox(u8g, 50, 10,25,40, a+1);
}

void u8g_string(u8g_t* u8g, uint8_t a)
{
    u8g_DrawStr(u8g, 30+a,31, " 0");
    u8g_DrawStr90(u8g, 30,31+a, " 90");
    u8g_DrawStr180(u8g, 30-a,31, " 180");
    u8g_DrawStr270(u8g, 30,31-a, " 270");
}

void u8g_line(u8g_t* u8g, uint8_t a)
{
    u8g_DrawStr(u8g,  0, 0, "drawLine");
    u8g_DrawLine(u8g, 7+a, 10, 40, 55);
    u8g_DrawLine(u8g, 7+a*2, 10, 60, 55);
    u8g_DrawLine(u8g, 7+a*3, 10, 80, 55);
    u8g_DrawLine(u8g, 7+a*4, 10, 100, 55);
}

void u8g_triangle(u8g_t* u8g, uint8_t a)
{
    uint16_t offset = a;
    u8g_DrawStr(u8g,  0, 0, "drawTriangle");
    u8g_DrawTriangle(u8g, 14,7, 45,30, 10,40);
    u8g_DrawTriangle(u8g, 14+offset,7-offset, 45+offset,30-offset, 57+offset,10-offset);
    u8g_DrawTriangle(u8g, 57+offset*2,10, 45+offset*2,30, 86+offset*2,53);
    u8g_DrawTriangle(u8g, 10+offset,40+offset, 45+offset,30+offset, 86+offset,53+offset);
}

void u8g_ascii_1(u8g_t* u8g)
{
    char s[2] = " ";
    uint8_t x, y;
    u8g_DrawStr(u8g,  0, 0, "ASCII page 1");
    for( y = 0; y < 6; y++ )
    {
        for( x = 0; x < 16; x++ )
        {
            s[0] = y*16 + x + 32;
            u8g_DrawStr(u8g, x*7, y*10+10, s);
        }
    }
}

void u8g_ascii_2(u8g_t* u8g)
{
    char s[2] = " ";
    uint8_t x, y;
    u8g_DrawStr(u8g,  0, 0, "ASCII page 2");
    for( y = 0; y < 6; y++ )
    {
        for( x = 0; x < 16; x++ )
        {
            s[0] = y*16 + x + 160;
            u8g_DrawStr(u8g, x*7, y*10+10, s);
        }
    }
}

void u8g_extra_page(u8g_t* u8g, uint8_t a)
{
    if (u8g_GetMode(u8g) == U8G_MODE_HICOLOR || u8g_GetMode(u8g) == U8G_MODE_R3G3B2)
    {
        u8g_uint_t r, g, b;
        b = a << 5;
        for( g = 0; g < 64; g++ )
        {
            for( r = 0; r < 64; r++ )
            {
                u8g_SetRGB(u8g, r<<2, g<<2, b );
                u8g_DrawPixel(u8g, g, r);
            }
        }
        u8g_SetRGB(u8g, 255,255,255);
        u8g_DrawStr(u8g,  66, 0, "Color Page");
    }
    else if (u8g_GetMode(u8g) == U8G_MODE_GRAY2BIT )
    {
        u8g_DrawStr(u8g,  66, 0, "Gray Level");
        u8g_SetColorIndex(u8g, 1);
        u8g_DrawBox(u8g, 0, 4, 64, 32);
        u8g_DrawBox(u8g, 70, 20, 4, 12);
        u8g_SetColorIndex(u8g, 2);
        u8g_DrawBox(u8g, 0+1*a, 4+1*a, 64-2*a, 32-2*a);
        u8g_DrawBox(u8g, 74, 20, 4, 12);
        u8g_SetColorIndex(u8g, 3);
        u8g_DrawBox(u8g, 0+2*a, 4+2*a, 64-4*a, 32-4*a);
        u8g_DrawBox(u8g, 78, 20, 4, 12);
    }
    else
    {
        u8g_DrawStr(u8g,  0, 12, "setScale2x2");
        u8g_SetScale2x2(u8g);
        u8g_DrawStr(u8g,  0, 6+a, "setScale2x2");
        u8g_UndoScale(u8g);
    }
}

void draw(u8g_t* u8g, uint8_t draw_state)
{
    u8g_prepare(u8g);
    switch(draw_state >> 3)
    {
        case 0: u8g_box_frame(u8g, draw_state&7); break;
        case 1: u8g_disc_circle(u8g, draw_state&7); break;
        case 2: u8g_r_frame(u8g, draw_state&7); break;
        case 3: u8g_string(u8g, draw_state&7); break;
        case 4: u8g_line(u8g, draw_state&7); break;
        case 5: u8g_triangle(u8g, draw_state&7); break;
        case 6: u8g_ascii_1(u8g); break;
        case 7: u8g_ascii_2(u8g); break;
        case 8: u8g_extra_page(u8g, draw_state&7); break;
    }
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
    uint8_t draw_state = 0;

    u8g_init_wiced_i2c_device(&oled_display);
    u8g_InitComFn(&u8g, &u8g_dev_ssd1306_128x64_i2c, u8g_com_hw_i2c_fn);

    while(1)
    {
        u8g_FirstPage(&u8g);

        do {
            draw(&u8g, draw_state);
        } while (u8g_NextPage(&u8g));

        draw_state++;
        if ( draw_state >= 9*8 )
        {
            draw_state = 0;
        }

        u8g_Delay(75);
    }
}

