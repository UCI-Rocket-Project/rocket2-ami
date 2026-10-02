# Routing handoff — 2026-10-01

Work stopped at the user's request. The main PCB is the most recent checked routing candidate without copper shorts or clearance violations. It is not ready for fabrication.

## Saved in the main PCB

- Two copper layers, 54 × 54 mm outline, 3 mm corner radii.
- Four 3.2 mm non-plated M3 holes at (102,102), (148,102), (148,148), (102,148) mm, with 6.4 mm diameter copper/track/via keepouts for screw heads.
- USB aligned with its footprint's specified PCB-edge line. Thermocouple and XT60 connector mouths extend approximately 0.5 mm beyond the outline.
- STM and ports on the front; inertial sensors centered together on the back. Original subsystem component arrangements remain rigid; only entire groups were translated for mechanical clearance.
- Most signal routing completed. USB data traces between protection device and STM were placed together on the front. Controlled impedance and uninterrupted USB return paths have not been verified against a fabrication stackup.
- Ground pours filled on both layers, with 56 additional ground-stitching vias.
- Local 16 × 16 mm 3.3 V pour outlines on both layers beside the LDO output tab, with 15 thermal vias (0.6 mm diameter / 0.3 mm drill), located outside the solderable output pad. Actual filled areas after routing are approximately 244 mm² front and 227 mm² back. Direct copper connections are used for the output tab.
- U5 pad-local clearance is 0.10 mm to accommodate its existing 0.12 mm pad gaps; default clearance elsewhere remains 0.20 mm.
- USB shield pads S1 were assigned GND in the PCB. The schematic custom USB symbol has no corresponding shield pin; review this PCB-only assignment when synchronizing the schematic.
- Connector outlines crossing the PCB edge and the inherited U6 silkscreen rectangle were moved to fabrication layers. This creates intentional footprint/library differences.

## Remaining work

The saved-board DRC reports **4 unconnected items**, **4 dangling vias**, **1 silkscreen/mask issue**, and **7 footprint/library mismatch warnings**. It reports no copper clearance violations or shorts. The planes are filled.

Unconnected items:
1. 3.3 V from the STM-side front copper near (103.3294,130.8794) to back copper near (106.2542,130.7172).
2. 3.3 V between the two U6 supply branches near (123.7513,122.1100) and (126.2763,123.1100).
3. 3.3 V from U5-side back copper near (124.5963,128.5400) to front copper near (123.1450,128.6750).
4. `/IMU_INT3`: U6 pin 12 to U1 pin 26.

Dangling vias are on SWDIO, reset, ACCEL_INT1, and IMU_INT3. The silkscreen issue affects C42's reference. Library differences concern the four board-added mounting holes and the visual edits to J3, TC1, and U6. Full schematic parity, signal integrity, fabrication stackup, and final mechanical/thermal review remain outstanding.

## Preserved work

- `layout-backups/rocket2-afs2-before-final-layout-20261001.kicad_pcb`: exact board before the first placement pass.
- `layout-backups/rocket2-afs2-before-routing-20261001.kicad_pcb`: 46 mm placement before routing/mechanics changes.
- `layout-backups/rocket2-afs2-before-routing-20261001.kicad_pro`: project settings snapshot.
- `layout-backups/rocket2-afs2-routing-experiments-unfinished-20261001.kicad_pcb`: later experimental IMU rerouting. This version is NOT the main board because it has one new 0.1627 mm clearance violation, unresolved connections, and unfinished routing stubs. Its report is included separately.
- Local helper scripts, routing exchanges, and logs remain under `tmp/routing/` and are not part of the commit.

Reload the PCB from disk before saving from an already-open KiCad window.

The LDO tab/output connection was checked against [ST's LD1086 datasheet](https://www.st.com/resource/en/datasheet/ld1086.pdf). The user confirmed they had already calculated thermal capacity; the additional copper is for headroom, and no new operating-point thermal calculation was performed.
