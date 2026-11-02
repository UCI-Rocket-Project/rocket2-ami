#include "tc_max31855_spi.h"

TcMax31855Spi::TcMax31855Spi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *csPort, uint16_t csPin, uint32_t timeout) : _hspi(hspi), _csPort(csPort), _csPin(csPin), _timeout(timeout) {}

TcMax31855Spi::Data TcMax31855Spi::Read() {
    TcMax31855Spi::Data data;
    data.valid = false;

    uint8_t buffer[4] = {0};
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);                        // start SPI transaction
    HAL_StatusTypeDef response = HAL_SPI_Receive(_hspi, buffer, 4, _timeout);  // read 8-bits four times from MISO line
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    if (response != HAL_OK) return data;

    uint32_t d = ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) | ((uint32_t)buffer[2] << 8) | ((uint32_t)buffer[3]);

    // detect no data
    if (d == 0) return data;
    // detect fault
    if (d & 0x10000) return data;

    // TC temperature is bit 31 to bit 18
    int32_t tcTempBits = ((d >> 18) & 0x3FFF);
    if (tcTempBits & (1 << 13)) tcTempBits |= 0xFFFFC000;  // sign extend
    data.tcTemperature = tcTempBits / 4.0f;                // 0.25 degrees per LSB

    // internal temperature is bit 15 to bit 4
    int32_t inTempBits = ((d >> 4) & 0xFFF);
    if (inTempBits & (1 << 11)) inTempBits |= 0xFFFFF000;  // sign extend
    data.internalTemperature = inTempBits / 16.0f;         // 0.0625 degrees per LSB

    data.valid = true;
    return data;
}
