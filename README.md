
# RTOS-Based Multi-Sensor IoT Node

## Overview
An ESP32-based IoT project designed to collect sensor readings,
display live data, and transmit measurements over Wi-Fi using MQTT.

The project explores multitasking with FreeRTOS instead of
handling all operations in a single loop.

## Hardware
- ESP32 development board
- DHT11 temperature and humidity sensor
- MPU6050 accelerometer and gyroscope
- I2C OLED display
- Breadboard and jumper wires

## Technologies
- C/C++
- ESP32
- FreeRTOS
- I2C
- Wi-Fi
- MQTT

## System Design
1. The sensor task collects readings.
2. Queues transfer readings to other tasks.
3. The display task updates the OLED.
4. The MQTT task sends sensor data to a broker.

A mutex can protect the shared I2C bus when it is
accessed by multiple tasks.

## Features
-  Sensor data acquisition
-  OLED display updates
-  FreeRTOS tasks and queues
-  MQTT data publishing
-  Wi-Fi reconnection
-  Watchdog recovery

# Setup and Usage
Hardware connections, library requirements, and firmware
upload instructions will be documented here.

## Project Status
In progress

## Author
Nitha V V
