# 🌾 Smart Farm - Système de Contrôle Intelligent

## 📋 Description du Projet

**Smart Farm** est un système de gestion automatisée d'une ferme intelligente basé sur ESP32. Il permet de surveiller et contrôler divers paramètres environnementaux en temps réel via une interface web accessible depuis n'importe quel appareil connecté au réseau WiFi.

### Objectifs Principaux

- 🌡️ **Surveillance en temps réel** : Monitoring continu de la température, humidité, température de l'eau et niveau du réservoir
- 🤖 **Automatisation intelligente** : Gestion automatique de la ventilation et du moteur d'irrigation selon des critères prédéfinis
- 📱 **Contrôle à distance** : Interface web responsive accessible depuis smartphone, tablette ou ordinateur
- ⏰ **Programmation horaire** : Planification des cycles d'irrigation avec support de multiples plages horaires
- 💾 **Persistance des données** : Sauvegarde automatique des paramètres et horaires en mémoire non-volatile

---

## 🔧 Matériel Requis

### Microcontrôleur
- **ESP32** (NodeMCU ou équivalent)

### Capteurs
- **DHT11** - Capteur de température et humidité ambiante
- **DS18B20** - Sonde de température étanche (pour l'eau)
- **HC-SR04** - Capteur ultrasonique de distance
- **Capteur magnétique de porte** (Reed switch)
- **DS3231** - Module RTC (Real Time Clock) avec batterie

### Actionneurs
- **3x Relais 5V** pour contrôler :
  - Moteur d'irrigation
  - Lampe d'éclairage
  - Ventilateur

### Composants Additionnels
- Résistances de pull-up 4.7kΩ (pour DS18B20)
- Fils de connexion
- Alimentation adaptée pour ESP32 et relais

---

## 📌 Configuration des Pins

| Composant | Pin ESP32 | Description |
|-----------|-----------|-------------|
| **DHT11** | GPIO 21 | Capteur température/humidité |
| **HC-SR04 TRIGGER** | GPIO 23 | Signal de déclenchement ultrason |
| **HC-SR04 ECHO** | GPIO 22 | Signal de réception ultrason |
| **Relais Moteur** | GPIO 26 | Contrôle du moteur d'irrigation |
| **Relais Lampe** | GPIO 25 | Contrôle de l'éclairage |
| **Relais Ventilation** | GPIO 32 | Contrôle du ventilateur |
| **DS18B20** | GPIO 13 | Sonde température eau (OneWire) |
| **Capteur Porte** | GPIO 19 | Détection ouverture/fermeture |
| **RTC SDA** | GPIO 16 | Communication I2C - Données |
| **RTC SCL** | GPIO 17 | Communication I2C - Horloge |

### Notes de Câblage

#### DS18B20 (Température Eau)
```
VCC  → 3.3V
GND  → GND
DATA → GPIO 13 (avec résistance 4.7kΩ vers 3.3V)
```

#### DS3231 RTC
```
VCC → 5V (ou 3.3V selon module)
GND → GND
SDA → GPIO 16
SCL → GPIO 17
```

#### DHT11
```
VCC  → 3.3V
GND  → GND
DATA → GPIO 21
```

#### Relais (Configuration Active LOW)
```
VCC → 5V
GND → GND
IN  → GPIO (26/25/32)
```
⚠️ **Important** : Les relais sont activés avec un signal LOW (0V)

---

## 🚀 Installation et Configuration

### 1. Prérequis Logiciels

- **Arduino IDE** (version 1.8.x ou 2.x)
- **Pilote ESP32** pour Arduino IDE

#### Installation du support ESP32 dans Arduino IDE :
```
Fichier → Préférences → URLs de gestionnaire de cartes supplémentaires
Ajouter : https://dl.espressif.com/dl/package_esp32_index.json
```

### 2. Bibliothèques Requises

Installez via le Gestionnaire de bibliothèques Arduino :

- `DHT sensor library` by Adafruit
- `Adafruit Unified Sensor`
- `OneWire`
- `DallasTemperature`
- `RTClib` by Adafruit
- `Preferences` (incluse avec ESP32)

### 3. Configuration WiFi

Le système crée un point d'accès WiFi :
```cpp
SSID     : ESP32_Control
Password : 12345678
IP       : 192.168.4.1
```

Pour modifier ces paramètres, éditez les lignes suivantes dans le code :
```cpp
const char* ssid = "ESP32_Control";
const char* password = "12345678";
```

### 4. Configuration de l'Horloge RTC

**Important** : Lors de la première utilisation, décommentez cette ligne pour régler l'heure :
```cpp
rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
```

Ou réglez manuellement :
```cpp
rtc.adjust(DateTime(2025, 1, 1, 12, 0, 0)); // année, mois, jour, heure, minute, seconde
```

Puis **re-commentez** la ligne et téléversez à nouveau pour éviter de réinitialiser l'heure à chaque redémarrage.

---

## 📖 Fonctionnalités

### 🌡️ Monitoring des Capteurs

- **Température ambiante** (DHT11)
- **Humidité relative** (DHT11)
- **Température de l'eau** (DS18B20)
- **Niveau du réservoir** (HC-SR04 - distance)
- **État de la porte** (ouvert/fermé)
- **Horloge en temps réel** (date et heure)

### ⚙️ Contrôle des Actionneurs

#### Moteur d'Irrigation
- Contrôle manuel (ON/OFF)
- **Mode automatique** avec programmation horaire
  - Support de multiples plages horaires
  - Activation/désactivation individuelle des horaires
  - Sauvegarde persistante en mémoire

#### Lampe
- Contrôle manuel (ON/OFF)

#### Ventilation
- Contrôle manuel (ON/OFF)
- **Mode automatique** basé sur la température
  - Seuil de démarrage configurable (défaut : 35°C)
  - Seuil d'arrêt configurable (défaut : 30°C)
  - Délai d'attente avant démarrage (défaut : 5 minutes)

### 💾 Persistance des Données

Tous les paramètres sont sauvegardés automatiquement dans la mémoire non-volatile (NVS) de l'ESP32 :
- États des relais
- Modes automatiques (ON/OFF)
- Horaires de programmation du moteur
- Seuils de température de ventilation

**Les paramètres sont restaurés automatiquement après un redémarrage !**

---

## 🌐 Interface Web

L'interface web est accessible à l'adresse : **http://192.168.4.1**

### Fonctionnalités de l'Interface

- 📊 **Dashboard** : Affichage en temps réel de tous les capteurs
- 🎛️ **Contrôles** : Boutons ON/OFF pour chaque actionneur
- ⏰ **Programmation** : Gestion des horaires d'irrigation
- 🤖 **Automatisation** : Configuration des modes automatiques
- 🔄 **Mise à jour automatique** : Rafraîchissement toutes les 3 secondes

---

## 🔍 Utilisation

### Connexion au Système

1. Alimenter l'ESP32
2. Se connecter au réseau WiFi `ESP32_Control` (mot de passe : `12345678`)
3. Ouvrir un navigateur et aller à `http://192.168.4.1`

### Configuration du Mode Auto Moteur

1. Activer le **Mode Auto Moteur**
2. Cliquer sur **"Ajouter un horaire"**
3. Définir l'heure de démarrage et d'arrêt
4. Valider avec le bouton **"Ajouter l'horaire"**
5. L'horaire apparaît dans la liste avec possibilité de :
   - ✅ Activer/Désactiver
   - 🗑️ Supprimer

### Configuration du Mode Auto Ventilation

1. Définir le **Seuil MAX** (température de démarrage)
2. Définir le **Seuil MIN** (température d'arrêt)
3. Définir la **Durée d'attente** avant démarrage
4. Cliquer sur **"Mettre à jour les paramètres"**
5. Activer le **Mode Auto Ventilation**




