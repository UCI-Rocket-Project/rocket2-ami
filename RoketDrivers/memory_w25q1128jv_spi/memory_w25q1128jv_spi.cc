#include <memory_w25q1128jv_spi.h>

MemoryW25q1128jvSpi::MemoryW25q1128jvSpi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *csPort, uint16_t csPin, GPIO_TypeDef *holdPort, uint16_t holdPin, GPIO_TypeDef *wpPort, uint16_t wpPin)

    : _hspi(hspi), _csPort(csPort), _csPin(csPin), _holdPort(holdPort), _holdPin(holdPin), _wpPort(wpPort), _wpPin(wpPin) {}

/* Notes */
// Any UPPER_SNAKE_CASE variables are opcodes for instruction to sent to memory

int MemoryW25q1128jvSpi::Init() {
    // Control pins his for no false memory actions
    HAL_GPIO_WritePin(_wpPort, _wpPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_holdPort, _holdPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // Buffer variable to read memory data
    uint8_t buffer[64];

    // Reads Chip ID, if chip ID is not W25Q128JV_MEMORY, then return -1
    if (DeviceID() != 0xEF17) return -1;

    // do while to find the first free 64 byte address
    do {
        address = ChipReadDump(buffer);
    } while (buffer[0] != 0xFF);

    return 0;
}

int MemoryW25q1128jvSpi::DeviceID() {
    // Control pins his for no false memory actions
    HAL_GPIO_WritePin(_wpPort, _wpPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_holdPort, _holdPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // input instruction hex + 3 dummy bytes
    uint8_t DEVICE_ID[4] = {0x90, 0x00, 0x00, 0x00};
    // output array of bytes
    uint8_t deviceValue[2];

    // SPI protocol
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, DEVICE_ID, 4, SPI_Timeout) != HAL_OK) return -1;
    if (HAL_SPI_Receive(_hspi, deviceValue, 2, SPI_Timeout) != HAL_OK) return -1;
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // returns the 2 bytes as a uint16_t
    return (int)(deviceValue[0] << 8 | deviceValue[1]);
}

int MemoryW25q1128jvSpi::ReadStatusReg1() {
    // Control pins his for no false memory actions
    HAL_GPIO_WritePin(_wpPort, _wpPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_holdPort, _holdPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // input operation code for reading status register 1
    uint8_t READ_STATUS_REG_1[1] = {0x05};
    // output -- 8 bit value of the status register
    uint8_t regValue[1];

    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, READ_STATUS_REG_1, 1, SPI_Timeout) != HAL_OK) return -1;
    if (HAL_SPI_Receive(_hspi, regValue, 1, SPI_Timeout) != HAL_OK) return -1;
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    return (int)regValue[0];
}

int MemoryW25q1128jvSpi::ChipErase() {
    // Control pins his for no false memory actions
    HAL_GPIO_WritePin(_wpPort, _wpPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_holdPort, _holdPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    /* Set Write Enable to 1 */
    uint8_t CHIP_ENABLE[1] = {0x06};

    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, CHIP_ENABLE, 1, SPI_Timeout) != HAL_OK) return -1;
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
    ;
    while ((MemoryW25q1128jvSpi::ReadStatusReg1() & 0x02) != 2) {
    }

    /* Initiate Chip Erase */
    uint8_t CHIP_ERASE[1] = {0xC7};

    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, CHIP_ERASE, 1, SPI_Timeout) != HAL_OK) return -1;
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // Waits until busy bit is = 0
    // write busy is 1st bit in status reg
    while ((MemoryW25q1128jvSpi::ReadStatusReg1() & 0x01) != 0) {
    }

    return 0;
}

