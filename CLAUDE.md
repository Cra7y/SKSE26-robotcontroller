# Tank-drive remote — ESP32-PICO-MINI-02

Two DC motors driven through an L298N breakout, controlled from a phone browser
over WiFi. The ESP32 runs its own access point and serves the control page
itself — no app, no router, no pairing.

Forward only for now. Reverse is deliberately out of scope; see *Deferred*.

## Hardware

- **Module:** ESP32-PICO-MINI-02 (contains ESP32-PICO-V3-02 — original Xtensa
  ESP32 silicon, 8 MB flash, 2 MB PSRAM) on a custom dev board.
- **Motor driver:** L298N breakout board.
- **Supply:** Do not worry about this, the owner has it covered.

## Pin map

This board exposes very few pins, and this map is **fixed**. Do not propose
alternative assignments; there are no spare pins to move to.

| Pin   | Function                    | Notes                                      |
|-------|-----------------------------|--------------------------------------------|
| IO7   | L298N `ENA` (PWM, motor A)  | Only free output. Header H2 pin 1.          |
| IO20  | L298N `ENB` (PWM, motor B)  | Only free output. Header H2 pin 2.          |
| IO8   | Switch input                | **Active low**, external pull-up fitted.    |
| IO19  | RGB LED — green             | Status only, digital on/off.                |
| IO22  | RGB LED — red               | Status only, digital on/off.                |
| IO21  | RGB LED — blue              | Status only, digital on/off.                |
| IO5   | Not connected               | Not pinned out, cannot be soldered.         |
| RXD0/TXD0 | Serial console          | Keep free for logging.                      |

Header H2 carries IO7 and IO20 only — **no ground pin**. The
motor ground return comes from elsewhere on the board.

RGB LED is common-anode

## L298N wiring

Direction is hardwired so both motors need only one control line each:

- `IN1` → 5 V, `IN2` → GND (motor A locked forward)
- `IN3` → 5 V, `IN4` → GND (motor B locked forward)
- `ENA` → IO7, `ENB` → IO20 — **the ENA/ENB jumpers on the breakout must be
  removed**, otherwise the enables are tied high and PWM does nothing.
- ESP32 GND and L298N GND must be common.

3.3 V logic into the L298N enable inputs is fine: the L298 datasheet specifies
V_IH min of 2.3 V.

## Firmware stack

PlatformIO, Arduino framework.

```ini
[env:pico]
; The official platform-espressif32 lags behind Arduino core 3.x.
; pioarduino is the community fork that tracks it:
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = adafruit_feather_esp32_v2      ; closest stock def: PICO-MINI-02, 8MB/2MB
framework = arduino
monitor_speed = 115200
lib_compat_mode = strict
lib_ldf_mode = chain
lib_deps =
  ESP32Async/AsyncTCP
  ESP32Async/ESPAsyncWebServer
```

`ESP32Async/*` is the maintained home of ESPAsyncWebServer — the old `me-no-dev`
packages and the many forks of them are stale. `lib_compat_mode = strict` and
`lib_ldf_mode = chain` are load-bearing: without them the dependency finder
pulls in a conflicting fork and the build fails at link time.

## Already verified — do not re-investigate

**GPIO20 works.** It only exists on the PICO-V3 chip package, and older
toolchains rejected it, which produces a lot of misleading search results. It is
genuinely supported on current ESP-IDF:

- `components/soc/esp32/include/soc/soc_caps.h` — `SOC_GPIO_VALID_GPIO_MASK`
  excludes only bits 24 and 28–31. Bit 20 is present, in both the valid and the
  valid-output masks.
- `components/soc/esp32/gpio_periph.c` — the table contains `IO_MUX_GPIO20_REG`
  with an inline comment that the pin is only available on ESP32-PICO-V3.

Arduino core 3.x sits on IDF 5.x, so `pinMode(20, OUTPUT)` and `ledcAttach(20, …)`
are valid. Reports that GPIO20 is unusable date from IDF 3.x / Arduino core 1.0.x.

