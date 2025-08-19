#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FirebaseESP32.h>
#include <time.h>

// WiFi credentials
const char* ssid = "Galaxy A26 5G";
const char* password = "malesmikir";

// Firebase configuration
#define FIREBASE_HOST "https://semarak-vision-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "GJMHgTXBnDzoRibBwwhnj2IRW784eILBWezcyD6s"

// Telegram Bot Configuration
String TELEGRAM_BOT_TOKEN = "8498483452:AAEUtjNq-5p1mgc9BhHm9_BDRhRKeYoAIUQ";
String TELEGRAM_CHAT_ID = "-1002964500193";

// Pin definitions
#define MQ135_PIN 34   // Analog pin untuk MQ135 (ESP32)
#define BUZZER_PIN 2
#define LED_GREEN 4
#define LED_RED 5

// Sensor calibration
#define RL_VALUE 5     // Load resistance on the board, in kilo ohms
#define RO_CLEAN_AIR_FACTOR 3.6 // RO_CLEAR_AIR_FACTOR=(Sensor resistance in clean air)/RO

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

struct AirQualityData {
  float co2_ppm;
  float nh3_ppm;
  float alcohol_ppm;
  float benzene_ppm;
  int air_quality_index;
  String status;
  String status_color;
  String timestamp;
  String location_id;
  int raw_sensor_value;
};

// Location info untuk RT (sesuaikan dengan lokasi RT Anda)
String RT_ID = "RT013_RW015_KEL_CM";
String LOCATION_NAME = "RT 013 RW 015 Kelurahan Cipinagn Muara";
float LATITUDE = -6.222870475586725;  // Ganti dengan koordinat RT Anda
float LONGITUDE = 106.8926745769288;

// Timing variables untuk alert
unsigned long lastAlert = 0;
const unsigned long ALERT_INTERVAL = 300000; // 5 menit

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  
  // Test LEDs at startup
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED, HIGH);
  delay(1000);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  
  // Connect to WiFi
  connectWiFi();
  
  // Initialize Firebase
  initFirebase();
  
  // Initialize time (Indonesia timezone UTC+7)
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
  
  Serial.println("\n🌿 Smart Air Quality Monitor - RT Edition");
  Serial.println("📍 Lokasi: " + LOCATION_NAME);
  Serial.println("✅ System initialized successfully!");
  Serial.println("🔄 Memulai monitoring kualitas udara...\n");
}

void loop() {
  AirQualityData data = readAirQuality();
  
  // Display data di Serial Monitor
  displayData(data);
  
  // Send to Firebase
  sendToFirebase(data);
  
  // Update current data untuk real-time monitoring
  updateCurrentData(data);
  
  // Check air quality status dan kirim alert jika perlu
  checkAirQualityAlert(data);
  
  // Update status LEDs
  updateStatusLEDs(data.air_quality_index);
  
  delay(30000); // Baca setiap 30 detik
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("🔗 Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("✅ Connected! IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi connection failed!");
  }
}

void initFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  Serial.println("🔥 Firebase initialized");
}

AirQualityData readAirQuality() {
  AirQualityData data;
  
  // Read MQ135 sensor
  int sensorValue = analogRead(MQ135_PIN);
  float voltage = sensorValue * (3.3 / 4095.0);
  
  // Store raw sensor value
  data.raw_sensor_value = sensorValue;
  
  // Calculate gas concentrations (simplified calculation)
  data.co2_ppm = calculateCO2(voltage);
  data.nh3_ppm = calculateNH3(voltage);
  data.alcohol_ppm = calculateAlcohol(voltage);
  data.benzene_ppm = calculateBenzene(voltage);
  
  // Calculate Air Quality Index
  data.air_quality_index = calculateAQI(data);
  data.status = getAQIStatus(data.air_quality_index);
  data.status_color = getAQIColor(data.air_quality_index);
  data.timestamp = getCurrentTimestamp();
  data.location_id = RT_ID;
  
  return data;
}

