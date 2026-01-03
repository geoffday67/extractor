#include <Arduino.h>
#include <WiFi.h>

#include "mqtt.h"

#define MQTT_SERVER "192.168.68.106"
#define MQTT_PORT 1883
#define MQTT_CLIENT "extractor-switch"
#define MQTT_STATE_TOPIC "shower/update-state"
#define MQTT_ON_MESSAGE "initial"
#define MQTT_OFF_MESSAGE "idle"

WiFiClient wifi;

bool connectWiFi() {
  bool result = false;
  unsigned long start;
  int tries;

  for (tries = 0; tries < 3; tries++) {
    WiFi.hostname("ExtractorSwitch");
    WiFi.begin("Wario", "mansion1");
    Serial.print("Connecting to Wario ");

    start = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - start > 5000) {
        Serial.println();
        Serial.println("Timed out connecting to access point");
        break;
      }
      delay(100);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Failed to connect to WiFi after %d tries\n", tries);
    goto exit;
  }

  Serial.println();
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  result = true;

exit:
  return result;
}

void disconnectWiFi() {
  wifi.flush();
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disconnected");
}

bool setState(char *pNewState) {
  MQTT mqtt(wifi);
  bool result = false;

  if (!connectWiFi()) {
    Serial.println("Error connecting WiFi");
    goto exit;
  }

  if (!mqtt.connect(MQTT_SERVER, MQTT_PORT, MQTT_CLIENT)) {
    Serial.println("Error connecting MQTT");
    goto exit;
  }

  if (!mqtt.publish(MQTT_STATE_TOPIC, pNewState)) {
    Serial.println("Error publishing to MQTT");
    goto exit;
  }

  result = true;

exit:
  mqtt.disconnect();
  disconnectWiFi();
  return result;
}

void handleButtonWakeup() {
  // pinMode(RED_LED, OUTPUT);
  // pinMode(GREEN_LED, OUTPUT);
  // pinMode(BLUE_LED, OUTPUT);

  // showLED(BLUE_LED);

  setState(MQTT_ON_MESSAGE);
  // if (kettleOn()) {
  //   showLED(GREEN_LED);
  // } else {
  //   showLED(RED_LED);
  // }
  delay(2000);
  //esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting");

  handleButtonWakeup();
  // switch (esp_sleep_get_wakeup_cause()) {
  //   case ESP_SLEEP_WAKEUP_EXT1:
  //     handleButtonWakeup();
  //     break;
  //   default:
  //     esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
  //     break;
  // }

  Serial.println("Sleeping");
  Serial.flush();
  esp_deep_sleep_start();
}

void loop() {
  // This is not going to be called
}