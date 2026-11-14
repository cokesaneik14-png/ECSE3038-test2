// ECSE3038 Test 2: the bathroom fan.
// Every 10 s: read humidity, POST {"mac", "humidity"} with X-API-Key.
// MQTT: will on status, settings JSON on fan/settings, on/off on fan/state.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHTesp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "secrets.h"                          // #define DEVICE_KEY "..."

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""
#define WIFI_CHANNEL 6

const char *API_URL = "http://tessan.mexicocentral.cloudapp.azure.com";
const char *MQTT_NAME = "yourname";
const char *BROKER_HOST = "135.232.203.144";
const uint16_t BROKER_PORT = 1883;

const unsigned long WIFI_TIMEOUT_MS = 15000;
const unsigned long RETRY_EVERY_MS = 5000;
const unsigned long SEND_EVERY_MS = 10000;
const int DHT_PIN = 15;                       // the DHT22's data wire
const int FAN_PIN = 2;                        // the LED that stands for the fan

DHTesp dht;
WiFiClient net;
PubSubClient mqtt(net);

String statusTopic;                           // ecse3038/<name>/status
String settingsTopic;                         // ecse3038/<name>/fan/settings
String stateTopic;                            // ecse3038/<name>/fan/state

unsigned long lastSend = 0;
unsigned long lastAttempt = 0;
String mode = "";                             // "" = no settings yet: fan off
float maxHumidity = NAN;
float lastHumidity = NAN;
String reportedFan = "";                      // what we last published on fan/state

bool connectWiFi() {
  Serial.print("Connecting to ");
  Serial.print(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println(" no Wi-Fi after 15 s, trying again in 5 s");
      return false;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println(" connected");
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  return true;
}

// B1: one humidity reading, with this board's key.
void sendReading(float humidity) {
  char body[80];
  snprintf(body, sizeof(body), "{\"mac\": \"%s\", \"humidity\": %.1f}",
           WiFi.macAddress().c_str(), humidity);

  HTTPClient http;
  http.begin(String(API_URL) + "/readings");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_KEY);
  int status = http.POST(body);

  Serial.print("POST ");
  Serial.print(body);
  Serial.print(" -> ");
  Serial.println(status);
  if (status == 401) {
    Serial.println("401: register this MAC with POST /fans, put the key in include/secrets.h");
  } else if (status == 403) {
    Serial.println("403: this key belongs to another fan");
  } else if (status < 0) {
    Serial.println(http.errorToString(status));
  }
  http.end();
}

// B3: accept only JSON with both fields and a valid mode.
bool parseSettings(const String &message, String &newMode, float &newMax) {
  JsonDocument doc;
  if (deserializeJson(doc, message)) {
    return false;                             // not JSON
  }
  if (!doc.is<JsonObject>() || !doc["mode"].is<const char *>() || !doc["max_humidity"].is<float>()) {
    return false;                             // a field missing, or the wrong type
  }
  String m = doc["mode"].as<const char *>();
  if (m != "auto" && m != "on" && m != "off") {
    return false;
  }
  newMode = m;
  newMax = doc["max_humidity"].as<float>();
  return true;
}

// B4 + B5: drive the fan, report it only when it changes.
void updateFan() {
  bool fanOn = false;                         // no settings, or "off"
  if (mode == "on") {
    fanOn = true;
  } else if (mode == "auto") {
    fanOn = !isnan(lastHumidity) && lastHumidity > maxHumidity;
  }
  digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
  String fan = fanOn ? "on" : "off";

  Serial.print("Mode: ");
  Serial.print(mode == "" ? "(none)" : mode);
  Serial.print("  Max: ");
  Serial.print(maxHumidity);
  Serial.print("  Humidity: ");
  Serial.print(lastHumidity);
  Serial.print("  -> fan ");
  Serial.println(fan);

  if (fan != reportedFan && mqtt.connected()) {
    mqtt.publish(stateTopic.c_str(), fan.c_str(), true);   // retained
    reportedFan = fan;
  }
}

void onMessage(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();
  Serial.print("Got ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(message);

  if (String(topic) == settingsTopic) {
    String newMode;
    float newMax;
    if (parseSettings(message, newMode, newMax)) {
      mode = newMode;
      maxHumidity = newMax;
      updateFan();                            // react now, not at the next reading
    } else {
      Serial.println("Refused: settings unchanged.");
    }
  }
}

// B2: last will, then online, then subscribe.
bool connectMqtt() {
  String clientId = "esp32-" + String(MQTT_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = mqtt.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "offline");
  if (!ok) {
    Serial.print("Broker said no, state ");
    Serial.println(mqtt.state());
    return false;
  }
  Serial.println("Connected to broker");
  mqtt.publish(statusTopic.c_str(), "online", true);
  mqtt.subscribe(settingsTopic.c_str());      // retained settings arrive straight away
  reportedFan = "";                           // new connection: report the fan again
  return true;
}

void setup() {
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);                 // fan off until the first settings
  Serial.begin(115200);
  dht.setup(DHT_PIN, DHTesp::DHT22);

  String base = "ecse3038/" + String(MQTT_NAME);
  statusTopic = base + "/status";
  settingsTopic = base + "/fan/settings";
  stateTopic = base + "/fan/state";

  mqtt.setServer(BROKER_HOST, BROKER_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(15);
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttempt >= RETRY_EVERY_MS) {
      lastAttempt = millis();
      connectWiFi();
    }
    return;
  }

  if (mqtt.connected()) {
    mqtt.loop();
  } else if (millis() - lastAttempt >= RETRY_EVERY_MS) {
    lastAttempt = millis();                   // B2: reconnect if the broker drops
    connectMqtt();
  }

  if (millis() - lastSend < SEND_EVERY_MS) {
    return;
  }
  lastSend = millis();

  float humidity = dht.getHumidity();
  if (isnan(humidity)) {
    Serial.println("No reading from the sensor");
    return;
  }
  lastHumidity = humidity;
  sendReading(humidity);
  updateFan();
}