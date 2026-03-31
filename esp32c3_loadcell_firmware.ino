#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <math.h> 
#include "HX711.h"

const int LOADCELL_DOUT_PIN = D0;
const int LOADCELL_SCK_PIN = D1;

HX711 scale;
Adafruit_MCP4725 dac;
WebServer server(80);
Preferences prefs;

long minReading, maxReading;
float brakeGamma, deadzone;
bool isInverted;
long currentRaw = 0;
int currentDAC = 0;

void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Pro Brake Hub</title><style>";
  html += "body{font-family:'Segoe UI', sans-serif; background:#050505; color:#f0f0f0; text-align:center; padding:15px;}";
  html += ".container{max-width:400px; margin:0 auto; background:#111; padding:25px; border-radius:12px; border:1px solid #222;}";
  html += "h1{font-size:1.2em; color:#ff3e3e; letter-spacing:2px; margin-bottom:20px;}";
  html += ".bar-bg{background:#222; border-radius:3px; height:30px; width:100%; margin:15px 0; border:1px solid #333;}";
  html += ".bar-fill{background:linear-gradient(90deg, #ff3e3e, #ff8e3e); height:100%; width:0%; transition:width 0.05s;}";
  html += "canvas{background:#0a0a0a; border:1px solid #333; margin:15px 0; border-radius:4px;}";
  html += "button{background:#ff3e3e; border:none; color:white; padding:12px 0; border-radius:4px; cursor:pointer; margin:8px 0; font-weight:bold; width:100%; text-transform:uppercase;}";
  html += "input[type=range]{width:100%; margin:10px 0; accent-color:#ff3e3e;}";
  html += ".label{text-align:left; font-size:0.8em; font-weight:bold; color:#888; text-transform:uppercase;}";
  html += ".stats{display:flex; justify-content:space-between; font-family:monospace; color:#666; font-size:0.9em; margin-bottom:5px;}";
  html += ".toggle-box{display:flex; justify-content:space-between; align-items:center; background:#1a1a1a; padding:8px; border-radius:4px; margin:10px 0;}";
  html += "</style><script>";
  
  html += "let gamma = 1.0; let dz = 0.0;";
  html += "function drawCurve() {";
  html += "  const ctx = document.getElementById('curveCanvas').getContext('2d');";
  html += "  ctx.clearRect(0,0,200,100); ctx.strokeStyle='#444'; ctx.beginPath(); ctx.moveTo(0,100); ctx.lineTo(200,0); ctx.stroke();"; // Linia referencyjna
  html += "  ctx.strokeStyle='#ff3e3e'; ctx.lineWidth=2; ctx.beginPath(); ctx.moveTo(0,100);";
  html += "  for(let x=0; x<=1; x+=0.05) {";
  html += "    let nx = (x < dz) ? 0 : (x - dz) / (1 - dz);";
  html += "    let y = Math.pow(nx, gamma);";
  html += "    ctx.lineTo(x*200, 100 - (y*100));";
  html += "  } ctx.stroke();";
  html += "}";

  html += "setInterval(function(){fetch('/data').then(r=>r.json()).then(d=>{";
  html += "  document.getElementById('fill').style.width = (d.dac/40.95) + '%';";
  html += "  document.getElementById('raw').innerText = d.raw; gamma = d.gamma; dz = d.dz;";
  html += "  document.getElementById('dac_val').innerText = Math.round(d.dac/40.95) + '%';";
  html += "  document.getElementById('g_val').innerText = d.gamma; drawCurve();";
  html += "});}, 100);";
  
  html += "function updateGamma(v){fetch('/setGamma?val='+v);} function updateDZ(v){fetch('/setDZ?val='+v);}";
  html += "function setCal(t){fetch('/calibrate?type='+t);}";
  html += "</script></head><body onload='drawCurve()'><div class='container'><h1>BRAKE SYSTEM PRO</h1>";
  
  html += "<div class='stats'><div>OUTPUT: <span id='dac_val'>0%</span></div><div>RAW: <span id='raw'>0</span></div></div>";
  html += "<div class='bar-bg'><div id='fill' class='bar-fill'></div></div>";
  
  html += "<div class='label'>Transfer Function (LUT)</div><canvas id='curveCanvas' width='200' height='100'></canvas>";
  
  html += "<div class='label'>Linearity (Gamma): <span id='g_val' style='color:#ff3e3e'>1.0</span></div>";
  html += "<input type='range' min='0.5' max='3.0' step='0.1' value='1.0' onchange='updateGamma(this.value)'>";
  
  html += "<div class='label'>Deadzone Threshold</div>";
  html += "<input type='range' min='0.0' max='0.2' step='0.01' value='0.0' onchange='updateDZ(this.value)'>";
  
  html += "<div class='toggle-box'><div class='label' style='margin:0'>Invert Signal</div><input type='checkbox' id='inv' onchange='fetch(\"/setInvert?val=\"+(this.checked?1:0))'></div>";
  
  html += "<button onclick=\"setCal('min')\">Calibrate Idle (0%)</button>";
  html += "<button onclick=\"setCal('max')\">Calibrate Peak (100%)</button>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

