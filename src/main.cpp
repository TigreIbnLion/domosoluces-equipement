#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <mqtt_client.h>
#include <ArduinoJson.h>
#include "KeyestudioAdapter.h"
#include "RecentCommandCache.h"
#include "DeviceConfig.h"
#include "ProvisioningPortal.h"
#include "KeyestudioHome.h"

// Development configuration is injected at build time. Never commit production secrets.
#ifndef DOMO_WIFI_SSID
#define DOMO_WIFI_SSID ""
#endif
#ifndef DOMO_WIFI_PASSWORD
#define DOMO_WIFI_PASSWORD ""
#endif
#ifndef DOMO_PROVISIONING_CODE
#define DOMO_PROVISIONING_CODE ""
#endif
#ifndef DOMO_PROVISION_BUTTON_PIN
#define DOMO_PROVISION_BUTTON_PIN 16
#endif
#ifndef DOMO_PROVISION_HOLD_MS
#define DOMO_PROVISION_HOLD_MS 3000UL
#endif
#ifndef DOMO_NTP_SERVER
#define DOMO_NTP_SERVER "pool.ntp.org"
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
#ifndef DOMO_MQTT_TLS
#define DOMO_MQTT_TLS 0
#endif
#ifndef DOMO_MQTT_CA_CERT
#define DOMO_MQTT_CA_CERT ""
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
#ifndef DOMO_RESTORE_LAST_STATE
#define DOMO_RESTORE_LAST_STATE 0
#endif

using domo::LogicalState;
esp_mqtt_client_handle_t mqtt = nullptr;
bool mqttConnected = false;
domo::KeyestudioAdapter hardware;
domo::RecentCommandCache recentCommands;
domo::DeviceConfig deviceConfig;
domo::WifiCredentials wifiCredentials;
domo::ProvisioningPortal provisioning(deviceConfig, DOMO_DEVICE_UID, DOMO_PROVISIONING_CODE);
domo::KeyestudioHome home;

String rootTopic;
String mqttClientId;
volatile uint32_t qos1PublishedCount = 0;
volatile uint32_t mqttPublishFailureCount = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastTelemetry = 0;
unsigned long localAlarmUntil = 0;
unsigned long lastHomeDiagnostic = 0;
constexpr unsigned long HOME_DIAGNOSTIC_MS = 10000;
unsigned long reconnectAt = 0;
bool firstMqttSession = true;
bool recoveredPhysicalState = false;
bool hardwareReady = false;
bool commandCacheReady = false;
unsigned long lastWifiBegin = 0;
bool wifiAttemptInProgress = false;
wl_status_t lastWifiStatus = WL_NO_SHIELD;
unsigned long mqttDisconnectedSince = 0;
constexpr unsigned long HEARTBEAT_MS = 30000;
constexpr unsigned long TELEMETRY_MS = 60000;
constexpr unsigned long RECONNECT_MS = 3000;
constexpr unsigned long WIFI_RETRY_MS = 30000;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr unsigned long MQTT_STUCK_MS = 60000;
constexpr unsigned long TIME_SYNC_RETRY_MS = 15000;
constexpr time_t MIN_VALID_UNIX_TIME = 1704067200; // 2024-01-01 UTC
unsigned long lastTimeSyncBegin = 0;
bool timeSyncRequested = false;
bool timeSyncReported = false;
constexpr size_t MQTT_COMMAND_MAX_BYTES = 512;
char mqttCommandBuffer[MQTT_COMMAND_MAX_BYTES];
int mqttCommandExpectedBytes = 0;
int mqttCommandReceivedBytes = 0;
bool mqttCommandDiscarding = false;

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

