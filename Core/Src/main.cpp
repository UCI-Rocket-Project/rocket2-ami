#include "main.h"
#include "cpp_main.h"
#include "ami_controller.h"

// // Inspect these in the debugger after startup. Registers 0x00..0x03 are
// // DEVID_AD, DEVID_MST, PART_ID, and PART_ID_REV_ID, respectively.
// volatile uint8_t adxl383_ids[4] = {};
// volatile HAL_StatusTypeDef adxl383_id_status = HAL_ERROR;
// volatile bool adxl383_id_valid = false;

// static HAL_StatusTypeDef adxl383_read_register(uint8_t address, uint8_t *value) {
//     // ADXL383 SPI command: 7-bit address followed by R/W=1, then 8 clocks
//     // to shift out the register value. Keep CS low for both bytes.
//     uint8_t tx[2] = {static_cast<uint8_t>((address << 1U) | 1U), 0x00};
//     uint8_t rx[2] = {};

//     HAL_GPIO_WritePin(ACCEL_nCS_GPIO_Port, ACCEL_nCS_Pin, GPIO_PIN_RESET);
//     HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, 100);
//     HAL_GPIO_WritePin(ACCEL_nCS_GPIO_Port, ACCEL_nCS_Pin, GPIO_PIN_SET);

//     if (status == HAL_OK) {
//         *value = rx[1];
//     }
//     return status;
// }

void cpp_main(void) {
    static AMIController controller;
    controller.Init();
    controller.Run();
    // // GPIO initialization leaves ACCEL_nCS low; deselect the sensor before
    // // starting a new SPI transaction. Allow its power-up/NVM load to finish.
    // HAL_GPIO_WritePin(ACCEL_nCS_GPIO_Port, ACCEL_nCS_Pin, GPIO_PIN_SET);
    // HAL_Delay(3);

    // adxl383_id_status = HAL_OK;
    // for (uint8_t address = 0; address < 4; ++address) {
    //     uint8_t value = 0;
    //     adxl383_id_status = adxl383_read_register(address, &value);
    //     if (adxl383_id_status != HAL_OK) {
    //         break;
    //     }
    //     adxl383_ids[address] = value;
    // }
    // adxl383_id_valid = adxl383_id_status == HAL_OK &&
    //                     adxl383_ids[0] == 0xAD && adxl383_ids[1] == 0x1D &&
    //                     adxl383_ids[2] == 0x17 && (adxl383_ids[3] & 0xF0) == 0xC0;
}
