# Verdrahtungsplan - Arduino Zählwaage
**Hardware**: Wemos D1 R2 (ESP8266)

## 1. I2C LCD Display (16x2)
| LCD Pin | Wemos D1 R2 Pin | Anmerkung |
| :--- | :--- | :--- |
| **VCC** | 5V |  |
| **GND** | GND |  |
| **SDA** | **D14** (SDA) | GPIO 4 |
| **SCL** | **D15** (SCL) | GPIO 5 |

## 2. HX711 Wägezelle
| HX711 Pin | Wemos D1 R2 Pin | Anmerkung |
| :--- | :--- | :--- |
| **VCC** | 3.3V | |
| **GND** | GND | |
| **DT** (Data) | **D5** | GPIO 14 |
| **SCK** (Clock)| **D6** | GPIO 12 |

## 3. Taster (Tare / Nullstellen)
| Taster Pin | Wemos D1 R2 Pin | Anmerkung |
| :--- | :--- | :--- |
| Bein 1 | **D7** | GPIO 13 (Signal) |
| Bein 2 | **RX** | Unten rechts. Wir nutzen RX als "Virtual GND". |

## 4. RGB LED (Common Cathode / Gemeinsame Masse)
| LED Pin | Wemos D1 R2 Pin | Anmerkung |
| :--- | :--- | :--- |
| **Rot (R)** | **TX** (D1) | Flackert kurz beim Einschalten (normal) |
| **Grün (G)** | **D2** | GPIO 16 |
| **Blau (B)** | **D10** | GPIO 15 (Muss beim Booten LOW sein) |
| **GND (-)** | GND | Gemeinsame Masse alle LEDs |

> [!NOTE]
> Die Pins D3 und D4 (GPIO 0 / 2) bleiben frei, da diese beim Starten Probleme verursachen können (Boot-Mode), wenn Peripherie daran angeschlossen ist.

## 5. Stromversorgung
Der Wemos D1 R2 kann über **USB** oder ein Netzteil an der Hohlbuchse (9-12V, Vorsicht!) versorgt werden. Für die Entwicklung ist USB optimal.
