#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <esp_task_wdt.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <RTClib.h>
#include <vector>
#include <Preferences.h>

// Configuration des pins
#define DHTPIN 21
#define DHTTYPE DHT11
#define TRIGGER_PIN 23
#define ECHO_PIN 22
#define MOTOR_RELAY_PIN 26
#define LIGHT_PIN 25
#define VENTILATION_RELAY_PIN 32
#define ONE_WIRE_BUS 13
#define DOOR_SENSOR_PIN 19
#define SDA_PIN 16
#define SCL_PIN 17

const char* ssid = "ESP32_Control";
const char* password = "12345678";

// Structure pour les horaires du moteur
struct MotorSchedule {
  String startTime;
  String stopTime;
  bool active;
};

// Objets
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature waterTempSensor(&oneWire);
RTC_DS3231 rtc;
Preferences preferences;

// Variables des capteurs
float temperature = 0.0;
float humidity = 0.0;
float waterTemp = 0.0;
float distance = 0.0;
bool doorClosed = true;
String currentTime = "--:--:--";
String currentDate = "--/--/----";
bool rtcAvailable = false;

// États des relais
bool motorRunning = false;
bool lampRunning = false;
bool ventilationRunning = false;

// Variables pour le mode automatique de ventilation
bool autoVentMode = false;
float tempThresholdMax = 35.0;
float tempThresholdMin = 30.0;
unsigned long ventilationStartTime = 0;
unsigned long ventilationDuration = 0;
unsigned long tempExceededTime = 0;
bool waitingToStart = false;
unsigned long waitDuration = 300000;

// Variables pour le mode automatique du moteur
bool autoMotorMode = false;
std::vector<MotorSchedule> motorSchedules;
int currentActiveSchedule = -1;

// Prototypes
void handleRoot();
void handleAPI();
void handleControl();
void readSensors();
void checkAutoMotor();
String getHTML();
void saveSettings();
void loadSettings();
void saveSchedules();
void loadSchedules();

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("       SmartFarm ESP32 - Démarrage");
  Serial.println("========================================\n");

  // Charger les paramètres sauvegardés
  loadSettings();
  loadSchedules();

  // Initialisation I2C pour RTC
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);
  
  // Initialisation RTC
  rtcAvailable = rtc.begin();
  if (!rtcAvailable) {
    Serial.println("ERREUR: RTC non trouvé! Vérifiez le câblage:");
    Serial.println("  - SDA sur pin 16");
    Serial.println("  - SCL sur pin 17");
    Serial.println("  - VCC et GND connectés");
  } else {
    Serial.println("✓ RTC initialisé avec succès");
    DateTime now = rtc.now();
    Serial.printf("Heure actuelle du RTC: %02d:%02d:%02d %02d/%02d/%04d\n", 
                  now.hour(), now.minute(), now.second(),
                  now.day(), now.month(), now.year());
                  
    // IMPORTANT: Décommenter la ligne suivante pour régler l'heure du RTC (UNE SEULE FOIS)
    // Puis re-commenter et téléverser à nouveau
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    
    // Ou régler manuellement (année, mois, jour, heure, minute, seconde):
    // rtc.adjust(DateTime(2025, 11, 15, 14, 30, 0));
  }

  // Initialisation des capteurs
  dht.begin();
  waterTempSensor.begin();
  
  // Configuration des pins
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MOTOR_RELAY_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(VENTILATION_RELAY_PIN, OUTPUT);
  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);

  // Restaurer les états des relais depuis la mémoire
  digitalWrite(MOTOR_RELAY_PIN, motorRunning ? LOW : HIGH);
  digitalWrite(LIGHT_PIN, lampRunning ? LOW : HIGH);
  digitalWrite(VENTILATION_RELAY_PIN, ventilationRunning ? LOW : HIGH);
  
  Serial.println("\n✓ États restaurés:");
  Serial.printf("  - Moteur: %s\n", motorRunning ? "ON" : "OFF");
  Serial.printf("  - Lampe: %s\n", lampRunning ? "ON" : "OFF");
  Serial.printf("  - Ventilation: %s\n", ventilationRunning ? "ON" : "OFF");
  Serial.printf("  - Mode Auto Moteur: %s\n", autoMotorMode ? "ON" : "OFF");
  Serial.printf("  - Mode Auto Ventilation: %s\n", autoVentMode ? "ON" : "OFF");
  Serial.printf("  - Nombre d'horaires: %d\n", motorSchedules.size());
  
  // Configuration du Watchdog
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(NULL);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.println("\n✓ Point d'accès WiFi démarré");
  Serial.print("  SSID: ");
  Serial.println(ssid);
  Serial.print("  Adresse IP: ");
  Serial.println(WiFi.softAPIP());

  // Configuration des routes du serveur web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/sensors", HTTP_GET, handleAPI);
  server.on("/api/control", HTTP_POST, handleControl);
  
  server.begin();
  Serial.println("✓ Serveur web démarré!");
  Serial.println("\n========================================\n");
  
  esp_task_wdt_reset();
}

