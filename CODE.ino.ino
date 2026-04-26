#define BLYNK_TEMPLATE_ID ""
#define BLYNK_TEMPLATE_NAME ""
#define BLYNK_AUTH_TOKEN ""
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

// ====================== PINS ======================
const int SOIL_PIN = 34;
const int LDR_PIN = 35;
const int PUMP_PIN = 26;
const int RED_LED = 27;
const int GREEN_LED = 25;

// ====================== WIFI ======================
char ssid[] = "";
char pass[] = "";

// ====================== CROP ======================
struct Crop {
  const char* name;
  int minMoisture;
};

Crop crops[5] = {
  {"Rice Paddy", 65}, {"Tomato", 55}, {"Maize", 45}, 
  {"Wheat", 40}, {"Cotton", 35}
};

int currentCropIndex = 2;
String currentCropName = "Maize";

int moistureThreshold = 45;
int airDryValue = 3100;
int waterWetValue = 1100;

bool autoMode = true;
bool pumpState = false;
float todayLiters = 0.0;
float flowRate = 1.5;
unsigned long pumpStartTime = 0;
unsigned long manualPumpTimeLimit = 600000;

// ====================== VIRTUAL PINS ======================
#define VP_SOIL      V0
#define VP_LDR       V1
#define VP_PUMP      V2
#define VP_AUTO      V3
#define VP_CROP      V4
#define VP_TIMER     V5
#define VP_WATER     V6
#define VP_AI        V7
#define VP_TERMINAL  V8
#define VP_RESET     V9
#define VP_TEMP      V10
#define VP_TIME      V11

// ====================== YOUR GEMINI API KEY ======================
const char* GEMINI_API_KEY = "";  

BlynkTimer timer;
Preferences prefs;
WiFiClientSecure client;

// ====================== LOCATION (Karunya Nagar, Coimbatore) ======================
const float YOUR_LAT = 10.9405;
const float YOUR_LNG = 76.7423;

// ====================== TIME & TEMPERATURE ======================
void getInternetTimeAndTemp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[40];
    strftime(timeStr, sizeof(timeStr), "%d %b %Y, %I:%M %p", &timeinfo);
    Blynk.virtualWrite(VP_TIME, timeStr);
  }

  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + 
               String(YOUR_LAT, 4) + "&longitude=" + String(YOUR_LNG, 4) + 
               "&current=temperature_2m";

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    int index = payload.indexOf("\"temperature_2m\":") + 17;
    if (index > 17) {
      String temp = payload.substring(index, index + 6);
      temp.trim();
      Blynk.virtualWrite(VP_TEMP, temp + " °C");
    }
  } else {
    Blynk.virtualWrite(VP_TEMP, "-- °C");
  }
  http.end();
}

// ====================== CORE FUNCTIONS ======================
int getMoisturePercent() {
  int raw = 0;
  for (int i = 0; i < 15; i++) {
    raw += analogRead(SOIL_PIN);
    delay(2);
  }
  raw /= 15;
  return constrain(map(raw, airDryValue, waterWetValue, 0, 100), 0, 100);
}

String getDayNight() {
  return (analogRead(LDR_PIN) > 800) ? "Day" : "Night";
}

void setPump(bool on) {
  pumpState = on;
  digitalWrite(PUMP_PIN, on ? HIGH : LOW);
  Blynk.virtualWrite(VP_PUMP, on ? 1 : 0);
  digitalWrite(GREEN_LED, on ? HIGH : LOW);
  digitalWrite(RED_LED, on ? LOW : HIGH);

  if (!on && pumpStartTime > 0) {
    float minutes = (millis() - pumpStartTime) / 60000.0;
    todayLiters += minutes * flowRate;
    Blynk.virtualWrite(VP_WATER, String(todayLiters, 1) + " L");
  }
  if (on) pumpStartTime = millis();
}

void loadSettings() {
  prefs.begin("irrigation", false);
  currentCropIndex = prefs.getInt("cropIndex", 2);
  prefs.end();
  moistureThreshold = crops[currentCropIndex].minMoisture;
  currentCropName = crops[currentCropIndex].name;
}

