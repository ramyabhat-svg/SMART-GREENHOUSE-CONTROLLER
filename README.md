# SMART-GREENHOUSE-CONTROLLER
Project Overview
A smart greenhouse monitoring and control system built using ESP32. It tracks soil moisture and ambient light, displays data on a TFT screen, and allows remote control via the Blynk IoT platform, with results displayed on an interactive dashboard. The system includes keypad-based security and real-time status visualization.

List the key capabilities (this is where you impress people):

📊 Real-time soil moisture & light monitoring
📱 Remote control via Blynk app
🔐 Keypad-based greenhouse lock/unlock system
📺 TFT display dashboard
🌈 NeoPixel status indication
🔔 Buzzer alerts for security events
⚙️ Manual + automatic control modes

🛠️ Hardware Used

Be specific:

ESP32
Potentiometer (soil simulation)
LDR (light sensor)
4x4 Keypad (I2C)
TFT Display (ILI9341 SPI)
NeoPixel LED ring
Buzzer
Breadboard + wires

| Component    | ESP32 Pin          |
| ------------ | ------------------ |
| Soil Sensor  | GPIO 36            |
| LDR          | GPIO 39            |
| NeoPixel     | GPIO 17            |
| Buzzer       | GPIO 25            |
| TFT (SPI)    | Config in TFT_eSPI |
| Keypad (I2C) | 0x3D Address       |

