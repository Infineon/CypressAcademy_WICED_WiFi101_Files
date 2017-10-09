-- =============================================================================
-- The following directives assign pins to the locations specific for the
-- CY8CKIT-041 PSOC 4000S PIONEER KIT.
-- =============================================================================

-- === I2C ===
attribute port_location of \I2C_Slave:scl(0)\ : label is "PORT(3,0)";
attribute port_location of \I2C_Slave:sda(0)\ : label is "PORT(3,1)";

-- === RGB LED ===
attribute port_location of Bootloader_Status(0) : label is "PORT(3,6)"; -- BLUE LED