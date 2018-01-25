#ifndef TESTLIMITS_H
#define TESTLIMITS_H

// What voltage to set the dac at for testing
#define TEST_LIMIT_DAC (1.0)
// Range to sweep the DAC
#define TEST_LIMIT_DAC_MAX 2.1
#define TEST_LIMIT_DAC_MIN 0.0
// max mV difference in set voltage and measured voltage for a passing result 
#define TEST_LIMIT_DAC_MAX_DIFF (0.1)

// put hand over sensor... it must detect less than this number
#define TEST_LIMIT_ALS_MIN 100.0
// shine light... it must detect over this number
#define TEST_LIMIT_ALS_MAX 1200.0 

// Humidity must be in the range of these two values
#define TEST_LIMIT_HUMIDITY_MAX 90.0
#define TEST_LIMIT_HUMIDITY_MIN 10.0
    

// Temperature must be in the range of these two values
#define TEST_LIMIT_TEMP_MAX 35.0
#define TEST_LIMIT_TEMP_MIN 20.0

// The pot must be swept below and above these two numbers
#define TEST_LIMIT_POT_MAX 2.0
#define TEST_LIMIT_POT_MIN 1.0

    
    
#endif