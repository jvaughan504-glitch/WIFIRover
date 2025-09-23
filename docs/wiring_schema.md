# Wiring Schema

This document captures the wiring expectations encoded in the Arduino sketches.
It focuses on pin assignments, power domains, and serial links so you can
reproduce the hardware layout when assembling the WIFIRover platform.

## Overall Topology
```
[ESP32 Controller] ⇄ Wi-Fi UDP ⇄ [ESP32 Robot Hub]
                                           ↘ UART2 ⇄ Vehicle Nano
                                            ↘ UART1 ⇄ Sensor Nano
```

- The **controller ESP32** is battery powered and drives a TFT_eSPI display,
  joystick, and two momentary buttons.
- The **robot ESP32** is chassis-mounted, supplies regulated 5 V to the Arduino
  Nanos, and bridges Wi-Fi to two independent UARTs.
- The **Vehicle Manager Nano** actuates the steering servos, ESC, horn relay, and
  auxiliary lights.
- The **Sensor Manager Nano** aggregates four ultrasonic rangefinders and a
  wheel speed sensor, then streams telemetry.

> ⚠️ *Always share a common ground between every board and peripheral.*

## Controller ESP32 (Handheld)
| Function            | ESP32 Pin | Notes |
|---------------------|-----------|-------|
| Joystick X (ADC)    | 34        | Connect wiper to pin 34, ends to 3.3 V and GND. |
| Joystick Y (ADC)    | 35        | Same wiring as X axis. |
| Button 1 (horn)     | 0         | Uses `INPUT_PULLUP`; connect button to ground. |
| Button 2 (lights)   | 2         | Uses `INPUT_PULLUP`; connect button to ground. |
| TFT display         | SPI pins  | Follows [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) default wiring for your board profile. |

Power the controller from a 3.3 V capable supply or LiPo pack via the ESP32's
regulated input. The joystick potentiometer and buttons operate at 3.3 V logic.

## Robot ESP32 (Wi-Fi Hub)
| Connection                      | ESP32 Pin | Destination                | Notes |
|---------------------------------|-----------|----------------------------|-------|
| UART to Sensor Manager (RX)     | 18        | Nano D1 (TX)               | HardwareSerial 1 RX. |
| UART to Sensor Manager (TX)     | 19        | Nano D0 (RX)               | HardwareSerial 1 TX. |
| UART to Vehicle Manager (RX)    | 16        | Nano D1 (TX)               | HardwareSerial 2 RX. |
| UART to Vehicle Manager (TX)    | 17        | Nano D0 (RX)               | HardwareSerial 2 TX. |
| Power output (recommended)      | 5 V / GND | Both Nanos + sensors       | Use a regulated 5 V rail rated for the servos. |

> 🔁 **Cross the serial wires:** ESP32 TX → Nano RX, ESP32 RX ← Nano TX.

Because the ESP32 operates at 3.3 V logic and the Nanos at 5 V, ensure your
hardware uses level shifters or Nanos tolerant to 3.3 V inputs. For short runs
most builders successfully drive ESP32 RX pins directly from the Nano using a
voltage divider (e.g. 1 kΩ / 2 kΩ) or a logic-level converter.

## Vehicle Manager Nano
| Function                 | Nano Pin | Target hardware                         |
|--------------------------|----------|------------------------------------------|
| Serial from ESP32 (RX)   | D0 (RX)  | Receive `CMD ...` frames from robot ESP32. |
| Serial to ESP32 (TX)     | D1 (TX)  | Echoes optional debug, unused by default. |
| Front steering servo     | D10      | Standard 3-pin servo header (signal on D10). |
| Rear steering servo      | D11      | Standard 3-pin servo header (signal on D11). |
| ESC signal               | D12      | Connect signal wire from ESC.            |
| Horn driver              | D13      | Drives a MOSFET/transistor controlling the horn. |
| Auxiliary lights         | D9       | Switches LED bar or lighting relay.      |
| 5 V / GND                | 5 V, GND | Shared with servos, ESC BEC, horn relay, lights. |

Servos and ESCs can draw significant current; route their +5 V supply directly
from the BEC or a dedicated regulator and only share the ground reference with
the Nano. The sketch expects standard hobby ESC and servo PWM pulses (typically
1–2 ms at ~50 Hz, generated via the Servo library).

## Sensor Manager Nano
The provided sketch reads four HC-SR04-style ultrasonic modules and a hall-effect
wheel speed sensor, then reports averaged speed and travelled distance:

| Function                     | Nano Pin(s) | Notes |
|------------------------------|-------------|-------|
| Serial to ESP32 (TX)         | D1 (TX)     | Streams telemetry frames at 115200 bps. |
| Serial from ESP32 (RX)       | D0 (RX)     | Reserved if future commands are added. |
| Front-left ultrasonic sensor | D3 (trig), D4 (echo) | Tie sensor VCC to 5 V. |
| Front-right ultrasonic sensor| D5 (trig), D6 (echo) | 10 µs trigger pulses generated in sketch. |
| Rear-left ultrasonic sensor  | D7 (trig), D8 (echo) | Echo pins expect 5 V tolerant input. |
| Rear-right ultrasonic sensor | D9 (trig), D10 (echo) | Keep sensor grounds common. |
| Wheel speed sensor input     | D2          | Uses `attachInterrupt()` on RISING edges. |
| 5 V / GND                    | 5 V, GND    | Shared with the robot ESP32 supply. |

Adjust pin assignments as necessary, but preserve the telemetry format described
in [`communication_protocol.md`](communication_protocol.md) so the controller UI
continues to parse the data correctly.