// Pozostałe funkcje (handleData, handleSetGamma, itp.) pozostają bez zmian względem poprzedniego kodu
void handleData() {
  String json = "{\"raw\":" + String(currentRaw) + ",\"dac\":" + String(currentDAC);
  json += ",\"gamma\":" + String(brakeGamma, 1) + ",\"dz\":" + String(deadzone, 2);
  json += ",\"inv\":" + String(isInverted ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleSetInvert() { isInverted = server.arg("val") == "1"; prefs.putBool("inv", isInverted); server.send(200, "text/plain", "OK"); }
void handleSetDZ() { deadzone = server.arg("val").toFloat(); prefs.putFloat("dz", deadzone); server.send(200, "text/plain", "OK"); }
void handleSetGamma() { brakeGamma = server.arg("val").toFloat(); prefs.putFloat("gamma", brakeGamma); server.send(200, "text/plain", "OK"); }
void handleCalibrate() {
  String type = server.arg("type");
  if(type == "min") { minReading = currentRaw; prefs.putLong("min", minReading); }
  if(type == "max") { maxReading = currentRaw; prefs.putLong("max", maxReading); }
  server.send(200, "text/plain", "OK");
}

void setup() {
    Serial.begin(115200); Wire.begin(); dac.begin(0x60);
    prefs.begin("brake_cal", false);
    minReading = prefs.getLong("min", 0); maxReading = prefs.getLong("max", 100000);
    brakeGamma = prefs.getFloat("gamma", 1.0); deadzone = prefs.getFloat("dz", 0.0);
    isInverted = prefs.getBool("inv", false);

    WiFiManager wm;
    if (wm.autoConnect("G923_Brake_Hub")) {
        MDNS.begin("logitech");
        server.on("/", handleRoot); server.on("/data", handleData);
        server.on("/calibrate", handleCalibrate); server.on("/setGamma", handleSetGamma);
        server.on("/setDZ", handleSetDZ); server.on("/setInvert", handleSetInvert);
        server.begin();
    }
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) server.handleClient();
    if (scale.is_ready()) {
        currentRaw = scale.get_value(5);
        float normalized = (float)(currentRaw - minReading) / (float)(maxReading - minReading);
        if (isInverted) normalized = 1.0f - normalized;
        if (normalized < deadzone) normalized = 0.0;
        else normalized = (normalized - deadzone) / (1.0 - deadzone);
        normalized = constrain(normalized, 0.0, 1.0);
        float curved = pow(normalized, brakeGamma);
        currentDAC = (int)(curved * 4095.0);
        currentDAC = constrain(currentDAC, 0, 4095);
        dac.setVoltage(currentDAC, false);
    }
}