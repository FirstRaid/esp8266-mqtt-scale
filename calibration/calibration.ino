/*
  Calibration Code for HX711 Load Cell

  HOW TO USE:
  1. Upload this code.
  2. Open Serial Monitor (115200 Baud).
  3. The scale will Tare (Reset to 0). Wait for "Ready".
  4. Place a known weight on the scale (e.g. 100g, 500g).
  5. Enter the weight value in the Serial Monitor (e.g. type "100" and press
  Enter). (If Serial Input doesn't work because of the wiring: Calculate
  manually!)

     MANUAL CALCULATION:
     The Serial Monitor will show the "Raw Value" (e.g. 45000).
     Formula: Factor = RawValue / KnownWeight
     Example: 45000 / 100g = 450.0

  6. Copy the resulting Factor into your main 'scale.ino' sketch.
*/

#include "HX711.h"
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// --- Pins (Matches your project) ---
#define LOADCELL_DOUT_PIN D5
#define LOADCELL_SCK_PIN D6

// RGB LED Pins
#define LED_RED_PIN 1    // TX
#define LED_GREEN_PIN 16 // D2
#define LED_BLUE_PIN 15  // D10

// LCD (Optional, but connected)
LiquidCrystal_I2C lcd(0x27, 16, 2);

HX711 scale;

void setLedColor(int r, int g, int b) {
  analogWrite(LED_RED_PIN, r);
  analogWrite(LED_GREEN_PIN, g);
  analogWrite(LED_BLUE_PIN, b);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- HX711 Calibration Tool ---");

  // Init LEDs
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);

  // LED Test Cycle
  setLedColor(255, 0, 0);
  delay(500); // Red
  setLedColor(0, 255, 0);
  delay(500); // Green
  setLedColor(0, 0, 255);
  delay(500); // Blue
  setLedColor(0, 0, 0);
  delay(200); // Off

  // Init LCD
  // Wire.begin(D4, D5); // Removed confusing line
  // Let's use Wire.begin(SDA, SCL) to be safe and match previous code logic.
  Wire.begin(SDA, SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Calibration Mode");

  // Init Scale
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(1.0); // Set scale to 1 to read raw "units"

  Serial.println("Taring... Remove all weights!");
  lcd.setCursor(0, 1);
  lcd.print("Taring...");
  delay(1000);
  scale.tare();

  Serial.println("Tare Complete.");
  Serial.println("Place known weight on scale now.");
  lcd.setCursor(0, 1);
  lcd.print("Place Weight...");
}

void loop() {
  if (scale.is_ready()) {
    // Read raw value (averaged)
    long reading = scale.get_units(10);

    Serial.print("Raw Reading (Factor 1.0): ");
    Serial.print(reading);

    // Help calculate
    // If you placed 100g:
    Serial.print(" | If 100g: Factor = ");
    Serial.print(reading / 100.0);

    // If you placed 500g:
    Serial.print(" | If 500g: Factor = ");
    Serial.print(reading / 500.0);

    Serial.println();

    // Update LCD
    lcd.setCursor(0, 1);
    lcd.print("Val:            "); // Clear line
    lcd.setCursor(5, 1);
    lcd.print(reading);
  } else {
    Serial.println("HX711 not found.");
  }

  delay(500);
}
