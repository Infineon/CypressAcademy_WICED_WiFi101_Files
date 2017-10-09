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