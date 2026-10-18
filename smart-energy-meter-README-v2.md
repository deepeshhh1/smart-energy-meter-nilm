# PowerPilot — Smart Energy Meter with NILM ⚡

**ESP32-based smart energy meter with appliance-level detection, bill forecasting and Telegram alerts.**

A normal energy meter tells you only one number: total consumption. **PowerPilot** goes further — it measures voltage and current in real time, computes energy (kWh), cost, Eco Score and CO₂ footprint, and uses **Non-Intrusive Load Monitoring (NILM)** to identify which appliance is running from the aggregate load signal at a single metering point — no separate sensor on every appliance.

## ✨ Features

- **Real-time energy monitoring** — voltage, current, power, kWh, running bill (₹), daily cost and peak-load tracking
- **AI Feature 1 — NILM appliance detection** — identifies the running appliance from load signatures
- **AI Feature 2 — Bill forecasting + anomaly alert** — 7-day consumption log predicts the monthly bill and flags abnormal usage
- **AI Feature 3 — Telegram bot** — live alerts and a `/status` command for remote monitoring
- **16×2 LCD display** — live readings and device IP on the meter itself
- **Web dashboard** — redesigned dark UI served by the ESP32 for live monitoring
- **Eco Score & CO₂ estimate** — see the environmental side of your consumption

## 🛠️ Hardware & stack

- **ESP32** (WiFi) with **EmonLib** energy monitoring
- Voltage sensing on GPIO 34, current sensing on GPIO 35, buzzer on GPIO 13
- LiquidCrystal I2C display (16×2), ESPmDNS, Preferences (persistent storage)

## 📂 In this repo

- `PowerPilot.ino` — the complete firmware (v57 PRO)

## 🔐 Note

WiFi and Telegram credentials are intentionally replaced with placeholders (`YOUR_BOT_TOKEN_HERE`, etc.) — add your own values before deployment.

## 👤 Author

**Deepesh Chauhan** — B.Tech Electrical Engineering, RGGEC Kangra
[LinkedIn](https://www.linkedin.com/in/deepeshhh1)
