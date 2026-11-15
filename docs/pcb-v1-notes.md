# Custom PCB v1: what it connects, and the fixes needed

Checked on 2026-11-15 against `PCB/PCB.kicad_pcb` and `PCB.kicad_sch` (every net is routed and connected; the issues
below are about what the nets are, not about broken copper). The board is already ordered, so these are fixed with
firmware (done) and a few small bodge wires (below).

## Pin map of the board

| Function | ESP32-C3 pin | Firmware |
|---|---|---|
| OLED SDA / SCL + MPU6050 SDA / SCL | GPIO8 / GPIO9 | matches |
| Button A (SW1, left) | GPIO7 | `HW_PCB_V1` build |
| Button B (SW2, middle) | GPIO5 | `HW_PCB_V1` build |
| Button C (SW3, right) | GPIO6 | `HW_PCB_V1` build |
| MPU6050 INT | GPIO2 | not used (leave it) |
| Touch pad | GPIO4 | **not wired on the board** (see fix 3) |
| Battery sense | GPIO3 | **not wired on the board** (see fix 4) |

Flash the PCB with the PlatformIO environment **`esp32-c3-pcb`**. The protoboard keeps using `esp32-c3-supermini`.

## Fixes to do before the first power-on

### 1. OLED power pins are swapped (confirmed)
The board's OLED header is, from the outer (square) pad inward: **3V3, GND, SCL, SDA**.
Your module is: **GND, VCC, SCL, SDA**. Plugging it straight in puts 3.3 V on GND and GND on VCC.

Do not plug it in. Instead remove the module's header and connect 4 short wires from the module to the board pads:

| Module pin | Board pad |
|---|---|
| VCC | pad 1 (outer, square) |
| GND | pad 2 |
| SCL | pad 3 |
| SDA | pad 4 |

(So only VCC and GND cross over.) Before powering up, check with a multimeter in continuity mode that pad 1 is the
ESP's 3V3 pin and pad 2 is GND.

### 2. The battery connector J1 is not connected to the charger
On the board, `J1` (battery) is its own net, separate from the TP4056 module's battery pads, so a battery in J1
would never charge. Do this instead, with no cuts:

- **Do not use J1.**
- Solder the battery **+** lead to a pad on the **Vout** side of the TP4056 outline (the module's OUT+/B+ pads are tied
  together on this board) and the battery **-** lead to a **GND** pad.
- The slide switch SW4 then works as the on/off switch: in one position the ESP is powered from that rail, in the
  other position its input floats (off). Try both positions to find which is ON.

Note: the board ties the module's B- and OUT- together, which bypasses the module's own over-discharge and
short-circuit chip. Use a LiPo cell that has its own protection circuit, and do not run the pet flat (a low-battery
cutoff in the firmware is planned with fix 4).

## Optional, but needed for full features

### 3. Touch pad (GPIO4)
Solder three wires from a TTP223 touch module to the ESP: OUT to **GPIO4**, VCC to **3V3**, GND to **GND**. Take them
from the ESP's own pins (the pads on the back of the board where the Super Mini is soldered).

### 4. Battery gauge (GPIO3)
Two 220 kOhm resistors as a divider: battery + (the Vout pad rail) to 220 k to **GPIO3** to 220 k to GND. A 100 nF
capacitor from GPIO3 to GND steadies the reading. At 4.2 V the pin sees 2.1 V, which is safe. (The firmware part is
not written yet.)

### 5. MPU6050 supply (check)
The MPU6050 module is powered from the 5V-pin rail (battery / USB side), not from 3V3. Its on-board regulator
handles that. After first power-up on battery, measure SDA at idle: it must read about 3.3 V. If it reads
4 V or more, the module's I2C pull-ups are tied to its VCC: move its VCC wire to 3V3.

## Deep sleep note
Only GPIO0 to 5 can wake the ESP32-C3 from deep sleep. On this board that is the touch pad (GPIO4) and button B
(GPIO5). Buttons A and C (GPIO7 and 6) cannot wake it.

## For a v2 board
Wire J1 + to the TP4056 B+ and put the power switch in series; correct the OLED pad order (VCC / GND swap);
add the touch pad connector on GPIO4 and the battery divider on GPIO3; put buttons on GPIO0, 1 and 5 for deep-sleep
wake-up; add a connector for the MPU6050 INT if it is going to be used.
