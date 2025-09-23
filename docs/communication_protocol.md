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
4. **Sensor Manager (Arduino Nano)** – Streams ultrasonic distance readings and
   wheel speed/distance telemetry to the Wi-Fi hub, which then relays the data
   to the controller.

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
STEER:<value>;THROT:<value>;HORN:<0|1>;LIGHTS:<0|1>;AUTO:<0|1>;
```

| Token | Description | Value range / meaning |
| --- | --- | --- |
| `STEER`  | Steering pulse width sent to both servos. Neutral is `90`. | `0` (full left) to `180` (full right) |
| `THROT`  | Electronic speed controller pulse width. Neutral/idle is `90`. | `0` (full reverse) to `180` (full forward) |
| `HORN`   | Horn push-button state. | `0` = off, `1` = on |
| `LIGHTS` | Latched auxiliary lighting state. | `0` = off, `1` = on |
| `AUTO`   | Reserved for autonomous mode selection. | `0` = manual (default) |

Tokens are delimited with semicolons (`;`) and use a colon (`:`) between the
token name and its value—there is no `CMD` prefix. The controller does not
append a newline, but the robot ESP32 adds a trailing `\n` before forwarding the
string to the Vehicle Manager over UART.

### Robot → Vehicle Serial Framing
The robot ESP32 forwards the UDP payload to the Vehicle Manager Nano and appends
`\n`. The Vehicle Manager tokenizes the incoming line on semicolons and searches
for the `STEER:`, `THROT:`, `HORN:`, `LIGHTS:`, and `AUTO:` prefixes. Additional
tokens can be added as long as they follow the same `NAME:value;` pattern.

### Failsafe Handling
- The robot ESP32 tracks the arrival time of UDP packets. If no packet arrives
  within 1 second it sends `CMD FAILSAFE\n` to the Vehicle Manager and sets an
  internal failsafe flag.
- The Vehicle Manager implements an additional watchdog. If it does not process
  any serial input for 500 ms it reverts steering and throttle to neutral
  (90-degree servo position / ESC idle) and clears horn, lights, and autonomous
  flags. It accepts either `CMD FAILSAFE` or `FAILSAFE` tokens.

## Sensor Telemetry Stream
The Sensor Manager Nano emits a telemetry frame every 200 ms using the format:

```
S:<distFL>,<distFR>,<distRL>,<distRR>,<avgSpeed>,<totalDistance>;
```

- `dist*` – Ultrasonic distance measurements for the front-left, front-right,
  rear-left, and rear-right sensors in centimetres. A value of `-1` denotes a
  timeout/no reading.
- `avgSpeed` – Averaged wheel speed in metres per second.
- `totalDistance` – Cumulative distance travelled in metres.

Example frame:

```
S:55.4,60.1,40.0,38.9,0.42,12.75;
```

The robot ESP32 reads newline-terminated frames from the sensor UART, forwards
complete lines to the controller via UDP, and mirrors them to its serial console
for debugging. Whitespace preceding or following the payload is trimmed before
transmission.

### Controller Telemetry Rendering
The controller searches the incoming telemetry string for the first colon, then
extracts comma-separated values and displays them on the TFT display in the
following order:

1. Front-left distance (`FL`)
2. Front-right distance (`FR`)
3. Rear-left distance (`RL`)
4. Rear-right distance (`RR`)
5. Average speed (`m/s`)
6. Total distance (`m`)

Distance values of `< 0` are shown as `---` to indicate missing data.