void loop() {
  static unsigned long lastSensorRead = 0;
  static unsigned long lastWatchdogReset = 0;
  static unsigned long lastAutoMotorCheck = 0;
  unsigned long currentMillis = millis();
  
  // Reset watchdog régulièrement
  if (currentMillis - lastWatchdogReset >= 25000) {
    esp_task_wdt_reset();
    lastWatchdogReset = currentMillis;
  }
  
  // Lecture des capteurs toutes les 2 secondes
  if (currentMillis - lastSensorRead >= 2000) {
    lastSensorRead = currentMillis;
    readSensors();
  }
  
  // Vérification du mode auto moteur toutes les 5 secondes
  if (currentMillis - lastAutoMotorCheck >= 5000) {
    lastAutoMotorCheck = currentMillis;
    if (autoMotorMode) {
      checkAutoMotor();
    }
  }
  
  // Gestion du serveur web
  server.handleClient();
  
  // Vérifier l'arrêt automatique de la ventilation
  if (ventilationRunning && ventilationDuration > 0 && 
      currentMillis - ventilationStartTime >= ventilationDuration) {
    ventilationRunning = false;
    digitalWrite(VENTILATION_RELAY_PIN, LOW);
    ventilationDuration = 0;
    Serial.println("Ventilation arrêtée automatiquement");
  }
  
  // Mode automatique de ventilation basé sur la température
  if (autoVentMode) {
    if (temperature > tempThresholdMax) {
      if (!waitingToStart && !ventilationRunning) {
        waitingToStart = true;
        tempExceededTime = currentMillis;
        Serial.printf("Température > %.1f°C, attente de %lu min\n", tempThresholdMax, waitDuration/60000);
      }
      
      if (waitingToStart && !ventilationRunning && 
          currentMillis - tempExceededTime >= waitDuration) {
        ventilationRunning = true;
        digitalWrite(VENTILATION_RELAY_PIN, LOW);
        waitingToStart = false;
        Serial.printf("Ventilation AUTO ON (Temp: %.1f°C)\n", temperature);
      }
    } else {
      if (waitingToStart && temperature <= tempThresholdMax) {
        waitingToStart = false;
        Serial.println("Attente annulée");
      }
    }
    
    if (ventilationRunning && temperature <= tempThresholdMin) {
      ventilationRunning = false;
      digitalWrite(VENTILATION_RELAY_PIN, HIGH);
      Serial.printf("Ventilation AUTO OFF (Temp: %.1f°C)\n", temperature);
    }
  } else {
    waitingToStart = false;
  }
  
  delay(10);
}

void readSensors() {
  // Lecture DHT11
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperature = t;
  }
  
  // Lecture DS18B20
  waterTempSensor.requestTemperatures();
  float wt = waterTempSensor.getTempCByIndex(0);
  if (wt != DEVICE_DISCONNECTED_C && wt > -127.0 && wt < 85.0) {
    waterTemp = wt;
  }
  
  // Lecture capteur ultrason
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration > 0) {
    distance = duration * 0.034 / 2;
  }
  
  // Lecture capteur de porte
  doorClosed = !digitalRead(DOOR_SENSOR_PIN);
  
  // Lecture RTC
  if (rtcAvailable) {
    DateTime now = rtc.now();
    char timeBuffer[9];
    char dateBuffer[11];
    sprintf(timeBuffer, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    sprintf(dateBuffer, "%02d/%02d/%04d", now.day(), now.month(), now.year());
    currentTime = String(timeBuffer);
    currentDate = String(dateBuffer);
  }
}

