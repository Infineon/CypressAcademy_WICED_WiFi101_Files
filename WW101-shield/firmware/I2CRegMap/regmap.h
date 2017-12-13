/*
* This is a header file used in both the Shield project and the TestProgram4M project.
*
* It defines:
* 1. Masks for the different buttons (4 CapSense, 2 mechanical, and proximity).
* 2. The EzI2C register map used to communicate between the shield (slave) and baseboard (master).
*
* See the CY8CKIT-032 kit guide for details.
*/
#include <project.h>

#ifndef REGMAP_H
#define REGMAP_H

/* Button register masks */
#define BVAL_B0_MASK      (0x01)
#define BVAL_B1_MASK      (0x02)
#define BVAL_B2_MASK      (0x04)
#define BVAL_B3_MASK      (0x08)
#define BVAL_ALLB_MASK    (0x0F)
#define BVAL_MB1_MASK     (0x10)
#define BVAL_MB2_MASK     (0x20)
#define BVAL_PROX_MASK    (0x40)

#define BVAL_ALL_MASK (BVAL_B0_MASK | BVAL_B1_MASK | BVAL_B2_MASK | BVAL_B3_MASK | BVAL_MB1_MASK | BVAL_MB2_MASK | BVAL_PROX_MASK)
    
/* This structure is the I2C register set. It is used in both the shield firmware (which is an I2C slave),
   and the test program firmware (which is an I2C master). */
CY_PACKED typedef struct {
    float32  dacVal;        
    uint8    ledVal;        
    uint8    ledControl;    
    uint8    buttonVal;      
    float32  temperature;
    float32  humidity;
    float32  illuminance;
    float32  potVal;
} CY_PACKED_ATTR dataSet_t;

#endif