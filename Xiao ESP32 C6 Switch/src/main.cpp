#include <Arduino.h>
#include <MQTT.h>
#include <WiFi.h>
#include <WiFiClient.h>

#define WAIT_ALL pdTRUE
#define WAIT_ONE pdFALSE
#define CLEAR_ON_EXIT pdTRUE
#define NO_CLEAR pdFALSE

#define SWITCH_ON_PIN 1
#define SWITCH_OFF_PIN 2

#define SWITCH_ON_LED 21
#define SWITCH_OFF_LED 22

// PWM levels (8-bit). Dim = "press registered, working"; kept low to minimise
// current draw during the radio's peak load. Bright = confirmed.
#define LED_DIM 40
#define LED_BRIGHT 255

#define MQTT_HOST "192.168.68.106"
#define MQTT_PORT 1883
#define MQTT_CLIENT "extractor-switch"
#define MQTT_STATE_TOPIC "shower/extractor"
#define MQTT_ON_MESSAGE "on"
#define MQTT_OFF_MESSAGE "off"

void showError(int led) {
  for (int n = 0; n < 5; n++) {
    analogWrite(led, 0);
    delay(100);
    analogWrite(led, LED_BRIGHT);
    delay(100);
  }
}

bool publishSwitch(bool on) {
  bool result = false, ok;
  const char* pmessage = on ? MQTT_ON_MESSAGE : MQTT_OFF_MESSAGE;
  WiFiClient net;
  MQTTClient client(256);  // buffer size; bump if payload/topic grows

  client.begin(MQTT_HOST, MQTT_PORT, net);  // broker IP, port
  client.setTimeout(10000);                  // per-operation blocking cap (ms)
  client.setKeepAlive(15);

  if (!client.connect(MQTT_CLIENT)) {
    Serial.printf("MQTT connect failed: %d\n", client.lastError());
    goto exit;
  }

  Serial.println("MQTT connected");

  // QoS 1: blocks until PUBACK (or setTimeout expires)
  ok = client.publish(MQTT_STATE_TOPIC, pmessage, false, 1);
  if (!ok) {
    Serial.printf("publish/PUBACK failed: %d\n", client.lastError());
    goto exit;
  }

  Serial.printf("MQTT \"%s\" published to \"%s\"\n", pmessage, MQTT_STATE_TOPIC);

  result = true;

exit:
  client.disconnect();  // sends DISCONNECT, closes socket cleanly
  return result;
}

bool connectWiFi() {
  unsigned long start;
  int count, n, max_rssi, network;
  bool result = false;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  // Limiting the Tx power is necessary for a consistent connection on the Supermini to avoid brown-out during connection.
  // But not on the Xiao ESP32 C6 board.
  // WiFi.setTxPower(WIFI_POWER_13dBm);

  WiFi.disconnect(true, true);
  delay(100);

  Serial.print("Scanning ... ");
  start = millis();
  // WiFi.setScanActiveMinTime(30);
  count = WiFi.scanNetworks(false, false, false, 100, 0, "Wario");
  Serial.printf("%d networks found in %d ms\n", count, millis() - start);

  max_rssi = -999;
  network = -1;
  for (n = 0; n < count; n++) {
    if (WiFi.RSSI(n) > max_rssi) {
      max_rssi = WiFi.RSSI(n);
      network = n;
    }
  }
  if (network == -1) {
    Serial.println("No usable network found");
    goto exit;
  }

  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin("Wario", "mansion1", WiFi.channel(network), WiFi.BSSID(network));
    Serial.print("Connecting WiFi ...");

    start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - start > 5000) {
        Serial.println(" timed out connecting to access point");
        goto exit;
      }
      Serial.print(".");
      delay(500);
    }
  }
  WiFi.setSleep(false);

  Serial.println();
  Serial.printf("Connected as %s to access point %s with RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str(), WiFi.RSSI());

  result = true;

exit:
  return result;
}

void disconnectWiFi() {
  WiFi.disconnect(true, true);
  delay(100);
}

// Wake on either GPIO0 (SWITCH_ON_PIN) or GPIO1 (SWITCH_OFF_PIN) for ext1
#define BUTTON_PIN_BITMASK ((1ULL << SWITCH_ON_PIN) | (1ULL << SWITCH_OFF_PIN))

void handleSwitch() {
  bool ok = false;
  bool on = esp_sleep_get_ext1_wakeup_status() & (1ULL << SWITCH_ON_PIN);
  int led = on ? SWITCH_ON_LED : SWITCH_OFF_LED;

  pinMode(SWITCH_ON_LED, OUTPUT);
  pinMode(SWITCH_OFF_LED, OUTPUT);

  // Dim glow: press registered, connection in progress.
  analogWrite(led, LED_DIM);

  if (!connectWiFi()) {
    goto exit;
  }

  if (!publishSwitch(on)) {
    goto exit;
  }

  ok = true;

exit:
  disconnectWiFi();

  if (ok) {
    // Solid bright: confirmed.
    analogWrite(led, LED_BRIGHT);
    delay(400);
  } else {
    showError(led);
  }

  analogWrite(SWITCH_ON_LED, 0);
  analogWrite(SWITCH_OFF_LED, 0);
}

void setup() {
  Serial.begin(115200);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    // We were woken by one of the swtich pins, handle it.
    handleSwitch();
  }

  // Use ext1 (ext0 is not supported on the ESP32-C6). The bitmask covers
  // both GPIO0 and GPIO1, so either pin going LOW wakes the device.
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_LOW);

  // Go to sleep now
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
}

void loop() {
}
