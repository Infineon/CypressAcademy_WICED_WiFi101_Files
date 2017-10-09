-- =============================================================================
-- The following directives assign pins to the locations specific for the
-- CY8CKIT-040 kit.
-- =============================================================================

-- === I2C ===
attribute port_location of \I2C_Slave:scl(0)\ : label is "PORT(1,2)";
attribute port_location of \I2C_Slave:sda(0)\ : label is "PORT(1,3)";

-- === RGB LED ===
attribute port_location of Bootloader_Status(0) : label is "PORT(0,2)"; -- BLUE LED