bool publishJson(const String& target, JsonDocument& doc, bool retained=false) {
  if (!mqtt || !mqttConnected) return false;
  constexpr size_t PayloadCapacity = 512;
  const size_t required = measureJson(doc);
  if (required == 0 || required >= PayloadCapacity) {
    ++mqttPublishFailureCount;
    return false;
  }
  char payload[PayloadCapacity];
  const size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n != required) {
    ++mqttPublishFailureCount;
    return false;
  }
  const int messageId = esp_mqtt_client_publish(
      mqtt, target.c_str(), payload, static_cast<int>(n), 1, retained ? 1 : 0);
  if (messageId < 0) {
    ++mqttPublishFailureCount;
    return false;
  }
  return true;
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
  if (!looksLikeUuid(id)) { publishAck(id, false, "invalid_command_id"); return; }
  if (!looksLikeIso8601(sentAt)) { publishAck(id, false, "invalid_sent_at"); return; }
  if (device != DOMO_DEVICE_UID || kit != DOMO_KIT_SERIAL) {
    publishAck(id, false, "identity_mismatch"); return;
  }
  if (!hardwareReady) { publishAck(id, false, "hardware_not_ready"); return; }
  if (!commandCacheReady) { publishAck(id, false, "idempotence_storage_unavailable"); return; }
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
    const auto confirmedState = hardware.readState();
    deviceConfig.saveConfirmedState(confirmedState);
    if (!recentCommands.remember(id, true, confirmedState)) {
      commandCacheReady = false;
      publishState("command");
      publishAck(id, false, "idempotence_persist_failed", confirmedState);
      return;
    }
    publishState("command");
    publishAck(id, true, nullptr, confirmedState);
  } else {
    const auto failedState = hardware.readState();
    if (!recentCommands.remember(id, false, failedState, "hardware_action_failed")) {
      commandCacheReady = false;
      publishAck(id, false, "idempotence_persist_failed", failedState);
      return;
    }
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

const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "idle";
    case WL_NO_SSID_AVAIL: return "ssid_unavailable";
    case WL_SCAN_COMPLETED: return "scan_completed";
    case WL_CONNECTED: return "connected";
    case WL_CONNECT_FAILED: return "connect_failed";
    case WL_CONNECTION_LOST: return "connection_lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "unknown";
  }
}

void connectWifi() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED || !wifiCredentials.configured() || provisioning.active()) return;

  const unsigned long now = millis();
  if (wifiAttemptInProgress) {
    if (now - lastWifiBegin < WIFI_CONNECT_TIMEOUT_MS) return;
    Serial.printf("[WIFI] attempt timeout status=%s; resetting station before retry\n",
                  wifiStatusName(status));
    WiFi.disconnect(false, false);
    wifiAttemptInProgress = false;
  }

  if (lastWifiBegin != 0 && now - lastWifiBegin < WIFI_RETRY_MS) return;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  Serial.printf("[WIFI] connecting ssid=%s\n", wifiCredentials.ssid.c_str());
  const wl_status_t beginStatus = WiFi.begin(wifiCredentials.ssid.c_str(), wifiCredentials.password.c_str());
  lastWifiBegin = now == 0 ? 1 : now;
  wifiAttemptInProgress = true;
  Serial.printf("[WIFI] begin status=%s\n", wifiStatusName(beginStatus));
}

bool systemTimeValid() {
  return time(nullptr) >= MIN_VALID_UNIX_TIME;
}

void reportSystemTimeOnce() {
  if (timeSyncReported || !systemTimeValid()) return;
  const time_t now = time(nullptr);
  struct tm utc {};
  if (gmtime_r(&now, &utc)) {
    char formatted[32];
    strftime(formatted, sizeof(formatted), "%Y-%m-%dT%H:%M:%SZ", &utc);
    Serial.printf("[TIME] UTC synchronized: %s (epoch=%lld)\n",
                  formatted, static_cast<long long>(now));
  } else {
    Serial.printf("[TIME] Clock valid (epoch=%lld)\n", static_cast<long long>(now));
  }
  timeSyncReported = true;
}

