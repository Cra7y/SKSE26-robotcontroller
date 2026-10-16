# Tank-drive remote — ESP32-PICO-MINI-02

Phone-controlled tank-drive robot. The ESP32 runs its own WiFi access point
and serves the control page itself — no app, no router, no pairing. Forward
only; reverse is out of scope (see *Deferred* in [CLAUDE.md](CLAUDE.md)).

## Hardware

- ESP32-PICO-MINI-02 on a custom board, driving two DC motors through an
  L298N breakout.
- Only two free GPIOs (**IO7**, **IO20**) drive the motors directly as PWM —
  direction is hardwired forward, speed comes from duty cycle. No pins exist
  for reverse.
- **IO8** is a latching e-stop (active low): once pressed, both motors are
  forced off until power-cycle.
- RGB status LED (common-anode) on IO19/IO22/IO21, digital on/off only.

Full pin map and L298N wiring are in [CLAUDE.md](CLAUDE.md#pin-map).

## Firmware

PlatformIO + Arduino framework, using the `pioarduino` platform fork (tracks
Arduino core 3.x; the stock `platform-espressif32` lags behind) and
`ESP32Async/AsyncTCP` + `ESP32Async/ESPAsyncWebServer`.

## VS Code / PlatformIO setup

This repo is a standard PlatformIO project — no manual toolchain install
needed.

1. Install the **PlatformIO IDE** extension in VS Code. `.vscode/extensions.json`
   recommends `pioarduino.pioarduino-ide` (the fork's own IDE extension) —
   install that instead of, or alongside, `platformio.platformio-ide`; either
   will pick up the `pioarduino` platform URL from `platformio.ini`.
2. Open this folder in VS Code (`File → Open Folder…`, pick the repo root —
   the one containing `platformio.ini`). PlatformIO auto-detects the project
   from that file.
3. On first open, PlatformIO downloads the `pioarduino` ESP32 platform
   toolchain and the two `lib_deps` libraries automatically — this can take a
   few minutes and needs internet access.
4. Use the PlatformIO toolbar at the bottom of the VS Code window (or the
   PlatformIO sidebar icon → *Project Tasks → pico*):
   - ✓ (checkmark) — **Build**
   - → (arrow) — **Upload** (flashes over USB; close the serial monitor
     first if it's open, or it'll hold the port)
   - plug icon — **Serial Monitor** (115200 baud, matches `monitor_speed` in
     `platformio.ini`) — needed since RXD0/TXD0 are kept free for logging.
   - 🗑 — **Clean**

### Command line equivalent

Same actions from a terminal with the `pio` CLI (installed alongside the
extension, or via `pip install platformio`):

```
pio run                # build
pio run -t upload      # build + flash
pio device monitor     # serial monitor at 115200
pio run -t clean        # clean
```

## Behaviour

- **Control:** tank style, two sliders (0…full forward, spring-return to
  zero), ~25 Hz heartbeat over WebSocket from the phone.
- **Failsafe:** no packet for 300 ms → both motors to zero duty.
- **E-stop:** latches both motors off; only a reset/power-cycle clears it.
- **Status LED:** blue (AP idle) / amber (connected, zero throttle) / green
  (driving) / solid red (failsafe) / blinking red (e-stop latched).
- **Network:** SoftAP `servo-remote-<suffix>`, WPA2, fixed IP `192.168.4.1`.
  No captive-portal popup (deliberately suppressed — breaks Android touch
  handling); connect to the AP, then browse to `http://192.168.4.1/` or
  `http://servo/`.

See [CLAUDE.md](CLAUDE.md) for the full spec, wiring rationale, known
gotchas (boot-safe GPIO state, PWM frequency, duty deadband, WiFi sleep),
and bring-up order.

## Status

Not yet working end-to-end — see commit history. Open question: motor
supply voltage / motor spec, which affects tuning `MIN_DUTY`.
