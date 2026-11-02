#include "magnetometer_bmi150_i2c.h"

MagBmi150i2c::MagBmi150i2c(I2C_HandleTypeDef *hi2c, GPIO_TypeDef *intPort, uint16_t intPin, GPIO_TypeDef *drdyPort, uint16_t drdyPin)
    : _hi2c(hi2c), _intPort(intPort), _intPin(intPin), _drdyPort(drdyPort), _drdyPin(drdyPin) {}

/* Notes */
// Any array in camelCase is an array that the device outputs to
// Any array in UPPER_SNAKE_CASE functions as a constant variable and is the
// input hex the device uses as an input

/******************** HELPER FUNCTION ********************/
/**
 * @brief       Shift Arithmetic Right
 * @param val   signed short, input to shift arithmetic right
 * @param sh    shift right by sh value
 * @retval      signed short, Result of shift arithmetic right
 */
int16_t sar(int16_t val, unsigned sh);

/******************** MAIN HEADER FUNCTIONS ********************/
int MagBmi150i2c::Reset() {
    /* check if I2C is busy */
    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(_hi2c, 0x10 << 1, 1, 100);

    /* reset i2c line if busy */
    if (status != HAL_OK) {
        GPIO_InitTypeDef GPIO_InitStruct = {0};

        HAL_I2C_DeInit(_hi2c);

        // Set SCLK as GPIO
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
        GPIO_InitStruct.Pin = GPIO_PIN_6;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_LOW;
        HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);

        // complete 10 cycles of SCLK to release module's last task
        for (int i = 0; i < 10; i++) {
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
            HAL_Delay(20);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
            HAL_Delay(20);
        }

        HAL_I2C_Init(_hi2c);
    }

    // if still busy, idk lol, its prob somehthing else
    if (HAL_I2C_IsDeviceReady(_hi2c, 0x10 << 1, 1, 100) != HAL_OK) return 1;

    /* Soft-Power reset */
    // writes 1's into 7 and 1 bit in power register 0x4B
    uint8_t POWER_CONTROL_1[2] = {0x4B, 0x82};

    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, POWER_CONTROL_1, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    return 0;
}

int MagBmi150i2c::Init() {
    /* Check if i2c is read */
    HAL_StatusTypeDef ret = HAL_I2C_IsDeviceReady(_hi2c, 0x10 << 1, 5, SERIAL_TIMEOUT);

    if (ret == HAL_BUSY) {
        return 3;
    } else if (ret == HAL_ERROR) {
        return 1;
    }

    /* Turn on Mag */
    // set mag from suspend to sleep mode
    uint8_t POWER_CONTROL_1[2] = {0x4B, 0x01};

    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, POWER_CONTROL_1, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    /* Chip ID read */
    uint8_t CHIP_ID[1] = {0x40};
    uint8_t chipIDRead[1];

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, CHIP_ID, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, chipIDRead, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;

    if (chipIDRead[0] != 0x32) return 2;

    /* Setting BMM150 to High accuracy preset */

    /* Set power mode and Output Data Rate */
    // 0x28 to set power mode to normal and ODR to 20 Hz
    uint8_t POWER_CONTROL_2[2] = {0x4C, 0x28};

    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, POWER_CONTROL_2, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    /* set REP_XY and REP_Z settings */
    uint8_t SET_REP_XY[2] = {0x51, 0x17};
    uint8_t SET_REP_Z[2] = {0x52, 0x53};

    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, SET_REP_XY, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, SET_REP_Z, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    /* Trim data for calibration */
    // Calibration register values found here
    // https://community.bosch-sensortec.com/t5/MEMS-sensors-forum/BMM150-Calibration-data-and-Range/td-p/72674

    // trim data output array to be data formatted
    uint8_t trimDataOut[2];

    // Memory Reg to be read
    uint8_t TRIM_DIG[1];

    // dig_x1 & dig_y1
    TRIM_DIG[0] = 0x5D;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_X1 = trimDataOut[0];
    dig_Y1 = trimDataOut[1];

    // dig_x2 dig_y2
    TRIM_DIG[0] = 0x64;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_X2 = trimDataOut[0];
    dig_Y2 = trimDataOut[1];

    // dig_xy1 & dig_xy2
    TRIM_DIG[0] = 0x70;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_XY1 = trimDataOut[1];
    dig_XY2 = trimDataOut[0];

    // dig_z1
    TRIM_DIG[0] = 0x6A;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_Z1 = (trimDataOut[1] << 8) | trimDataOut[0];

    // dig_z2
    TRIM_DIG[0] = 0x68;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_Z2 = (trimDataOut[1] << 8) | trimDataOut[0];

    // dig_z3
    TRIM_DIG[0] = 0x6E;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_Z3 = (trimDataOut[1] << 8) | trimDataOut[0];

    // dig_z4
    TRIM_DIG[0] = 0x62;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_Z4 = (trimDataOut[1] << 8) | trimDataOut[0];

    // dig_xyz1
    TRIM_DIG[0] = 0x6C;

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, TRIM_DIG, 1, SERIAL_TIMEOUT) != HAL_OK) return 1;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, trimDataOut, 2, SERIAL_TIMEOUT) != HAL_OK) return 1;

    dig_XYZ1 = (trimDataOut[1] << 8) | trimDataOut[0];

    return 0;
}