float calculateCO2(float voltage) {
  // Improved CO2 calculation for MQ135
  if (voltage <= 0.1) return 400; // Default CO2 level
  
  float rs = ((3.3 * RL_VALUE) / voltage) - RL_VALUE;
  if (rs <= 0) return 400;
  
  float ratio = rs / (RL_VALUE * RO_CLEAN_AIR_FACTOR);
  float co2_ppm = 116.6020682 * pow(ratio, -2.769034857);
  
  // Realistic CO2 bounds (outdoor: 400-450 ppm, indoor: 400-2000 ppm)
  return constrain(co2_ppm, 400, 5000);
}

float calculateNH3(float voltage) {
  if (voltage <= 0.1) return 0;
  
  float rs = ((3.3 * RL_VALUE) / voltage) - RL_VALUE;
  if (rs <= 0) return 0;
  
  float ratio = rs / (RL_VALUE * RO_CLEAN_AIR_FACTOR);
  float nh3_ppm = 102.694 * pow(ratio, -2.668);
  
  return constrain(nh3_ppm, 0, 200);
}

float calculateAlcohol(float voltage) {
  if (voltage <= 0.1) return 0;
  
  float rs = ((3.3 * RL_VALUE) / voltage) - RL_VALUE;
  if (rs <= 0) return 0;
  
  float ratio = rs / (RL_VALUE * RO_CLEAN_AIR_FACTOR);
  float alcohol_ppm = 77.255 * pow(ratio, -3.18);
  
  return constrain(alcohol_ppm, 0, 100);
}

float calculateBenzene(float voltage) {
  if (voltage <= 0.1) return 0;
  
  float rs = ((3.3 * RL_VALUE) / voltage) - RL_VALUE;
  if (rs <= 0) return 0;
  
  float ratio = rs / (RL_VALUE * RO_CLEAN_AIR_FACTOR);
  float benzene_ppm = 37.22 * pow(ratio, -2.78);
  
  return constrain(benzene_ppm, 0, 50);
}

int calculateAQI(AirQualityData data) {
  // Enhanced AQI calculation berdasarkan standar Indonesia
  int aqi = 50; // Base AQI untuk udara bersih
  
  // CO2 contribution (normal outdoor: 400 ppm, indoor: 400-1000 ppm)
  if (data.co2_ppm > 2000) aqi += 80;
  else if (data.co2_ppm > 1500) aqi += 60;
  else if (data.co2_ppm > 1000) aqi += 40;
  else if (data.co2_ppm > 800) aqi += 20;
  
  // NH3 contribution (normal: < 25 ppm)
  if (data.nh3_ppm > 75) aqi += 100;
  else if (data.nh3_ppm > 50) aqi += 70;
  else if (data.nh3_ppm > 35) aqi += 50;
  else if (data.nh3_ppm > 25) aqi += 30;
  else if (data.nh3_ppm > 10) aqi += 15;
  
  // Alcohol contribution
  if (data.alcohol_ppm > 20) aqi += 60;
  else if (data.alcohol_ppm > 15) aqi += 40;
  else if (data.alcohol_ppm > 10) aqi += 25;
  else if (data.alcohol_ppm > 5) aqi += 15;
  
  // Benzene contribution (sangat berbahaya, batas rendah)
  if (data.benzene_ppm > 10) aqi += 150;
  else if (data.benzene_ppm > 5) aqi += 100;
  else if (data.benzene_ppm > 2) aqi += 60;
  else if (data.benzene_ppm > 1) aqi += 30;
  else if (data.benzene_ppm > 0.5) aqi += 15;
  
  return constrain(aqi, 0, 500); // AQI maksimal 500
}

String getAQIStatus(int aqi) {
  if (aqi <= 50) return "Baik";
  else if (aqi <= 100) return "Sedang";
  else if (aqi <= 150) return "Tidak Sehat untuk Kelompok Sensitif";
  else if (aqi <= 200) return "Tidak Sehat";
  else if (aqi <= 300) return "Sangat Tidak Sehat";
  else return "Berbahaya";
}

