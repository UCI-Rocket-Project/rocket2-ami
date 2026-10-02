# AFS2 initial overall placement — 2026-10-01

Historical record: this 46 mm placement was followed by the 54 mm routing/mechanical pass. See `../routing/README.md` for the current saved board and remaining work.

The main `rocket2-afs2.kicad_pcb` now has a 46 × 46 mm square outline, from (100,100) to (146,146) mm. This is a compact placement candidate, not a proven minimum or a manufacturing-ready routed board.

An exact pre-edit backup is at `layout-backups/rocket2-afs2-before-final-layout-20261001.kicad_pcb`. SHA-256: `ba21ea5efff1b3d767419de69aa43b6d39f9894b00b8ba6dcdf86a24e3213268`.

- Front: STM subsystem, USB, thermocouple subsystem, debug header, and power subsystem.
- Back: inertial subsystem, memory subsystem, and barometer subsystem.
- U5/U6 midpoint is exactly at board center (123,123) mm; their original mutual orientation and spacing are retained.
- USB and thermocouple enter from the top, debug is near the top right, and XT60 power enters from the bottom right.
- All 47 footprints, 113 existing tracks, pad identities, and net assignments are preserved. Each subsystem underwent only a rigid move/rotation or whole-block flip. Component and track coordinates were verified against the original using rigid-transform checks.
- Eight named KiCad groups preserve the inferred subsystem clusters for future whole-block moves. Some reference labels were relocated to resolve new text collisions; component placement within blocks was not edited.

DRC comparison: 100 unrouted items both before and after. Non-connectivity violations decreased from 12 to 11 by adding the missing outline. No new violations were introduced. The inherited issues are nine U5 pad-clearance errors (0.12 mm versus the current 0.20 mm rule), one R121/TC1 silkscreen overlap, and one U6 silkscreen/mask issue. These were left unchanged to respect the subsystem-layout constraint.

Further routing, copper pours, and mechanical/thermal validation remain. No mounting-hole requirement was provided, so none was introduced. Reload the PCB from disk in any already-open KiCad window before saving, to avoid replacing this layout with the old in-memory copy.
