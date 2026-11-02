#pragma once

#include <cstdint>

#include "main.h"

#ifndef SERIAL_TIMEOUT
#define SERIAL_TIMEOUT 10
#endif

/* Datasheet */
/* https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi088-ds001.pdf */

class ImuBmi088Spi {
  public:
    struct Data {
        // Angular velocity counterclockwise along x-axis, launch vehicle frame
        // Units 6.1*10^-2 degree/s
        int16_t angularVelocityX = 0xFFFF;

        // Angular velocity counterclockwise along y-axis, launch vehicle frame
        // Units 6.1*10^-2 degree/s
        int16_t angularVelocityY = 0xFFFF;

        // Angular velocity counterclockwise along z-axis, launch vehicle frame
        // Units 6.1*10^-2 degree/s
        int16_t angularVelocityZ = 0xFFFF;

        // Acceleration along x-axis, launch vehicle frame
        // Units: 7.3 * 10^-4 g
        int16_t accelerationX = 0xFFFF;

        // Acceleration along y-axis, launch vehicle frame
        // Units: 7.3 * 10^-4 g
        int16_t accelerationY = 0xFFFF;

        // Acceleration along x-axis, launch vehicle frame
        // Units: 7.3 * 10^-4 g
        int16_t accelerationZ = 0xFFFF;
    };

    /**
     * @param hspi        SPI bus handler
     * @param accCsPort   chip select port accelerometer
     * @param accCsPin    chip select pin  accelerometer
     * @param gyroCsPort  chip select port gyroscope
     * @param gyroCsPin   chip select pin  gyroscope
     */

    ImuBmi088Spi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *accCsPort, uint16_t accCsPin, GPIO_TypeDef *gyroCsPort, uint16_t gyroCsPin);

    /**
     * @brief Resets the IMU
     * @retval Operation status, 0 for success
     * @retval Operation failure, 1 for SPI transmit/recieve failure
     */
    int Reset();

    /**
     * @brief Initializes the  IMU
     * @retval Operation status, 0 for success
     * @retval Operation failure, 1 for SPI transmit/recieve failure
     * @retval Operation failure, 2 for accelerometer ID fault
     * @retval Operation failure, 3 for gyroscope ID fault
     */
    int Init();

    /**
     * @brief Reads Gyroscope and Acceleration data
     * @retval Output is struct Data
     * @retval if any values = FFFF, then ERROR
     */
    Data Read();

  private:
    // SPI bus handler
    SPI_HandleTypeDef *_hspi;

    // Chip select port
    GPIO_TypeDef *_accCsPort;

    // chip select pin  accelerometer
    uint16_t _accCsPin;

    // chip select port gyroscope
    GPIO_TypeDef *_gyroCsPort;

    // chip select pin  gyroscope
    uint16_t _gyroCsPin;
};
