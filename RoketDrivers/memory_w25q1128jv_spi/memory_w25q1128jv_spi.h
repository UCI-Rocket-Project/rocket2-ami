/*
 * W25Q128JV_MEMORY.h
 *
 *  Created on: Jan 1, 2024
 *      Author: rober
 */
#pragma once

#include <cmath>
#include <cstdint>

#include "main.h" // # includes "stm32xxxx_hal.h"

#ifndef SPI_Timeout
#define SPI_Timeout 10
#endif

class MemoryW25q1128jvSpi {
  public:
    enum class State { CHIP_ENABLE, PAGE_WRITE, COMPLETE, ERROR };

    /**
     * @param hspi 		 SPI bus handler
     * @param csPort 	 chip select GPIO port
     * @param csPin 	 chip select GPIO pin
     * @param holdPort  Write HOLD GPIO port
     * @param holdPin 	 Write HOLD GPIO pin
     * @param wpPort 	 Write Protect GPIO port
     * @param wpPin	 Write Protect GPIO pin
     */
    MemoryW25q1128jvSpi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *csPort, uint16_t csPin, GPIO_TypeDef *holdPort, uint16_t holdPin, GPIO_TypeDef *wpPort, uint16_t wpPin);

    /**
     * @brief Initialises the memory by finding the first free 64 bytes
     * @retval Operation Sucess, 0 for success
     * @retval Operation Failure, -1 for wrong devoce
     */
    int Init();

    /**
     * @brief Reads the device ID of the memory/
     * 		  Should output 0xEF for manufacturer ID,
     * 		  Should output 0x90 for Device ID
     * @retval Operation Success, 0xEF17 is return value
     * @retval Operation Failure, -1 for SPI protocol failure
     */
    int DeviceID();

    /**
     * @brief Reads the first status register from memort
     * @retval Operation Sucess, Register value from 0-15 for success
     * @retval Operation Failure, -1 for SPI protocol failure
     */
    int ReadStatusReg1();

    /**
     * @brief Erases all data on chip
     * @retval Operation Sucess, 0 for success
     * @retval Operation Failure, -1 for SPI protocol failure
     */
    int ChipErase();

    /**
     * @brief Writes a 64 byte array into flasg memory,
     *        Auto increments from address found in Init
     * @retval Operation FSM status
     */
    State ChipWrite(uint8_t (&data)[64]);

    /**
     * @brief Reads 64 bytes starting from read_address input,
     *        ChipRead outout value is updated in input chipData
     * @retval Operation Sucess, 0 for success
     * @retval Operation Failure, -1 for SPI protocol failure
     */
    int ChipRead(uint32_t read_address, uint8_t (&chipData)[64]);

    /**
     * @brief Dumps each 64 byte data package from address 0,
     *        auto increments each 64 byte data package
     * @retval Operation Sucess, current read address value for success
     * @retval Operation Failure, -1 for SPI protocol failure
     */
    int ChipReadDump(uint8_t (&chipData)[64]);

    /* 64 byte structs for use in UCIRP projects*/
#pragma pack(push, 1)
    struct EcuTelemetryData {
        uint8_t type = 0x10;
        uint32_t timestamp;
        uint64_t : 8;
        int16_t angularVelocityX = 0xFFFF;
        int16_t angularVelocityY = 0xFFFF;
        int16_t angularVelocityZ = 0xFFFF;
        int16_t accelerationX = 0xFFFF;
        int16_t accelerationY = 0xFFFF;
        int16_t accelerationZ = 0xFFFF;
        int16_t magneticFieldX = 0xFFFF;
        int16_t magneticFieldY = 0xFFFF;
        int16_t magneticFieldZ = 0xFFFF;
        int16_t temperature = 0xFFFF;
        int32_t altitude = 0xFFFFFFFF;
        int32_t ecefPositionX = 0xFFFFFFFF;
        int32_t ecefPositionY = 0xFFFFFFFF;
        int32_t ecefPositionZ = 0xFFFFFFFF;
        uint32_t ecefPositionAccuracy = 0xFFFFFFFF;
        int32_t ecefVelocityX = 0xFFFFFFFF;
        int32_t ecefVelocityY = 0xFFFFFFFF;
        int32_t ecefVelocityZ = 0xFFFFFFFF;
        uint32_t ecefVelocityAccuracy = 0xFFFFFFFF;
        uint16_t crc = 0x0000;
    };
#pragma pack(pop)

#pragma pack(push, 1)
    struct AfsTelemetryData {
        uint8_t type = 0x00;
        uint32_t timestamp;
        uint8_t state;
        int16_t angularVelocityX = 0xFFFF;
        int16_t angularVelocityY = 0xFFFF;
        int16_t angularVelocityZ = 0xFFFF;
        int16_t accelerationX = 0xFFFF;
        int16_t accelerationY = 0xFFFF;
        int16_t accelerationZ = 0xFFFF;
        int16_t magneticFieldX = 0xFFFF;
        int16_t magneticFieldY = 0xFFFF;
        int16_t magneticFieldZ = 0xFFFF;
        int16_t temperature = 0xFFFF;
        int32_t altitude = 0xFFFFFFFF;
        int32_t ecefPositionX = 0xFFFFFFFF;
        int32_t ecefPositionY = 0xFFFFFFFF;
        int32_t ecefPositionZ = 0xFFFFFFFF;
        uint32_t ecefPositionAccuracy = 0xFFFFFFFF;
        int32_t ecefVelocityX = 0xFFFFFFFF;
        int32_t ecefVelocityY = 0xFFFFFFFF;
        int32_t ecefVelocityZ = 0xFFFFFFFF;
        uint32_t ecefVelocityAccuracy = 0xFFFFFFFF;
        uint16_t crc = 0x0000;
    };
#pragma pack(pop)

#pragma pack(push, 1)
    struct AfsState {
        bool armPinState : 1;       // 0: unarmed, 1: armed
        bool drogueContinuity : 1;  // 0: no continuity, 1: continuity
        bool mainContinuity : 1;    // 0: no continuity, 1: continuity
        uint16_t : 1;               // reserved
        /**
         * 0x0: standby
         * 0x1: armed
         * 0x2: boost
         * 0x3: coast
         * 0x4: apogee
         * 0x5: drogue fired
         * 0x6: drogue opened
         * 0x7 drogue failure
         * 0x8: main fired
         * 0x9: main opened
         * 0xA: main failure
         * 0xB: land    return 0;
         */
        uint8_t state : 4;
    };
#pragma pack(pop)

  private:
    SPI_HandleTypeDef *_hspi;

    GPIO_TypeDef *_csPort;

    uint16_t _csPin;

    GPIO_TypeDef *_holdPort;

    uint16_t _holdPin;

    GPIO_TypeDef *_wpPort;

    uint16_t _wpPin;

    State _state = State::CHIP_ENABLE;

    uint32_t address = 0;
};