String getAQIColor(int aqi) {
  if (aqi <= 50) return "#00E400";      // Hijau
  else if (aqi <= 100) return "#FFFF00";   // Kuning
  else if (aqi <= 150) return "#FF7E00";   // Oranye
  else if (aqi <= 200) return "#FF0000";   // Merah
  else if (aqi <= 300) return "#8F3F97";   // Ungu
  else return "#7E0023";                   // Maroon
}

void displayData(AirQualityData data) {
  Serial.println("\n╔══════════════════════════════════╗");
  Serial.println("║     🌿 AIR QUALITY MONITOR RT    ║");
  Serial.println("╠══════════════════════════════════╣");
  Serial.println("║ 📍 Lokasi: " + LOCATION_NAME);
  Serial.println("║ 🕒 Waktu: " + data.timestamp);
  Serial.println("╠══════════════════════════════════╣");
  Serial.println("║ 📊 DATA SENSOR:");
  Serial.println("║   Raw Value: " + String(data.raw_sensor_value));
  Serial.println("║   CO2: " + String(data.co2_ppm, 1) + " ppm");
  Serial.println("║   NH3: " + String(data.nh3_ppm, 1) + " ppm");
  Serial.println("║   Alkohol: " + String(data.alcohol_ppm, 1) + " ppm");
  Serial.println("║   Benzena: " + String(data.benzene_ppm, 1) + " ppm");
  Serial.println("╠══════════════════════════════════╣");
  Serial.println("║ 🎯 AQI: " + String(data.air_quality_index));
  Serial.println("║ 📈 Status: " + data.status);
  Serial.println("╚══════════════════════════════════╝");
}

void sendToFirebase(AirQualityData data) {
  if (WiFi.status() == WL_CONNECTED) {
    String historyPath = "/air_quality/history/" + RT_ID + "/" + String(millis());

    DynamicJsonDocument doc(1024);
    doc["timestamp"] = data.timestamp;
    doc["location_id"] = data.location_id;
    doc["location_name"] = LOCATION_NAME;
    doc["latitude"] = LATITUDE;
    doc["longitude"] = LONGITUDE;
    doc["raw_sensor_value"] = data.raw_sensor_value;
    doc["co2_ppm"] = data.co2_ppm;
    doc["nh3_ppm"] = data.nh3_ppm;
    doc["alcohol_ppm"] = data.alcohol_ppm;
    doc["benzene_ppm"] = data.benzene_ppm;
    doc["aqi"] = data.air_quality_index;
    doc["status"] = data.status;
    doc["status_color"] = data.status_color;

    String jsonString;
    serializeJson(doc, jsonString);

    FirebaseJson jsonObj;
    jsonObj.setJsonData(jsonString);

    if (Firebase.setJSON(firebaseData, historyPath, jsonObj)) {
      Serial.println("✅ Data history berhasil dikirim ke Firebase");
    } else {
      Serial.println("❌ Error mengirim data history: " + firebaseData.errorReason());
    }
  }
}

void updateCurrentData(AirQualityData data) {
  if (WiFi.status() == WL_CONNECTED) {
    String currentPath = "/air_quality/current/" + RT_ID;

    DynamicJsonDocument doc(1024);
    doc["timestamp"] = data.timestamp;
    doc["location_id"] = data.location_id;
    doc["location_name"] = LOCATION_NAME;
    doc["latitude"] = LATITUDE;
    doc["longitude"] = LONGITUDE;
    doc["raw_sensor_value"] = data.raw_sensor_value;
    doc["co2_ppm"] = data.co2_ppm;
    doc["nh3_ppm"] = data.nh3_ppm;
    doc["alcohol_ppm"] = data.alcohol_ppm;
    doc["benzene_ppm"] = data.benzene_ppm;
    doc["aqi"] = data.air_quality_index;
    doc["status"] = data.status;
    doc["status_color"] = data.status_color;
    doc["device_status"] = "online";

    String jsonString;
    serializeJson(doc, jsonString);

    FirebaseJson jsonObj;
    jsonObj.setJsonData(jsonString);

    if (Firebase.setJSON(firebaseData, currentPath, jsonObj)) {
      Serial.println("✅ Data current berhasil diupdate");
    } else {
      Serial.println("❌ Error update data current: " + firebaseData.errorReason());
    }
  }
}