void checkAutoMotor() {
  if (!autoMotorMode) {
    // Si le mode auto est désactivé, ne rien faire
    if (currentActiveSchedule != -1) {
      currentActiveSchedule = -1;
    }
    return;
  }
  
  if (motorSchedules.empty()) {
    Serial.println("Mode Auto Moteur ON mais aucun horaire programmé");
    return;
  }
  
  if (!rtcAvailable) {
    Serial.println("ERREUR: RTC non disponible - impossible d'utiliser le mode automatique");
    Serial.println("Veuillez vérifier le câblage du module RTC DS3231");
    return;
  }
  
  DateTime now = rtc.now();
  int currentMinutes = now.hour() * 60 + now.minute();
  
  Serial.printf("=== Vérification horaires moteur ===\n");
  Serial.printf("Heure actuelle: %02d:%02d (%d minutes depuis minuit)\n", 
                now.hour(), now.minute(), currentMinutes);
  Serial.printf("Nombre d'horaires: %d\n", motorSchedules.size());
  
  bool shouldBeOn = false;
  int activeScheduleIndex = -1;
  
  // Vérifier tous les horaires programmés
  for (int i = 0; i < motorSchedules.size(); i++) {
    Serial.printf("\nHoraire #%d: ", i + 1);
    
    if (!motorSchedules[i].active) {
      Serial.println("DESACTIVE - ignoré");
      continue;
    }
    
    int startHour = motorSchedules[i].startTime.substring(0, 2).toInt();
    int startMin = motorSchedules[i].startTime.substring(3, 5).toInt();
    int stopHour = motorSchedules[i].stopTime.substring(0, 2).toInt();
    int stopMin = motorSchedules[i].stopTime.substring(3, 5).toInt();
    
    int startMinutes = startHour * 60 + startMin;
    int stopMinutes = stopHour * 60 + stopMin;
    
    Serial.printf("ACTIF - %s (%d min) -> %s (%d min)\n", 
                  motorSchedules[i].startTime.c_str(), startMinutes,
                  motorSchedules[i].stopTime.c_str(), stopMinutes);
    
    // Vérifier si on est dans la plage horaire
    if (currentMinutes >= startMinutes && currentMinutes < stopMinutes) {
      shouldBeOn = true;
      activeScheduleIndex = i;
      Serial.printf(">>> DANS LA PLAGE HORAIRE! Le moteur devrait être ON <<<\n");
      break;
    } else {
      Serial.printf("Hors plage (actuel: %d, besoin: %d-%d)\n", 
                    currentMinutes, startMinutes, stopMinutes);
    }
  }
  
  // Activer ou désactiver le moteur selon les horaires
  if (shouldBeOn && !motorRunning) {
    motorRunning = true;
    digitalWrite(MOTOR_RELAY_PIN, LOW);
    currentActiveSchedule = activeScheduleIndex;
    Serial.printf("\n*** MOTEUR AUTO ON (Horaire #%d: %s - %s) ***\n\n", 
                  activeScheduleIndex + 1,
                  motorSchedules[activeScheduleIndex].startTime.c_str(),
                  motorSchedules[activeScheduleIndex].stopTime.c_str());
  } else if (!shouldBeOn && motorRunning && currentActiveSchedule != -1) {
    motorRunning = false;
    digitalWrite(MOTOR_RELAY_PIN, HIGH);
    Serial.printf("\n*** MOTEUR AUTO OFF (Horaire #%d terminé) ***\n\n", currentActiveSchedule + 1);
    currentActiveSchedule = -1;
  } else if (!shouldBeOn) {
    Serial.println("\n=> Moteur reste OFF (hors de toute plage horaire)\n");
  } else {
    Serial.println("\n=> Moteur déjà dans le bon état\n");
  }
}

void handleRoot() {
  server.send(200, "text/html", getHTML());
}

