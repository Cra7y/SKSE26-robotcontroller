#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "index_html.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer dnsServer;

// Written by the AsyncTCP task in the WS callback, read from loop(). Each is
// a single-writer/single-reader byte/word, so plain volatile is enough — no
// motor/LED code runs from the callback itself.
static volatile uint8_t rawA = 0;
static volatile uint8_t rawB = 0;
static volatile uint32_t lastPacketMillis = 0;

// Latching E-stop: once IO8 is pressed, motors stay disabled until the board
// is power-cycled (or reset) — set-only, nothing in this firmware clears it.
static bool estopLatched = false;

enum LedColor { LED_UNSET, LED_OFF, LED_BLUE, LED_AMBER, LED_GREEN, LED_RED };
static LedColor currentLed = LED_UNSET;

static void setLed(LedColor color) {
  if (color == currentLed) return;
  currentLed = color;
  // Common anode: LOW turns a channel on, HIGH turns it off.
  bool r = false, g = false, b = false;
  switch (color) {
    case LED_BLUE:  b = true; break;
    case LED_AMBER: r = true; g = true; break;
    case LED_GREEN: g = true; break;
    case LED_RED:   r = true; break;
    default: break;
  }
  digitalWrite(PIN_LED_R, r ? LOW : HIGH);
  digitalWrite(PIN_LED_G, g ? LOW : HIGH);
  digitalWrite(PIN_LED_B, b ? LOW : HIGH);
}

static uint8_t mapDuty(uint8_t raw) {
  if (raw == 0) return 0;
  return (uint8_t)map(raw, 1, 255, MIN_DUTY, 255);
}

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
    return; // ignore fragmented/binary frames, protocol doesn't use them
  }

  char buf[16];
  size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, data, n);
  buf[n] = '\0';

  int a = 0, b = 0;
  if (sscanf(buf, "%d,%d", &a, &b) == 2) {
    rawA = (uint8_t)constrain(a, 0, 255);
    rawB = (uint8_t)constrain(b, 0, 255);
    lastPacketMillis = millis();
  }
}

void setup() {
  // Motors must not spin at boot. IO7 has a pull-up in its reset default,
  // so both enables get driven low before anything else runs.
  pinMode(PIN_ENA, OUTPUT);
  pinMode(PIN_ENB, OUTPUT);
  digitalWrite(PIN_ENA, LOW);
  digitalWrite(PIN_ENB, LOW);

  Serial.begin(115200);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setLed(LED_OFF);

  // E-stop button: active low, external pull-up fitted. Internal pull-up is
  // redundant but harmless. Outputs are enabled by default; pressing this
  // latches them off until power-cycle (see estopLatched in loop()).
  pinMode(PIN_SWITCH, INPUT_PULLUP);

  ledcAttach(PIN_ENA, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttach(PIN_ENB, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcWrite(PIN_ENA, 0);
  ledcWrite(PIN_ENB, 0);

  WiFi.setSleep(false); // avoid modem-sleep latency spikes on the control loop
  WiFi.mode(WIFI_AP);

  // WiFi.softAPmacAddress() reads back as all-zero here — the AP interface
  // isn't actually up yet at this point (before WiFi.softAP() below). Read
  // the factory-burned base MAC straight from eFuse instead; it's valid
  // immediately and doesn't depend on WiFi driver bring-up order.
  uint64_t chipMac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", (unsigned int)(chipMac & 0xFFFFFF));
  String ssid = String(WIFI_SSID_PREFIX) + suffix;
  WiFi.softAP(ssid.c_str(), WIFI_PASSPHRASE);

  Serial.print("AP SSID: ");
  Serial.println(ssid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", INDEX_HTML);
  });

  // Answer each OS's connectivity-check probe with exactly what it expects,
  // so none of them conclude this AP "requires sign-in" and auto-launch
  // their own captive-portal browser. That browser (at least Android's) has
  // an unfixable-from-page-content pull-to-refresh bug — see CLAUDE.md,
  // "Network". Users open Chrome themselves and browse to the AP's fixed IP.
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(204); // Android / ChromeOS
  });
  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(204); // Android (older)
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    // iOS/macOS check the body for this exact string, not just the status.
    request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  });
  server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Microsoft Connect Test"); // Windows
  });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Microsoft NCSI"); // Windows
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/"); // anything else stray lands on the control page
  });

  server.begin();
  setLed(LED_BLUE); // AP up, no client yet
}

void loop() {
  dnsServer.processNextRequest();
  ws.cleanupClients();

  bool clientConnected = ws.count() > 0;
  bool failsafeTripped = (millis() - lastPacketMillis) > FAILSAFE_TIMEOUT_MS;

  if (digitalRead(PIN_SWITCH) == LOW) {
    estopLatched = true; // sticky — only a reset/power-cycle clears this
  }

  uint8_t dutyA = mapDuty(rawA);
  uint8_t dutyB = mapDuty(rawB);

  if (!clientConnected || failsafeTripped || estopLatched) {
    dutyA = 0;
    dutyB = 0;
  }

  ledcWrite(PIN_ENA, dutyA);
  ledcWrite(PIN_ENB, dutyB);

  if (estopLatched) {
    // Blinking red so it reads as distinct from the solid-red failsafe
    // state: this one needs a power-cycle, failsafe clears itself.
    bool blinkOn = (millis() / 250) % 2 == 0;
    setLed(blinkOn ? LED_RED : LED_OFF);
  } else if (!clientConnected) {
    setLed(LED_BLUE);
  } else if (failsafeTripped) {
    setLed(LED_RED);
  } else if (dutyA == 0 && dutyB == 0) {
    setLed(LED_AMBER);
  } else {
    setLed(LED_GREEN);
  }

  delay(5);
}
