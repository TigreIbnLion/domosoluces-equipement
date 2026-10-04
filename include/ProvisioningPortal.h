#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "DeviceConfig.h"
namespace domo {
class ProvisioningPortal {
 public:
  ProvisioningPortal(DeviceConfig& config, const String& deviceUid, const String& pairingCode)
      : config_(config), deviceUid_(deviceUid), pairingCode_(pairingCode), server_(80) {}
  bool begin() {
    if (pairingCode_.length() < 8) { Serial.println("[PROVISION] pairing code missing/too short"); return false; }
    String suffix=deviceUid_; suffix.replace("-", ""); if (suffix.length()>6) suffix=suffix.substring(suffix.length()-6);
    apSsid_="DOMOSOLUCES-"+suffix; WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(apSsid_.c_str(), pairingCode_.c_str())) return false;
    const char* headers[]={"X-Domosoluces-Pairing"}; server_.collectHeaders(headers,1);
    server_.on("/v1/provision/status", HTTP_GET, [this]{ status(); });
    server_.on("/v1/provision/networks", HTTP_GET, [this]{ networks(); });
    server_.on("/v1/provision/wifi", HTTP_POST, [this]{ configure(); });
    server_.onNotFound([this]{ server_.send(404,"application/json","{\"error\":\"not_found\"}"); });
    server_.begin(); active_=true; startedAt_=millis();
    Serial.printf("[PROVISION] active ssid=%s ip=%s\n", apSsid_.c_str(), WiFi.softAPIP().toString().c_str()); return true;
  }
  void loop() { if (!active_) return; server_.handleClient(); if (millis()-startedAt_ > 600000UL) { stop(); ESP.restart(); } }
  bool active() const { return active_; }
  void stop() { if (!active_) return; server_.stop(); WiFi.softAPdisconnect(true); active_=false; }
 private:
  bool authorized() { return server_.hasHeader("X-Domosoluces-Pairing") && server_.header("X-Domosoluces-Pairing")==pairingCode_; }
  void status() { JsonDocument d; d["device_uid"]=deviceUid_; d["mode"]="provision"; d["ap_ssid"]=apSsid_; d["wifi_2_4ghz_required"]=true; String out; serializeJson(d,out); server_.send(200,"application/json",out); }
  void networks() {
    if (!authorized()) { server_.send(401,"application/json","{\"error\":\"unauthorized\"}"); return; }
    int n=WiFi.scanNetworks(false,true); JsonDocument d; auto a=d["networks"].to<JsonArray>();
    for(int i=0;i<n;i++){ auto x=a.add<JsonObject>(); x["ssid"]=WiFi.SSID(i); x["rssi"]=WiFi.RSSI(i); x["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN; }
    WiFi.scanDelete(); String out; serializeJson(d,out); server_.send(200,"application/json",out);
  }
  void configure() {
    if (!authorized()) { server_.send(401,"application/json","{\"error\":\"unauthorized\"}"); return; }
    JsonDocument d; if(deserializeJson(d,server_.arg("plain"))){ server_.send(400,"application/json","{\"error\":\"invalid_json\"}"); return; }
    String ssid=d["ssid"]|""; String password=d["password"]|"";
    if(!config_.saveWifi(ssid,password)){ server_.send(422,"application/json","{\"error\":\"invalid_wifi_credentials\"}"); return; }
    server_.send(200,"application/json","{\"saved\":true,\"restarting\":true}"); delay(300); ESP.restart();
  }
  DeviceConfig& config_; String deviceUid_, pairingCode_, apSsid_; WebServer server_; bool active_{false}; unsigned long startedAt_{0};
};
}