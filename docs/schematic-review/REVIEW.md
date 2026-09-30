# AFS2 schematic cleanup and review

Open `rocket2-afs2-cleaned.kicad_pro` or `rocket2-afs2-cleaned.kicad_sch` in the repository root. The cleaned sheet uses A3, ten numbered subsystem frames, consistent text, and component grouping. The original file was saved externally during cleanup (including a change to A4), so the final result is a separate project/schematic pair. The copied project retains the original project settings. No PCB layout was changed.

## Changes and verification

- Grouped MCU, regulator, clock, SPI flash, SWD, barometer, thermocouple, high-g accelerometer, USB, and IMU.
- Kept each existing chip-select pull-up with its subsystem and grouped supply bypass capacitors.
- Converted 74 root-sheet hierarchical labels to local labels, retaining their exact names and anchors relative to circuitry. This is a single-sheet design.
- Renamed the MCU's 100 nF duplicate C2 to unused reference C14; the regulator's 10 uF C2 retains its reference. No component values, footprints, or symbol pin mappings were changed.
- Preserved all component UUIDs, wire/junction/no-connect UUIDs, component counts, and pin-to-net membership. Components, wires, and labels were translated together. Hid placeholder `~` values and replaced the informal interrupt note with readable wording; the interrupt assignment still requires firmware configuration.
- KiCad 9.0.9 successfully parsed, plotted, exported netlists, and ran ERC on the result. All **89 nets** have identical sets of connected component pins. For a valid comparison, the same C2-to-C14 annotation correction was applied to a temporary copy of the original: otherwise KiCad merges the duplicate C2 references during export.
- ERC: **174 findings before (105 errors, 69 warnings); 106 after (37 errors, 69 warnings)**. The reduction is from correcting label usage, not suppressing errors. Remaining issues are recorded in `erc-after.rpt`.
- Visual inspection used the actual KiCad SVG export. `preview.png` is a raster preview; the SVG supports zooming.

## Pull-ups and related resistors

| Function | Existing parts | Finding |
| --- | --- | --- |
| Flash chip select | R5, 10K | Connected between +3.3 V and MEM_~{CS}. |
| Barometer chip select | R2, 10K | Connected between +3.3 V and ALT_~{CS}. |
| IMU chip selects | R3/R4, 10K each | Connected between +3.3 V and the accelerometer/gyro chip-select nets. |
| ADXL chip select | R6, 10K | Connected between +3.3 V and ACCEL_~{CS}. |
| Thermocouple chip select | R79, 10K | Connected between +3.3 V and TC_~{CS}. |
| Flash HOLD | None | U3 pin 7 connects only to U1 pin 39. Recommend an external pull-up for a defined inactive level during MCU reset/startup in standard SPI mode; 10K is a reasonable candidate to confirm against the selected part and timing. |
| Flash WP | None | U3 pin 3 connects only to U1 pin 38. Review its startup state and the desired write-protection policy; consider a pull-up if inactive-high is intended. This is a design recommendation, not a claim that every operating mode requires it. |
| MCU reset | No external resistor | NRST has an internal pull-up per ST. An external resistor is not automatically missing; check the reset capacitor/noise-immunity network against ST guidance. No reset capacitor is present on this net. |
| USB-C CC1/CC2 | R15/R16, 5k1 each | Each has its own resistor to ground. These are pull-downs, not missing pull-ups. |

Winbond recommends a chip-select pull-up and a HOLD pull-up when Quad SPI is not active: [Winbond technical FAQ](https://winbond.com/hq/support/faq/technical/?__locale=en). STM32 NRST characteristics and reset guidance: [STM32F105 datasheet](https://www.st.com/resource/en/datasheet/stm32f105rc.pdf).

No resistors or new electrical connections were added, in accordance with the request to preserve connectivity. Schematic grouping does not establish PCB placement: the PCB file contains no placed layout, so physical proximity and routing remain to be designed.

## Existing issues needing attention

1. **U2 regulator ground is unconnected.** Pin 1 (ADJ/GND) is on an unconnected net. This is a functional issue, not merely an ERC power-flag warning. The VCC input also has no supply source/connector in this sheet: its exported net contains only C1 and U2 IN. Determine the intended input supply; VCC and USB VBUS are distinct nets.
2. **Y1's identity is inconsistent.** Its visible value is 8MHz, but the stored part number is ECS-1633-160-BN-TR, a **16 MHz, 2.0 x 1.6 mm active oscillator** per [ECS](https://ecsxtal.com/products/oscillators/surface-mount-oscillators/ecs-1633/). The assigned footprint is a 2.5 x 2.0 mm crystal footprint, the symbol is `Crystal_GND24_Small`, and its datasheet field points to an Abracon ASDMB document. Its wiring resembles an active oscillator (pins 1/4 to +3.3 V, pin 2 GND, pin 3 OSC_IN). Confirm the actual part, package/pad layout, frequency, and MCU HSE bypass configuration before replacing the symbol or footprint. Do not treat this as a verified passive crystal circuit.
3. **IMU interrupt nets have no MCU destination.** IMU_INT1 through IMU_INT4 each have only a U6 pin in the netlist. Choose whether firmware polls the sensor or needs interrupt connections.
4. **Unassigned MCU pins remain unmarked.** ERC reports 26 unconnected MCU pins plus U2 ground. Do not blanket-add no-connect markers until the unused pins are intentional.
5. **Shared SPI MISO produces output/output ERC errors.** U7 SDO, U30 SO, and U6 SDO1/SDO2 share a bus. This can be intentional with chip-select-controlled tri-state outputs; verify each device's deselected behavior and correct custom-symbol electrical pin types rather than hiding a possible contention problem.
6. **Library aliases/footprints need repair.** The project symbol table defines `UCIRP`, but some symbols request `UCIRP-KiCAD-Lib`. Footprint aliases are also inconsistent, and the thermocouple footprint `0UCIRP-KiCAD-Lib:AM-K-PCB` is not found. Embedded symbols allow plotting, but library updates and PCB transfer need these references resolved. Many cached power symbols differ from installed libraries. The existing modified library submodule was left untouched.

This is a layout cleanup and targeted electrical review, not a complete component-by-component design qualification. Preserve the findings above when making the next electrical revision.
