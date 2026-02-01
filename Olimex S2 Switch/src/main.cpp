#include <Arduino.h>
#include <esp32_wifi/wifi.h>
#include "soc/rtc.h"
#include <driver/rtc_io.h>
#include <driver/gpio.h>

#include "mqtt.h"

#define MQTT_SERVER "loft.local"
#define MQTT_PORT 1883
#define MQTT_CLIENT "extractor-switch"
#define MQTT_STATE_TOPIC "shower/extractor"
#define MQTT_ON_MESSAGE "on"
#define MQTT_OFF_MESSAGE "off"

#define WAIT_ALL pdTRUE
#define WAIT_ONE pdFALSE
#define CLEAR_ON_EXIT pdTRUE
#define NO_CLEAR pdFALSE

// Event flags
#define WIFI_CONNECTED 0x01
#define WIFI_STARTED 0x02

#define ON_PIN 1
#define OFF_PIN 6

#define ON_LIGHT 2
#define OFF_LIGHT 7

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)

EventGroupHandle_t eventGroup;
ESP32Wifi network;
WiFiClient wifi;

bool connectWiFi() {
  char ipAddress[ESP32Wifi::IpAddressLength];
  uint8_t accessPointBSSSID[6];
  uint8_t bssid[] = {0xC0, 0x06, 0xC3, 0xD5, 0x7B, 0x4E};
  bool result = false;

  eventGroup = xEventGroupCreate();
  esp_event_loop_create_default();
  Serial.println("Event groups created");

  network.init(eventGroup, WIFI_STARTED);
  if ((xEventGroupWaitBits(eventGroup, WIFI_STARTED, NO_CLEAR, WAIT_ALL, pdMS_TO_TICKS(10000)) && WIFI_STARTED) == 0) {
    goto exit;
  }
  Serial.println("WiFi initialised");

  network.connect(WIFI_CONNECTED, 4);
  // network.connect(WIFI_CONNECTED, "Wario", "mansion1", bssid);
  if ((xEventGroupWaitBits(eventGroup, WIFI_CONNECTED, NO_CLEAR, WAIT_ALL, pdMS_TO_TICKS(10000)) && WIFI_CONNECTED) == 0) {
    goto exit;
  }

  network.getIpAddress(ipAddress);
  network.getAccessPointBSSID(accessPointBSSSID);
  Serial.printf("WiFi connected to %02X:%02X:%02X:%02X:%02X:%02X with address %s\n", accessPointBSSSID[0], accessPointBSSSID[1], accessPointBSSSID[2], accessPointBSSSID[3], accessPointBSSSID[4], accessPointBSSSID[5], ipAddress);

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

bool setState(char* pNewState) {
  bool result = false;
  MQTT mqtt(wifi);

  Serial.printf("Setting state to %s\n", pNewState);

  if (!connectWiFi()) {
    Serial.println("Error connecting WiFi");
    goto exit;
  }

  wifi.setTimeout(5000);

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
  Serial.begin(115200);
  Serial.println("Starting");

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("Waking");
    handleButtonWakeup();
  }

  Serial.println("Sleeping");
  Serial.flush();

  //rtc_clk_8m_enable(true, true);
  //rtc_clk_slow_freq_set(RTC_SLOW_FREQ_8MD256);

  esp_sleep_enable_ext1_wakeup_io(BUTTON_PIN_BITMASK(ON_PIN) | BUTTON_PIN_BITMASK(OFF_PIN), ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  gpio_set_direction((gpio_num_t)ON_PIN, GPIO_MODE_INPUT);
  esp_deep_sleep_start();
}

void loop() {
}