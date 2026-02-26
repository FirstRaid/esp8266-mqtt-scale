// ==========================================
// Arduino Scale Project - "Endmontage"
// Hardware: Wemos D1 R2 (ESP8266)
// ==========================================

// --- CONFIGURATION ---
// Select Station ID: 1 = EM1, 2 = EM2
#define STATION_ID 2

#if STATION_ID == 1
#define STATION_NAME "Endmontage 1"
#define MQTT_TOPIC_SUFFIX "/em1"
#else
#define STATION_NAME "Endmontage 2"
#define MQTT_TOPIC_SUFFIX "/em2"
#endif

#include "HX711.h"
#include "secrets.h"
#include <ESP8266WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <Wire.h>

// --- Hardware Configuration ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Load Cell
#define LOADCELL_DOUT_PIN D5
#define LOADCELL_SCK_PIN D6
HX711 scale;

// Button
#define BUTTON_PIN D7
#define BUTTON_GND_PIN 3

// RGB LED
#define LED_RED_PIN 1
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN 15

// --- Network Objects ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Settings & Variables ---
int maxStock = 10;                // Initial Stock at Tare
float partWeight = 9.8;           // Weight per piece in Grams
float calibration_factor = 425.0; // Verify with calibration sketch

int currentCount = 0;
long rawValue = 0;
int lastPublishedCount = -100;

// Debounce / Stability
int lastTentativeCount = 0;
unsigned long stableStartTime = 0;
#define STABILITY_DELAY_MS 1000

// Blink Variables
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// --- Helper Functions ---

void setLedColor(int r, int g, int b) {
  analogWrite(LED_RED_PIN, r);
  analogWrite(LED_GREEN_PIN, g);
  analogWrite(LED_BLUE_PIN, b);
}

void printCentered(String text, int row) {
  int spaces = (16 - text.length()) / 2;
  if (spaces < 0)
    spaces = 0;
  lcd.setCursor(spaces, row);
  lcd.print(text);
}

void updateDisplay() {
  lcd.clear();
  printCentered(STATION_NAME, 0);
  String statusLine =
      "Bestand: " + String(currentCount) + "/" + String(maxStock);
  printCentered(statusLine, 1);
}

void updateTrafficLight() {
  // Logic: REVERSED (Green = Full, Red = Empty)
  // > 6 : Green
  // 4-6 : Orange
  // 2-3 : Red
  // <= 1: Blink Red (Critical)

  if (currentCount <= 1) {
    if (millis() - lastBlinkTime > 300) {
      lastBlinkTime = millis();
      blinkState = !blinkState;
      if (blinkState)
        setLedColor(255, 0, 0); // Red
      else
        setLedColor(0, 0, 0);
    }
  } else {
    // Solid Colors
    if (currentCount > 6) {
      setLedColor(0, 255, 0); // Green (Good)
    } else if (currentCount >= 4) {
      setLedColor(255, 120, 0); // Orange (Warn)
    } else {
      setLedColor(255, 0, 0); // Red (Alert)
    }
  }
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
    String clientId = "EMScale-";
    clientId += String(STATION_ID);
    if (client.connect(clientId.c_str())) {
      String topic = MQTT_TOPIC;
      topic += MQTT_TOPIC_SUFFIX;
      client.publish(topic.c_str(), "online");
    }
  }
}

// --- Main Code ---

void setup() {
  // Serial.begin(115200);

  // 1. Pins
  pinMode(BUTTON_GND_PIN, OUTPUT);
  digitalWrite(BUTTON_GND_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);

  // 2. LCD
  Wire.begin(SDA, SCL);
  lcd.init();
  lcd.backlight();

  // 3. Network
  setup_wifi();
  client.setServer(MQTT_SERVER, MQTT_PORT);

  // 4. Scale
  lcd.clear();
  printCentered("Init Waage...", 0);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  delay(1000);

  if (scale.is_ready()) {
    scale.set_scale(calibration_factor);
    scale.tare();

    currentCount = maxStock;
    setLedColor(0, 255, 0);
  } else {
    lcd.clear();
    lcd.print("HX711 Error");
    while (1)
      ;
  }

  updateDisplay();
}

void loop() {
  // --- Network ---
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected())
      reconnect();
    client.loop();
  }

  // --- Button (Restock / Tare to Full) ---
  if (digitalRead(BUTTON_PIN) == LOW) {
    setLedColor(0, 0, 255);
    lcd.clear();
    printCentered("Restock (14)...", 0);
    scale.tare(); // Creates 0 reference at current weight
    delay(500);

    // Tare means we have 14 items
    currentCount = maxStock;
    lastTentativeCount = maxStock;
    // Force Publish
    lastPublishedCount = -100;

    updateDisplay();
    while (digitalRead(BUTTON_PIN) == LOW)
      delay(10);
  }

  // --- Logic ---
  if (scale.is_ready()) {
    float weight = scale.get_units(5);

    // Weight Logic:
    // Scale is Tared at 14 items.
    // If we remove items, weight becomes negative (e.g. -9.8g).
    // Count = 14 + (Weight / 9.8)
    // Example: Weight -19.6 -> 14 + (-2) = 12.
    // Example: Weight +9.8 -> 14 + 1 = 15.

    int change = (int)((weight / partWeight));
    // Rounding optimization: (weight / partWeight) + (sign * 0.5) if needed
    // Simple integer cast truncates toward zero.
    // Better: round(weight/part).
    if (weight > 0)
      change = (int)((weight / partWeight) + 0.5);
    else
      change = (int)((weight / partWeight) - 0.5);

    int instantCount = maxStock + change;

    if (instantCount < 0)
      instantCount = 0; // Sanity check

    if (instantCount != currentCount) {
      if (instantCount == lastTentativeCount) {
        if (millis() - stableStartTime > STABILITY_DELAY_MS) {
          currentCount = instantCount;
          updateDisplay();
        }
      } else {
        lastTentativeCount = instantCount;
        stableStartTime = millis();
      }
    } else {
      // Reset timer if matches current
      // stableStartTime = millis(); // Don't reset, allows quick confirmation?
      // No, if it fluctuates away and back, we want to debounce.
      // The 'else' here means 'instant == current', so we are stable at
      // current.
      lastTentativeCount = currentCount;
    }
  }

  // --- MQTT ---
  if (currentCount != lastPublishedCount && client.connected()) {
    char msg[10];
    itoa(currentCount, msg, 10);
    String topic = MQTT_TOPIC;
    topic += MQTT_TOPIC_SUFFIX;
    client.publish(topic.c_str(), msg);
    lastPublishedCount = currentCount;
  }

  updateTrafficLight();
  delay(50);
}
