#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <math.h> 
#include "HX711.h"

// ==========================================
// ZWYCIĘSKA KONFIGURACJA PINÓW (XIAO ESP32-C3)
// ==========================================
const int LOADCELL_DOUT_PIN = D2; // Linia danych z HX711
const int LOADCELL_SCK_PIN = D1;  // Linia zegara do HX711
const int I2C_SDA_PIN = D9;       // Linia danych I2C dla DAC MCP4725
const int I2C_SCL_PIN = D8;       // Linia zegara I2C dla DAC MCP4725

// ==========================================
// OBIEKTY GLOBALNE
// ==========================================
HX711 scale;
Adafruit_MCP4725 dac;
WebServer server(80);
Preferences prefs;

// ==========================================
// ZMIENNE KONFIGURACYJNE I ROBOCZE
// ==========================================
long minReading, maxReading;
float brakeGamma, deadzone;
bool isInverted;
long currentRaw = 0;
int currentDAC = 0;

// ==========================================
// KOD HTML / CSS / JS (Wbudowany interfejs WWW)
// ==========================================
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Pro Brake Hub</title>
  <style>
    body {font-family:'Segoe UI', sans-serif; background:#050505; color:#f0f0f0; text-align:center; padding:15px;}
    .container {max-width:400px; margin:0 auto; background:#111; padding:25px; border-radius:12px; border:1px solid #222;}
    h1 {font-size:1.2em; color:#ff3e3e; letter-spacing:2px; margin-bottom:20px;}
    .val-box {background:#1a1a1a; padding:15px; border-radius:8px; margin:10px 0; border-left:4px solid #ff3e3e;}
    .val-box span {font-size:1.5em; font-weight:bold; color:#fff;}
    button {background:#333; color:#fff; border:1px solid #444; padding:12px 20px; border-radius:6px; cursor:pointer; width:100%; margin:8px 0; font-size:1em; transition:0.2s;}
    button:hover {background:#ff3e3e; border-color:#ff3e3e;}
    input[type=range] {width:100%; margin:15px 0;}
    .slider-container {text-align:left; margin-top:20px; padding-top:20px; border-top:1px solid #333;}
  </style>
</head>
<body>
  <div class='container'>
    <h1>G923 BRAKE HUB</h1>
    <div class='val-box'>RAW ADC<br><span id='raw'>0</span></div>
    <div class='val-box' style='border-color:#00ff88'>DAC OUT (0-4095)<br><span id='dac'>0</span></div>
    
    <button onclick='cal("min")'>SET MIN (REST)</button>
    <button onclick='cal("max")'>SET MAX (FULL)</button>
    <button onclick='inv()' id='invBtn'>INVERT: OFF</button>
    
    <div class='slider-container'>
      <label>Gamma Curve: <span id='gVal'>1.0</span></label><br>
      <input type='range' id='gamma' min='0.1' max='3.0' step='0.1' onchange='setG(this.value)' oninput='document.getElementById("gVal").innerText=this.value'>
      <br><br>
      <label>Deadzone: <span id='dzVal'>0%</span></label><br>
      <input type='range' id='dz' min='0' max='0.5' step='0.01' onchange='setDZ(this.value)' oninput='document.getElementById("dzVal").innerText=Math.round(this.value*100)+"%";'>
    </div>
  </div>

  <script>
    function update() {
      fetch('/data').then(r => r.json()).then(d => {
        document.getElementById('raw').innerText = d.raw;
        document.getElementById('dac').innerText = d.dac;
        document.getElementById('gamma').value = d.gamma;
        document.getElementById('gVal').innerText = d.gamma;
        document.getElementById('dz').value = d.dz;
        document.getElementById('dzVal').innerText = Math.round(d.dz*100) + '%';
        document.getElementById('invBtn').innerText = 'INVERT: ' + (d.inv ? 'ON' : 'OFF');
        document.getElementById('invBtn').style.borderColor = d.inv ? '#ff3e3e' : '#444';
      });
    }
    setInterval(update, 100); // Super szybkie odświeżanie interfejsu
    
    function cal(t) { fetch('/calibrate?type=' + t); }
    function setG(v) { fetch('/setGamma?v=' + v); }
    function setDZ(v) { fetch('/setDZ?v=' + v); }
    function inv() { fetch('/setInvert'); }
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// ENDPOINTY SERWERA HTTP
// ==========================================
void handleRoot() {
    server.send(200, "text/html", htmlPage);
}

void handleData() {
    String json = "{\"raw\":"; json += currentRaw;
    json += ",\"dac\":"; json += currentDAC;
    json += ",\"min\":"; json += minReading;
    json += ",\"max\":"; json += maxReading;
    json += ",\"gamma\":"; json += brakeGamma;
    json += ",\"dz\":"; json += deadzone;
    json += ",\"inv\":"; json += (isInverted ? "true" : "false");
    json += "}";
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

void handleSetGamma() {
    brakeGamma = server.arg("v").toFloat();
    prefs.putFloat("gamma", brakeGamma);
    server.send(200, "text/plain", "OK");
}

void handleSetDZ() {
    deadzone = server.arg("v").toFloat();
    prefs.putFloat("dz", deadzone);
    server.send(200, "text/plain", "OK");
}

void handleSetInvert() {
    isInverted = !isInverted;
    prefs.putBool("inv", isInverted);
    server.send(200, "text/plain", "OK");
}

// ==========================================
// SETUP (INICJALIZACJA)
// ==========================================
void setup() {
    Serial.begin(115200);
    
    // Inicjalizacja magistrali I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); 
    Wire.setClock(400000); // Fast Mode dla minimalnych opóźnień
    
    // Inicjalizacja przetwornika DAC
    if(!dac.begin(0x60)) {
       Serial.println("BLAD: Nie znaleziono modulu MCP4725!");
    } else {
       Serial.println("SUKCES: Modul MCP4725 zainicjalizowany poprawnie.");
    }

    // Odczyt zapisanej kalibracji z pamięci Flash
    prefs.begin("brake_cal", false);
    minReading = prefs.getLong("min", 0); 
    maxReading = prefs.getLong("max", 100000);
    brakeGamma = prefs.getFloat("gamma", 1.0); 
    deadzone = prefs.getFloat("dz", 0.0);
    isInverted = prefs.getBool("inv", false);

    // Konfiguracja menedżera WiFi
    WiFiManager wm;
    if (wm.autoConnect("G923_Brake_Hub")) {
        MDNS.begin("logitech");
        server.on("/", handleRoot); 
        server.on("/data", handleData);
        server.on("/calibrate", handleCalibrate); 
        server.on("/setGamma", handleSetGamma);
        server.on("/setDZ", handleSetDZ); 
        server.on("/setInvert", handleSetInvert);
        server.begin();
    }
    
    // Uruchomienie układu HX711 (podajemy DOUT jako pierwsze, potem SCK)
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
}

// ==========================================
// GŁÓWNA PĘTLA CZASU RZECZYWISTEGO
// ==========================================
void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        server.handleClient();
    }

    // Błyskawiczny odczyt RAW, zoptymalizowany pod tryb 80 Hz
    if (scale.is_ready()) {
        currentRaw = scale.read();
        
        long range = maxReading - minReading;
        if (range == 0) range = 1; 

        float normalized = (float)(currentRaw - minReading) / (float)range;

        // Ogranicznik sygnału chroniący bazę kierownicy
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;

        if (isInverted) normalized = 1.0f - normalized;

        if (normalized <= deadzone) {
            normalized = 0.0f;
        } else {
            normalized = (normalized - deadzone) / (1.0f - deadzone);
            normalized = pow(normalized, brakeGamma);
        }

        // Konwersja na wartość sprzętową przetwornika (0 - 4095)
        currentDAC = (int)(normalized * 4095.0f);

        if (currentDAC > 4095) currentDAC = 4095;
        if (currentDAC < 0) currentDAC = 0;
        
        // Cichy, błyskawiczny zapis do DAC (omijamy zapis do EEPROM)
        dac.setVoltage(currentDAC, false);
    }
}