bool ensureTlsClockReady() {
  if (!DOMO_MQTT_TLS) return true;
  if (systemTimeValid()) return true;

  const unsigned long now = millis();
  if (!timeSyncRequested || now - lastTimeSyncBegin >= TIME_SYNC_RETRY_MS) {
    configTime(0, 0, DOMO_NTP_SERVER);
    timeSyncRequested = true;
    lastTimeSyncBegin = now == 0 ? 1 : now;
    Serial.println("[TIME] NTP sync requested; MQTT TLS waits for a valid clock");
  }
  return false;
}

void resetMqttCommandAssembly() {
  mqttCommandExpectedBytes = 0;
  mqttCommandReceivedBytes = 0;
  mqttCommandDiscarding = false;
}

void handleMqttData(esp_mqtt_event_handle_t event) {
  if (event->data_len <= 0 || event->total_data_len <= 0 ||
      event->current_data_offset < 0) {
    resetMqttCommandAssembly();
    return;
  }

  if (event->current_data_offset == 0) {
    resetMqttCommandAssembly();
    const String incomingTopic(event->topic ? event->topic : "", event->topic_len);
    if (incomingTopic != topic("command")) return;

    mqttCommandExpectedBytes = event->total_data_len;
    if (mqttCommandExpectedBytes > static_cast<int>(MQTT_COMMAND_MAX_BYTES)) {
      mqttCommandDiscarding = true;
    }
  } else if (mqttCommandExpectedBytes == 0 && !mqttCommandDiscarding) {
    // Continuation without an active first fragment is never executable.
    return;
  }

  if (mqttCommandDiscarding) {
    if (event->current_data_offset + event->data_len >= event->total_data_len) {
      resetMqttCommandAssembly();
    }
    return;
  }

  if (mqttCommandExpectedBytes != event->total_data_len ||
      event->current_data_offset != mqttCommandReceivedBytes ||
      event->current_data_offset + event->data_len > mqttCommandExpectedBytes) {
    resetMqttCommandAssembly();
    return;
  }

  memcpy(mqttCommandBuffer + event->current_data_offset, event->data, event->data_len);
  mqttCommandReceivedBytes += event->data_len;

  if (mqttCommandReceivedBytes == mqttCommandExpectedBytes) {
    String commandTopic = topic("command");
    onMessage(const_cast<char*>(commandTopic.c_str()),
              reinterpret_cast<byte*>(mqttCommandBuffer),
              static_cast<unsigned int>(mqttCommandReceivedBytes));
    resetMqttCommandAssembly();
  }
}

void onMqttEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  auto event = static_cast<esp_mqtt_event_handle_t>(eventData);
  if (eventId == MQTT_EVENT_CONNECTED) {
    Serial.println("[MQTT] TLS/MQTT connected");
    mqttConnected = true;
    mqttDisconnectedSince = 0;
    lastHeartbeat = millis();
    lastTelemetry = millis();
    esp_mqtt_client_subscribe(mqtt, topic("command").c_str(), 1);
    esp_mqtt_client_subscribe(mqtt, topic("schedule").c_str(), 1);
    publishHeartbeat();
    publishState(firstMqttSession ? (recoveredPhysicalState ? "recovery" : "boot") : "reconnect");
    firstMqttSession = false;
  } else if (eventId == MQTT_EVENT_ERROR) {
    Serial.println("[MQTT] connection error");
    if (event && event->error_handle) {
      const auto* error = event->error_handle;
      Serial.printf("[MQTT] error_type=%d tls_last=0x%x tls_stack=0x%x cert_flags=0x%x sock_errno=%d\n",
                    static_cast<int>(error->error_type),
                    static_cast<unsigned int>(error->esp_tls_last_esp_err),
                    static_cast<unsigned int>(error->esp_tls_stack_err),
                    static_cast<unsigned int>(error->esp_tls_cert_verify_flags),
                    error->esp_transport_sock_errno);
    }
  } else if (eventId == MQTT_EVENT_DISCONNECTED) {
    mqttConnected = false;
    resetMqttCommandAssembly();
    if (mqttDisconnectedSince == 0) {
      mqttDisconnectedSince = millis() == 0 ? 1 : millis();
    }
  } else if (eventId == MQTT_EVENT_PUBLISHED) {
    // For QoS 1, ESP-MQTT emits this after the broker PUBACK is received.
    ++qos1PublishedCount;
  } else if (eventId == MQTT_EVENT_DATA) {
    handleMqttData(event);
  }
}

