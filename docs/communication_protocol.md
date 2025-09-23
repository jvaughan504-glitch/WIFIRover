# Command and Communication Protocols

## System Overview
The WIFIRover platform is composed of four microcontroller roles that cooperate over
Wi-Fi and serial links:

1. **Handheld Controller (ESP32 + TFT)** – Reads joystick and button inputs and
   sends driving commands over UDP while rendering telemetry on the TFT screen.
2. **Robot Wi-Fi Hub (ESP32)** – Hosts the Wi-Fi access point, relays control
   commands to the vehicle electronics, forwards telemetry back to the
   controller, and enforces a failsafe.
3. **Vehicle Manager (Arduino Nano)** – Drives the steering servos, electronic
   speed controller (ESC), horn, and lighting system based on commands received
   from the Wi-Fi hub.
4. **Sensor Manager (Arduino Nano)** – Streams distance, temperature, and
   humidity readings to the Wi-Fi hub, which are then sent to the controller.

```
[Controller ESP32] ⇄ Wi-Fi UDP ⇄ [Robot ESP32] ⇄ UART1 ⇄ [Sensor Nano]
                                            ⇓ UART2
                                         [Vehicle Nano]
```

## Wi-Fi Transport Layer
- **Network SSID:** `Robot_AP`
- **Passphrase:** `12345678`
- **Mode:** The robot ESP32 starts an access point; the controller connects as a
  station.
- **UDP Port:** `4210` for both transmission directions.
- **Send Interval:** The controller transmits a control packet every 100 ms.

## Controller → Robot Control Packets
The controller concatenates multiple command tokens into a single ASCII string
before transmitting it via UDP:

```
CMD STEER <value>;CMD THROT <value>;CMD HORN <0|1>;CMD LIGHTS <0|1>;
```

| Token | Description | Value range / meaning |
| --- | --- | --- |
| `CMD STEER`  | Front/rear steering command mapped from the joystick X axis. | `-90` (full left) to `90` (full right) |
| `CMD THROT`  | Throttle command mapped from the joystick Y axis. | `-100` (full reverse) to `100` (full forward) |
| `CMD HORN`   | Horn push-button state. | `0` = off, `1` = on |
| `CMD LIGHTS` | Latched auxiliary lighting state. | `0` = off, `1` = on |

Each packet is terminated with a newline before being forwarded to the Vehicle
Manager over UART.

### Robot → Vehicle Serial Framing
The robot ESP32 forwards the UDP payload without modification to the Vehicle
Manager Nano (followed by `\n`). The Vehicle Manager parses tokens separated by
semicolons (`;`) and expects the prefix shown above. Additional tokens can be
introduced by following the `CMD <NAME> <value>;` convention.

### Failsafe Handling
- The robot ESP32 tracks the arrival time of UDP packets. If no packet arrives
  within 1 second it sends a `CMD FAILSAFE` newline-terminated message to the
  Vehicle Manager and sets an internal failsafe flag.
- The Vehicle Manager implements an additional watchdog. If it does not process
  any serial input for 500 ms it reverts steering and throttle to neutral (90°
  servo position/ESC idle) and clears horn/lights/autonomous flags.

## Sensor Telemetry Stream
The Sensor Manager Nano emits a telemetry frame every 500 ms using the format:

```
S:<front>,<frontLeft>,<frontRight>,<left>,<right>,<temperature>,<humidity>;
```

All fields are integers representing centimetres for distance values, degrees
Celsius for temperature, and percentage for relative humidity. Example:

```
S:100,95,102,85,90,25,50;
```

The robot ESP32 reads newline-terminated frames from the sensor UART, forwards
complete lines to the controller via UDP, and mirrors them to its serial console
for debugging.

### Controller Telemetry Rendering
The controller searches the incoming telemetry string for the first colon, then
extracts comma-separated values and displays them on the TFT display in the
following order:

1. Front distance (`F`)
2. Front-left distance (`FL`)
3. Front-right distance (`FR`)
4. Left side distance (`L`)
5. Right side distance (`R`)
6. Temperature (`T`)
7. Humidity (`H`)

Values are labelled and units appended (`cm`, `C`, `%`).

