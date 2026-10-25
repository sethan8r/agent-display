# Enclosure

3D-printable case for the ESP32-2432S028 "Cheap Yellow Display" (two-port version), portrait,
USB pointing down.

> Not published yet — files will appear here.

## Files

| File | What |
|---|---|
| `*.stl` / `*.3mf` | ready to slice and print |
| `*.step` | source geometry, if you want to modify it |
| `drawings/` | dimensioned drawings of the board and the case (PDF + PNG): outline, mounting holes, screen window, LDR, buttons, ports — measured with calipers on a real board |
| `photos/` | how it looks assembled |

## Print settings

Tested on Elegoo Neptune 4 Pro, PETG. Details will be added together with the model.

## Design requirements

- Window for the light sensor (LDR) next to the screen — otherwise auto brightness sees darkness.
- Access to the RST and BOOT buttons (at least with a needle).
- Opening for the micro-USB port.
- The RGB LED on the back is not visible through the case without a light guide.
