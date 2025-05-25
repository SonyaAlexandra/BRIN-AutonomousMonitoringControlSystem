# BRIN-AutonomousMonitoringControlSystem
An IoT-driven environmental monitoring and automation system developed during my Bootcamp Activity at BRIN (Badan Riset dan Inovasi Nasional). This project enables autonomous temperature and humidity control for hydroponic farming using an ESP32 microcontroller, BME280 sensor, and a MOSFET-triggered spray mist actuator.
# Component List
- ESP32
- BME280
- IRF5305 MOSFET Module
- 12V Power Supply
- 12V Pump
- Spray Mist Nozzle
# Codes Description
- AMCS_Original: The original code for the system, which is not yet integrated with any existing system or communication method like ESP-NOW. It controls a pump via a MOSFET, and sends formatted messages containing sensor data, actuator statuses, and threshold values over the Serial Monitor.
- AMCS_ConnectESPNOW: The code that has been integrated with ESP-NOW, Sends 3 messages wirelessly to another ESP32 using ESP-NOW.
- AMCS_CombinedwithExistingSystem: The that integrates with an existing system using Arduino Mega
    * added components
     - DS18B20
     - pH Sensor
     - TDS Sensor
     - DO Sensor
     - Water Level
     - RTC






