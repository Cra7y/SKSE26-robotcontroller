#pragma once

// Pin map — fixed by the board, see CLAUDE.md. Do not reassign; there are no
// spare pins on this module.
#define PIN_ENA 7        // L298N ENA, PWM, motor A
#define PIN_ENB 20       // L298N ENB, PWM, motor B
#define PIN_SWITCH 8     // Kill switch, active low, external pull-up fitted
#define PIN_LED_G 19     // RGB LED green (common anode)
#define PIN_LED_R 22     // RGB LED red (common anode)
#define PIN_LED_B 21     // RGB LED blue (common anode)

// PWM — the L298N is a bipolar bridge, switches slowly. ~1 kHz is the
// sensible starting point; higher just makes it hot.
#define PWM_FREQ_HZ 1000
#define PWM_RESOLUTION_BITS 8

// These motors won't start below roughly 30-40% duty. Non-zero throttle is
// mapped onto MIN_DUTY..255; exact zero stays exact zero. Needs tuning on
// the bench (bring-up step 3) — this is a starting guess, not a measured
// value.
#define MIN_DUTY 90

// If no control packet arrives within this window, motors go to zero.
#define FAILSAFE_TIMEOUT_MS 300

// SoftAP. The SSID gets a per-board suffix derived from the SoftAP MAC
// address, so it's stable across reboots of one board but distinct from
// any other board running this firmware.
#define WIFI_SSID_PREFIX "servo-remote-"
// TODO: placeholder passphrase (WPA2 requires 8+ chars) — change before
// this leaves the bench.
#define WIFI_PASSPHRASE "drivebot1"

#define DNS_PORT 53
