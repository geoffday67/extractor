#include <Arduino.h>
#include <WiFi.h>
#include <mqtt.h>

#define WAIT_ALL pdTRUE
#define WAIT_ONE pdFALSE
#define CLEAR_ON_EXIT pdTRUE
#define NO_CLEAR pdFALSE

#define MQTT_SERVER "192.168.68.106"
#define MQTT_PORT 1883
#define MQTT_CLIENT "extractor-switch"
#define MQTT_STATE_TOPIC "shower/test"
#define MQTT_ON_MESSAGE "initial"
#define MQTT_OFF_MESSAGE "idle"

bool connectWiFi() {
  unsigned long start;
  int count, n, max_rssi, network;
  bool result = false;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  // Limiting the Tx power is necessary for a consistent connection on the Supermini to avoid brown-out during connection.
  WiFi.setTxPower(WIFI_POWER_13dBm);

  WiFi.disconnect(true, true);
  delay(100);

  Serial.print("Scanning ... ");
  start = millis();
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
  Serial.println();
  Serial.printf("Connected as %s to access point %s\n", WiFi.localIP().toString().c_str(), WiFi.BSSIDstr().c_str());
  result = true;

exit:
  return result;
}

void disconnectWiFi() {
  WiFi.disconnect();
}

void setup() {
  char s[64];

  Serial.begin(115200);
  Serial.println("Starting");

  connectWiFi();
  delay(1000);
  //disconnectWiFi();
  return;

  pinMode(8, OUTPUT);
  digitalWrite(8, LOW);

  connectWiFi();

  MQTT mqtt;
  mqtt.enableDebug(true);
  if (!mqtt.connect(MQTT_SERVER, MQTT_PORT, MQTT_CLIENT)) {
    Serial.println("Error connecting MQTT");
  }
  sprintf(s, "0x%02X", esp_sleep_get_gpio_wakeup_status());
  if (!mqtt.publish(MQTT_STATE_TOPIC, s)) {
    Serial.println("Error publishing to MQTT");
  }
  mqtt.disconnect();

  disconnectWiFi();

  delay(500);

  gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
  gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
  esp_deep_sleep_enable_gpio_wakeup(0x03, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();
}

void loop() {
  vTaskSuspend(NULL);
}