MemoryW25q1128jvSpi::State MemoryW25q1128jvSpi::ChipWrite(uint8_t (&data)[64]) {
    // Control pins his for no false memory actions
    HAL_GPIO_WritePin(_wpPort, _wpPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_holdPort, _holdPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // if address variable goes over the amount of pages memory has
    // no more collected data will be written into the memory
    if (address > 0xFFFFFF) _state = State::ERROR;

    switch (_state) {
        case State::CHIP_ENABLE: {
            _state = State::PAGE_WRITE;

            /* Set Write Enable to 1 */
            uint8_t CHIP_ENABLE[1] = {0x06};

            // chip enable instruction to enable chip
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
            if (HAL_SPI_Transmit(_hspi, CHIP_ENABLE, 1, SPI_Timeout) != HAL_OK) {
                _state = State::ERROR;
                break;
            }
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
        }

        case State::PAGE_WRITE: {
            // Checks if write enable latch is = 1
            // write enable latch is 1st bit in status reg
            if ((MemoryW25q1128jvSpi::ReadStatusReg1() & 0x02) != 2) break;

            _state = State::COMPLETE;

            /* Write the 64 bytes into the memory  */
            uint8_t PAGE_PROGRAM[4] = {0x02, (uint8_t)(address >> 16), (uint8_t)(address >> 8), (uint8_t)(address)};

            // Write 64 byte Data  into memory
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
            if (HAL_SPI_Transmit(_hspi, PAGE_PROGRAM, 4, SPI_Timeout) != HAL_OK) {
                _state = State::ERROR;
                break;
            }
            if (HAL_SPI_Transmit(_hspi, data, 64, SPI_Timeout) != HAL_OK) {
                _state = State::ERROR;
                break;
            }
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
        }

        case State::COMPLETE: {
            // Checks if busy bit is = 0
            // write busy is 1st bit in status reg
            if ((MemoryW25q1128jvSpi::ReadStatusReg1() & 0x01) != 0) break;

            _state = State::CHIP_ENABLE;

            // increments to the next 64 byte address
            address += 64;

            break;
        }

        default:
            break;
    }
    return _state;
}

int MemoryW25q1128jvSpi::ChipRead(uint32_t readAddress, uint8_t (&chipData)[64]) {
    // Control pins his for no false memory actions
    HAL_GPIO_WritePin(_wpPort, _wpPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_holdPort, _holdPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // initalisation of input, output, and dumby telemtrydata variable for temporary writing to DataBundle
    uint8_t READ_DATA[4] = {0x03, (uint8_t)(readAddress >> 16), (uint8_t)(readAddress >> 8), (uint8_t)(readAddress)};

    // Reads address page in memory
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, READ_DATA, 4, SPI_Timeout) != HAL_OK) return -1;
    if (HAL_SPI_Receive(_hspi, chipData, 64, SPI_Timeout) != HAL_OK) return -1;
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    // Waits until busy bit is = 0
    // write busy is 1st bit in status reg
    while ((MemoryW25q1128jvSpi::ReadStatusReg1() & 0x01) != 0) {
    }

    return 0;
}

int MemoryW25q1128jvSpi::ChipReadDump(uint8_t (&chipData)[64]) {
    // counter of address being read in function
    static uint32_t dumpAddress = 0;

    // initalisation of input, output, and dumby telemtrydata variable for temporary writing to DataBundle
    static uint8_t READ_DATA[4] = {0x03, 0x00, 0x00, 0x00};

    if (dumpAddress > 0) {
        // Reads continuously the next 64 byte data package
        if (HAL_SPI_Receive(_hspi, chipData, 64, SPI_Timeout) != HAL_OK) return -1;
    } else if (dumpAddress == 0) {
        // Reads the 1st address page in memory
        HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
        if (HAL_SPI_Transmit(_hspi, READ_DATA, 4, SPI_Timeout) != HAL_OK) return -1;
        if (HAL_SPI_Receive(_hspi, chipData, 64, SPI_Timeout) != HAL_OK) return -1;
    }

    // Checks if read 64 bytes is a data package
    if (chipData[0] == 0xFF) {
        HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
        return dumpAddress;
    }

    dumpAddress += 64;

    return dumpAddress - 64;
}

// test the chip erase function
