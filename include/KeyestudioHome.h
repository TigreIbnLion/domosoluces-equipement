#pragma once
#include "HardwareAdapter.h"
#ifndef DOMO_LED_PIN
#define DOMO_LED_PIN 12
#endif
#ifndef DOMO_PIR_PIN
#define DOMO_PIR_PIN 14
#endif
#ifndef DOMO_DHT_PIN
#define DOMO_DHT_PIN 17
#endif
#ifndef DOMO_FAN_A_PIN
#define DOMO_FAN_A_PIN 19
#endif
#ifndef DOMO_FAN_B_PIN
#define DOMO_FAN_B_PIN 18
#endif
#ifndef DOMO_BUTTON1_PIN
#define DOMO_BUTTON1_PIN 16
#endif
#ifndef DOMO_BUTTON2_PIN
#define DOMO_BUTTON2_PIN 27
#endif
#ifndef DOMO_RGB_PIN
#define DOMO_RGB_PIN 26
#endif
#ifndef DOMO_GAS_PIN
#define DOMO_GAS_PIN 23
#endif
#ifndef DOMO_BUZZER_PIN
#define DOMO_BUZZER_PIN 25
#endif
#ifndef DOMO_WINDOW_PIN
#define DOMO_WINDOW_PIN 5
#endif
#ifndef DOMO_DOOR_PIN
#define DOMO_DOOR_PIN 13
#endif
#ifndef DOMO_STEAM_PIN
#define DOMO_STEAM_PIN 34
#endif
#ifndef DOMO_LOCAL_GAS_ALARM
#define DOMO_LOCAL_GAS_ALARM 1
#endif
#ifndef DOMO_LOCAL_RAIN_ALARM
#define DOMO_LOCAL_RAIN_ALARM 0
#endif
namespace domo {
struct HomeInputs { bool motion{false}; bool button1{false}; bool button2{false}; int gas{0}; int steam{0}; };
struct HomeEvents { bool motionStarted{false}; bool button1Pressed{false}; bool button2Pressed{false}; bool gasAlarm{false}; bool rainAlarm{false}; };
class KeyestudioHome {
 public:
  bool begin() {
    pinMode(DOMO_PIR_PIN,INPUT); pinMode(DOMO_BUTTON1_PIN,INPUT_PULLUP); pinMode(DOMO_BUTTON2_PIN,INPUT_PULLUP);
    pinMode(DOMO_GAS_PIN,INPUT); pinMode(DOMO_STEAM_PIN,INPUT);
    pinMode(DOMO_FAN_A_PIN,OUTPUT); pinMode(DOMO_FAN_B_PIN,OUTPUT); pinMode(DOMO_BUZZER_PIN,OUTPUT);
    fan(false); buzzer(false); return true;
  }
  HomeInputs inputs() const {
    HomeInputs x; x.motion=digitalRead(DOMO_PIR_PIN)==HIGH; x.button1=digitalRead(DOMO_BUTTON1_PIN)==LOW;
    x.button2=digitalRead(DOMO_BUTTON2_PIN)==LOW; x.gas=analogRead(DOMO_GAS_PIN); x.steam=analogRead(DOMO_STEAM_PIN); return x;
  }
  HomeEvents poll() {
    const unsigned long now=millis(); if(now-lastPoll_<100) return {};
    lastPoll_=now; const auto cur=inputs(); HomeEvents e;
    e.motionStarted=cur.motion&&!last_.motion; e.button1Pressed=cur.button1&&!last_.button1; e.button2Pressed=cur.button2&&!last_.button2;
    e.gasAlarm=cur.gas>=gasThreshold_&&last_.gas<gasThreshold_; e.rainAlarm=cur.steam>=steamThreshold_&&last_.steam<steamThreshold_;
    last_=cur; return e;
  }
  void fan(bool on) { digitalWrite(DOMO_FAN_A_PIN,on?HIGH:LOW); digitalWrite(DOMO_FAN_B_PIN,LOW); fanOn_=on; }
  void buzzer(bool on) { digitalWrite(DOMO_BUZZER_PIN,on?HIGH:LOW); buzzerOn_=on; }
  bool fanOn() const { return fanOn_; } bool buzzerOn() const { return buzzerOn_; }
  void setThresholds(int gas,int steam){ gasThreshold_=constrain(gas,0,4095); steamThreshold_=constrain(steam,0,4095); }
  bool shouldAlarm(const HomeEvents& e) const { return (DOMO_LOCAL_GAS_ALARM && e.gasAlarm) || (DOMO_LOCAL_RAIN_ALARM && e.rainAlarm); }
 private:
  HomeInputs last_{}; unsigned long lastPoll_{0}; int gasThreshold_{1800}; int steamThreshold_{1800}; bool fanOn_{false}; bool buzzerOn_{false};
};
}