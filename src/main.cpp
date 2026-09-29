#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "KeyestudioAdapter.h"
#include "RecentCommandCache.h"

// Development configuration is injected at build time. Never commit production secrets.
#ifndef DOMO_WIFI_SSID
#define DOMO_WIFI_SSID ""
#endif
#ifndef DOMO_WIFI_PASSWORD
#define DOMO_WIFI_PASSWORD ""
#endif
#ifndef DOMO_MQTT_HOST
#define DOMO_MQTT_HOST ""
#endif
#ifndef DOMO_MQTT_PORT
#define DOMO_MQTT_PORT 1883
#endif
#ifndef DOMO_MQTT_USER
#define DOMO_MQTT_USER ""
#endif
#ifndef DOMO_MQTT_PASSWORD
#define DOMO_MQTT_PASSWORD ""
#endif
#ifndef DOMO_KIT_SERIAL
#define DOMO_KIT_SERIAL "KIT-DEV"
#endif
#ifndef DOMO_DEVICE_UID
#define DOMO_DEVICE_UID "DEVICE-DEV"
#endif
#ifndef DOMO_FIRMWARE_VERSION
#define DOMO_FIRMWARE_VERSION "0.1.0-dev"
#endif

using domo::LogicalState;
WiFiClient network;
PubSubClient mqtt(network);
domo::KeyestudioAdapter hardware;
domo::RecentCommandCache recentCommands;

String rootTopic;
unsigned long lastHeartbeat = 0;
unsigned long lastTelemetry = 0;
unsigned long reconnectAt = 0;
bool firstMqttSession = true;
constexpr unsigned long HEARTBEAT_MS = 30000;
constexpr unsigned long TELEMETRY_MS = 60000;
constexpr unsigned long RECONNECT_MS = 3000;

const char* stateName(LogicalState s) { return s == LogicalState::On ? "on" : "off"; }
String topic(const char* suffix) { return rootTopic + "/" + suffix; }

bool looksLikeUuid(const String& value) {
  if (value.length() != 36) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (value[i] != '-') return false;
    } else if (!isxdigit(static_cast<unsigned char>(value[i]))) {
      return false;
    }
  }
  return true;
}

bool looksLikeIso8601(const String& value) {
  // V1 requires sent_at. Full clock validation belongs to the server; firmware
  // rejects clearly malformed timestamps without depending on Internet time.
  return value.length() >= 20 && value[4] == '-' && value[7] == '-' &&
         value[10] == 'T' && value.indexOf(':', 11) > 0;
}

void publishJson(const String& target, JsonDocument& doc, bool retained=false) {
  char payload[512];
  const size_t n = serializeJson(doc, payload, sizeof(payload));
  mqtt.publish(target.c_str(), reinterpret_cast<const uint8_t*>(payload), n, retained);
}

void publishState(const char* reason) {
  JsonDocument d;
  d["state"] = stateName(hardware.readState());
  d["reason"] = reason;
  publishJson(topic("state"), d, true);
}

void publishHeartbeat() {
  JsonDocument d;
  d["state"] = stateName(hardware.readState());
  d["mode"] = "normal";
  d["rssi"] = WiFi.RSSI();
  d["ip_address"] = WiFi.localIP().toString();
  d["firmware_version"] = DOMO_FIRMWARE_VERSION;
  d["uptime_ms"] = millis();
  publishJson(topic("heartbeat"), d);
}

void publishAck(const String& commandId, bool ok, const char* error=nullptr, LogicalState ackState=LogicalState::Unknown) {
  JsonDocument d;
  d["command_id"] = commandId;
  d["status"] = ok ? "executed" : "failed";
  const auto state = ackState == LogicalState::Unknown ? hardware.readState() : ackState;
  if (state != LogicalState::Unknown) d["state"] = stateName(state);
  d["error"] = error ? error : nullptr;
  d["firmware_version"] = DOMO_FIRMWARE_VERSION;
  d["uptime_ms"] = millis();
  publishJson(topic("ack"), d);
}