**IO7 and IO20 are free on this module.** Per the ESP32-PICO-MINI-02 datasheet,
the in-package flash uses CMD/IO11 and CLK/IO6, the PSRAM uses SD2/IO9 and
SD3/IO10, and none of those are led out. Neither IO7 nor IO20 is a strapping pin.

## Constraints and gotchas

**Motors must not spin at boot.** IO7 and IO20 float until firmware configures
them, and GPIO7 has a pull-up in its reset default. A floating or high enable
line means a motor runs the moment power is applied. Drive both pins low as the
very first thing in `setup()`, before WiFi or anything else. External 10 k
pulldowns on ENA/ENB are the belt-and-braces fix if this shows up in practice.

**PWM frequency: ~1 kHz.** The L298N is a bipolar transistor bridge and switches
slowly; high frequencies just make it hot. 1 kHz at 8-bit resolution is the
sensible starting point. It is audible — that is normal for this driver.

**Duty deadband.** These motors will not start below roughly 30–40 % duty. Map
non-zero throttle onto `MIN_DUTY..255` rather than `0..255`, and treat exact zero
as off. `MIN_DUTY` needs tuning on the bench.

**WebSocket callbacks run in the AsyncTCP task, not `loop()`.** Parse and store
in the callback; do all LEDC writes from `loop()`. Do not call motor code from
inside the event handler.

**`WiFi.setSleep(false)` is required.** Modem sleep adds 100 ms+ latency spikes
to the control loop. This is a latency fix, not a power one.

**LEDC channels.** Two motor channels out of sixteen. The RGB LED is digital
on/off — do not attach it to LEDC.

## Behaviour spec

**Control:** tank style. Left on-screen slider → motor A (IO7), right slider →
motor B (IO20). Range is 0…full forward, spring-returning to **zero** on release
(not to centre — there is no reverse). Phone sends a fixed ~25 Hz heartbeat with
both values; do not send one message per pointer event.

**Failsafe:** if no packet arrives for 300 ms, both motors go to zero duty. This
matters more than it did with servos — a stalled servo holds position, a runaway
motor keeps driving. Do not rely on the WebSocket close event: a phone carried
out of range produces no FIN, and the socket can take tens of seconds to notice.
Screen lock and tab backgrounding also suspend the browser's timers, which
correctly trips this timeout.

**RGB LED status (digital, on/off combinations):**

| State                              | Colour            |
|------------------------------------|-------------------|
| AP up, no client connected         | Blue              |
| Client connected, throttle at zero | Amber (red+green) |
| Live, motors commanded             | Green             |
| Failsafe tripped                   | Red               |

Blink patterns are fine and cost nothing; PWM breathing is explicitly not wanted.

**Network:** SoftAP, SSID `servo-remote` (rename), WPA2, ≥8 char passphrase.
Wildcard `DNSServer` on port 53 so the phone's captive-portal probe opens the
control page automatically. `server.onNotFound` redirects to the UI.

**UI:** single HTML page embedded in `PROGMEM` as a raw string literal — no
LittleFS, no build-time asset pipeline. `touch-action: none`, pointer events with
capture, Wake Lock API requested so screen sleep doesn't trip the failsafe.

## Bring-up order

1. Blink IO20 alone. This is the one assumption worth killing first, despite the
   source-level verification above.
2. RGB LED — confirm common-anode vs common-cathode and fix drive polarity.
3. Motors on the bench, wheels off the ground, driven from hardcoded duty values.
   Find `MIN_DUTY`.
4. SoftAP + page serving, no motors attached.
5. WebSocket plumbing end to end.
6. Join it up, then test the failsafe deliberately: walk away, lock the screen,
   kill WiFi on the phone.

## Deferred

- **Reverse.** Would need IN1/IN2 under firmware control — four more pins that do
  not exist. Realistically requires an I²C or shift-register expander sharing
  IO7/IO20, at which point the PWM pins are gone too. Do not design for it now.

## Open questions
- Motor supply voltage and motor spec — affects `MIN_DUTY` only.
