#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <espMqttClient.h>

#define MQTT_CLIENT "shower-test"

#define ROOT_TOPIC "shower-test"
#define STATE_TOPIC ROOT_TOPIC "/state"
#define RATE_TOPIC ROOT_TOPIC "/rate"
#define TEMPERATURE_TOPIC ROOT_TOPIC "/temperature"
#define EXTRACTOR_TOPIC ROOT_TOPIC "/extractor"
#define SETTINGS_TOPIC ROOT_TOPIC "/settings"

#define FAN_PIN 4
#define ONE_WIRE_BUS 13

#define STATE_IDLE 1
#define STATE_INITIAL 2
#define STATE_WAITING 3

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

espMqttClient mqttClient;
const char mqttHost[] = "192.168.68.106";
const int mqttPort = 1883;
char mqttMessage[256];

int state;
float readings[5];
int readingIndex = 0;

// Settings
struct Settings {
  int signature;
  float onTemperature, onRate, offTemperature;
  int minimumMinutes;
} settings;

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];

float rateOfChange() {
  if (readingIndex < 5) {
    return 0.0;
  } else {
    return readings[0] - (8 * readings[1]) + (8 * readings[3]) - readings[4];
  }
}

void dumpReadings() {
  int n;

  for (n = 0; n < 5; n++) {
    Serial.printf("%0.1f", readings[n]);
  }
  Serial.println();
}

void recordReading(float reading) {
  int n;

  if (readingIndex < 5) {
    readings[readingIndex++] = reading;
    return;
  }

  for (n = 0; n < 4; n++) {
    readings[n] = readings[n + 1];
  }
  readings[4] = reading;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wario", "mansion1");

  Serial.print("Connecting WiFi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print(" connected, IP address: ");
  Serial.println(WiFi.localIP());
}

void handleMqttConnected(bool sessionPresent) {
  Serial.printf("MQTT connected\n");
}

void handleSubscribed(uint16_t packetId, const espMqttClientTypes::SubscribeReturncode* returncodes, size_t len) {
  Serial.printf("MQTT subscribed\n");
}

void fanOn() {
  digitalWrite(FAN_PIN, 1);
  if (!mqttClient.publish(EXTRACTOR_TOPIC, 1, true, "on")) {
    ESP.restart();
  }
}

void fanOff() {
  digitalWrite(FAN_PIN, 0);
  if (!mqttClient.publish(EXTRACTOR_TOPIC, 1, true, "off")) {
    ESP.restart();
  }
}

void dumpSettings() {
  Serial.println("SETTINGS");
  Serial.printf(" On temperature: %0.1f\n", settings.onTemperature);
  Serial.printf(" On rate: %0.1f\n", settings.onRate);
  Serial.printf(" Off temperature: %0.1f\n", settings.offTemperature);
  Serial.printf(" Minimum minutes: %d\n", settings.minimumMinutes);
}

void loadSettings() {
  EEPROM.get(0, settings);

  if (settings.signature != 12345) {
    // This is the first time we've loaded the settings, set some sensible defaults.
    Serial.println("Saved settings not found, using defaults");

    settings.signature = 12345;
    settings.onTemperature = 25;
    settings.onRate = 10;
    settings.offTemperature = 23;
    settings.minimumMinutes = 5;

    EEPROM.put(0, settings);
    EEPROM.commit();
  }

  dumpSettings();
}

void procesSettings(const char* psettings) {
  JsonDocument doc;
  deserializeJson(doc, psettings);

  settings.onTemperature = doc["onTemperature"];
  settings.onRate = doc["onRate"];
  settings.offTemperature = doc["offTemperature"];
  settings.minimumMinutes = doc["minimumMinutes"];

  EEPROM.put(0, settings);
  EEPROM.commit();

  dumpSettings();
}

void handleMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t len, size_t index, size_t total) {
  Serial.printf("MQTT message received for topic: %s\n", topic);

  if (!strcmp(topic, "shower/override")) {
    memcpy(mqttMessage, payload, len);
    mqttMessage[len] = 0;
    Serial.printf("MQTT message: %s\n", mqttMessage);
    if (!strcmp(mqttMessage, "on")) {
      fanOn();
    } else if (!strcmp(mqttMessage, "off")) {
      fanOff();
    }
  } else if (!strcmp(topic, SETTINGS_TOPIC)) {
    memcpy(mqttMessage, payload, len);
    mqttMessage[len] = 0;
    procesSettings(mqttMessage);
  } else {
    Serial.println("Unknown topic");
  }
}