void onMessage(char* incomingTopic, byte* bytes, unsigned int length) {
  if (String(incomingTopic) != topic("command")) return;
  JsonDocument d;
  if (deserializeJson(d, bytes, length)) return;

  const String id = d["command_id"] | "";
  const String action = d["action"] | "";
  const String requested = d["state"] | "";
  const String device = d["device_uid"] | "";
  const String kit = d["kit_serial"] | "";
  const String sentAt = d["sent_at"] | "";

  if (id.isEmpty()) return; // cannot correlate an ACK safely
  if (!looksLikeUuid(id)) { publishAck(id, false, "invalid_command_id"); return; }
  if (!looksLikeIso8601(sentAt)) { publishAck(id, false, "invalid_sent_at"); return; }
  if (device != DOMO_DEVICE_UID || kit != DOMO_KIT_SERIAL) {
    publishAck(id, false, "identity_mismatch"); return;
  }
  domo::CommandResult previous;
  if (recentCommands.find(id, previous)) {
    publishAck(id, true, nullptr, previous.state);
    return; // idempotent: replay original result, never the physical action
  }
  if (action != "set_state" || (requested != "on" && requested != "off")) {
    publishAck(id, false, "invalid_command"); return;
  }

  const auto target = requested == "on" ? LogicalState::On : LogicalState::Off;
  const bool ok = hardware.setState(target);
  if (ok) {
    recentCommands.remember(id, hardware.readState());
    publishState("command");
    publishAck(id, true, nullptr, hardware.readState());
  } else {
    publishAck(id, false, "hardware_action_failed");
  }
}

void publishTelemetry() {
  const auto t = hardware.readTelemetry();
  if (!t.hasCurrentPower && !t.hasEnergyKwh && !t.hasVoltage && !t.hasCurrent && !t.hasPowerFactor) return;
  JsonDocument d;
  if (t.hasCurrentPower) d["current_power"] = t.currentPower;
  if (t.hasEnergyKwh) d["energy_kwh"] = t.energyKwh;
  if (t.hasVoltage) d["voltage_v"] = t.voltage;
  if (t.hasCurrent) d["current_a"] = t.current;
  if (t.hasPowerFactor) d["power_factor"] = t.powerFactor;
  publishJson(topic("telemetry"), d);
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED || strlen(DOMO_WIFI_SSID) == 0) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(DOMO_WIFI_SSID, DOMO_WIFI_PASSWORD);
}

void connectMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED || strlen(DOMO_MQTT_HOST) == 0) return;
  const String clientId = String("domosoluces-") + DOMO_KIT_SERIAL + "-" + DOMO_DEVICE_UID;
  if (mqtt.connect(clientId.c_str(), DOMO_MQTT_USER, DOMO_MQTT_PASSWORD)) {
    mqtt.subscribe(topic("command").c_str(), 1);
    mqtt.subscribe(topic("schedule").c_str(), 1);
    publishHeartbeat();
    publishState(firstMqttSession ? "boot" : "reconnect");
    firstMqttSession = false;
  }
}

void setup() {
  Serial.begin(115200);
  rootTopic = String("domosoluces/kits/") + DOMO_KIT_SERIAL + "/devices/" + DOMO_DEVICE_UID;
  hardware.begin();
  recentCommands.begin();
  mqtt.setServer(DOMO_MQTT_HOST, DOMO_MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(768);
  connectWifi();
}

void loop() {
  hardware.loop();
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() >= reconnectAt) { reconnectAt = millis() + RECONNECT_MS; connectWifi(); }
    delay(10); return;
  }
  if (!mqtt.connected() && millis() >= reconnectAt) {
    reconnectAt = millis() + RECONNECT_MS; connectMqtt();
  }
  mqtt.loop();
  if (mqtt.connected() && millis() - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = millis();
    publishHeartbeat();
  }
  if (mqtt.connected() && millis() - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = millis();
    publishTelemetry();
  }
  delay(5);
}
