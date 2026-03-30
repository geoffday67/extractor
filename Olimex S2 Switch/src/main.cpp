#include <Arduino.h>
#include <MQTT.h>
#include <WiFi.h>

#define WIFI_SSID "Wario"
#define WIFI_PASSWORD "mansion1"

#define MQTT_HOST "loft.local"
#define MQTT_PORT 1883
#define MQTT_CLIENT "extractor-switch"
#define MQTT_STATE_TOPIC "shower/extractor"
#define MQTT_ON_MESSAGE "on"
#define MQTT_OFF_MESSAGE "off"

#define ON_PIN 1
#define OFF_PIN 6

#define ON_LIGHT 2
#define OFF_LIGHT 7

#define STATUS_LED_PIN 18  // WS2812 data line — driven LOW during sleep

WiFiClient wifiClient;
MQTTClient mqttClient;

// Scans for all APs with matching SSID and connects to the one with the best RSSI.
void connectWifi() {
  Serial.println("Scanning for WiFi networks...");

  int found = WiFi.scanNetworks();
  int bestIndex = -1;
  int bestRSSI = -32768;

  for (int i = 0; i < found; i++) {
    if (WiFi.SSID(i) == WIFI_SSID && WiFi.RSSI(i) > bestRSSI) {
      bestRSSI = WiFi.RSSI(i);
      bestIndex = i;
    }
  }

  if (bestIndex < 0) {
    Serial.println("SSID not found in scan, connecting anyway...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.printf("Best AP: BSSID %s  channel %d  RSSI %d dBm\n",
                  WiFi.BSSIDstr(bestIndex).c_str(),
                  WiFi.channel(bestIndex),
                  bestRSSI);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD,
               WiFi.channel(bestIndex),
               WiFi.BSSID(bestIndex));
  }

  WiFi.scanDelete();

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected — IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMqtt() {
  Serial.print("Connecting to MQTT broker");
  while (!mqttClient.connect(MQTT_CLIENT)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nMQTT connected");
}

void goToSleep() {
  // Shut down WiFi radio before sleeping
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Drive WS2812 data line low — floating input causes the chip to draw current
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Return LED pins to INPUT so they don't source/sink current during sleep
  pinMode(ON_LIGHT,  INPUT);
  pinMode(OFF_LIGHT, INPUT);

  // Wakeup pins must be INPUT before isolation so HOLD latches them
  // high-impedance — otherwise HOLD locks them in whatever driven state
  // they were in and the EXT1 comparator can't see the button signal.
  pinMode(ON_PIN,  INPUT);
  pinMode(OFF_PIN, INPUT);

  // Wake when either button pulls its pin high
  esp_sleep_enable_ext1_wakeup_io(
      (1ULL << ON_PIN) | (1ULL << OFF_PIN),
      ESP_EXT1_WAKEUP_ANY_HIGH);

  // Power down all RTC domains — external pull-downs hold the pins stable
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

  Serial.println("Entering deep sleep...");
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t wakeStatus = esp_sleep_get_ext1_wakeup_status();
    bool isOn  = wakeStatus & (1ULL << ON_PIN);
    bool isOff = wakeStatus & (1ULL << OFF_PIN);

    Serial.printf("Woke on EXT1 — status=0x%llx  isOn=%d  isOff=%d\n",
                  wakeStatus, isOn, isOff);

    if (isOn) {
      pinMode(ON_LIGHT, OUTPUT);
      digitalWrite(ON_LIGHT, HIGH);

      connectWifi();
      mqttClient.begin(MQTT_HOST, MQTT_PORT, wifiClient);
      connectMqtt();
      mqttClient.publish(MQTT_STATE_TOPIC, MQTT_ON_MESSAGE, false, 1);
      mqttClient.disconnect();

      digitalWrite(ON_LIGHT, LOW);
    }

    if (isOff) {
      pinMode(OFF_LIGHT, OUTPUT);
      digitalWrite(OFF_LIGHT, HIGH);

      connectWifi();
      mqttClient.begin(MQTT_HOST, MQTT_PORT, wifiClient);
      connectMqtt();
      mqttClient.publish(MQTT_STATE_TOPIC, MQTT_OFF_MESSAGE, false, 1);
      mqttClient.disconnect();

      digitalWrite(OFF_LIGHT, LOW);
    }

    delay(500);
  }

  goToSleep();
}

void loop() {
  // Never reached — deep sleep wakes as a full reset, re-entering setup()
}