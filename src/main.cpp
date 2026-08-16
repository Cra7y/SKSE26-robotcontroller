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

  // Kill switch: active low, external pull-up fitted. Internal pull-up is
  // redundant but harmless, and keeps the pin from floating high (disabled
  // = safe) if the external pull-up is ever missing during bring-up.
  pinMode(PIN_SWITCH, INPUT_PULLUP);

  ledcAttach(PIN_ENA, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttach(PIN_ENB, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcWrite(PIN_ENA, 0);
  ledcWrite(PIN_ENB, 0);

  WiFi.setSleep(false); // avoid modem-sleep latency spikes on the control loop
  WiFi.mode(WIFI_AP);

  String mac = WiFi.softAPmacAddress(); // "AA:BB:CC:DD:EE:FF"
  mac.replace(":", "");
  String ssid = String(WIFI_SSID_PREFIX) + mac.substring(6); // last 3 bytes
  WiFi.softAP(ssid.c_str(), WIFI_PASSPHRASE);

  Serial.print("AP SSID: ");
  Serial.println(ssid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/"); // captive-portal probes land here
  });

  server.begin();
  setLed(LED_BLUE); // AP up, no client yet
}

void loop() {
  dnsServer.processNextRequest();
  ws.cleanupClients();

  bool clientConnected = ws.count() > 0;
  bool failsafeTripped = (millis() - lastPacketMillis) > FAILSAFE_TIMEOUT_MS;
  bool killSwitchClosed = digitalRead(PIN_SWITCH) == LOW;

  uint8_t dutyA = mapDuty(rawA);
  uint8_t dutyB = mapDuty(rawB);

  if (!clientConnected || failsafeTripped) {
    dutyA = 0;
    dutyB = 0;
  }
  if (!killSwitchClosed) {
    // Independent physical cutout: motors stay off regardless of what the
    // network side is commanding.
    dutyA = 0;
    dutyB = 0;
  }

  ledcWrite(PIN_ENA, dutyA);
  ledcWrite(PIN_ENB, dutyB);

  if (!clientConnected) {
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
