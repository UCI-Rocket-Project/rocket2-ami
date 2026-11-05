#pragma once

#include "stm32f1xx.h"

// Debug-only timings in CPU cycles (72 cycles = 1 us at the current clock).
struct ProfileSlot {
    volatile uint32_t lastCycles;
    volatile uint32_t maxCycles;
    volatile uint32_t calls;
    volatile uint64_t totalCycles;
    volatile uint32_t averageCycles;
};

struct QuickProfile {
    ProfileSlot ledTask;
    ProfileSlot accelTask;
    ProfileSlot accelDataCallback;
    ProfileSlot watermarkIsr;
    ProfileSlot spiCallback;
    ProfileSlot countSetup;
    ProfileSlot dataSetup;
    ProfileSlot clearSetup;
    ProfileSlot countDma;
    ProfileSlot dataDma;
    ProfileSlot clearDma;
    ProfileSlot irqToCountStart;
    ProfileSlot dataDoneToClearStart;
    ProfileSlot irqToClearDone;
    volatile uint32_t busyWatermarkIrqs;
    volatile uint32_t dmaStartCycles;
    volatile uint32_t irqStartCycles;
    volatile uint32_t dataDoneCycles;
};

extern "C" {
extern volatile QuickProfile quickProfile;
}

inline void ProfileRecord(volatile ProfileSlot& slot, uint32_t startCycles) {
    const uint32_t elapsed = DWT->CYCCNT - startCycles;
    slot.lastCycles = elapsed;
    if (elapsed > slot.maxCycles) slot.maxCycles = elapsed;
    slot.totalCycles += elapsed;
    ++slot.calls;
}

inline void ProfileCalculateAverage(volatile ProfileSlot& slot) {
    const uint32_t calls = slot.calls;
    slot.averageCycles = calls ? static_cast<uint32_t>(slot.totalCycles / calls) : 0;
}

inline void ProfileCalculateAverages() {
    ProfileCalculateAverage(quickProfile.ledTask);
    ProfileCalculateAverage(quickProfile.accelTask);
    ProfileCalculateAverage(quickProfile.accelDataCallback);
    ProfileCalculateAverage(quickProfile.watermarkIsr);
    ProfileCalculateAverage(quickProfile.spiCallback);
    ProfileCalculateAverage(quickProfile.countSetup);
    ProfileCalculateAverage(quickProfile.dataSetup);
    ProfileCalculateAverage(quickProfile.clearSetup);
    ProfileCalculateAverage(quickProfile.countDma);
    ProfileCalculateAverage(quickProfile.dataDma);
    ProfileCalculateAverage(quickProfile.clearDma);
    ProfileCalculateAverage(quickProfile.irqToCountStart);
    ProfileCalculateAverage(quickProfile.dataDoneToClearStart);
    ProfileCalculateAverage(quickProfile.irqToClearDone);
}
