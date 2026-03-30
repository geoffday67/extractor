#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp32_wifi/wifi.h>

#include "esp_wifi.h"
#include "mqtt.h"

#define MQTT_SERVER "192.168.68.106"
#define MQTT_PORT 1883
#define MQTT_CLIENT "extractor-switch"
#define MQTT_STATE_TOPIC "shower/test"
#define MQTT_ON_MESSAGE "ON"
#define MQTT_OFF_MESSAGE "OFF"

#define WAIT_ALL pdTRUE
#define WAIT_ONE pdFALSE
#define CLEAR_ON_EXIT pdTRUE
#define NO_CLEAR pdFALSE

// Event flags
#define WIFI_CONNECTED 0x01
#define WIFI_STARTED 0x02

#define ON_PIN 26
#define OFF_PIN 27

#define ON_LIGHT 33
#define OFF_LIGHT 25

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)

EventGroupHandle_t eventGroup;
ESP32Wifi network;
char ipAddress[ESP32Wifi::IpAddressLength];
WiFiMulti wifiMulti;
WiFiClient wifi;

bool connectWiFi() {
  bool result = false;

  eventGroup = xEventGroupCreate();
  esp_event_loop_create_default();
  Serial.println("Event groups created");

  network.init(eventGroup, WIFI_STARTED);
  if ((xEventGroupWaitBits(eventGroup, WIFI_STARTED, NO_CLEAR, WAIT_ALL, pdMS_TO_TICKS(10000)) && WIFI_STARTED) == 0) {
    goto exit;
  }
  Serial.println("WiFi initialised");

  network.connect(WIFI_CONNECTED);
  if ((xEventGroupWaitBits(eventGroup, WIFI_CONNECTED, NO_CLEAR, WAIT_ALL, pdMS_TO_TICKS(10000)) && WIFI_CONNECTED) == 0) {
    goto exit;
  }

  network.getIpAddress(ipAddress);
  Serial.printf("WiFi connected at %s\n", ipAddress);

  result = true;

exit:
  return result;
}

void disconnectWiFi() {
  // wifi.flush();
  // WiFi.mode(WIFI_OFF);
  network.stop();
  Serial.println("WiFi disconnected");
}

bool setState(const char* pNewState) {
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
  pinMode(ON_LIGHT, OUTPUT);
  pinMode(OFF_LIGHT, OUTPUT);

  if (esp_sleep_get_ext1_wakeup_status() & BUTTON_PIN_BITMASK(ON_PIN)) {
    digitalWrite(ON_LIGHT, HIGH);
    setState(MQTT_ON_MESSAGE);
  }

  if (esp_sleep_get_ext1_wakeup_status() & BUTTON_PIN_BITMASK(OFF_PIN)) {
    digitalWrite(OFF_LIGHT, HIGH);
    setState(MQTT_OFF_MESSAGE);
  }

  pinMode(ON_LIGHT, INPUT);
  pinMode(OFF_LIGHT, INPUT);
}

void setup() {
  esp_err_t err;
  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();

  Serial.begin(115200);
  Serial.println("Starting");

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    esp_sleep_get_ext1_wakeup_status();
    handleButtonWakeup();
  }

  Serial.println("Sleeping");
  Serial.flush();
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK(ON_PIN) | BUTTON_PIN_BITMASK(OFF_PIN), ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
}

void loop() {
}