// ==========================================
// Arduino Scale Project - "Zugentlastung"
// Hardware: Wemos D1 R2 (ESP8266)
// ==========================================

#include "HX711.h"
#include "secrets.h"
#include <ESP8266WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <Wire.h>

// --- Hardware Configuration ---
// LCD (I2C) - Pins: D14 (SDA), D15 (SCL)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Load Cell (HX711) - Pins: D5 (DT), D6 (SCK)
#define LOADCELL_DOUT_PIN D5
#define LOADCELL_SCK_PIN D6
HX711 scale;

// Button (Tare) - Pins: D7 (Signal), RX/GPIO3 (Virtual GND)
#define BUTTON_PIN D7
#define BUTTON_GND_PIN 3

// RGB LED (Common Cathode) - Pins: TX(R), D2(G), D10(B)
#define LED_RED_PIN 1
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN 15

// --- Network Objects ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Settings & Variables ---
String stationName = "Zugentlastung";
int bufferCount = 4; // Target quantity (Max/Bin Size)
float partWeight =
    1.58; // Weight per piece in Grams (updated for Zugentlastung)
float calibration_factor = 425.0;

int currentCount = 0;
long rawValue = 0;
int lastPublishedCount = -1; // To track changes for MQTT

// Debounce / Stability Variables
int lastTentativeCount = 0;
unsigned long stableStartTime = 0;
#define STABILITY_DELAY_MS 1200

// Blink Variables
unsigned long lastBlinkTime = 0;
bool blinkState = false;

// --- Helper Functions ---

void setLedColor(int r, int g, int b) {
  analogWrite(LED_RED_PIN, r);
  analogWrite(LED_GREEN_PIN, g);
  analogWrite(LED_BLUE_PIN, b);
}

// Helper to center text
void printCentered(String text, int row) {
  int spaces = (16 - text.length()) / 2;
  if (spaces < 0)
    spaces = 0;
  lcd.setCursor(spaces, row);
  lcd.print(text);
}

void updateDisplay() {
  lcd.clear();
  // Row 0: Station Name
  printCentered(stationName, 0);
  // Row 1: Stock Status
  String statusLine =
      "Bestand: " + String(currentCount) + "/" + String(bufferCount);
  printCentered(statusLine, 1);
}

void updateTrafficLight() {
  // CRITICAL OVERFLOW (> Buffer + 1) -> Blink RED
  if (currentCount > (bufferCount + 1)) {
    if (millis() - lastBlinkTime > 300) {
      lastBlinkTime = millis();
      blinkState = !blinkState;
      if (blinkState)
        setLedColor(255, 0, 0);
      else
        setLedColor(0, 0, 0);
    }
  }
  // NORMAL STATES
  else {
    if (currentCount == 0)
      setLedColor(255, 0, 0); // Empty -> Red
    else if (currentCount > bufferCount)
      setLedColor(255, 0, 0); // Full -> Red
    else if (currentCount == bufferCount)
      setLedColor(255, 120, 0); // Near Full -> Orange
    else
      setLedColor(0, 255, 0); // OK -> Green
  }
}

// --- Network Functions ---

void setup_wifi() {
  delay(10);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connect...");
  lcd.setCursor(0, 1);
  lcd.print(WIFI_SSID);

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
  // Loop until we're reconnected
  if (!client.connected()) {
    // Attempt to connect
    if (client.connect("EndmontageScale")) { // Client ID
      // Connected!
      client.publish(MQTT_TOPIC, "online");
    } else {
      // Wait 5 seconds before retrying (non-blocking in main loop would be
      // better, but keep simple) For now, we just skip this loop iteration to
      // not block weighing too long
    }
  }
}

// --- Main Code ---

void setup() {
  // Serial.begin(115200); // Standalone

  // 1. Setup Button (Virtual GND)
  pinMode(BUTTON_GND_PIN, OUTPUT);
  digitalWrite(BUTTON_GND_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 2. Setup LED
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);

  // 3. Initialize LCD
  Wire.begin(SDA, SCL);
  lcd.init();
  lcd.backlight();

  // 4. Connect WiFi
  setup_wifi();

  // 5. Setup MQTT
  client.setServer(MQTT_SERVER, MQTT_PORT);

  // 6. Initialize Load Cell
  lcd.clear();
  printCentered("Init Waage...", 0);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  delay(1000);

  if (scale.is_ready()) {
    scale.set_scale(calibration_factor);
    scale.tare();
    setLedColor(0, 255, 0);
  } else {
    lcd.clear();
    lcd.print("HX711 Error");
    setLedColor(255, 0, 0);
    while (1)
      ;
  }

  updateDisplay();
}

void loop() {
  // --- Network Tasks ---
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }

  // --- Button Logic ---
  if (digitalRead(BUTTON_PIN) == LOW) {
    setLedColor(0, 0, 255);
    lcd.clear();
    printCentered("Tare...", 0);
    scale.tare();
    delay(500);

    currentCount = 0;
    lastTentativeCount = 0;
    // Force publish of 0
    lastPublishedCount = -1;

    updateDisplay();
  }

  // --- Weighing Logic ---
  if (scale.is_ready()) {
    float weight = scale.get_units(
        10); // Average 10 readings for better noise reduction on light parts
    if (weight < 0)
      weight = 0;

    int instantCount = (int)((weight / partWeight) + 0.5);

    if (instantCount != currentCount) {
      if (instantCount == lastTentativeCount) {
        if (millis() - stableStartTime > STABILITY_DELAY_MS) {
          currentCount = instantCount;
          rawValue = (long)weight;
          updateDisplay();
        }
      } else {
        lastTentativeCount = instantCount;
        stableStartTime = millis();
      }
    }
  }

  // --- MQTT Publish on Change ---
  if (currentCount != lastPublishedCount && client.connected()) {
    char msg[10];
    itoa(currentCount, msg, 10);
    client.publish(MQTT_TOPIC, msg);
    lastPublishedCount = currentCount;
  }

  updateTrafficLight();
  delay(50);
}