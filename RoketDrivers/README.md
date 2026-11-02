# UCIRP STM32 HAL Drivers
To use the drivers, add this repository as a submodule with:
```sh
git submodule add https://github.com/UCI-Rocket-Project/stm32hal-drivers.git lib
```

### Contributing
1. Drivers for each device should be placed in a separate directory:
```
|--device_one
|  |--docs
|  |--examples
|  |--src
|  |  |- device_one.cc
|  |  |- device_one.h
|--device_two
|  |- device_two.cc
|  |- device_two.h
|-README.md
```
2. Dedicated READMEs for each driver is recommended but not required, while comments within the source files that outline the essentials (function parameters, structs) are required.

3. Drivers should have a descriptive name in the following format:
```
[deviceType]_[devicePartNumber]_[communicationProtocol]
```
For example, the MS5607 Altimeter Driver over SPI will have the name:
```
altimeter_ms5607_spi
```

4. All supported STM32 product lines shall be referenced inside the header file via macros. For example:
```c
#if defined(STM32F1)
#include "stm32f1xx_hal.h"
#endif
```

5. All contributed code should be formatted with `clang-format` and screened with `pre-commit`.\
Main branch is locked and all changes require a PR.

6. Naming conventions are as follows:
    * Files: snake_case.cc
    * Variables: snake_case
    * Constant variables: kPascalCase
    * Functions: PascalCase
    * Types: PascalCase
    * Private variables within classes: snake_case_
