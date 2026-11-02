#include "imu_bmi088_spi.h"

ImuBmi088Spi::ImuBmi088Spi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *accCsPort, uint16_t accCsPin, GPIO_TypeDef *gyroCsPort, uint16_t gyroCsPin)
    :

      _hspi(hspi),
      _accCsPort(accCsPort),
      _accCsPin(accCsPin),
      _gyroCsPort(gyroCsPort),
      _gyroCsPin(gyroCsPin) {}

/* Notes */
// | 0x00 is for write, | 0x80 is for read
// Accelerometer instruction has a 2nd dummy byte after instruction for reads
// Any array in UPPER_SNAKE_CASE functions as a constant variable

int ImuBmi088Spi::Reset() {
    // setting the chip select pins high
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_SET);

    /* Accelerometer softreset */
    // Accelerometer will be in suspend mode afterwards
    uint8_t ACC_SOFTRESET[2] = {0x7E | 0x00, 0xB6};

    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, ACC_SOFTRESET, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);

    /* Gyroscope softreset */
    uint8_t GYRO_SOFTRESET[2] = {0x14 | 0x00, 0xB6};

    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, GYRO_SOFTRESET, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_SET);

    // Delay for Gyro_softreset to complete
    HAL_Delay(30);

    return 0;
}

int ImuBmi088Spi::Init() {
    // setting the chip select pins high
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_SET);

    /* Accelerometer power on sequence */
    // Switches the Accelerometer from suspend to normal mode
    uint8_t ACC_PWR_CONF[2] = {0x7C | 0x00, 0x00};

    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, ACC_PWR_CONF, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);

    // Switches Accelerometer on
    // 0x04 for for accelerometer on
    uint8_t ACC_PWR_CTRL[2] = {0x7D | 0x00, 0x04};

    // spi write to ACC_PWR_CTRL register to move to normal mode
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, ACC_PWR_CTRL, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);

    // 1 ms delay after change to normal mode
    HAL_Delay(1);

    /* Setting Accelerometer range to +- 24g */
    uint8_t ACC_RANGE[2] = {0x41 | 0x00, 0x03};

    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, ACC_RANGE, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);
    //

    /* Gyro power normal power mode */
    // Switches Gyro from suspend to normal mode
    // 0x00 for set to normal mode
    uint8_t gyro_Lpm1[2] = {0x11 | 0x00, 0x00};

    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, gyro_Lpm1, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_SET);

    // Delay to ensure state change
    HAL_Delay(30);

    /* Accelerometer Chip ID read */
    // Dummy byte 0x00
    uint8_t ACC_CHIP_ID[2] = {0x00 | 0x80, 0x00};
    uint8_t accID[1] = {0};

    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, ACC_CHIP_ID, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_SPI_Receive(_hspi, accID, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);

    /* Gyroscope Chip ID read */
    uint8_t GYRO_CHIP_ID[1] = {0x00 | 0x80};
    uint8_t gyroID[1] = {0};

    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, GYRO_CHIP_ID, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_SPI_Receive(_hspi, gyroID, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_SET);

    /* Self check if device ID of gyro and accelerometer are correct */
    if (accID[0] != 0x1E) return 2;
    if (gyroID[0] != 0x0F) return 3;

    return 0;
}

ImuBmi088Spi::Data ImuBmi088Spi::Read() {
    ImuBmi088Spi::Data data;

    /* Accelerometer data registers read */
    uint8_t ACC_DATA_REGISTERS[2] = {0x12 | 0x80, 0x00};
    uint8_t accelerometerData[6] = {0};

    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, ACC_DATA_REGISTERS, 2, SERIAL_TIMEOUT) != HAL_OK) return data;
    if (HAL_SPI_Receive(_hspi, accelerometerData, 6, SERIAL_TIMEOUT) != HAL_OK) return data;
    HAL_GPIO_WritePin(_accCsPort, _accCsPin, GPIO_PIN_SET);

    /* Gyroscope data registers read */
    uint8_t GYRO_DATA_REGISTERS[1] = {0x02 | 0x80};
    uint8_t gyroscopeData[6] = {0};

    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, GYRO_DATA_REGISTERS, 1, SERIAL_TIMEOUT) != HAL_OK) return data;
    if (HAL_SPI_Receive(_hspi, gyroscopeData, 6, SERIAL_TIMEOUT) != HAL_OK) return data;
    HAL_GPIO_WritePin(_gyroCsPort, _gyroCsPin, GPIO_PIN_SET);

    // Acceleration and angular velocity data being attributed into the Data struct
    data.accelerationX = ((accelerometerData[1] << 8) + (accelerometerData[0]));
    data.accelerationY = ((accelerometerData[3] << 8) + (accelerometerData[2]));
    data.accelerationZ = ((accelerometerData[5] << 8) + (accelerometerData[4]));

    data.angularVelocityX = ((gyroscopeData[1] << 8) + (gyroscopeData[0]));
    data.angularVelocityY = ((gyroscopeData[3] << 8) + (gyroscopeData[2]));
    data.angularVelocityZ = ((gyroscopeData[5] << 8) + (gyroscopeData[4]));

    return data;
}