void saveSettings() {
  prefs.begin("irrigation", false);
  prefs.putInt("cropIndex", currentCropIndex);
  prefs.end();
}

// ====================== FIXED AI FUNCTION - 2026 Compatible ======================
void getAIAdvice() {
  Blynk.virtualWrite(VP_TERMINAL, "🤖 Asking AI...");

  int moisture = getMoisturePercent();
  String light = getDayNight();

  String prompt = "You are a smart farming advisor. Current crop: " + currentCropName + 
                  ". Soil moisture: " + String(moisture) + 
                  "%. Light condition: " + light + 
                  ". Water used today: " + String(todayLiters,1) + " liters. "
                  "Give short, practical advice to the farmer (1-2 sentences only).";

  // Updated model name for 2026
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + String(GEMINI_API_KEY);

  HTTPClient http;
  client.setInsecure();
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  String body = "{\"contents\":[{\"parts\":[{\"text\":\"" + prompt + "\"}]}]}";

  int httpCode = http.POST(body);
  
  Serial.println("AI HTTP Code: " + String(httpCode));

  if (httpCode == 200) {
    String response = http.getString();
    int pos = response.indexOf("\"text\": \"");
    if (pos > 0) {
      String advice = response.substring(pos + 9);
      int endPos = advice.indexOf("\"");
      if (endPos > 0) advice = advice.substring(0, endPos);
      advice.replace("\\n", "\n");
      Blynk.virtualWrite(VP_TERMINAL, "🤖 AI Advice:\n" + advice);
    } else {
      Blynk.virtualWrite(VP_TERMINAL, "✅ AI Connected!\nReply format issue.");
    }
  } else {
    Blynk.virtualWrite(VP_TERMINAL, "❌ AI Error: " + String(httpCode));
    Serial.println("Response: " + http.getString().substring(0, 400));
  }
  http.end();
}
// ====================== BLYNK ======================
BLYNK_WRITE(VP_PUMP)  { if(!autoMode) setPump(param.asInt()); }
BLYNK_WRITE(VP_AUTO)  { autoMode = param.asInt(); if(!autoMode) setPump(false); }
BLYNK_WRITE(VP_CROP)  { 
  currentCropIndex = param.asInt();
  if(currentCropIndex>=0 && currentCropIndex<5){
    moistureThreshold = crops[currentCropIndex].minMoisture;
    currentCropName = crops[currentCropIndex].name;
    saveSettings();
    Blynk.virtualWrite(VP_TERMINAL, "🌾 Crop: " + currentCropName);
  }
}
BLYNK_WRITE(VP_TIMER) { manualPumpTimeLimit = param.asInt() * 60000UL; }
BLYNK_WRITE(VP_AI)    { if(param.asInt()==1) getAIAdvice(); }
BLYNK_WRITE(VP_RESET) { 
  if(param.asInt()==1){
    todayLiters = 0;
    Blynk.virtualWrite(VP_WATER, "0.0 L");
    Blynk.virtualWrite(VP_TERMINAL, "✅ Water reset");
  }
}

void sendBlynkData() {
  Blynk.virtualWrite(VP_SOIL, getMoisturePercent());
  Blynk.virtualWrite(VP_LDR, analogRead(LDR_PIN));
  Blynk.virtualWrite(VP_WATER, String(todayLiters, 1) + " L");

  if (autoMode) {
    bool should = (getDayNight() == "Day" && getMoisturePercent() < moistureThreshold);
    if (should != pumpState) setPump(should);
  }

  if (pumpState && !autoMode && (millis() - pumpStartTime > manualPumpTimeLimit)) {
    setPump(false);
  }
}

void setup() {
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);

  Serial.begin(115200);
  loadSettings();
  setPump(false);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  configTime(19800, 0, "pool.ntp.org");

  timer.setInterval(2000L, sendBlynkData);
  timer.setInterval(60000L, getInternetTimeAndTemp);

  Serial.println("✅ System Started with New API Key");
}

void loop() {
  Blynk.run();
  timer.run();
}
