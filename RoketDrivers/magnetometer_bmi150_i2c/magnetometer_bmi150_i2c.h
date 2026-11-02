#pragma once

#include <cstdint>

#include "main.h"

#ifndef SERIAL_TIMEOUT
#define SERIAL_TIMEOUT 10
#endif

#define I2C_DELAY 5

/* Datasheet */
/* https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmm150-ds001.pdf
 */

class MagBmi150i2c {
  public:
    struct Data {
        // Magnetic field along x-axis, launch vehicle frame
        // Units: microTesla (uT)
        int16_t magneticFieldX = 0xFFFF;

        // Magnetic field along y-axis, launch vehicle frame
        // Units: microTesla (uT)
        int16_t magneticFieldY = 0xFFFF;

        // Magnetic field along z-axis, launch vehicle frame
        // Units: microTesla (uT)
        int16_t magneticFieldZ = 0xFFFF;
    };

    /**
     * @param hi2c        i2c bus handler
     * @param intPort     Configureable interupt port
     * @param intPin      Configureable interupt pin
     * @param drdyPort    Data ready interupt port
     * @param drdyPin     Data ready interupt pin
     * st
     */
    MagBmi150i2c(I2C_HandleTypeDef *hi2c, GPIO_TypeDef *intPort, uint16_t intPin, GPIO_TypeDef *drdyPort, uint16_t drdyPin);

    /**
     * @brief Resets the Magnetometer
     * @retval Operation status, 0 for success
     * @retval Operation failure, 1 for I2C transmit/recieve failure
     */
    int Reset();

    /**
     * @brief Initializes the  Magnetometer
     * @retval Operation status, 0 for success
     * @retval Operation failure, 1 for I2C transmit/recieve failure
     * @retval Operation failure, 2 for wrong device failure
     * @retval Operation failure, 3 for I2C is HAL_BUSY, should call reset
     */
    int Init();

    /**
     * @brief Reads Magnetometer data
     * @retval Output is struct Data
     * @retval if any values = FFFF, then ERROR
     */
    Data Read();

  private:
    // SPI bus handler
    I2C_HandleTypeDef *_hi2c;
    // Configureable interupt port
    GPIO_TypeDef *_intPort;
    // Configureable interupt pin
    uint16_t _intPin;
    // Data ready interupt port
    GPIO_TypeDef *_drdyPort;
    // Data ready interupt pin
    uint16_t _drdyPin;

    /* Trim Data for calibration*/
    int8_t dig_X1;
    int8_t dig_Y1;

    int8_t dig_X2;
    int8_t dig_Y2;

    uint8_t dig_XY1;
    int8_t dig_XY2;

    uint16_t dig_Z1;
    int16_t dig_Z2;
    int16_t dig_Z3;
    int16_t dig_Z4;

    uint16_t dig_XYZ1;
};
