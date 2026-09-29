#include <Arduino.h>
#include <WiFi.h>
#include <mqtt_client.h>
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
esp_mqtt_client_handle_t mqtt = nullptr;
bool mqttConnected = false;
domo::KeyestudioAdapter hardware;
domo::RecentCommandCache recentCommands;

String rootTopic;
unsigned long lastHeartbeat = 0;
unsigned long lastTelemetry = 0;
unsigned long reconnectAt = 0;
bool firstMqttSession = true;
bool hardwareReady = false;
bool commandCacheReady = false;
bool wifiConnectStarted = false;
constexpr unsigned long HEARTBEAT_MS = 30000;
constexpr unsigned long TELEMETRY_MS = 60000;
constexpr unsigned long RECONNECT_MS = 3000;

const char* stateName(LogicalState s) {
  if (s == LogicalState::On) return "on";
  if (s == LogicalState::Off) return "off";
  return nullptr;
}
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
  if (!mqtt || !mqttConnected) return;
  char payload[512];
  const size_t n = serializeJson(doc, payload, sizeof(payload));
  esp_mqtt_client_publish(mqtt, target.c_str(), payload, static_cast<int>(n), 1, retained ? 1 : 0);
}

void publishState(const char* reason) {
  const char* state = stateName(hardware.readState());
  if (!state) return; // V1 state only allows confirmed on/off.
  JsonDocument d;
  d["state"] = state;
  d["reason"] = reason;
  publishJson(topic("state"), d, true);
}

void publishHeartbeat() {
  const char* state = stateName(hardware.readState());
  if (!state) return; // Never fabricate off when hardware state is unknown.
  JsonDocument d;
  d["state"] = state;
  d["mode"] = hardwareReady ? "normal" : "error";
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
  if (!hardwareReady) { publishAck(id, false, "hardware_not_ready"); return; }
  if (!commandCacheReady) { publishAck(id, false, "idempotence_storage_unavailable"); return; }
  if (!looksLikeUuid(id)) { publishAck(id, false, "invalid_command_id"); return; }
  if (!looksLikeIso8601(sentAt)) { publishAck(id, false, "invalid_sent_at"); return; }
  if (device != DOMO_DEVICE_UID || kit != DOMO_KIT_SERIAL) {
    publishAck(id, false, "identity_mismatch"); return;
  }
  domo::CommandResult previous;
  if (recentCommands.find(id, previous)) {
    publishAck(id, previous.executed,
               previous.error.isEmpty() ? nullptr : previous.error.c_str(),
               previous.state);
    return; // idempotent: replay exact cached outcome, never the physical action
  }
  if (action != "set_state" || (requested != "on" && requested != "off")) {
    publishAck(id, false, "invalid_command"); return;
  }

  const auto target = requested == "on" ? LogicalState::On : LogicalState::Off;
  const bool ok = hardware.setState(target);
  if (ok) {
    recentCommands.remember(id, true, hardware.readState());
    publishState("command");
    publishAck(id, true, nullptr, hardware.readState());
  } else {
    const auto failedState = hardware.readState();
    recentCommands.remember(id, false, failedState, "hardware_action_failed");
    publishAck(id, false, "hardware_action_failed", failedState);
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
  if (WiFi.status() == WL_CONNECTED || wifiConnectStarted || strlen(DOMO_WIFI_SSID) == 0) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(DOMO_WIFI_SSID, DOMO_WIFI_PASSWORD);
  wifiConnectStarted = true;
}

void handleMqttData(esp_mqtt_event_handle_t event) {
  String incomingTopic(event->topic, event->topic_len);
  if (incomingTopic != topic("command")) return;
  if (event->total_data_len != event->data_len || event->data_len <= 0) return;
  onMessage(const_cast<char*>(incomingTopic.c_str()),
            reinterpret_cast<byte*>(event->data),
            static_cast<unsigned int>(event->data_len));
}

void onMqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
  if (eventId == MQTT_EVENT_CONNECTED) {
    mqttConnected = true;
    lastHeartbeat = millis();
    lastTelemetry = millis();
    esp_mqtt_client_subscribe(mqtt, topic("command").c_str(), 1);
    esp_mqtt_client_subscribe(mqtt, topic("schedule").c_str(), 1);
    publishHeartbeat();
    publishState(firstMqttSession ? "boot" : "reconnect");
    firstMqttSession = false;
  } else if (eventId == MQTT_EVENT_DISCONNECTED) {
    mqttConnected = false;
  } else if (eventId == MQTT_EVENT_DATA) {
    handleMqttData(event);
  }
}

void startMqtt() {
  if (mqtt || WiFi.status() != WL_CONNECTED || strlen(DOMO_MQTT_HOST) == 0) return;
  String uri = String("mqtt://") + DOMO_MQTT_HOST + ":" + DOMO_MQTT_PORT;
  esp_mqtt_client_config_t config = {};
  config.uri = uri.c_str();
  config.username = strlen(DOMO_MQTT_USER) ? DOMO_MQTT_USER : nullptr;
  config.password = strlen(DOMO_MQTT_PASSWORD) ? DOMO_MQTT_PASSWORD : nullptr;
  config.client_id = nullptr;
  mqtt = esp_mqtt_client_init(&config);
  if (!mqtt) return;
  esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, onMqttEvent, nullptr);
  esp_mqtt_client_start(mqtt);
}

void setup() {
  Serial.begin(115200);
  rootTopic = String("domosoluces/kits/") + DOMO_KIT_SERIAL + "/devices/" + DOMO_DEVICE_UID;
  hardwareReady = hardware.begin();
  commandCacheReady = recentCommands.begin();
  connectWifi();
}

void loop() {
  hardware.loop();
  if (WiFi.status() != WL_CONNECTED) {
    mqttConnected = false;
    if (!wifiConnectStarted && millis() >= reconnectAt) {
      reconnectAt = millis() + RECONNECT_MS;
      connectWifi();
    }
    delay(10); return;
  }
  wifiConnectStarted = false;
  if (!mqtt && millis() >= reconnectAt) {
    reconnectAt = millis() + RECONNECT_MS;
    startMqtt();
  }
  if (mqttConnected && millis() - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = millis();
    publishHeartbeat();
  }
  if (mqttConnected && millis() - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = millis();
    publishTelemetry();
  }
  delay(5);
}