void startMqtt() {
  if (mqtt || WiFi.status() != WL_CONNECTED || strlen(DOMO_MQTT_HOST) == 0) return;
  if (DOMO_MQTT_TLS && strlen(DOMO_MQTT_CA_CERT) == 0) return;
  // X.509 validity checks require a trustworthy wall clock. Never weaken TLS
  // verification to work around an unsynchronised RTC after boot.
  if (!ensureTlsClockReady()) return;
  String uri = String(DOMO_MQTT_TLS ? "mqtts://" : "mqtt://") +
               DOMO_MQTT_HOST + ":" + DOMO_MQTT_PORT;
  esp_mqtt_client_config_t config = {};
  config.uri = uri.c_str();
  if (DOMO_MQTT_TLS) {
    // Trust only the injected recipe/production CA. Never disable certificate
    // verification and never embed a private CA key in firmware.
    config.cert_pem = DOMO_MQTT_CA_CERT;
  }
  config.username = strlen(DOMO_MQTT_USER) ? DOMO_MQTT_USER : nullptr;
  config.password = strlen(DOMO_MQTT_PASSWORD) ? DOMO_MQTT_PASSWORD : nullptr;
  config.client_id = mqttClientId.c_str();
  mqtt = esp_mqtt_client_init(&config);
  if (!mqtt) return;
  if (esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, onMqttEvent, nullptr) != ESP_OK) {
    esp_mqtt_client_destroy(mqtt);
    mqtt = nullptr;
    return;
  }
  if (esp_mqtt_client_start(mqtt) != ESP_OK) {
    esp_mqtt_client_destroy(mqtt);
    mqtt = nullptr;
    return;
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.printf("\n[BOOT] DOMOSOLUCES firmware=%s reset_reason=%d\n",
                DOMO_FIRMWARE_VERSION, static_cast<int>(esp_reset_reason()));
  Serial.printf("[BOOT] identity kit=%s device=%s\n", DOMO_KIT_SERIAL, DOMO_DEVICE_UID);
  deviceConfig.begin();
  wifiCredentials = deviceConfig.wifi(DOMO_WIFI_SSID, DOMO_WIFI_PASSWORD);
  Serial.printf("[BOOT] config wifi=%s mqtt_host=%s mqtt_port=%d tls=%s\n",
                wifiCredentials.configured() ? "configured" : "missing",
                strlen(DOMO_MQTT_HOST) ? DOMO_MQTT_HOST : "missing",
                DOMO_MQTT_PORT, DOMO_MQTT_TLS ? "on" : "off");
  rootTopic = String("domosoluces/kits/") + DOMO_KIT_SERIAL + "/devices/" + DOMO_DEVICE_UID;
  mqttClientId = String("domosoluces-") + DOMO_KIT_SERIAL + "-" + DOMO_DEVICE_UID;
  pinMode(DOMO_PROVISION_BUTTON_PIN, INPUT_PULLUP);
  bool forceProvision = false;
  if (digitalRead(DOMO_PROVISION_BUTTON_PIN) == LOW) {
    const unsigned long heldFrom = millis();
    while (digitalRead(DOMO_PROVISION_BUTTON_PIN) == LOW &&
           millis() - heldFrom < DOMO_PROVISION_HOLD_MS) { delay(10); }
    forceProvision = digitalRead(DOMO_PROVISION_BUTTON_PIN) == LOW &&
                     millis() - heldFrom >= DOMO_PROVISION_HOLD_MS;
    Serial.printf("[PROVISION] boot button=%s hold_ms=%lu\n",
                  forceProvision ? "accepted" : "ignored_short_press",
                  static_cast<unsigned long>(millis() - heldFrom));
  }
  hardwareReady = hardware.begin();
  home.begin();
  if (hardwareReady) {
    const bool restoreLastState=deviceConfig.restoreLastStateEnabled(DOMO_RESTORE_LAST_STATE != 0);
    const auto recovered=deviceConfig.confirmedState();
    if (restoreLastState && recovered != LogicalState::Unknown && hardware.setState(recovered)) {
      recoveredPhysicalState=true;
      Serial.printf("[RECOVERY] restored confirmed state=%s policy=restore_last_state\n", stateName(recovered));
    } else {
      Serial.printf("[RECOVERY] policy=%s stored_state=%s\n",
                    restoreLastState ? "restore_last_state" : "force_off",
                    stateName(recovered) ? stateName(recovered) : "unknown");
    }
  }
  commandCacheReady = recentCommands.begin();
  Serial.printf("[BOOT] hardware=%s command_cache=%s\n",
                hardwareReady ? "ready" : "not_ready",
                commandCacheReady ? "ready" : "not_ready");
  if (!wifiCredentials.configured() || forceProvision) {
    Serial.printf("[PROVISION] requested reason=%s\n", forceProvision ? "physical_button" : "wifi_missing");
    provisioning.begin();
  } else {
    connectWifi();
  }
}