void handleAPI() {
  String json = "{";
  json += "\"temperature\":" + String(temperature, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"waterTemp\":" + String(waterTemp, 1) + ",";
  json += "\"distance\":" + String(distance, 1) + ",";
  json += "\"doorClosed\":" + String(doorClosed ? "true" : "false") + ",";
  json += "\"currentTime\":\"" + currentTime + "\",";
  json += "\"currentDate\":\"" + currentDate + "\",";
  json += "\"motorRunning\":" + String(motorRunning ? "true" : "false") + ",";
  json += "\"lampRunning\":" + String(lampRunning ? "true" : "false") + ",";
  json += "\"ventilationRunning\":" + String(ventilationRunning ? "true" : "false") + ",";
  json += "\"autoVentMode\":" + String(autoVentMode ? "true" : "false") + ",";
  json += "\"autoMotorMode\":" + String(autoMotorMode ? "true" : "false") + ",";
  
  // Ajouter les horaires du moteur
  json += "\"motorSchedules\":[";
  for (int i = 0; i < motorSchedules.size(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"id\":" + String(i) + ",";
    json += "\"startTime\":\"" + motorSchedules[i].startTime + "\",";
    json += "\"stopTime\":\"" + motorSchedules[i].stopTime + "\",";
    json += "\"active\":" + String(motorSchedules[i].active ? "true" : "false");
    json += "}";
  }
  json += "],";
  
  json += "\"tempThresholdMax\":" + String(tempThresholdMax, 1) + ",";
  json += "\"tempThresholdMin\":" + String(tempThresholdMin, 1) + ",";
  json += "\"waitDuration\":" + String(waitDuration / 60000);
  json += "}";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleControl() {
  if (server.hasArg("device") && server.hasArg("action")) {
    String device = server.arg("device");
    String action = server.arg("action");
    
    if (device == "motor") {
      motorRunning = (action == "on");
      digitalWrite(MOTOR_RELAY_PIN, motorRunning ? LOW : HIGH);
      Serial.printf("Moteur: %s\n", motorRunning ? "ON" : "OFF");
      saveSettings();
    }
    else if (device == "motor_auto") {
      autoMotorMode = (action == "on");
      Serial.printf("Mode Auto Moteur: %s\n", autoMotorMode ? "ON" : "OFF");
      saveSettings();
      if (autoMotorMode) {
        checkAutoMotor(); // Vérification immédiate
      }
    }
    else if (device == "motor_schedule_add") {
      if (server.hasArg("startTime") && server.hasArg("stopTime")) {
        MotorSchedule newSchedule;
        newSchedule.startTime = server.arg("startTime");
        newSchedule.stopTime = server.arg("stopTime");
        newSchedule.active = true;
        motorSchedules.push_back(newSchedule);
        Serial.printf("Horaire ajouté: %s - %s\n", 
                     newSchedule.startTime.c_str(), 
                     newSchedule.stopTime.c_str());
        saveSchedules();
        if (autoMotorMode) {
          checkAutoMotor(); // Vérification immédiate après ajout
        }
      }
    }
    else if (device == "motor_schedule_delete") {
      if (server.hasArg("id")) {
        int id = server.arg("id").toInt();
        if (id >= 0 && id < motorSchedules.size()) {
          Serial.printf("Horaire supprimé: %s - %s\n", 
                       motorSchedules[id].startTime.c_str(), 
                       motorSchedules[id].stopTime.c_str());
          motorSchedules.erase(motorSchedules.begin() + id);
          saveSchedules();
        }
      }
    }
    else if (device == "motor_schedule_toggle") {
      if (server.hasArg("id")) {
        int id = server.arg("id").toInt();
        if (id >= 0 && id < motorSchedules.size()) {
          motorSchedules[id].active = !motorSchedules[id].active;
          Serial.printf("Horaire #%d %s\n", id + 1, 
                       motorSchedules[id].active ? "activé" : "désactivé");
          saveSchedules();
        }
      }
    }
    else if (device == "lamp") {
      lampRunning = (action == "on");
      digitalWrite(LIGHT_PIN, lampRunning ? LOW : HIGH);
      Serial.printf("Lampe: %s\n", lampRunning ? "ON" : "OFF");
      saveSettings();
    }
    else if (device == "ventilation") {
      if (action == "on") {
        ventilationRunning = true;
        digitalWrite(VENTILATION_RELAY_PIN, LOW);
        
        if (server.hasArg("duration")) {
          int duration = server.arg("duration").toInt();
          ventilationStartTime = millis();
          ventilationDuration = duration * 1000;
        } else {
          ventilationDuration = 0;
        }
      } else {
        ventilationRunning = false;
        digitalWrite(VENTILATION_RELAY_PIN, HIGH);
        ventilationDuration = 0;
      }
      Serial.printf("Ventilation: %s\n", ventilationRunning ? "ON" : "OFF");
      saveSettings();
    }
    else if (device == "ventilation_auto") {
      autoVentMode = (action == "on");
      if (server.hasArg("thresholdMax")) {
        tempThresholdMax = server.arg("thresholdMax").toFloat();
      }
      if (server.hasArg("thresholdMin")) {
        tempThresholdMin = server.arg("thresholdMin").toFloat();
      }
      if (server.hasArg("waitDuration")) {
        waitDuration = server.arg("waitDuration").toInt() * 60000;
      }
      waitingToStart = false;
      tempExceededTime = 0;
      Serial.printf("Mode Auto Ventilation: %s (Max: %.1f°C, Min: %.1f°C)\n", 
                   autoVentMode ? "ON" : "OFF", tempThresholdMax, tempThresholdMin);
      saveSettings();
    }
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

// ====== FONCTIONS DE SAUVEGARDE EN MÉMOIRE ======

void saveSettings() {
  preferences.begin("smartfarm", false);
  
  preferences.putBool("motorRun", motorRunning);
  preferences.putBool("lampRun", lampRunning);
  preferences.putBool("ventRun", ventilationRunning);
  preferences.putBool("autoMotor", autoMotorMode);
  preferences.putBool("autoVent", autoVentMode);
  preferences.putFloat("tempMax", tempThresholdMax);
  preferences.putFloat("tempMin", tempThresholdMin);
  preferences.putULong("waitDur", waitDuration);
  
  preferences.end();
  Serial.println("💾 Paramètres sauvegardés");
}

void loadSettings() {
  preferences.begin("smartfarm", true); // mode lecture seule
  
  motorRunning = preferences.getBool("motorRun", false);
  lampRunning = preferences.getBool("lampRun", false);
  ventilationRunning = preferences.getBool("ventRun", false);
  autoMotorMode = preferences.getBool("autoMotor", false);
  autoVentMode = preferences.getBool("autoVent", false);
  tempThresholdMax = preferences.getFloat("tempMax", 35.0);
  tempThresholdMin = preferences.getFloat("tempMin", 30.0);
  waitDuration = preferences.getULong("waitDur", 300000);
  
  preferences.end();
  Serial.println("📂 Paramètres chargés depuis la mémoire");
}

void saveSchedules() {
  preferences.begin("smartfarm", false);
  
  // Sauvegarder le nombre d'horaires
  preferences.putInt("schedCount", motorSchedules.size());
  
  // Sauvegarder chaque horaire
  for (int i = 0; i < motorSchedules.size() && i < 10; i++) { // Max 10 horaires
    String startKey = "start" + String(i);
    String stopKey = "stop" + String(i);
    String activeKey = "active" + String(i);
    
    preferences.putString(startKey.c_str(), motorSchedules[i].startTime);
    preferences.putString(stopKey.c_str(), motorSchedules[i].stopTime);
    preferences.putBool(activeKey.c_str(), motorSchedules[i].active);
  }
  
  preferences.end();
  Serial.printf("💾 %d horaire(s) sauvegardé(s)\n", motorSchedules.size());
}

void loadSchedules() {
  preferences.begin("smartfarm", true);
  
  int count = preferences.getInt("schedCount", 0);
  motorSchedules.clear();
  
  for (int i = 0; i < count && i < 10; i++) {
    String startKey = "start" + String(i);
    String stopKey = "stop" + String(i);
    String activeKey = "active" + String(i);
    
    MotorSchedule schedule;
    schedule.startTime = preferences.getString(startKey.c_str(), "08:00");
    schedule.stopTime = preferences.getString(stopKey.c_str(), "09:00");
    schedule.active = preferences.getBool(activeKey.c_str(), true);
    
    motorSchedules.push_back(schedule);
  }
  
  preferences.end();
  Serial.printf("📂 %d horaire(s) chargé(s) depuis la mémoire\n", motorSchedules.size());
}

String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SmartFarm Control</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
        }
        .header {
            text-align: center;
            color: white;
            margin-bottom: 30px;
        }
        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }
        .datetime-display {
            background: rgba(255,255,255,0.2);
            padding: 10px;
            border-radius: 10px;
            margin-top: 10px;
            font-size: 1.2em;
        }
        .card {
            background: white;
            border-radius: 15px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        .card h2 {
            color: #667eea;
            margin-bottom: 15px;
            font-size: 1.5em;
            border-bottom: 2px solid #667eea;
            padding-bottom: 10px;
        }
        .sensor-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }
        .sensor-item {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 15px;
            border-radius: 10px;
            color: white;
            text-align: center;
        }
        .sensor-label {
            font-size: 0.9em;
            opacity: 0.9;
            margin-bottom: 5px;
        }
        .sensor-value {
            font-size: 1.8em;
            font-weight: bold;
        }
        .control-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px;
            margin-bottom: 10px;
            background: #f8f9fa;
            border-radius: 10px;
            transition: all 0.3s;
        }
        .control-item:hover {
            background: #e9ecef;
            transform: translateX(5px);
        }
        .control-label {
            font-weight: 600;
            color: #333;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .status-indicator {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            display: inline-block;
        }
        .status-on {
            background: #28a745;
            box-shadow: 0 0 10px #28a745;
        }
        .status-off {
            background: #dc3545;
        }
        .btn-group {
            display: flex;
            gap: 10px;
        }
        .btn {
            padding: 10px 20px;
            border: none;
            border-radius: 8px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
            font-size: 14px;
        }
        .btn-on {
            background: #28a745;
            color: white;
        }
        .btn-on:hover {
            background: #218838;
            transform: scale(1.05);
        }
        .btn-off {
            background: #dc3545;
            color: white;
        }
        .btn-off:hover {
            background: #c82333;
            transform: scale(1.05);
        }
        .btn-delete {
            background: #ff6b6b;
            color: white;
            padding: 5px 10px;
            font-size: 12px;
        }
        .btn-delete:hover {
            background: #ff5252;
        }
        .auto-control {
            background: #fff3cd;
            padding: 15px;
            border-radius: 10px;
            margin-top: 10px;
        }
        .threshold-control {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-top: 10px;
            flex-wrap: wrap;
        }
        .threshold-control input {
            padding: 8px;
            border: 2px solid #667eea;
            border-radius: 5px;
            width: 80px;
            font-size: 16px;
        }
        .time-control {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-top: 10px;
            flex-wrap: wrap;
        }
        .time-control input {
            padding: 8px;
            border: 2px solid #667eea;
            border-radius: 5px;
            font-size: 16px;
        }
        .schedule-list {
            margin-top: 15px;
        }
        .schedule-item {
            background: white;
            padding: 10px;
            border-radius: 8px;
            margin-bottom: 8px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border: 2px solid #667eea;
        }
        .schedule-item.inactive {
            opacity: 0.5;
            border-color: #ccc;
        }
        .schedule-info {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .schedule-actions {
            display: flex;
            gap: 5px;
        }
        .btn-toggle {
            background: #ffc107;
            color: #333;
            padding: 5px 10px;
            font-size: 12px;
        }
        .btn-toggle:hover {
            background: #ffb300;
        }
        .add-schedule-form {
            background: white;
            padding: 15px;
            border-radius: 8px;
            margin-top: 10px;
        }
        .refresh-btn {
            background: white;
            color: #667eea;
            border: 2px solid white;
            padding: 10px 20px;
            border-radius: 8px;
            font-weight: 600;
            cursor: pointer;
            margin-top: 20px;
            width: 100%;
        }
        .refresh-btn:hover {
            background: #f8f9fa;
        }
        @media (max-width: 600px) {
            .sensor-grid {
                grid-template-columns: repeat(2, 1fr);
            }
            .control-item {
                flex-direction: column;
                gap: 10px;
            }
            .btn-group {
                width: 100%;
            }
            .btn {
                flex: 1;
            }
            .schedule-item {
                flex-direction: column;
                gap: 10px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🌾 SmartFarm</h1>
            <p>Contrôle de la ferme intelligente</p>
            <div class="datetime-display">
                <div>⏰ <span id="currentTime">--:--:--</span></div>
                <div>📅 <span id="currentDate">--/--/----</span></div>
            </div>
        </div>

        <div class="card">
            <h2>📊 Capteurs</h2>
            <div class="sensor-grid">
                <div class="sensor-item">
                    <div class="sensor-label">🌡️ Température</div>
                    <div class="sensor-value" id="temp">--</div>
                </div>
                <div class="sensor-item">
                    <div class="sensor-label">💧 Humidité</div>
                    <div class="sensor-value" id="humidity">--</div>
                </div>
                <div class="sensor-item">
                    <div class="sensor-label">🌊 Temp. Eau</div>
                    <div class="sensor-value" id="waterTemp">--</div>
                </div>
                <div class="sensor-item">
                    <div class="sensor-label">📏 Distance</div>
                    <div class="sensor-value" id="distance">--</div>
                </div>
                <div class="sensor-item">
                    <div class="sensor-label">🚪 Porte</div>
                    <div class="sensor-value" id="door">--</div>
                </div>
            </div>
        </div>

        <div class="card">
            <h2>🎛️ Contrôles</h2>
            
            <div class="control-item">
                <div class="control-label">
                    <span class="status-indicator" id="motor-status"></span>
                    <span>⚙️ Moteur</span>
                </div>
                <div class="btn-group">
                    <button class="btn btn-on" onclick="control('motor', 'on')">ON</button>
                    <button class="btn btn-off" onclick="control('motor', 'off')">OFF</button>
                </div>
            </div>

            <div class="auto-control">
                <div class="control-item" style="background: white;">
                    <div class="control-label">
                        <span class="status-indicator" id="motor-auto-status"></span>
                        <span>🤖 Mode Auto Moteur</span>
                    </div>
                    <div class="btn-group">
                        <button class="btn btn-on" onclick="controlMotorAuto('on')">ON</button>
                        <button class="btn btn-off" onclick="controlMotorAuto('off')">OFF</button>
                    </div>
                </div>
                
                <div class="add-schedule-form">
                    <h3 style="margin-bottom: 10px; color: #667eea;">➕ Ajouter un horaire</h3>
                    <div class="time-control">
                        <label>Démarrage:</label>
                        <input type="time" id="newStartTime" value="08:00">
                        <label>Arrêt:</label>
                        <input type="time" id="newStopTime" value="09:00">
                    </div>
                    <div class="time-control">
                        <button class="btn btn-on" onclick="addSchedule()" style="width: 100%;">Ajouter l'horaire</button>
                    </div>
                </div>

                <div class="schedule-list" id="scheduleList">
                    <h3 style="margin-bottom: 10px; color: #667eea;">📋 Horaires programmés</h3>
                    <div id="schedules"></div>
                </div>

                <div style="font-size: 0.9em; color: #856404; margin-top: 10px;">
                    ⏱️ Le moteur démarrera et s'arrêtera automatiquement selon les horaires programmés
                </div>
            </div>

            <div class="control-item">
                <div class="control-label">
                    <span class="status-indicator" id="lamp-status"></span>
                    <span>💡 Lampe</span>
                </div>
                <div class="btn-group">
                    <button class="btn btn-on" onclick="control('lamp', 'on')">ON</button>
                    <button class="btn btn-off" onclick="control('lamp', 'off')">OFF</button>
                </div>
            </div>

            <div class="control-item">
                <div class="control-label">
                    <span class="status-indicator" id="vent-status"></span>
                    <span>🌀 Ventilation</span>
                </div>
                <div class="btn-group">
                    <button class="btn btn-on" onclick="control('ventilation', 'on')">ON</button>
                    <button class="btn btn-off" onclick="control('ventilation', 'off')">OFF</button>
                </div>
            </div>

            <div class="auto-control">
                <div class="control-item" style="background: white;">
                    <div class="control-label">
                        <span class="status-indicator" id="auto-status"></span>
                        <span>🤖 Mode Auto Ventilation</span>
                    </div>
                    <div class="btn-group">
                        <button class="btn btn-on" onclick="controlAuto('on')">ON</button>
                        <button class="btn btn-off" onclick="controlAuto('off')">OFF</button>
                    </div>
                </div>
                <div class="threshold-control">
                    <label>Seuil MAX (démarrage):</label>
                    <input type="number" id="thresholdMax" value="35" step="0.5" min="20" max="50">
                    <span>°C</span>
                </div>
                <div class="threshold-control">
                    <label>Seuil MIN (arrêt):</label>
                    <input type="number" id="thresholdMin" value="30" step="0.5" min="15" max="45">
                    <span>°C</span>
                </div>
                <div class="threshold-control">
                    <label>Durée d'attente:</label>
                    <input type="number" id="waitDuration" value="5" step="1" min="1" max="60">
                    <span>minutes</span>
                </div>
                <div class="threshold-control">
                    <button class="btn btn-on" onclick="updateThreshold()" style="width: 100%;">Mettre à jour les paramètres</button>
                </div>
                <div style="font-size: 0.9em; color: #856404; margin-top: 10px;">
                    ⏱️ La ventilation démarrera après la durée d'attente si la température dépasse le seuil MAX
                </div>
            </div>
        </div>

        <button class="refresh-btn" onclick="updateData()">🔄 Actualiser</button>
    </div>

    <script>
        let isUpdating = false;
        let userThresholdMax = null;
        let userThresholdMin = null;
        let userWaitDuration = null;
        
        function updateData() {
            if (isUpdating) return;
            
            fetch('/api/sensors')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('temp').textContent = data.temperature.toFixed(1) + '°C';
                    document.getElementById('humidity').textContent = data.humidity.toFixed(1) + '%';
                    document.getElementById('waterTemp').textContent = data.waterTemp.toFixed(1) + '°C';
                    document.getElementById('distance').textContent = data.distance.toFixed(1) + 'cm';
                    document.getElementById('door').textContent = data.doorClosed ? 'Fermée' : 'Ouverte';
                    document.getElementById('currentTime').textContent = data.currentTime;
                    document.getElementById('currentDate').textContent = data.currentDate;
                    
                    updateStatus('motor-status', data.motorRunning);
                    updateStatus('lamp-status', data.lampRunning);
                    updateStatus('vent-status', data.ventilationRunning);
                    updateStatus('auto-status', data.autoVentMode);
                    updateStatus('motor-auto-status', data.autoMotorMode);
                    
                    if (userThresholdMax === null) {
                        document.getElementById('thresholdMax').value = data.tempThresholdMax;
                    }
                    if (userThresholdMin === null) {
                        document.getElementById('thresholdMin').value = data.tempThresholdMin;
                    }
                    if (userWaitDuration === null) {
                        document.getElementById('waitDuration').value = data.waitDuration;
                    }
                    
                    displaySchedules(data.motorSchedules);
                })
                .catch(error => console.error('Erreur:', error));
        }
        
        function displaySchedules(schedules) {
            const schedulesDiv = document.getElementById('schedules');
            if (schedules.length === 0) {
                schedulesDiv.innerHTML = '<div style="color: #856404; padding: 10px;">Aucun horaire programmé</div>';
                return;
            }
            
            let html = '';
            schedules.forEach((schedule, index) => {
                const activeClass = schedule.active ? '' : 'inactive';
                html += `
                    <div class="schedule-item ${activeClass}">
                        <div class="schedule-info">
                            <span style="font-weight: bold;">Horaire #${index + 1}:</span>
                            <span>🕐 ${schedule.startTime} → ${schedule.stopTime}</span>
                            <span style="color: ${schedule.active ? '#28a745' : '#dc3545'};">
                                ${schedule.active ? '✓ Actif' : '✗ Inactif'}
                            </span>
                        </div>
                        <div class="schedule-actions">
                            <button class="btn btn-toggle" onclick="toggleSchedule(${schedule.id})">
                                ${schedule.active ? 'Désactiver' : 'Activer'}
                            </button>
                            <button class="btn btn-delete" onclick="deleteSchedule(${schedule.id})">
                                🗑️ Supprimer
                            </button>
                        </div>
                    </div>
                `;
            });
            schedulesDiv.innerHTML = html;
        }
        
        document.addEventListener('DOMContentLoaded', function() {
            document.getElementById('thresholdMax').addEventListener('input', function() {
                userThresholdMax = this.value;
            });
            document.getElementById('thresholdMin').addEventListener('input', function() {
                userThresholdMin = this.value;
            });
            document.getElementById('waitDuration').addEventListener('input', function() {
                userWaitDuration = this.value;
            });
        });

        function updateStatus(id, isOn) {
            const element = document.getElementById(id);
            element.className = 'status-indicator ' + (isOn ? 'status-on' : 'status-off');
        }

        function control(device, action) {
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `device=${device}&action=${action}`
            })
            .then(() => setTimeout(updateData, 500))
            .catch(error => console.error('Erreur:', error));
        }

        function controlMotorAuto(action) {
            isUpdating = true;
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `device=motor_auto&action=${action}`
            })
            .then(() => {
                setTimeout(() => {
                    updateData();
                    isUpdating = false;
                }, 500);
            })
            .catch(error => {
                console.error('Erreur:', error);
                isUpdating = false;
            });
        }

        function addSchedule() {
            const startTime = document.getElementById('newStartTime').value;
            const stopTime = document.getElementById('newStopTime').value;
            
            if (!startTime || !stopTime) {
                alert('Veuillez entrer les heures de démarrage et d\'arrêt!');
                return;
            }
            
            if (startTime >= stopTime) {
                alert('Erreur: L\'heure de démarrage doit être avant l\'heure d\'arrêt!');
                return;
            }
            
            isUpdating = true;
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `device=motor_schedule_add&action=add&startTime=${startTime}&stopTime=${stopTime}`
            })
            .then(() => {
                alert('Horaire ajouté avec succès!');
                document.getElementById('newStartTime').value = '08:00';
                document.getElementById('newStopTime').value = '09:00';
                setTimeout(() => {
                    updateData();
                    isUpdating = false;
                }, 500);
            })
            .catch(error => {
                console.error('Erreur:', error);
                isUpdating = false;
            });
        }

        function deleteSchedule(id) {
            if (!confirm('Voulez-vous vraiment supprimer cet horaire?')) {
                return;
            }
            
            isUpdating = true;
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `device=motor_schedule_delete&action=delete&id=${id}`
            })
            .then(() => {
                alert('Horaire supprimé!');
                setTimeout(() => {
                    updateData();
                    isUpdating = false;
                }, 500);
            })
            .catch(error => {
                console.error('Erreur:', error);
                isUpdating = false;
            });
        }

        function toggleSchedule(id) {
            isUpdating = true;
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `device=motor_schedule_toggle&action=toggle&id=${id}`
            })
            .then(() => {
                setTimeout(() => {
                    updateData();
                    isUpdating = false;
                }, 500);
            })
            .catch(error => {
                console.error('Erreur:', error);
                isUpdating = false;
            });
        }

        function controlAuto(action) {
            const thresholdMax = document.getElementById('thresholdMax').value;
            const thresholdMin = document.getElementById('thresholdMin').value;
            const waitDuration = document.getElementById('waitDuration').value;
            
            if (parseFloat(thresholdMin) >= parseFloat(thresholdMax)) {
                alert('Erreur: Le seuil MIN doit être inférieur au seuil MAX!');
                return;
            }
            
            isUpdating = true;
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `device=ventilation_auto&action=${action}&thresholdMax=${thresholdMax}&thresholdMin=${thresholdMin}&waitDuration=${waitDuration}`
            })
            .then(() => {
                userThresholdMax = null;
                userThresholdMin = null;
                userWaitDuration = null;
                setTimeout(() => {
                    updateData();
                    isUpdating = false;
                }, 500);
            })
            .catch(error => {
                console.error('Erreur:', error);
                isUpdating = false;
            });
        }

        function updateThreshold() {
            const thresholdMax = document.getElementById('thresholdMax').value;
            const thresholdMin = document.getElementById('thresholdMin').value;
            const waitDuration = document.getElementById('waitDuration').value;
            
            if (parseFloat(thresholdMin) >= parseFloat(thresholdMax)) {
                alert('Erreur: Le seuil MIN doit être inférieur au seuil MAX!');
                return;
            }
            
            isUpdating = true;
            const autoOn = document.getElementById('auto-status').classList.contains('status-on');
            fetch('/api/control', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `device=ventilation_auto&action=${autoOn ? 'on' : 'off'}&thresholdMax=${thresholdMax}&thresholdMin=${thresholdMin}&waitDuration=${waitDuration}`
            })
            .then(() => {
                alert('Paramètres mis à jour:\nMAX: ' + thresholdMax + '°C\nMIN: ' + thresholdMin + '°C\nAttente: ' + waitDuration + ' min');
                userThresholdMax = null;
                userThresholdMin = null;
                userWaitDuration = null;
                setTimeout(() => {
                    updateData();
                    isUpdating = false;
                }, 500);
            })
            .catch(error => {
                console.error('Erreur:', error);
                isUpdating = false;
            });
        }

        setInterval(updateData, 3000);
        updateData();
    </script>
</body>
</html>
)rawliteral";
  
  return html;
}