#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FirebaseESP32.h>
#include "time.h"

// WiFi credentials
const char* ssid = "Galaxy A26 5G";
const char* password = "malesmikir";

// Firebase config
#define FIREBASE_HOST "https://semarak-vision-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "GJMHgTXBnDzoRibBwwhnj2IRW784eILBWezcyD6s"

// Telegram Bot config
const String telegramBotToken = "8498483452:AAEUtjNq-5p1mgc9BhHm9_BDRhRKeYoAIUQ";
const String telegramChatID = "-1002964500193";

// Pin definitions
#define MQ135_PIN 34  // Analog pin for MQ135

// Firebase objects
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

// Air quality thresholds
const int GOOD_THRESHOLD = 100;
const int MODERATE_THRESHOLD = 200;
const int UNHEALTHY_THRESHOLD = 300;

// Timing variables
unsigned long lastReading = 0;
const unsigned long READING_INTERVAL = 30000; // 30 seconds
unsigned long lastAlert = 0;
const unsigned long ALERT_INTERVAL = 300000; // 5 minutes

// Device info
String deviceID = "SEMARAK_RT_001";
String location = "RT13 RW15";

void setup() {
  Serial.begin(115200);
  
  // Initialize WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Initialize Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Initialize time
  configTime(25200, 0, "pool.ntp.org"); // UTC+7 for Indonesia
  
  Serial.println("Air Quality Monitor initialized!");
}

void loop() {
  if (millis() - lastReading >= READING_INTERVAL) {
    readAndSendData();
    lastReading = millis();
  }
  
  delay(1000);
}

void readAndSendData() {
  // Read MQ135 sensor
  int sensorValue = analogRead(MQ135_PIN);
  
  // Convert to air quality index (simplified calculation)
  int airQualityIndex = map(sensorValue, 0, 4095, 0, 500);
  
  // Determine air quality status
  String status = getAirQualityStatus(airQualityIndex);
  String color = getStatusColor(airQualityIndex);
  
  // Get timestamp
  time_t now;
  time(&now);
  
  // Create data object
  DynamicJsonDocument doc(1024);
  doc["deviceID"] = deviceID;
  doc["location"] = location;
  doc["timestamp"] = now;
  doc["rawValue"] = sensorValue;
  doc["airQualityIndex"] = airQualityIndex;
  doc["status"] = status;
  doc["color"] = color;
  doc["latitude"] = -6.2088; // Example coordinates (Jakarta)
  doc["longitude"] = 106.8456;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Send to Firebase
  sendToFirebase(jsonString, airQualityIndex);
  
  // Check for alerts
  if (airQualityIndex > MODERATE_THRESHOLD && 
      millis() - lastAlert >= ALERT_INTERVAL) {
    sendTelegramAlert(status, airQualityIndex);
    lastAlert = millis();
  }
  
  // Print to serial
  Serial.println("Data sent:");
  Serial.println(jsonString);
}

void sendToFirebase(String data, int aqi) {
  String path = "/airquality/" + deviceID;
  
  if (Firebase.setString(firebaseData, path + "/current", data)) {
    Serial.println("Data sent to Firebase successfully");
  } else {
    Serial.println("Failed to send data to Firebase");
    Serial.println(firebaseData.errorReason());
  }
  
  // Store historical data
  time_t now;
  time(&now);
  String historyPath = "/history/" + deviceID + "/" + String(now);
  Firebase.setString(firebaseData, historyPath, data);
}

void sendTelegramAlert(String status, int aqi) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + telegramBotToken + "/sendMessage";
    
    String message = "🚨 PERINGATAN KUALITAS UDARA 🚨\\n\\n";
    message += "📍 Lokasi: " + location + "\\n";
    message += "📊 Status: " + status + "\\n";
    message += "🔢 AQI: " + String(aqi) + "\\n";
    message += "⏰ Waktu: " + getCurrentTime() + "\\n\\n";
    
    if (aqi > UNHEALTHY_THRESHOLD) {
      message += "⚠️ Kualitas udara tidak sehat! Hindari aktivitas luar ruangan.";
    } else {
      message += "⚠️ Kualitas udara sedang. Batasi aktivitas luar ruangan.";
    }
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(1024);
    doc["chat_id"] = telegramChatID;
    doc["text"] = message;
    doc["parse_mode"] = "Markdown";
    
    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpResponseCode = http.POST(requestBody);
    
    if (httpResponseCode == 200) {
      Serial.println("Telegram alert sent successfully");
    } else {
      Serial.println("Failed to send Telegram alert");
    }
    
    http.end();
  }
}

String getAirQualityStatus(int aqi) {
  if (aqi <= GOOD_THRESHOLD) return "Baik";
  else if (aqi <= MODERATE_THRESHOLD) return "Sedang";
  else if (aqi <= UNHEALTHY_THRESHOLD) return "Tidak Sehat";
  else return "Berbahaya";
}

String getStatusColor(int aqi) {
  if (aqi <= GOOD_THRESHOLD) return "#00E400";      // Green
  else if (aqi <= MODERATE_THRESHOLD) return "#FFFF00"; // Yellow
  else if (aqi <= UNHEALTHY_THRESHOLD) return "#FF7E00"; // Orange
  else return "#FF0000"; // Red
}

String getCurrentTime() {
  time_t now;
  time(&now);
  struct tm* timeinfo = localtime(&now);
  
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", timeinfo);
  
  return String(timeString);
}