void loop() {
  hardware.loop();
  const auto localEvents = home.poll();
  if (localEvents.button1Pressed && hardwareReady) {
    const auto current=hardware.readState(); const auto target=current==LogicalState::On?LogicalState::Off:LogicalState::On;
    if(hardware.setState(target)) { deviceConfig.saveConfirmedState(hardware.readState()); if(mqttConnected) publishState("local"); }
  }
  if (home.shouldAlarm(localEvents)) { home.buzzer(true); localAlarmUntil=millis()+5000UL; }
  if (localAlarmUntil && static_cast<long>(millis()-localAlarmUntil)>=0) { home.buzzer(false); localAlarmUntil=0; }
  if (millis()-lastHomeDiagnostic >= HOME_DIAGNOSTIC_MS) {
    lastHomeDiagnostic=millis(); const auto s=home.snapshot();
    Serial.printf("[HOME] motion=%d gas=%d steam=%d climate=%s temp=%.1fC humidity=%.1f%% fan=%d buzzer=%d door=%d window=%d indicator=%d\n",
      s.inputs.motion,s.inputs.gas,s.inputs.steam,s.hasClimate?"valid":"unavailable",s.temperatureC,s.humidityPct,s.fanOn,s.buzzerOn,s.doorOpen,s.windowOpen,s.indicatorOn);
  }
  provisioning.loop();
  if (provisioning.active()) { delay(5); return; }
  const wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != lastWifiStatus) {
    Serial.printf("[WIFI] status=%s (%d)\n", wifiStatusName(wifiStatus), static_cast<int>(wifiStatus));
    lastWifiStatus = wifiStatus;
  }
  if (wifiStatus != WL_CONNECTED) {
    mqttConnected = false;
    if (millis() >= reconnectAt) {
      reconnectAt = millis() + RECONNECT_MS;
      connectWifi();
    }
    delay(10); return;
  }
  if (wifiAttemptInProgress) {
    wifiAttemptInProgress = false;
    Serial.printf("[WIFI] connected ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  if (DOMO_MQTT_TLS && !systemTimeValid()) {
    ensureTlsClockReady();
  } else if (DOMO_MQTT_TLS) {
    reportSystemTimeOnce();
  }
  if (!mqtt && millis() >= reconnectAt) {
    reconnectAt = millis() + RECONNECT_MS;
    startMqtt();
  }
  if (mqtt && !mqttConnected && mqttDisconnectedSince != 0 &&
      millis() - mqttDisconnectedSince >= MQTT_STUCK_MS) {
    esp_mqtt_client_stop(mqtt);
    esp_mqtt_client_destroy(mqtt);
    mqtt = nullptr;
    mqttDisconnectedSince = 0;
    resetMqttCommandAssembly();
    reconnectAt = millis() + RECONNECT_MS;
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
