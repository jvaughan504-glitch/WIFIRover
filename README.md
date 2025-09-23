# WIFIRover

WIFIRover is a Wi-Fi enabled rover platform that combines an ESP32-based
handheld controller, an onboard ESP32 Wi-Fi hub, and multiple Arduino Nanos to
control drivetrain actuators and collect telemetry. The sketches in this
repository provide a starting point for assembling the full system or for
studying each subsystem in isolation.

## Project Highlights
- **Wireless control** – The controller ESP32 connects to the rover's onboard
  ESP32 access point and streams joystick, throttle, and accessory commands over
  UDP at 10 Hz.
- **Modular managers** – Dedicated Arduino Nanos handle vehicle actuation and
  environment sensing, allowing the Wi-Fi hub to focus on networking and
  failsafe logic.
- **Telemetry feedback** – Sensor data is forwarded to the controller and shown
  on a oled display.

## Repository Layout
| File / Folder | Purpose |
| --- | --- |
| `ControllerWifiSimplified.ino` | Firmware for the handheld ESP32 controller with oled UI. |
| `MCUwifiSimplified.ino` | ESP32 access-point firmware that bridges Wi-Fi to dual UARTs. |
| `MCUwifiSimplified_Autonomous.ino` | Optional ESP32 hub firmware with onboard obstacle avoidance and cruise control logic. |
| `MCUwifiSimplified_Autonomous_Web.ino` | Optional ESP32 hub firmware that adds a mobile WebSocket dashboard plus autonomous toggles. |
| `VehicleManager.ino` | Arduino Nano sketch controlling servos, ESC, horn, and lights. |
| `SensorManager.ino`  | Arduino Nano telemetry node for ultrasonic rangefinders and wheel speed sensing. |
| `docs/communication_protocol.md` | Detailed description of command packets and telemetry framing. |
| `docs/wiring_schema.md` | Pinout reference and wiring guidelines for all modules. |

## Getting Started
1. **Prepare the hardware**
   - Assemble the rover electronics following the wiring guidelines in
     [`docs/wiring_schema.md`](docs/wiring_schema.md).
   - Ensure all boards share a common ground and that the power delivery can
     supply servos/ESC current peaks.
2. **Flash the firmware**
   - Upload each sketch to its respective board (controller ESP32, robot ESP32,
     Vehicle Manager Nano, and Sensor Manager Nano). The robot hub can run the
     standard `MCUwifiSimplified.ino` bridge or one of the optional autonomous
     variants if you want the ESP32 to handle obstacle avoidance or expose the
     mobile dashboard.
   - Confirm serial baud rates match the sketches (115200 bps for all inter-board
     links).
3. **Establish the network link**
   - Power the robot ESP32 to start the `Robot_AP` access point (password
     `12345678`).
   - Power the controller ESP32; it will connect to the AP automatically and
     begin transmitting command packets every 100 ms.
4. **Verify operation**
   - Watch the Vehicle Manager servos/ESC respond to joystick inputs.
   - Check that telemetry values appear on the controller TFT, confirming the
     Sensor Manager and Wi-Fi bridge are operating.

## Communication and Telemetry
The command string format, failsafe behaviour, and telemetry framing are fully
documented in [`docs/communication_protocol.md`](docs/communication_protocol.md).
Consult that guide when integrating new sensors or extending the command set.

### Optional Robot Hub Firmware Variants

The alternative ESP32 hub sketches build on the base networking code and remain
drop-in replacements:

- **`MCUwifiSimplified_Autonomous.ino`** – Enables on-device obstacle avoidance.
  When the controller sends `AUTO:1;` the ESP32 stops forwarding joystick
  packets and instead computes steering/throttle commands from the live
  telemetry stream. Sending `AUTO:0;` returns control to the operator.
- **`MCUwifiSimplified_Autonomous_Web.ino`** – Adds the same autonomous features
  plus an integrated HTTP/WebSocket interface (port 80/81). The dashboard shows
  distance, environmental, and speed readings and lets you toggle horn/lights or
  change modes from a phone without reflashing other boards.

If you stick with the base firmware the rover behaves exactly as documented in
the original manual-control workflow.

## Contributing
Pull requests and issue reports are welcome. Please include relevant wiring
notes and protocol updates in the documentation when modifying the sketches.
