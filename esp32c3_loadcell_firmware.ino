#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <ElegantOTA.h>
#include "HX711.h"

// ==========================================
// KONFIGURACJA PINÓW
// ==========================================
const int LOADCELL_DOUT_PIN = D2; 
const int LOADCELL_SCK_PIN = D1;  
const int I2C_SDA_PIN = D9;       
const int I2C_SCL_PIN = D8;       

// ==========================================
// OBIEKTY GLOBALNE
// ==========================================
HX711 scale;
Adafruit_MCP4725 dac;
WebServer server(80);
Preferences prefs;

// ==========================================
// ZMIENNE ROBOCZE I KONFIGURACYJNE
// ==========================================
long minReading, maxReading;
float deadzone;
bool isInverted;
long currentRaw = 0;
int currentDAC = 0;
float curve[5] = {0.0, 0.25, 0.50, 0.75, 1.0};

// ==========================================
// KOD HTML / CSS / JS
// ==========================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Smartgrid Brake Hub</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:#050505;color:#f0f0f0;text-align:center;padding:15px;margin:0}
.container{max-width:450px;margin:0 auto;background:#111;padding:20px;border-radius:12px;border:1px solid #222}
h1{font-size:1.5em;color:#00ffff;letter-spacing:2px;margin-bottom:20px;text-transform:uppercase}
.val-box{background:#1a1a1a;padding:12px;border-radius:8px;margin:8px 0;border-left:4px solid #00ffff;display:flex;justify-content:space-between;align-items:center}
.val-box span{font-size:1.4em;font-weight:bold;color:#fff}
.label-txt{font-size:0.9em;color:#888}
button{background:#222;color:#fff;border:1px solid #444;padding:12px 20px;border-radius:6px;cursor:pointer;width:100%;margin:6px 0;font-size:0.9em;font-weight:bold;transition:0.2s}
button:hover{background:#00ffff;border-color:#00ffff;color:#000}
.btn-save{background:#00ffff;color:#000;border-color:#00ffff;margin-top:20px;font-size:1.1em}
.btn-save:hover{background:#00cccc;border-color:#00cccc}
.section-title{text-align:left;font-size:0.8em;color:#666;text-transform:uppercase;margin-top:20px;margin-bottom:10px;border-bottom:1px solid #333;padding-bottom:5px}
.slider-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px}
.slider-row label{width:60px;text-align:left;font-size:0.8em;color:#aaa}
.slider-row span{width:45px;text-align:right;font-size:0.8em;font-weight:bold;color:#00ffff}
input[type=range]{flex-grow:1;margin:0 10px;accent-color:#00ffff}
canvas{width:100%;height:200px;background:#1a1a1a;border-radius:8px;border:1px solid #333;margin-bottom:15px;display:block}
</style></head><body>
<div class='container'>
  <h1>SMARTGRID BRAKE HUB</h1>
  <div class='val-box'><div class='label-txt'>RAW ADC INPUT</div><span id='raw'>0</span></div>
  <div class='val-box'><div class='label-txt'>DAC OUTPUT</div><span id='dac'>0</span></div>
  <button onclick='cal("min")'>1. SET MIN (REST)</button>
  <button onclick='cal("max")'>2. SET MAX (FULL)</button>
  <button onclick='inv()' id='invBtn'>INVERT: OFF</button>
  <div class='section-title'>Deadzone</div>
  <div class='slider-row'><label>Start</label><input type='range' id='dz' min='0' max='50' step='1' oninput='document.getElementById("dzVal").innerText=this.value+"%"'><span id='dzVal'>0%</span></div>
  <div class='section-title'>Brake Force Curve</div>
  <canvas id="curveChart" width="400" height="200"></canvas>
  <div class='slider-row'><label>0% In</label><input type='range' id='c0' min='0' max='100' step='1' oninput='updateCurve(0, this.value)'><span id='cv0'>0%</span></div>
  <div class='slider-row'><label>25% In</label><input type='range' id='c1' min='0' max='100' step='1' oninput='updateCurve(1, this.value)'><span id='cv1'>25%</span></div>
  <div class='slider-row'><label>50% In</label><input type='range' id='c2' min='0' max='100' step='1' oninput='updateCurve(2, this.value)'><span id='cv2'>50%</span></div>
  <div class='slider-row'><label>75% In</label><input type='range' id='c3' min='0' max='100' step='1' oninput='updateCurve(3, this.value)'><span id='cv3'>75%</span></div>
  <div class='slider-row'><label>100% In</label><input type='range' id='c4' min='0' max='100' step='1' oninput='updateCurve(4, this.value)'><span id='cv4'>100%</span></div>
  <button class="btn-save" onclick='saveAll()' id='saveBtn'>SAVE SETTINGS</button>
</div>
<script>
window.addEventListener('load', function() {
  drawChart(); 
  fetch('/getConfig').then(r => r.json()).then(d => {
    document.getElementById('dz').value = Math.round(d.dz * 100);
    document.getElementById('dzVal').innerText = Math.round(d.dz * 100) + '%';
    for(let i=0; i<5; i++) {
      document.getElementById('c'+i).value = Math.round(d.curve[i] * 100);
      document.getElementById('cv'+i).innerText = Math.round(d.curve[i] * 100) + '%';
    }
    document.getElementById('invBtn').innerText = 'INVERT: ' + (d.inv ? 'ON' : 'OFF');
    document.getElementById('invBtn').style.borderColor = d.inv ? '#00ffff' : '#444';
    drawChart(); 
  }).catch(e => console.log("Config error:", e));
});
setInterval(() => {
  fetch('/getLive').then(r => r.json()).then(d => {
    document.getElementById('raw').innerText = d.raw;
    document.getElementById('dac').innerText = d.dac;
  }).catch(e => {}); 
}, 1000); 
function updateCurve(idx, val) {
  document.getElementById('cv'+idx).innerText = val + '%';
  requestAnimationFrame(drawChart); 
}
function drawChart() {
  const canvas = document.getElementById('curveChart');
  if(!canvas) return; 
  const ctx = canvas.getContext('2d');
  const w = 400; const h = 200; canvas.width = w; canvas.height = h;
  ctx.clearRect(0, 0, w, h); 
  let vals = [
    parseInt(document.getElementById('c0').value) || 0,
    parseInt(document.getElementById('c1').value) || 0,
    parseInt(document.getElementById('c2').value) || 0,
    parseInt(document.getElementById('c3').value) || 0,
    parseInt(document.getElementById('c4').value) || 0
  ];
  ctx.strokeStyle = '#333'; ctx.lineWidth = 1; ctx.beginPath();
  for(let i=1; i<4; i++) { let x = i * (w/4); ctx.moveTo(x, 0); ctx.lineTo(x, h); }
  for(let i=1; i<4; i++) { let y = i * (h/4); ctx.moveTo(0, y); ctx.lineTo(w, y); }
  ctx.stroke();
  ctx.strokeStyle = '#00ffff'; ctx.lineWidth = 4; ctx.beginPath();
  for(let i=0; i<5; i++) {
    let x = i * (w/4); let y = h - (vals[i] / 100.0) * h; 
    if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
  ctx.fillStyle = '#111'; ctx.strokeStyle = '#00ffff'; ctx.lineWidth = 2;
  for(let i=0; i<5; i++) {
    let x = i * (w/4); let y = h - (vals[i] / 100.0) * h;
    ctx.beginPath(); ctx.arc(x, y, 6, 0, Math.PI*2); ctx.fill(); ctx.stroke();
  }
}
function cal(t) { fetch('/calibrate?type=' + t); }
function inv() { fetch('/setInvert').then(()=>location.reload()); }
function saveAll() {
  const btn = document.getElementById('saveBtn'); btn.innerText = "SAVING...";
  let dz = document.getElementById('dz').value / 100.0; let params = `dz=${dz}`;
  for(let i=0; i<5; i++) { params += `&c${i}=${document.getElementById('c'+i).value / 100.0}`; }
  fetch(`/saveAll?${params}`).then(r => {
    if(r.ok) { btn.innerText = "SAVED!"; setTimeout(() => btn.innerText = "SAVE SETTINGS", 2000); }
  });
}
</script></body></html>
)rawliteral";

// ==========================================
// FUNKCJA INTERPOLACJI (5-PUNKTOWA KRZYWA)
// ==========================================
float applyCurve(float x) {
    if (x <= 0.0f) return curve[0];
    if (x >= 1.0f) return curve[4];

    int index = (int)(x * 4.0f); 
    if (index > 3) index = 3;

    float x0 = index * 0.25f;
    float y0 = curve[index];
    float y1 = curve[index + 1];

    float t = (x - x0) / 0.25f;
    return y0 + t * (y1 - y0);
}

// ==========================================
// ENDPOINTY SERWERA HTTP
// ==========================================
void handleRoot() { server.send(200, "text/html", htmlPage); }

void handleLive() { 
    String json = "{\"raw\":"; json += currentRaw;
    json += ",\"dac\":"; json += currentDAC;
    json += "}";
    server.send(200, "application/json", json);
}

void handleConfig() { 
    String json = "{\"dz\":"; json += deadzone;
    json += ",\"inv\":"; json += (isInverted ? "true" : "false");
    json += ",\"curve\":[";
    for(int i=0; i<5; i++) {
        json += curve[i];
        if(i<4) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleCalibrate() {
    String type = server.arg("type");
    if (type == "min") { 
        minReading = scale.read_average(10); 
        prefs.putLong("min", minReading); 
    }
    if (type == "max") { 
        maxReading = scale.read_average(10); 
        prefs.putLong("max", maxReading); 
    }
    server.send(200, "text/plain", "OK");
}

void handleSetInvert() {
    isInverted = !isInverted;
    prefs.putBool("inv", isInverted);
    server.send(200, "text/plain", "OK");
}

void handleSaveAll() {
    if (server.hasArg("dz")) {
        deadzone = server.arg("dz").toFloat();
        prefs.putFloat("dz", deadzone);
    }
    for (int i=0; i<5; i++) {
        String argName = "c" + String(i);
        if (server.hasArg(argName)) {
            curve[i] = server.arg(argName).toFloat();
            String key = "c" + String(i);
            prefs.putFloat(key.c_str(), curve[i]);
        }
    }
    server.send(200, "text/plain", "OK");
}

// ==========================================
// SETUP (INICJALIZACJA)
// ==========================================
void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA); // Wymuszenie trybu stacji dla czystego startu radia

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); 
    Wire.setClock(400000); 
    
    if(!dac.begin(0x60)) {
       Serial.println("BLAD: MCP4725");
    }

    prefs.begin("brake_cal", false);
    minReading = prefs.getLong("min", 0); 
    maxReading = prefs.getLong("max", 100000);
    deadzone = prefs.getFloat("dz", 0.0);
    isInverted = prefs.getBool("inv", false);
    
    for(int i=0; i<5; i++) {
        String key = "c" + String(i);
        curve[i] = prefs.getFloat(key.c_str(), i * 0.25f); 
    }

    WiFiManager wm;
    
    // --- NOWE USTAWIENIA WIFI MANAGER ---
    // Zwiększamy próg cierpliwości mikrokontrolera. Dajemy mu 40 sekund na 
    // połączenie się z routerem zanim w panice uruchomi swój AP.
    wm.setConnectTimeout(40); 
    // ------------------------------------

    if (wm.autoConnect("G923_Brake_Hub")) {
        MDNS.begin("logitech");
        
        server.on("/", handleRoot); 
        server.on("/getLive", handleLive); 
        server.on("/getConfig", handleConfig); 
        server.on("/calibrate", handleCalibrate); 
        server.on("/setInvert", handleSetInvert);
        server.on("/saveAll", handleSaveAll); 
        
        ElegantOTA.begin(&server); 
        server.begin();
    }
    
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
}

// ==========================================
// GŁÓWNA PĘTLA CZASU RZECZYWISTEGO
// ==========================================
void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        server.handleClient();
        ElegantOTA.loop(); 
    }

    if (scale.is_ready()) {
        currentRaw = scale.read();
        
        long range = maxReading - minReading;
        if (range == 0) range = 1; 

        long clampedRaw = currentRaw;
        if (clampedRaw < minReading) clampedRaw = minReading;
        if (clampedRaw > maxReading) clampedRaw = maxReading;

        float normalized = (float)(clampedRaw - minReading) / (float)range;
        if (isInverted) normalized = 1.0f - normalized;

        if (normalized <= deadzone) {
            normalized = 0.0f;
        } else {
            normalized = (normalized - deadzone) / (1.0f - deadzone);
            normalized = applyCurve(normalized);
        }

        if (normalized > 1.0f) normalized = 1.0f;
        if (normalized < 0.0f) normalized = 0.0f;

        currentDAC = (int)(normalized * 4095.0f);
        dac.setVoltage(currentDAC, false);
    }
}