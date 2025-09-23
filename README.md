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
  on a TFT display.

## Repository Layout
| File / Folder | Purpose |
| --- | --- |
| `ControllerWifiSimplified.ino` | Firmware for the handheld ESP32 controller with TFT UI. |
| `MCUwifiSimplified.ino` | ESP32 access-point firmware that bridges Wi-Fi to dual UARTs. |
| `VehicleManager.ino` | Arduino Nano sketch controlling servos, ESC, horn, and lights. |
| `SensorManager,ino`  | Ardino Nano sketch controlling sesors. |
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
     Vehicle Manager Nano, and Sensor Manager Nano).
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

## Contributing
Pull requests and issue reports are welcome. Please include relevant wiring
notes and protocol updates in the documentation when modifying the sketches.