void checkAirQualityAlert(AirQualityData data) {
  // Kirim alert jika AQI > 150 (Tidak Sehat untuk Kelompok Sensitif)
  if (data.air_quality_index > 150 && (millis() - lastAlert) > ALERT_INTERVAL) {
    Serial.println("🚨 PERINGATAN: Kualitas udara tidak sehat!");
    
    // Kirim alert ke Telegram
    sendTelegramAlert(data);
    
    // Bunyikan buzzer
    alertBuzzer();
    
    lastAlert = millis();
  }
}

void sendTelegramAlert(AirQualityData data) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + TELEGRAM_BOT_TOKEN + "/sendMessage";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String alertMessage = "🚨 *PERINGATAN KUALITAS UDARA* 🚨\\n\\n";
    alertMessage += "📍 *Lokasi:* " + LOCATION_NAME + "\\n";
    alertMessage += "🕒 *Waktu:* " + data.timestamp + "\\n";
    alertMessage += "📊 *AQI:* " + String(data.air_quality_index) + "\\n";
    alertMessage += "📈 *Status:* " + data.status + "\\n\\n";
    
    alertMessage += "💨 *Detail Gas:*\\n";
    alertMessage += "• CO2: " + String(data.co2_ppm, 1) + " ppm\\n";
    alertMessage += "• NH3: " + String(data.nh3_ppm, 1) + " ppm\\n";
    alertMessage += "• Alkohol: " + String(data.alcohol_ppm, 1) + " ppm\\n";
    alertMessage += "• Benzena: " + String(data.benzene_ppm, 1) + " ppm\\n\\n";
    
    if (data.air_quality_index > 300) {
      alertMessage += "⚠️ *BAHAYA TINGGI!* Segera hindari area ini dan gunakan masker!";
    } else if (data.air_quality_index > 200) {
      alertMessage += "⚠️ *Kualitas udara sangat tidak sehat!* Hindari aktivitas luar ruangan.";
    } else {
      alertMessage += "⚠️ *Kualitas udara tidak sehat!* Batasi aktivitas luar ruangan dan gunakan masker.";
    }
    
    DynamicJsonDocument alertDoc(1024);
    alertDoc["chat_id"] = TELEGRAM_CHAT_ID;
    alertDoc["text"] = alertMessage;
    alertDoc["parse_mode"] = "Markdown";
    
    String alertJson;
    serializeJson(alertDoc, alertJson);
    
    int httpResponseCode = http.POST(alertJson);
    if (httpResponseCode == 200) {
      Serial.println("✅ Alert Telegram berhasil dikirim");
    } else {
      Serial.println("❌ Gagal mengirim alert Telegram. HTTP Code: " + String(httpResponseCode));
    }
    http.end();
  }
}

void alertBuzzer() {
  // Pattern buzzer untuk alert
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
}

void updateStatusLEDs(int aqi) {
  if (aqi <= 100) {
    // Hijau: Baik sampai Sedang
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, LOW);
  } else if (aqi <= 200) {
    // Kuning/Oranye: Tidak sehat untuk sensitif sampai tidak sehat
    // Blink hijau dan merah bergantian
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, HIGH);
  } else {
    // Merah: Sangat tidak sehat sampai berbahaya
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, HIGH);
  }
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not available";
  }
  
  char timeString[30];
  strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeString);
}