MagBmi150i2c::Data MagBmi150i2c::Read() {
    MagBmi150i2c::Data data;

    // Raw Data xyz and rHall values
    int16_t rawXData;
    int16_t rawYData;
    int16_t rawZData;
    int16_t rawRHALLData;

    /* reading RAW data */
    uint8_t magData[8] = {0x42};

    HAL_Delay(I2C_DELAY);
    if (HAL_I2C_Master_Transmit(_hi2c, 0x10 << 1, magData, 1, SERIAL_TIMEOUT) != HAL_OK) return data;
    if (HAL_I2C_Master_Receive(_hi2c, 0x10 << 1, magData, 8, SERIAL_TIMEOUT) != HAL_OK) return data;

    rawXData = ((uint16_t)magData[1] << 8 | magData[0]);
    rawYData = ((uint16_t)magData[3] << 8 | magData[2]);
    rawZData = ((uint16_t)magData[5] << 8 | magData[4]);
    rawRHALLData = ((uint16_t)magData[7] << 8 | magData[6]);

    rawXData = sar(rawXData, 3);
    rawYData = sar(rawYData, 3);
    rawZData = sar(rawXData, 1);
    rawRHALLData = sar(rawRHALLData, 2);

    /* Compensated calculation for XYZ data values */
    // Compensated code below found on BMM150 API code
    // https://github.com/boschsensortec/BMM150_SensorAPI/blob/master/bmm150.c

    /* compensated X calculation */
    uint16_t process_comp_x0 = 0;
    int32_t process_comp_x1;
    uint16_t process_comp_x2;
    int32_t process_comp_x3;
    int32_t process_comp_x4;
    int32_t process_comp_x5;
    int32_t process_comp_x6;
    int32_t process_comp_x7;
    int32_t process_comp_x8;
    int32_t process_comp_x9;
    int32_t process_comp_x10;

    /* Overflow condition check */
    if (rawXData != -4096) {
        if (rawRHALLData != 0) {
            /* Availability of valid data */
            process_comp_x0 = rawRHALLData;
        } else if (dig_XYZ1 != 0) {
            process_comp_x0 = dig_XYZ1;
        } else {
            process_comp_x0 = 0;
        }

        if (process_comp_x0 != 0) {
            /* Processing compensation equations */
            process_comp_x1 = ((int32_t)dig_XYZ1) * 16384;
            process_comp_x2 = ((uint16_t)(process_comp_x1 / process_comp_x0)) - ((uint16_t)0x4000);
            data.magneticFieldX = ((int16_t)process_comp_x2);
            process_comp_x3 = (((int32_t)data.magneticFieldX) * ((int32_t)data.magneticFieldX));
            process_comp_x4 = (((int32_t)dig_XY2) * (process_comp_x3 / 128));
            process_comp_x5 = (int32_t)(((int16_t)dig_XY1) * 128);
            process_comp_x6 = ((int32_t)data.magneticFieldX) * process_comp_x5;
            process_comp_x7 = (((process_comp_x4 + process_comp_x6) / 512) + ((int32_t)0x100000));
            process_comp_x8 = ((int32_t)(((int16_t)dig_X2) + ((int16_t)0xA0)));
            process_comp_x9 = ((process_comp_x7 * process_comp_x8) / 4096);
            process_comp_x10 = ((int32_t)rawXData) * process_comp_x9;
            data.magneticFieldX = ((int16_t)(process_comp_x10 / 8192));
            data.magneticFieldX = (data.magneticFieldX + (((int16_t)dig_X1) * 8)) / 16;
        } else {
            data.magneticFieldX = -32768;
        }
    } else {
        /* Overflow condition */
        data.magneticFieldX = -32768;
    }

    /* compensated X calculation */
    uint16_t process_comp_y0 = 0;
    int32_t process_comp_y1;
    uint16_t process_comp_y2;
    int32_t process_comp_y3;
    int32_t process_comp_y4;
    int32_t process_comp_y5;
    int32_t process_comp_y6;
    int32_t process_comp_y7;
    int32_t process_comp_y8;
    int32_t process_comp_y9;

    /* Overflow condition check */
    if (rawYData != -4096) {
        if (rawRHALLData != 0) {
            /* Availability of valid data */
            process_comp_y0 = rawRHALLData;
        } else if (dig_XYZ1 != 0) {
            process_comp_y0 = dig_XYZ1;
        } else {
            process_comp_y0 = 0;
        }

        if (process_comp_y0 != 0) {
            /* Processing compensation equations */
            process_comp_y1 = (((int32_t)dig_XYZ1) * 16384) / process_comp_y0;
            process_comp_y2 = ((uint16_t)process_comp_y1) - ((uint16_t)0x4000);
            data.magneticFieldY = ((int16_t)process_comp_y2);
            process_comp_y3 = ((int32_t)data.magneticFieldY) * ((int32_t)data.magneticFieldY);
            process_comp_y4 = ((int32_t)dig_XY2) * (process_comp_y3 / 128);
            process_comp_y5 = ((int32_t)(((int16_t)dig_XY1) * 128));
            process_comp_y6 = ((process_comp_y4 + (((int32_t)data.magneticFieldY) * process_comp_y5)) / 512);
            process_comp_y7 = ((int32_t)(((int16_t)dig_Y2) + ((int16_t)0xA0)));
            process_comp_y8 = (((process_comp_y6 + ((int32_t)0x100000)) * process_comp_y7) / 4096);
            process_comp_y9 = (((int32_t)rawYData) * process_comp_y8);
            data.magneticFieldY = (int16_t)(process_comp_y9 / 8192);
            data.magneticFieldY = (data.magneticFieldY + (((int16_t)dig_Y1) * 8)) / 16;
        } else {
            data.magneticFieldY = -32768;
        }
    } else {
        /* Overflow condition */
        data.magneticFieldY = -32768;
    }

    /* compensated Z calculation */
    int16_t process_comp_z0;
    int32_t process_comp_z1;
    int32_t process_comp_z2;
    int32_t process_comp_z3;
    int16_t process_comp_z4;

    if (rawZData != -16384) {
        if ((dig_Z2 != 0) && (dig_Z1 != 0) && (rawRHALLData != 0) && (dig_XYZ1 != 0)) {
            /*Processing compensation equations */
            process_comp_z0 = ((int16_t)rawRHALLData) - ((int16_t)dig_XYZ1);
            process_comp_z1 = (((int32_t)dig_Z3) * ((int32_t)(process_comp_z0))) / 4;
            process_comp_z2 = (((int32_t)(rawZData - dig_Z4)) * 32768);
            process_comp_z3 = ((int32_t)dig_Z1) * (((int16_t)rawRHALLData) * 2);
            process_comp_z4 = (int16_t)((process_comp_z3 + (32768)) / 65536);
            data.magneticFieldZ = ((process_comp_z2 - process_comp_z1) / (dig_Z2 + process_comp_z4));

            /* Saturate result to +/- 2 micro-tesla */
            if (data.magneticFieldZ > 32767) {
                data.magneticFieldZ = 32767;
            } else if (data.magneticFieldZ < -32767) {
                data.magneticFieldZ = -32767;
            }

            /* Conversion of LSB to micro-tesla */
            data.magneticFieldZ = data.magneticFieldZ / 16;
        } else {
            data.magneticFieldZ = -32768;
        }
    } else {
        /* Overflow condition */
        data.magneticFieldZ = -32768;
    }

    return data;
}

int16_t sar(int16_t val, unsigned sh) { return (int16_t)((int32_t)val >> sh); }
