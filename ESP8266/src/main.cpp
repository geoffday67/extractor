#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <espMqttClient.h>

#define MQTT_CLIENT "shower"

#define ROOT_TOPIC "shower"

#define CURRENT_TOPIC ROOT_TOPIC "/current"

#define CURRENT_STATE_TOPIC CURRENT_TOPIC "/state"
#define CURRENT_RATE_TOPIC CURRENT_TOPIC "/rate"
#define CURRENT_TEMPERATURE_TOPIC CURRENT_TOPIC "/temperature"
#define CURRENT_EXTRACTOR_TOPIC CURRENT_TOPIC "/extractor"

#define SETTINGS_TOPIC ROOT_TOPIC "/settings"
#define UPDATE_SETTINGS_TOPIC ROOT_TOPIC "/update-settings"
#define UPDATE_STATE_TOPIC ROOT_TOPIC "/update-state"

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
const char mqttHost[] = "loft.local";
const int mqttPort = 1883;
char mqttMessage[256];

int state;
float readings[5];
int readingIndex;
unsigned long fanOnTime;
unsigned long lastMsg;

struct Settings {
  int signature;
  float onTemperature, onRate, offTemperature;
  int minimumMinutes;
} settings;

void publish(const char* ptopic, const float value) {
  char s[32];

  sprintf(s, "%0.1f", value);
  mqttClient.publish(ptopic, 1, true, s);
}

void publish(const char* ptopic, const int value) {
  char s[32];

  sprintf(s, "%d", value);
  mqttClient.publish(ptopic, 1, true, s);
}

void publish(const char* ptopic, const char* pvalue) {
  mqttClient.publish(ptopic, 1, true, pvalue);
}

void fanOn() {
  digitalWrite(FAN_PIN, 1);
  publish(CURRENT_EXTRACTOR_TOPIC, "on");
}

void fanOff() {
  digitalWrite(FAN_PIN, 0);
  publish(CURRENT_EXTRACTOR_TOPIC, "off");
}

void transition(int newState) {
  switch (newState) {
    case STATE_IDLE:
      fanOff();
      state = STATE_IDLE;
      break;

    case STATE_INITIAL:
      fanOn();
      fanOnTime = millis();
      state = STATE_INITIAL;
      break;

    case STATE_WAITING:
      state = STATE_WAITING;
      break;
  }
}

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

void dumpSettings() {
  Serial.println("SETTINGS");
  Serial.printf(" On temperature: %0.1f\n", settings.onTemperature);
  Serial.printf(" On rate: %0.1f\n", settings.onRate);
  Serial.printf(" Off temperature: %0.1f\n", settings.offTemperature);
  Serial.printf(" Minimum minutes: %d\n", settings.minimumMinutes);

  publish(SETTINGS_TOPIC "/on-temperature", settings.onTemperature);
  publish(SETTINGS_TOPIC "/on-rate", settings.onRate);
  publish(SETTINGS_TOPIC "/off-temperature", settings.offTemperature);
  publish(SETTINGS_TOPIC "/minimum-minutes", settings.minimumMinutes);
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

  settings.onTemperature = doc["on-temperature"];
  settings.onRate = doc["on-rate"];
  settings.offTemperature = doc["off-temperature"];
  settings.minimumMinutes = doc["minimum-minutes"];

  EEPROM.put(0, settings);
  EEPROM.commit();

  dumpSettings();
}

void processState(const char *pstate) {
  if (!strcmp(pstate, "idle")) {
    transition(STATE_IDLE);
    lastMsg = 0;
  } else if (!strcmp(pstate, "initial")) {
    transition(STATE_INITIAL);
    lastMsg = 0;
  } else if (!strcmp(pstate, "waiting")) {
    transition(STATE_WAITING);
    lastMsg = 0;
  } else {
    Serial.printf("Unknown state: %s\n", pstate);
  }
}

void handleMessage(const espMqttClientTypes::MessageProperties& properties, const char* topic, const uint8_t* payload, size_t len, size_t index, size_t total) {
  Serial.printf("MQTT message received for topic: %s\n", topic);

  if (!strcmp(topic, UPDATE_STATE_TOPIC)) {
    memcpy(mqttMessage, payload, len);
    mqttMessage[len] = 0;
    processState(mqttMessage);
  } else if (!strcmp(topic, UPDATE_SETTINGS_TOPIC)) {
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

  mqttClient.subscribe(UPDATE_STATE_TOPIC, 1);
  mqttClient.subscribe(UPDATE_SETTINGS_TOPIC, 1);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting");

  EEPROM.begin(64);

  connectWiFi();
  Serial.println("WiFi initialised");

  connectMqtt();
  Serial.println("MQTT initialised");

  fanOff();
  pinMode(FAN_PIN, OUTPUT);
  Serial.println("Extractor initialised");

  loadSettings();
  Serial.println("Settings initialised");

  sensors.begin();
  sensors.setWaitForConversion(true);
  int numberOfDevices = sensors.getDeviceCount();
  Serial.printf("%d temperature sensor(s) initialised\n", numberOfDevices);

  state = STATE_IDLE;
  Serial.println("State initialised");
}

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

    publish(CURRENT_RATE_TOPIC, roc);
    publish(CURRENT_TEMPERATURE_TOPIC, tempC);

    // Serial.printf("Temp: %0.1f, rate of change: %0.1f, current state: %d\n", tempC, roc, state);

    // Always watch for rapidly increasing temperature whatever the current state.
    if (roc > settings.onRate) {
      transition(STATE_INITIAL);
    }

    switch (state) {
      case STATE_IDLE:
        publish(CURRENT_STATE_TOPIC, "idle");

        // Check for high temperature and start the cycle if found.
        if (tempC > settings.onTemperature) {
          transition(STATE_INITIAL);
        }

        break;

      case STATE_INITIAL:
        publish(CURRENT_STATE_TOPIC, "initial");

        // We've just turned the fan on, keep it on for a period.
        if ((millis() - fanOnTime) > 1000UL * 60UL * settings.minimumMinutes) {
          transition(STATE_WAITING);
        }

        break;

      case STATE_WAITING:
        publish(CURRENT_STATE_TOPIC, "waiting");

        // Initial delay is over, wait for the temperature to be low enough to switch the fan off.
        if (tempC < settings.offTemperature) {
          transition(STATE_IDLE);
        }

        break;
    }

    lastMsg = now;
  }
}
