# Pocketpet PCB (paused, not ordered)

A custom KiCad board was designed for the keychain, then the project was paused before the board was
finished or ordered. Nothing here has been built, so treat every number as a plan, not a tested result.

## What exists
- A KiCad 10 project (`Pocketpet`, kept on Salman's PC, not in this repo) with a schematic and a first
  rectangular PCB layout. Parts: ESP32-C3 Super Mini (`U1`), MPU6050 GY-521 (`U2`), TP4056 charger
  module (`U3`), 1.3" 4-pin I2C OLED (`DS1`), battery connector (`J1`), three push buttons (`SW1`-`SW3`).
- Pins in the schematic follow `src/config.h` (buttons on GPIO 0/1/2, OLED on 8/9, MPU on 5/6, touch on 4).
  Salman fixed some pin differences in the PCB by hand, so check the PCB netlist against `src/config.h`
  before ordering anything.
- `ghost_outline.svg`: the chosen board shape, a ghost, 52 x 65.5 mm (half-circle top, three-scallop
  bottom, 3.4 mm keyring hole at (26, 5.5)). Import it in the PCB editor with
  File > Import > Graphics, on the Edge.Cuts layer, 1:1 in mm.
- `ghost_layout.py`: a KiCad scripting-console script that draws the same outline and places the parts.
  Run "Update PCB from Schematic" first, then in Tools > Scripting Console:
  `exec(open(r"C:\path\to\ghost_layout.py").read())`. Run it once on a fresh board.
  Written for KiCad 7/8 with a KiCad 6 fallback, tested only against a fake `pcbnew` module, never in real
  KiCad (Salman has KiCad 10). The rotations of `U1` and `U3` are guesses.

## Planned layout (mm from the board's top-left corner, front view)
Front: OLED centred at (26, 30); three SMD buttons at (14, 52.5), (26, 52.5), (38, 52.5).
Back (seen through the board): MPU6050 at the top (x 15.5-36.5, y 20.5-36.5), TP4056 on the left
(x 0.5-28.5, y 38.5-55.5, USB-C at the left edge), ESP32-C3 on the right (x 29.5-51.5, y 38-56,
USB at the right edge). Keep x 22-30, y 13-19 free for the OLED pins. Battery sits behind the modules in
the case, not on the board.

## Known problems to fix before ordering
1. The schematic wires the battery connector `J1` to the VCC net, the same as the TP4056 Vout. The battery
   must go to the module's B+/B- pads and Out+ must feed VCC, otherwise the battery bypasses the protection.
2. No power switch yet (put it between TP4056 Out+ and VCC).
3. No touch-pad header yet (signal to GPIO4, 3V3, GND).
4. The OLED's 4 pins may be on the wrong edge for this layout. If so, rotate the screen 180 degrees and
   use the flip setting in firmware.
5. Module sizes came from web listings (ESP 22.5x18, OLED about 35.5x33.5x4, GY-521 about 21x16,
   TP4056 USB-C about 28x17). Measure the real parts before locking footprints.
6. Run DRC and the 3D viewer, round the two sharp scallop tips (about 0.5 mm), and print the layout 1:1 on
   paper with the real modules on it.

## Other shapes that were considered
Only the ghost is kept in this folder. Also looked at: a 46 x 58 mm egg, a shuriken disc, a hex crystal, a shield,
and a fat-body bat (about 76 x 55 mm). The ghost won because both USB ports get a flat edge.

## To resume
Do the five fixes above, run the script, check DRC, export Gerbers, and order a small run (2 layers, 5 pcs)
from JLCPCB or PCBWay. Build a 3D printed case afterwards, and order a protected TP4056 first (the old
module was faulty).
