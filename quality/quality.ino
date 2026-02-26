// ==========================================
// Arduino Scale Project - "QM Station"
// Hardware: Wemos D1 R2 (ESP8266)
// ==========================================

// --- CONFIGURATION ---
// Select Station Type: 0 = QM Gut (Good), 1 = QM Schlecht (Bad)
#define STATION_TYPE 1

#if STATION_TYPE == 0
#define STATION_NAME "QM Gutteile"
#define MQTT_TOPIC_SUFFIX "/qm/good"
#define LED_R_DEFAULT 0
#define LED_G_DEFAULT 255
#define LED_B_DEFAULT 0
#else
#define STATION_NAME "QM Ausschuss"
#define MQTT_TOPIC_SUFFIX "/qm/bad"
#define LED_R_DEFAULT 255
#define LED_G_DEFAULT 0
#define LED_B_DEFAULT 0
#endif

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

// Button (Tare/Reset) - Pins: D7 (Signal), RX/GPIO3 (Virtual GND)
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
// int bufferCount = DEFAULT_BUFFER_COUNT; // Removed as per USER request
float calibration_factor = 425.0; // Adjust as needed
float noiseThreshold = 2.0; // Minimum weight change (grams) to count as a part

int currentCount = 0;
int lastPublishedCount = -1;

// Logic Variables for Edge Detection
float lastStableWeight = 0.0;
float currentWeight = 0.0;
unsigned long stableStartTime = 0;
bool isStable = false;
#define STABILITY_DELAY_MS 800 // Time weight must be stable to accept new value

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
  printCentered(STATION_NAME, 0);
  // Row 1: Count
  String statusLine = "Anzahl: " + String(currentCount);
  printCentered(statusLine, 1);
}

void updateTrafficLight() {
  // Simple Logic: If active/ready -> Default Color (Green for Good, Red for
  // Bad). Blink on Count is handled in the counting logic.
  setLedColor(LED_R_DEFAULT, LED_G_DEFAULT, LED_B_DEFAULT);
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
    // Client ID construction to be unique
    String clientId = "QMScale-";
    clientId += String(STATION_TYPE);

    if (client.connect(clientId.c_str())) {
      client.publish(MQTT_TOPIC_SUFFIX, "online");
    }
  }
}

// --- Main Code ---

void setup() {
  // Serial.begin(115200); // Standalone

  // 1. Setup Button
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
    scale.tare(); // Zero the scale on boot
    scale.tare(); // Zero the scale on boot
    lastStableWeight = 0.0;
    setLedColor(LED_R_DEFAULT, LED_G_DEFAULT, LED_B_DEFAULT);
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

  // --- Button Logic (Reset Count & Tare) ---
  if (digitalRead(BUTTON_PIN) == LOW) {
    setLedColor(0, 0, 255);
    lcd.clear();
    printCentered("Reset & Tare...", 0);
    scale.tare();
    delay(500);

    currentCount = 0;
    lastStableWeight = 0.0;
    lastPublishedCount = -1; // Force republish

    updateDisplay();
    // Wait for button release to avoid loops
    while (digitalRead(BUTTON_PIN) == LOW)
      delay(10);
  }

  // --- Weighing & Counting Logic ---
  if (scale.is_ready()) {
    // Get average of 5 readings for filtering
    float reading = scale.get_units(5);

    // Simple noise gate for zero
    // if (reading < -100) ... typically we just trust the value relative to
    // lastStable

    // Check Stability
    if (abs(reading - currentWeight) < 1.0) { // If fluctuating less than 1g
      if (!isStable) {
        stableStartTime = millis();
        isStable = true;
      }
    } else {
      isStable = false;
      currentWeight = reading; // Follow the reading
    }

    // Logic Trigger: If stable for X time
    if (isStable && (millis() - stableStartTime > STABILITY_DELAY_MS)) {
      // We have a stable weight value: 'currentWeight' (approx)
      // Actually let's use the 'reading' as the precise stable value
      float stableValue = reading;

      float delta = stableValue - lastStableWeight;

      if (delta > noiseThreshold) {
        // Positive Change -> ITEM ADDED
        // You could determine HOW MANY items by dividing delta / avgWeight if
        // known, but here we just count "events" or "approx items" if we want.
        // Requirement: "bei jeder positiven Gewichtsänderung ein Teil
        // hochzählen" We assume +1 for any significant positive stable change.

        currentCount++;
        lastStableWeight = stableValue;
        updateDisplay();

        // Visual Feedback for Count (Quick Blink White?)
        setLedColor(255, 255, 255);
        delay(200);
        updateTrafficLight(); // Restore color

      } else if (delta < -noiseThreshold) {
        // Negative Change -> ITEM REMOVED
        // "Negative Gewichtsänderungen sollen ignoriert werden" (regarding
        // count) But we MUST update lastStableWeight so we can detect the next
        // add!
        lastStableWeight = stableValue;
      }

      // If delta is small (noise), do nothing.
      // Re-arm stability? We are constantly checking.
      // To prevent double counting the same stable period, we need to ensure we
      // only act ONCE per stable plateau logic? Actually 'lastStableWeight =
      // stableValue' handles this. Once updated, the delta becomes 0 next loop.
    }
  }

  // --- MQTT Publish on Change ---
  if (currentCount != lastPublishedCount && client.connected()) {
    char msg[10];
    itoa(currentCount, msg, 10);
    // Topic Construction: "scale/" + Suffix
    // For simplicity, we assume MQTT_TOPIC in secrets was generic or we
    // construct here:
    String topic = MQTT_TOPIC;
    topic += MQTT_TOPIC_SUFFIX;

    client.publish(topic.c_str(), msg);
    lastPublishedCount = currentCount;
  }

  updateTrafficLight();
  delay(50);
}
