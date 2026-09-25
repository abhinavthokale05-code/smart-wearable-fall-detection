# Smart Wearable Fall Detection System

A smart wearable system designed to detect fall events using motion and physiological sensing, embedded firmware, and a machine-learning-based fall detection model.

## 📌 Project Overview

Falls can be dangerous, particularly when a person is unable to immediately call for assistance.

This project implements a wearable fall detection system that combines:

- Motion sensing
- Heart-rate monitoring
- Embedded processing
- Rule-based fall detection
- Machine learning
- User interface/display
- Wireless communication

The system processes sensor data from the wearable device and evaluates the collected features to determine whether a fall event has occurred.

---

## 🏗️ System Architecture

The project consists of four major parts:

```text
Sensors
   ↓
ESP32-based Wearable
   ↓
Sensor Data Processing
   ↓
Feature Extraction
   ↓
Fall Detection
 ┌───────────────┐
 │ Rule-Based    │
 │ Detection     │
 └───────┬───────┘
         │
         +──────────────┐
                        ↓
              Machine Learning
                        ↓
                 Final Decision
                        ↓
              User Notification