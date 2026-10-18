// ============================================================
//  POWERPILOT v57 PRO — B.Tech Final Year Project
//  FEATURES:
//  1. Formula Fix (kWh, Eco Score, CO2)
//  2. AI Feature 1 — NILM Appliance Detection
//  3. AI Feature 2 — AI Bill Forecasting + Anomaly Alert
//  4. AI Feature 3 — Telegram Bot (live alerts + /status command)
//  5. Android Fix (IP on LCD)
//  6. Redesigned Dark Dashboard UI
// ============================================================
/*
============================================================
SECURITY NOTE:
Sensitive credentials such as WiFi SSID, Password,
Telegram Bot Token, and Chat ID have been removed
from this code for security and privacy purposes.

During real deployment, these values must be replaced
with valid credentials by the user.
============================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <ESPmDNS.h>
#include "EmonLib.h"
#include "time.h"

// ---------------- PIN DEFINES ----------------
#define VOLT_PIN    34
#define AMPS_PIN    35
#define BUZZER_PIN  13

// ---------------- TELEGRAM CONFIG ----------------
// NOTE: Credentials hidden for security reasons
#define TELEGRAM_TOKEN  "YOUR_BOT_TOKEN_HERE"
#define TELEGRAM_CHATID "YOUR_CHAT_ID_HERE"


// ---------------- WIFI CONFIG ----------------
// ---------------- WIFI CONFIG ----------------
// NOTE: Replace with your own network credentials during deployment
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

// ---------------- OBJECTS ----------------
EnergyMonitor emon;
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer server(80);
Preferences preferences;

// ---------------- ENERGY VARIABLES ----------------
float unit_price       = 5.0;
float kWh              = 0;
float total_bill       = 0;
float daily_cost       = 0;
float monthly_budget   = 500;
float todays_allowance = 16.6;
float rollover_balance = 0;

// ---------------- SYSTEM STATE ----------------
String systemStatus = "Healthy";
String loadType     = "None";
String txLog        = "";
String myIP         = "";

// ---------------- PEAK TRACKING ----------------
float peakLoad = 0;
int   peakHour = 0;

// ---------------- AI FEATURE 1: NILM ----------------
#define NILM_WINDOW 5
float nilmBuffer[NILM_WINDOW] = {0};
int   nilmIndex    = 0;
float lastWatts    = 0;
String detectedApp = "Monitoring...";
unsigned long nilmMillis = 0;

// ---------------- AI FEATURE 2: FORECASTING ----------------
#define FORECAST_DAYS 7
float dailyKWhLog[FORECAST_DAYS] = {0};
int   forecastIndex  = 0;
float forecastedBill = 0;
bool  anomalyAlert   = false;
float avgDailyCost   = 0;

// ---------------- AI FEATURE 3: TELEGRAM ----------------
unsigned long lastTelegramCheck = 0;
unsigned long lastAlertSent     = 0;
int           lastUpdateId      = 0;
bool          telegramReady     = false;

// ---------------- TIMERS ----------------
int           lcdScreen    = 0;
int           last_day     = 0;
unsigned long lastMillis   = 0;
unsigned long lcdMillis    = 0;
unsigned long saveMillis   = 0;
unsigned long buzzerMillis = 0;

bool buzzerOn   = false;
bool buzzerLock = false;

// ============================================================
//  HELPERS
// ============================================================

void shortBeep(int d = 150) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(d);
  digitalWrite(BUZZER_PIN, LOW);
}

void lcdMsg(String a, String b) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(a);
  lcd.setCursor(0, 1); lcd.print(b);
}

float getCO2() {
  return kWh * 0.82;
}

float getEcoScore() {
  float eco = 100.0 - ((daily_cost / (todays_allowance > 0 ? todays_allowance : 1)) * 100.0);
  if (eco < 0)   eco = 0;
  if (eco > 100) eco = 100;
  return eco;
}

String peakText() {
  if (peakHour == 0) return "No Data";
  if (peakHour < 12) return String(peakHour) + " AM";
  if (peakHour == 12) return "12 PM";
  return String(peakHour - 12) + " PM";
}

// ============================================================
//  AI FEATURE 3 — TELEGRAM
// ============================================================

void sendTelegram(String msg) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (String(TELEGRAM_TOKEN) == "YOUR_BOT_TOKEN_HERE") return;
  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_TOKEN;
  url += "/sendMessage";
  String payload = "{\"chat_id\":\"" + String(TELEGRAM_CHATID) + "\",\"text\":\"" + msg + "\",\"parse_mode\":\"Markdown\"}";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.POST(payload);
  http.end();
}

void checkTelegramCommands() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (String(TELEGRAM_TOKEN) == "YOUR_BOT_TOKEN_HERE") return;
  if (millis() - lastTelegramCheck < 5000) return;
  lastTelegramCheck = millis();
  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_TOKEN;
  url += "/getUpdates?offset=" + String(lastUpdateId + 1) + "&timeout=1";
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();
    if (resp.indexOf("/status") > 0) {
      float V  = (emon.Vrms < 50) ? 0 : emon.Vrms;
      float I  = (V == 0 || emon.Irms < 0.05) ? 0 : emon.Irms;
      float W  = V * I;
      float eco = getEcoScore();
      String reply = "*PowerPilot Live Status*\n";
      reply += "Voltage: "    + String(V, 0)   + " V\n";
      reply += "Current: "    + String(I, 2)   + " A\n";
      reply += "Power: "      + String(W, 0)   + " W\n";
      reply += "Units: "      + String(kWh, 3) + " kWh\n";
      reply += "Bill: Rs "    + String(total_bill, 1) + "\n";
      reply += "Load: "       + loadType + "\n";
      reply += "Eco Score: "  + String(eco, 0) + "/100\n";
      reply += "Appliance: "  + detectedApp + "\n";
      reply += "Status: "     + systemStatus;
      sendTelegram(reply);
      int idx = resp.indexOf("\"update_id\":");
      if (idx > 0) lastUpdateId = resp.substring(idx + 12, idx + 22).toInt();
    }
  }
  http.end();
}

// ============================================================
//  AI FEATURE 1 — NILM
// ============================================================

String detectAppliance(float wDelta) {
  float d = abs(wDelta);
  if (d < 5) return detectedApp;
  if (wDelta > 0) {
    if (d < 15)  return "LED Bulb ON";
    if (d < 40)  return "Tubelight ON";
    if (d < 80)  return "Fan ON";
    if (d < 150) return "Computer/TV ON";
    if (d < 300) return "Fridge ON";
    if (d < 600) return "Water Pump ON";
    if (d < 900) return "Washing Machine ON";
    return "AC / Geyser ON";
  }
  if (d < 15)  return "LED Bulb OFF";
  if (d < 40)  return "Tubelight OFF";
  if (d < 80)  return "Fan OFF";
  if (d < 150) return "Computer OFF";
  if (d < 300) return "Fridge OFF";
  return "Heavy Load OFF";
}

void runNILM(float W) {
  if (millis() - nilmMillis < 2000) return;
  nilmMillis = millis();
  nilmBuffer[nilmIndex % NILM_WINDOW] = W;
  nilmIndex++;
  if (nilmIndex >= NILM_WINDOW) {
    float avg = 0;
    for (int i = 0; i < NILM_WINDOW; i++) avg += nilmBuffer[i];
    avg /= NILM_WINDOW;
    float delta = avg - lastWatts;
    String newApp = detectAppliance(delta);
    if (newApp != detectedApp) {
      detectedApp = newApp;
      if (abs(delta) > 80) sendTelegram("Appliance: *" + detectedApp + "*\nChange: " + String(delta, 0) + "W");
    }
    lastWatts = avg;
  }
}

// ============================================================
//  AI FEATURE 2 — FORECASTING
// ============================================================

void updateForecast() {
  float weightedSum = 0, weightTotal = 0;
  int count = 0;
  avgDailyCost = 0;
  for (int i = 0; i < FORECAST_DAYS; i++) {
    if (dailyKWhLog[i] > 0) {
      float weight = i + 1;
      weightedSum  += dailyKWhLog[i] * unit_price * weight;
      weightTotal  += weight;
      avgDailyCost += dailyKWhLog[i] * unit_price;
      count++;
    }
  }
  if (count > 0) {
    forecastedBill = (weightedSum / weightTotal) * 30;
    avgDailyCost   = avgDailyCost / count;
  } else {
    struct tm ti;
    if (getLocalTime(&ti)) {
      int day = ti.tm_mday > 0 ? ti.tm_mday : 1;
      forecastedBill = (total_bill / day) * 30;
    }
  }
  anomalyAlert = (avgDailyCost > 0 && daily_cost > avgDailyCost * 1.5);
  if (anomalyAlert && millis() - lastAlertSent > 3600000) {
    lastAlertSent = millis();
    sendTelegram("*Anomaly Alert!*\nToday Rs " + String(daily_cost, 1) + " vs avg Rs " + String(avgDailyCost, 1));
  }
}

// ============================================================
//  WEB DASHBOARD — REDESIGNED
// ============================================================

String buildPage() {
  float pf = emon.powerFactor;
  if (pf < 0) pf = 0;
  if (pf > 1) pf = 1;

  float V   = (emon.Vrms < 50) ? 0 : emon.Vrms;
  float I   = (V == 0 || emon.Irms < 0.05) ? 0 : emon.Irms;
  float W   = V * I;
  float co2 = getCO2();
  float eco = getEcoScore();

  int barPct = (int)(W / 15);
  if (barPct > 100) barPct = 100;

  int budgetPct = (int)((daily_cost / (todays_allowance > 0 ? todays_allowance : 1)) * 100);
  if (budgetPct > 100) budgetPct = 100;

  // Status color
  String statusColor = "#00e5a0";
  if (systemStatus == "High Voltage!" || systemStatus == "Overload!") statusColor = "#ff4d4d";
  else if (systemStatus == "No Supply") statusColor = "#ffaa00";

  // Eco color
  String ecoColor = "#00e5a0";
  if (eco < 40) ecoColor = "#ff4d4d";
  else if (eco < 70) ecoColor = "#ffaa00";

  String h = "<!DOCTYPE html><html lang='en'><head>";
  h += "<meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<meta http-equiv='refresh' content='3'>";
  h += "<title>PowerPilot PRO</title>";
  h += "<link href='https://fonts.googleapis.com/css2?family=Rajdhani:wght@400;500;600;700&family=Share+Tech+Mono&display=swap' rel='stylesheet'>";

  h += "<style>";
  h += ":root{--bg:#0a0e1a;--surface:#111827;--surface2:#1a2236;--border:#1e2d45;--accent:#00e5a0;--accent2:#0066ff;--warn:#ffaa00;--danger:#ff4d4d;--text:#e8f0fe;--muted:#4a6080;}";
  h += "*{box-sizing:border-box;margin:0;padding:0;}";
  h += "body{background:var(--bg);color:var(--text);font-family:'Rajdhani',sans-serif;min-height:100vh;padding:16px;background-image:radial-gradient(ellipse at 20% 20%,rgba(0,229,160,0.04) 0%,transparent 50%),radial-gradient(ellipse at 80% 80%,rgba(0,102,255,0.04) 0%,transparent 50%);}";

  // Header
  h += ".header{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px;padding-bottom:16px;border-bottom:1px solid var(--border);}";
  h += ".logo{font-size:22px;font-weight:700;letter-spacing:2px;color:var(--accent);text-shadow:0 0 20px rgba(0,229,160,0.4);}";
  h += ".logo span{color:var(--text);font-weight:400;}";
  h += ".live-dot{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--muted);letter-spacing:1px;}";
  h += ".dot{width:8px;height:8px;border-radius:50%;background:var(--accent);animation:pulse 1.5s infinite;}";
  h += "@keyframes pulse{0%,100%{opacity:1;box-shadow:0 0 0 0 rgba(0,229,160,0.4);}50%{opacity:.7;box-shadow:0 0 0 6px rgba(0,229,160,0);}}";

  // Anomaly banner
  h += ".anomaly{background:linear-gradient(90deg,rgba(255,77,77,0.15),rgba(255,77,77,0.05));border:1px solid var(--danger);border-radius:10px;padding:12px 16px;margin-bottom:16px;display:flex;align-items:center;gap:10px;font-size:14px;color:var(--danger);font-weight:600;letter-spacing:1px;}";

  // Grid
  h += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:12px;}";
  h += ".full{grid-column:1/-1;}";

  // Cards
  h += ".card{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:14px 16px;position:relative;overflow:hidden;}";
  h += ".card::before{content:'';position:absolute;top:0;left:0;right:0;height:1px;background:linear-gradient(90deg,transparent,rgba(0,229,160,0.2),transparent);}";
  h += ".card-label{font-size:11px;font-weight:600;letter-spacing:2px;color:var(--muted);text-transform:uppercase;margin-bottom:6px;}";
  h += ".card-value{font-family:'Share Tech Mono',monospace;font-size:28px;color:var(--text);line-height:1;}";
  h += ".card-value.accent{color:var(--accent);}";
  h += ".card-sub{font-size:12px;color:var(--muted);margin-top:4px;font-family:'Share Tech Mono',monospace;}";

  // Big bill card
  h += ".bill-card{background:linear-gradient(135deg,#0a1628,#0d2040);border:1px solid rgba(0,102,255,0.3);border-radius:14px;padding:20px;position:relative;overflow:hidden;}";
  h += ".bill-card::after{content:'';position:absolute;bottom:-30px;right:-30px;width:120px;height:120px;border-radius:50%;background:rgba(0,102,255,0.08);pointer-events:none;}";
  h += ".bill-label{font-size:11px;letter-spacing:2px;color:rgba(100,160,255,0.6);text-transform:uppercase;margin-bottom:8px;}";
  h += ".bill-amount{font-family:'Share Tech Mono',monospace;font-size:42px;font-weight:400;color:#fff;line-height:1;}";
  h += ".bill-amount span{font-size:18px;color:rgba(255,255,255,0.5);margin-right:4px;}";
  h += ".bill-kwh{font-size:13px;color:rgba(100,160,255,0.6);margin-top:6px;font-family:'Share Tech Mono',monospace;}";

  // Progress bars
  h += ".bar-wrap{background:rgba(255,255,255,0.05);border-radius:6px;height:6px;overflow:hidden;margin-top:10px;}";
  h += ".bar-fill{height:6px;border-radius:6px;transition:width .5s ease;}";
  h += ".bar-fill.power{background:linear-gradient(90deg,var(--accent2),var(--accent));}";
  h += ".bar-fill.budget{background:linear-gradient(90deg,var(--accent),var(--warn));}";

  // Status badge
  h += ".status-badge{display:inline-flex;align-items:center;gap:6px;padding:6px 12px;border-radius:20px;font-size:12px;font-weight:600;letter-spacing:1px;}";

  // AI section
  h += ".ai-card{background:linear-gradient(135deg,#0a1a14,#0d1f2a);border:1px solid rgba(0,229,160,0.2);border-radius:14px;padding:14px 16px;}";
  h += ".ai-tag{display:inline-block;background:rgba(0,229,160,0.1);color:var(--accent);font-size:10px;letter-spacing:2px;padding:2px 8px;border-radius:4px;margin-bottom:8px;font-weight:600;}";
  h += ".ai-value{font-size:20px;font-weight:600;color:var(--text);margin-bottom:2px;}";

  // Divider
  h += ".section-title{font-size:11px;letter-spacing:3px;color:var(--muted);text-transform:uppercase;margin:16px 0 10px;padding-left:2px;}";

  // Inputs & buttons
  h += "input[type=number]{width:100%;background:var(--surface2);border:1px solid var(--border);border-radius:10px;padding:12px 14px;color:var(--text);font-family:'Rajdhani',sans-serif;font-size:15px;outline:none;margin-bottom:10px;}";
  h += "input[type=number]:focus{border-color:var(--accent);}";
  h += ".btn{display:block;width:100%;padding:14px;border:none;border-radius:10px;font-family:'Rajdhani',sans-serif;font-size:15px;font-weight:700;letter-spacing:2px;cursor:pointer;text-align:center;}";
  h += ".btn-primary{background:linear-gradient(135deg,#0055dd,#0077ff);color:#fff;}";
  h += ".btn-pay{background:linear-gradient(135deg,#00b87a,#00e5a0);color:#0a0e1a;}";
  h += ".btn-danger{background:linear-gradient(135deg,#cc2222,#ff4444);color:#fff;}";

  // Modal
  h += "#paybox{display:none;}#paybox:target{display:flex;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:100;align-items:center;justify-content:center;}";
  h += ".modal{background:var(--surface);border:1px solid var(--border);border-radius:20px;padding:28px 24px;width:90%;max-width:320px;text-align:center;}";
  h += ".modal-title{font-size:20px;font-weight:700;letter-spacing:2px;margin-bottom:6px;}";
  h += ".modal-amt{font-family:'Share Tech Mono',monospace;font-size:38px;color:var(--accent);margin:16px 0;}";
  h += ".modal-amt span{font-size:18px;color:var(--muted);}";

  h += "a{text-decoration:none;}";
  h += "</style></head><body>";

  // ---- HEADER ----
  h += "<div class='header'>";
  h += "<div class='logo'>POWER<span>PILOT</span></div>";
  h += "<div class='live-dot'><div class='dot'></div>LIVE</div>";
  h += "</div>";

  // ---- ANOMALY BANNER ----
  if (anomalyAlert) {
    h += "<div class='anomaly'>&#9888; ANOMALY DETECTED &mdash; Unusually high consumption today</div>";
  }

  // ---- BILL CARD ----
  h += "<div class='bill-card full' style='margin-bottom:12px;'>";
  h += "<div class='bill-label'>TOTAL BILL THIS MONTH</div>";
  h += "<div class='bill-amount'><span>Rs</span>" + String(total_bill, 1) + "</div>";
  h += "<div class='bill-kwh'>" + String(kWh, 4) + " kWh consumed</div>";
  h += "</div>";

  // ---- LIVE READINGS ----
  h += "<div class='section-title'>Live Readings</div>";
  h += "<div class='grid'>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Voltage</div>";
  h += "<div class='card-value'>" + String((int)V) + "<span style='font-size:14px;color:var(--muted)'>V</span></div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Current</div>";
  h += "<div class='card-value'>" + String(I, 2) + "<span style='font-size:14px;color:var(--muted)'>A</span></div>";
  h += "</div>";

  h += "<div class='card full'>";
  h += "<div class='card-label'>Live Power</div>";
  h += "<div style='display:flex;align-items:baseline;gap:8px;'>";
  h += "<div class='card-value accent'>" + String((int)W) + "</div>";
  h += "<div style='font-size:14px;color:var(--muted);'>Watts</div>";
  h += "</div>";
  h += "<div class='bar-wrap'><div class='bar-fill power' style='width:" + String(barPct) + "%'></div></div>";
  h += "</div>";

  h += "</div>"; // end grid

  // ---- LOAD ANALYSIS ----
  h += "<div class='section-title'>Load Analysis</div>";
  h += "<div class='grid'>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Load Type</div>";
  h += "<div class='card-value' style='font-size:20px;'>" + loadType + "</div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Power Factor</div>";
  h += "<div class='card-value accent'>" + String(pf, 2) + "</div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Peak Hour</div>";
  h += "<div class='card-value' style='font-size:20px;'>" + peakText() + "</div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Peak Load</div>";
  h += "<div class='card-value'>" + String((int)peakLoad) + "<span style='font-size:14px;color:var(--muted)'>W</span></div>";
  h += "</div>";

  h += "</div>"; // end grid

  // ---- BUDGET & ECO ----
  h += "<div class='section-title'>Budget & Eco</div>";
  h += "<div class='grid'>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Today Spent</div>";
  h += "<div class='card-value' style='font-size:22px;'>Rs " + String(daily_cost, 1) + "</div>";
  h += "<div class='card-sub'>of Rs " + String(todays_allowance, 1) + " allowed</div>";
  h += "<div class='bar-wrap'><div class='bar-fill budget' style='width:" + String(budgetPct) + "%'></div></div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Eco Score</div>";
  h += "<div class='card-value' style='color:" + ecoColor + ";font-size:30px;'>" + String((int)eco) + "<span style='font-size:14px;color:var(--muted)'>/100</span></div>";
  h += "<div class='card-sub'>CO2: " + String(co2, 2) + " kg</div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Monthly Budget</div>";
  h += "<div class='card-value' style='font-size:22px;'>Rs " + String((int)monthly_budget) + "</div>";
  h += "</div>";

  h += "<div class='card'>";
  h += "<div class='card-label'>Rollover</div>";
  h += "<div class='card-value' style='font-size:22px;" + String(rollover_balance < 0 ? "color:var(--danger)" : "color:var(--accent)") + ";'>Rs " + String((int)rollover_balance) + "</div>";
  h += "</div>";

  h += "</div>"; // end grid

  // ---- AI FEATURES ----
  h += "<div class='section-title'>AI Intelligence</div>";

  h += "<div class='ai-card full' style='margin-bottom:12px;'>";
  h += "<div class='ai-tag'>NILM &bull; APPLIANCE DETECTION</div>";
  h += "<div class='ai-value'>" + detectedApp + "</div>";
  h += "<div style='font-size:12px;color:var(--muted);'>Non-Intrusive Load Monitoring</div>";
  h += "</div>";

  h += "<div class='grid'>";

  h += "<div class='ai-card'>";
  h += "<div class='ai-tag'>AI FORECAST</div>";
  h += "<div class='ai-value'>Rs " + String((int)forecastedBill) + "</div>";
  h += "<div style='font-size:12px;color:var(--muted);'>Predicted month-end bill</div>";
  h += "</div>";

  h += "<div class='ai-card'>";
  h += "<div class='ai-tag'>SYSTEM</div>";
  h += "<div style='display:flex;align-items:center;gap:6px;margin:4px 0;'>";
  h += "<div style='width:8px;height:8px;border-radius:50%;background:" + statusColor + ";box-shadow:0 0 8px " + statusColor + ";'></div>";
  h += "<div class='ai-value'>" + systemStatus + "</div>";
  h += "</div>";
  h += "<div style='font-size:12px;color:var(--muted);'>Real-time safety monitor</div>";
  h += "</div>";

  h += "</div>"; // end grid

  // ---- CONTROLS ----
  h += "<div class='section-title'>Controls</div>";

  h += "<div class='card full' style='margin-bottom:12px;'>";
  h += "<form action='/setBudget'>";
  h += "<input type='number' name='v' placeholder='Set monthly budget (Rs)'>";
  h += "<button type='submit' class='btn btn-primary'>SAVE BUDGET</button>";
  h += "</form></div>";

  h += "<a href='#paybox'><div class='btn btn-pay' style='margin-bottom:20px;'>PAY BILL &mdash; Rs " + String(total_bill, 1) + "</div></a>";

  // ---- PAYMENT MODAL ----
  h += "<div id='paybox'>";
  h += "<div class='modal'>";
  h += "<div class='modal-title'>SECURE CHECKOUT</div>";
  h += "<div style='font-size:12px;color:var(--muted);letter-spacing:1px;'>AMOUNT DUE</div>";
  h += "<div class='modal-amt'><span>Rs </span>" + String(total_bill, 1) + "</div>";
  h += "<form action='/pay' style='margin-bottom:10px;'><button type='submit' class='btn btn-pay'>CONFIRM PAYMENT</button></form>";
  h += "<a href='/'><button class='btn btn-danger'>CANCEL</button></a>";
  h += "</div></div>";

  h += "</body></html>";
  return h;
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcdMsg("PowerPilot v57", "Starting...");

  WiFi.begin(ssid, pass);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(19800, 0, "pool.ntp.org");
    myIP = WiFi.localIP().toString();

    if (MDNS.begin("powerpilot")) {
      MDNS.addService("http", "tcp", 80);
    }

    lcdMsg("Open Browser:", myIP);
    delay(3000);

    Serial.println("Dashboard: http://" + myIP);
    Serial.println("Also try:  http://powerpilot.local");

    sendTelegram("*PowerPilot v57 Online!*\nIP: " + myIP + "\nSend /status for live readings.");
    telegramReady = true;

  } else {
    lcdMsg("WiFi Failed", "Meter Mode");
    delay(1500);
  }

  preferences.begin("eco", false);
  kWh              = preferences.getFloat("kWh",    0);
  monthly_budget   = preferences.getFloat("budget", 500);
  todays_allowance = preferences.getFloat("allow",  16.6);
  rollover_balance = preferences.getFloat("roll",   0);
  txLog            = preferences.getString("tx",    "");
  peakLoad         = preferences.getFloat("peak",   0);
  peakHour         = preferences.getInt("ph",       0);
  forecastIndex    = preferences.getInt("fi",       0);
  for (int i = 0; i < FORECAST_DAYS; i++) {
    dailyKWhLog[i] = preferences.getFloat(("dk" + String(i)).c_str(), 0);
  }

  emon.voltage(VOLT_PIN,  234.26, 1.7);
  emon.current(AMPS_PIN,  60.6);

  struct tm ti;
  if (getLocalTime(&ti)) last_day = ti.tm_mday;

  server.on("/", []() {
    server.send(200, "text/html", buildPage());
  });

  server.on("/setBudget", []() {
    if (server.hasArg("v")) {
      monthly_budget   = server.arg("v").toFloat();
      todays_allowance = monthly_budget / 30.0;
      preferences.putFloat("budget", monthly_budget);
      preferences.putFloat("allow",  todays_allowance);
      shortBeep();
      lcdMsg("Budget Saved", "Rs " + String((int)monthly_budget));
      delay(1000);
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/pay", []() {
    txLog = "Paid Rs " + String(total_bill, 0) + "<br>" + txLog;
    if (txLog.length() > 250) txLog = txLog.substring(0, 250);
    preferences.putString("tx", txLog);

    sendTelegram("Bill Paid! Rs " + String(total_bill, 0) + " — Meter reset.");

    kWh        = 0;
    total_bill = 0;
    daily_cost = 0;
    preferences.putFloat("kWh", 0);

    shortBeep(300);
    lcdMsg("Payment Done", "Bill Reset");
    delay(1200);

    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.begin();
  lastMillis = millis();
}

// ============================================================
//  LOOP
// ============================================================

void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();

  checkTelegramCommands();

  emon.calcVI(20, 2000);

  float V  = (emon.Vrms < 50) ? 0 : emon.Vrms;
  float I  = (V == 0 || emon.Irms < 0.05) ? 0 : emon.Irms;
  float PF = emon.powerFactor;
  if (PF < 0) PF = 0;
  if (PF > 1) PF = 1;

  float W = V * I;

  if (V > 50) {
    if (W < 10)         loadType = "No Load";
    else if (PF > 0.94) loadType = "Resistive";
    else if (PF < 0.85) loadType = "Inductive";
    else                loadType = "Mixed";
  } else {
    loadType = "No Supply";
  }

  runNILM(W);

  struct tm ti;
  if (getLocalTime(&ti)) {
    if (W > peakLoad) {
      peakLoad = W;
      peakHour = ti.tm_hour;
      preferences.putFloat("peak", peakLoad);
      preferences.putInt("ph",    peakHour);
    }
  }

  if (V > 285 || I > 15) {
    systemStatus = (V > 285) ? "High Voltage!" : "Overload!";
    if (!buzzerLock) {
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerMillis = millis();
      buzzerOn     = true;
      buzzerLock   = true;
      if (millis() - lastAlertSent > 60000) {
        lastAlertSent = millis();
        sendTelegram("*SAFETY ALERT!*\n" + systemStatus + "\nV:" + String(V, 0) + " I:" + String(I, 2));
      }
    }
  } else {
    systemStatus = (V == 0) ? "No Supply" : "Normal";
    buzzerLock   = false;
  }

  if (buzzerOn && millis() - buzzerMillis > 3000) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOn = false;
  }

  // ---- FIXED kWh formula ----
  unsigned long now = millis();
  float delta = (W * (float)(now - lastMillis)) / 1000.0 / 3600000.0;
  kWh        += delta;
  daily_cost += delta * unit_price;
  total_bill  = kWh * unit_price;
  lastMillis  = now;

  // ---- Daily rollover ----
  if (getLocalTime(&ti)) {
    if (last_day != 0 && ti.tm_mday != last_day) {
      float todayKWh = daily_cost / unit_price;
      dailyKWhLog[forecastIndex % FORECAST_DAYS] = todayKWh;
      forecastIndex++;
      preferences.putInt("fi", forecastIndex);
      for (int i = 0; i < FORECAST_DAYS; i++) {
        preferences.putFloat(("dk" + String(i)).c_str(), dailyKWhLog[i]);
      }

      float leftover   = todays_allowance - daily_cost;
      rollover_balance = leftover;
      todays_allowance = (monthly_budget / 30.0) + rollover_balance;
      if (todays_allowance < 0) todays_allowance = 0;

      daily_cost = 0;
      peakLoad   = 0;
      peakHour   = 0;

      preferences.putFloat("roll",  rollover_balance);
      preferences.putFloat("allow", todays_allowance);
      preferences.putFloat("peak",  0);
      preferences.putInt("ph",      0);

      last_day = ti.tm_mday;
      updateForecast();
    }
  }

  // ---- LCD ----
  if (now - lcdMillis > 4000) {
    lcdMillis  = now;
    lcdScreen  = (lcdScreen + 1) % 8;
    lcd.clear();

    float co2 = getCO2();
    int   eco = (int)getEcoScore();

    if (lcdScreen == 0) {
      lcd.print("Volt:"); lcd.print((int)V); lcd.print("V");
      lcd.setCursor(0, 1);
      lcd.print("Curr:"); lcd.print(I, 1); lcd.print("A");
    } else if (lcdScreen == 1) {
      lcd.print("Bill:"); lcd.print((int)total_bill); lcd.print("Rs");
      lcd.setCursor(0, 1);
      lcd.print("Unit:"); lcd.print(kWh, 2); lcd.print("kWh");
    } else if (lcdScreen == 2) {
      lcd.print("Load:"); lcd.print(loadType.substring(0, 11));
      lcd.setCursor(0, 1);
      lcd.print("PF:"); lcd.print(PF, 2);
    } else if (lcdScreen == 3) {
      lcd.print("Bud:"); lcd.print((int)monthly_budget); lcd.print("Rs");
      lcd.setCursor(0, 1);
      lcd.print("Roll:"); lcd.print((int)rollover_balance); lcd.print("Rs");
    } else if (lcdScreen == 4) {
      lcd.print("CO2:"); lcd.print(co2, 1); lcd.print("kg");
      lcd.setCursor(0, 1);
      lcd.print("Eco:"); lcd.print(eco); lcd.print("/100");
    } else if (lcdScreen == 5) {
      lcd.print("AI Appliance:");
      lcd.setCursor(0, 1);
      lcd.print(detectedApp.substring(0, 16));
    } else if (lcdScreen == 6) {
      lcd.print("Forecast Bill:");
      lcd.setCursor(0, 1);
      lcd.print("Rs "); lcd.print((int)forecastedBill);
      if (anomalyAlert) lcd.print(" !");
    } else {
      lcd.print(systemStatus.substring(0, 16));
      lcd.setCursor(0, 1);
      lcd.print(myIP.length() > 0 ? myIP : "powerpilot.local");
    }
  }

  // ---- Auto Save ----
  if (now - saveMillis > 30000) {
    preferences.putFloat("kWh",   kWh);
    preferences.putFloat("roll",  rollover_balance);
    preferences.putFloat("allow", todays_allowance);
    preferences.putString("tx",   txLog);
    preferences.putFloat("peak",  peakLoad);
    preferences.putInt("ph",      peakHour);
    saveMillis = now;
  }
}
