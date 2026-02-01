#include <Arduino.h>
#include <esp32_wifi/wifi.h>
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

// Event flags
#define WIFI_CONNECTED 0x01
#define WIFI_STARTED 0x02

EventGroupHandle_t eventGroup;
ESP32Wifi network;
char ipAddress[ESP32Wifi::IpAddressLength];

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
  network.stop();
  esp_wifi_stop();
  Serial.println("WiFi disconnected");
}

void setup() {
  char s[64];

  Serial.begin(115200);
  Serial.println("Starting");

  pinMode(8, OUTPUT);
  digitalWrite(8, LOW);

  connectWiFi();

  MQTT mqtt;
  mqtt.enableDebug(true);
  if (!mqtt.connect(MQTT_SERVER, MQTT_PORT, MQTT_CLIENT)) {
    Serial.println("Error connecting MQTT");
  }
  sprintf (s, "0x%02X", esp_sleep_get_gpio_wakeup_status());
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
