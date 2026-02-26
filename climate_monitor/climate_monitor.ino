// ==========================================
// Environment Monitor Project
// Hardware: Wemos D1 R2 (ESP8266) + DHT11 + RGB LED + LCD
// ==========================================

#include "secrets.h"
#include <DHT.h>
#include <ESP8266WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <Wire.h>

// --- Hardware Configuration ---
// --- Hardware Configuration ---
// LCD (I2C) - Wemos D1 R2 specific:
// SDA = GPIO 4 (Labeled bottom right as D14/SDA)
// SCL = GPIO 5 (Labeled bottom right as D15/SCL)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// DHT11 Sensor
// Connected to Pin labeled "D13/SCK" which is GPIO 14
#define DHTPIN 14
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// RGB LED (Common Cathode)
// RED -> Pin labeled "TX" (GPIO 1)
// GREEN -> Pin labeled "D2" (GPIO 16 on R2)
// BLUE -> Pin labeled "D10/SS" (GPIO 15)
#define LED_RED_PIN 1    // TX
#define LED_GREEN_PIN 16 // D2
#define LED_BLUE_PIN 15  // D10 / SS

// --- Network Objects ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Settings & Variables ---
unsigned long lastMeasureTime = 0;
#define MEASURE_INTERVAL_MS 2000

float humidity = 0.0;
float temperature = 0.0;
float lastPublishedTemp = -999.0;
float lastPublishedHum = -999.0;

// --- Helper Functions ---

void setLedColor(int r, int g, int b) {
  analogWrite(LED_RED_PIN, r);
  analogWrite(LED_GREEN_PIN, g);
  analogWrite(LED_BLUE_PIN, b);
}

// Traffic Light Logic
void updateTrafficLight(float t, float h) {
  // Define Comfort Zones
  // Temp: 20-25 OK. 18-20 or 25-28 WARNING. else BAD.
  // Hum: 40-60 OK. 30-40 or 60-70 WARNING. else BAD.

  bool tempOk = (t >= 20.0 && t <= 25.0);
  bool tempWarn = ((t >= 18.0 && t < 20.0) || (t > 25.0 && t <= 28.0));

  bool humOk = (h >= 40.0 && h <= 60.0);
  bool humWarn = ((h >= 30.0 && h < 40.0) || (h > 60.0 && h <= 70.0));

  if (tempOk && humOk) {
    // GREEN: Everything Optimal
    setLedColor(0, 255, 0);
  } else if ((tempOk || tempWarn) && (humOk || humWarn)) {
    // YELLOW: One or both in warning, but none critical
    setLedColor(255, 120, 0);
  } else {
    // RED: Critical
    setLedColor(255, 0, 0);
  }
}

void updateDisplay() {
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(temperature, 1);
  lcd.print(" C  "); // Spaces to clear remnants

  lcd.setCursor(0, 1);
  lcd.print("Hum : ");
  lcd.print(humidity, 0); // Int for humidity usually enough
  lcd.print(" %  ");
}

// --- Network Functions ---

void setup_wifi() {
  delay(10);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connect...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    setLedColor(0, 0, 255); // Blink Blue
    delay(100);
    setLedColor(0, 0, 0);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.print("WiFi OK!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    lcd.clear();
    lcd.print("WiFi Failed!");
    delay(2000);
  }
}

void reconnect() {
  if (!client.connected()) {
    if (client.connect("EndmontageClimate")) {
      client.publish(MQTT_TOPIC, "online");
    }
  }
}

// --- Main Code ---

void setup() {
  // 1. Setup LED
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);

  // 2. Initialize LCD
  Wire.begin(4, 5); // Force SDA=GPIO4 (D14), SCL=GPIO5 (D15)
  lcd.init();
  lcd.backlight();

  // 3. Initialize DHT
  dht.begin();

  // 4. Connect WiFi
  setup_wifi();

  // 5. Setup MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);

  lcd.clear();
  lcd.print("Init DHT11...");
}

void loop() {
  // --- Network Tasks ---
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }

  // --- Measurement Logic ---
  if (millis() - lastMeasureTime > MEASURE_INTERVAL_MS) {
    lastMeasureTime = millis();

    // Reading DHT
    float newH = dht.readHumidity();
    float newT = dht.readTemperature();

    // Check if any reads failed and exit early (to try again).
    if (isnan(newH) || isnan(newT)) {
      lcd.setCursor(13, 0);
      lcd.print("Err");
      setLedColor(0, 0, 255); // Blue Flash for Error
      return;
    }

    temperature = newT;
    humidity = newH;

    updateTrafficLight(temperature, humidity);
    updateDisplay();

    // --- MQTT Publish ---
    if (client.connected()) {
      // Publish only on significant change (or periodically if desired, here
      // simply on change 0.1)
      if (abs(temperature - lastPublishedTemp) >= 0.1) {
        char msg[10];
        dtostrf(temperature, 4, 1, msg);
        String topic = String(MQTT_TOPIC) + "/temp";
        client.publish(topic.c_str(), msg);
        lastPublishedTemp = temperature;
      }

      if (abs(humidity - lastPublishedHum) >= 1.0) {
        char msg[10];
        dtostrf(humidity, 4, 0, msg); // No decimals for hum
        String topic = String(MQTT_TOPIC) + "/humidity";
        client.publish(topic.c_str(), msg);
        lastPublishedHum = humidity;
      }
    }
  }
}
