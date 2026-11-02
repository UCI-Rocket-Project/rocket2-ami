# MAX11614 External ADC I2C Driver

## Overview

This is a C++ implementation using the STM32 HAL for the ADC MAX11614, primarily using the [datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX11612-MAX11617.pdf)
from the 11612-11617 family of ADCs.

## Usage

### Configuration

The driver exposes a `Config` struct that can be passed into the constructor. This struct is used to write the setup pyte to the ADC during initialization.

This defaults to using the ADC's internal clock for I2C, VDD for the reference voltage when converting data, and unipolar, single-ended channel mode (each of the 8 channels independently reads from 0-4095). Use the provided defaults unless overriding is strictly necessary. The default values were chosen from the electrical setup indicated by the GSE2.1 hardware repo, and `max_attempts` for I2C reads/writes was arbitrarily chosen to be **3**.

The driver will default to reading every channel, unless overridden with `SetChannels()`.

### Initialization

Calling the `Init()` method with a Config object writes the configuration byte to the ADC and returns a boolean status, with `true` indicating success. This write is attempted up to the `max_attempts` limit specified in the config struct (defaults to 3), with currently a fixed delay between attempts. If initialization fails after the attempts, the driver records that initialization failed and sets all channels and channel status to 0.

### Reading Data

Calling the `StartReadAsync()` method will attempt to read data from the channels specified in the configuration struct/selected by `SetChannels()` with an 8-bit bitmask. If initialization failed earlier, `StartReadAsync()` returns `false` early without starting a transaction on the bus. Otherwise, it will return `true` if it successfully transmits a read request, with `false` if busy or transmission failed. Since the ADC can only read from a single channel; from channel 0 up to a selected channel; or from channel 6 up to a selected channel, **this may read extraneous channels in order to capture the ones specified**.

The caller should check `IsDataReady()` after initiating the read and before calling `FetchData()`.

The `FetchData()` method will return `std::nullopt` if the transaction is not ready or failed. Each channel read correctly will have its corresponding bit set to 1 in the 8-bit `channelValidBitmask` bitmask and its 12-bit output (0-4095) stored. Any channels that were corrupted, failed to transmit, or were not read will have their bit in the bitmask set to 0 and their channel output set to 0.

## Current Limitations

### Extraneous Channel Reads

As noted in the [**Reading Data**](#reading-data) section, the driver currently reads any channels in between specified channels, and possibly channels before the first specified one. For example, if the user requested to read channels 1 and 3, the driver would read from 0-3 inclusive due to the ADCs interface limitations.

This could be improved by chaining multiple single-channel reads when a sparse `channelSelect` is detected, which would increase complexity but also reduce total read time for these cases.

### Configuration Limitations

**The driver does not currently support bipolar + differential mode**, where data is read as the difference between two input channels from `-VREF/2` to `VREF/2`, instead of from each channel as 0 to `VREF` as it does in the default mode (single-ended, unipolar).

## Basic Usage Example

```cpp
#include <optional>

// 1. Define your configuration
AdcMax11614i2c::Config adcConfig;
adcConfig.clockMode = AdcMax11614i2c::ClockMode::Internal;
adcConfig.refMode = AdcMax11614i2c::ReferenceMode::VDD;
adcConfig.maxAttempts = 3;

// 2. Initialize
if (!adcDriver.Init(adcConfig)) {
    // Handle initialization failure
}

// 3. Read channels (here using default of all 8 channels)
if (!adcDriver.StartReadAsync()) {
    // Handle read configuration transmission error
}

// ... handle other tasks while waiting ...

// 4. Get data once it's ready
if (adcDriver.IsDataReady()) {
    std::optional<AdcMax11614i2c::Data> adcData = adcDriver.FetchData();
    // 5. Process returned optional data
    if (adcData.has_value()) {
        if (adcData->channelValidBitmask & (1 << 0)) { // or adcData->IsChannelGood(0)
            uint16_t ch0 = adcData->channelOutput0;
            // ... process valid data
        }
    } else {
        // Handle total I2C transaction failure
    }
}

```