void connectMqtt() {
  mqttClient
      .setServer(mqttHost, mqttPort)
      .setClientId(MQTT_CLIENT)
      .setCleanSession(false)
      .onConnect(handleMqttConnected)
      .onSubscribe(handleSubscribed)
      .onMessage(handleMessage)
      .connect();

  while (!mqttClient.connected()) {
    mqttClient.loop();
    delay(100);
  }

  // mqttClient.subscribe("shower/override", 1);
  mqttClient.subscribe(SETTINGS_TOPIC, 1);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting");

  EEPROM.begin(64);
  loadSettings();
  Serial.println("Settings initialised");

  fanOff();
  pinMode(FAN_PIN, OUTPUT);

  connectWiFi();
  Serial.println("WiFi initialised");

  connectMqtt();
  Serial.println("MQTT initialised");

  sensors.begin();
  sensors.setWaitForConversion(true);
  int numberOfDevices = sensors.getDeviceCount();
  Serial.printf("%d temperature sensor(s) initialised\n", numberOfDevices);

  state = STATE_IDLE;
  Serial.println("State initialised");
}

// Smoothed rate of change > 10, min 5 minutes, then until temp < 20

unsigned long fanOnTime;

void loop() {
  float tempC, roc;

  if (!WiFi.isConnected() || !mqttClient.connected()) {
    ESP.restart();
  }

  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastMsg > 5000) {
    sensors.requestTemperatures();
    tempC = sensors.getTempCByIndex(0);
    recordReading(tempC);
    roc = rateOfChange();

    snprintf(msg, MSG_BUFFER_SIZE, "%0.1f", roc);
    if (!mqttClient.publish(RATE_TOPIC, 1, true, msg)) {
      ESP.restart();
    }

    snprintf(msg, MSG_BUFFER_SIZE, "%0.1f", tempC);
    if (!mqttClient.publish(TEMPERATURE_TOPIC, 1, true, msg)) {
      ESP.restart();
    }

    Serial.printf("Temp: %0.1f, rate of change: %0.1f, current state: %d\n", tempC, roc, state);

    switch (state) {
      case STATE_IDLE:
        if (!mqttClient.publish(STATE_TOPIC, 1, true, "idle")) {
          ESP.restart();
        }

        // Idle state, if we see the temp go up fast or the temp rises too much then turn the fan on cos the water's flowing.
        if (roc > settings.onRate || tempC > settings.onTemperature) {
          fanOn();
          fanOnTime = millis();
          state = STATE_INITIAL;
        }

        break;

      case STATE_INITIAL:
        if (!mqttClient.publish(STATE_TOPIC, 1, true, "initial")) {
          ESP.restart();
        }

        // We've just turned the fan on, keep it on a period, resetting the timer if there's another ramp up in the temperature.
        if (roc > settings.onRate) {
          fanOnTime = millis();
          break;
        }

        if ((millis() - fanOnTime) > 1000UL * 60UL * settings.minimumMinutes) {
          state = STATE_WAITING;
        }

        break;

      case STATE_WAITING:
        if (!mqttClient.publish(STATE_TOPIC, 1, true, "waiting")) {
          ESP.restart();
        }

        // Initial delay is over, wait for the temperature to be low enough to switch the fan off, unless there's another ramp up.
        if (roc > settings.onRate) {
          fanOnTime = millis();
          state = STATE_INITIAL;
          break;
        }

        if (tempC < settings.offTemperature) {
          fanOff();
          state = STATE_IDLE;
          break;
        }
    }

    lastMsg = now;
  }
}
