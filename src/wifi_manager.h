#pragma once
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>
#include "../../wifi_credentials.h"

#define WIFI_CRED_PATH   "/wifi.txt"
#define PORTAL_SSID      "ESP32-Setup"
#define CONNECT_TIMEOUT  8000

// ─── Flash credential storage ─────────────────────────────────────────────────

static bool wm_loadCred(String& ssid, String& pass) {
    if (!LittleFS.exists(WIFI_CRED_PATH)) return false;
    File f = LittleFS.open(WIFI_CRED_PATH, "r");
    if (!f) return false;
    ssid = f.readStringUntil('\n'); ssid.trim();
    pass = f.readStringUntil('\n'); pass.trim();
    f.close();
    return ssid.length() > 0;
}

static void wm_saveCred(const String& ssid, const String& pass) {
    File f = LittleFS.open(WIFI_CRED_PATH, "w");
    if (!f) return;
    f.println(ssid);
    f.println(pass);
    f.close();
}

// ─── Connection helper ────────────────────────────────────────────────────────

static bool wm_tryConnect(const char* ssid, const char* pass) {
    WiFi.begin(ssid, pass);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < CONNECT_TIMEOUT)
        delay(200);
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.disconnect(true);
    return false;
}

// ─── Portal HTML ──────────────────────────────────────────────────────────────

static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Setup</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#12121f;color:#ddd;font-family:system-ui,sans-serif;
       display:flex;flex-direction:column;align-items:center;padding:28px;gap:16px}
  h2{color:#a8b4ff;font-size:1.1rem}
  h3{color:#a8b4ff;font-size:.9rem;align-self:flex-start;width:100%;max-width:340px}
  form{display:flex;flex-direction:column;gap:10px;width:100%;max-width:340px}
  label{font-size:12px;color:#99a}
  input{padding:8px 12px;border:1px solid #445;border-radius:5px;
        background:#1e2035;color:#ddd;font-size:14px;width:100%}
  button{padding:10px;background:#3a4080;border:1px solid #a8b4ff;
         border-radius:5px;color:#fff;font-size:14px;cursor:pointer}
  button:hover{background:#4a50a0}
  #status{font-size:12px;color:#778;min-height:16px}
  .net{display:flex;justify-content:space-between;padding:6px 4px;
       border-bottom:1px solid #223;cursor:pointer;font-size:13px}
  .net:hover{background:#1e2035}
  #nets{width:100%;max-width:340px}
</style>
</head>
<body>
<h2>WiFi Setup</h2>
<form onsubmit="go(event)">
  <label>SSID</label>
  <input id="ssid" type="text" placeholder="Network name" required autocomplete="off">
  <label>Password</label>
  <input id="pass" type="password" placeholder="Password" autocomplete="new-password">
  <button type="submit">Connect &amp; Save</button>
</form>
<div id="status"></div>
<h3>Nearby networks</h3>
<div id="nets"><em style="font-size:12px;color:#556">Scanning…</em></div>
<script>
fetch('/scan').then(r=>r.json()).then(nets=>{
  const d=document.getElementById('nets');
  d.innerHTML='';
  nets.forEach(n=>{
    const row=document.createElement('div');
    row.className='net';
    row.innerHTML='<span>'+n.ssid+'</span><span style="color:#667">'+n.rssi+' dBm</span>';
    row.onclick=()=>document.getElementById('ssid').value=n.ssid;
    d.appendChild(row);
  });
  if(!nets.length) d.innerHTML='<em style="font-size:12px;color:#556">None found</em>';
});
async function go(e){
  e.preventDefault();
  const st=document.getElementById('status');
  st.style.color='#99a'; st.textContent='Connecting…';
  const ssid=document.getElementById('ssid').value;
  const pass=document.getElementById('pass').value;
  const r=await fetch('/connect?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass),{method:'POST'});
  const txt=await r.text();
  st.style.color=r.ok&&txt.startsWith('OK')?'#5c8':'#e66';
  st.textContent=txt;
}
</script>
</body>
</html>
)rawliteral";

// ─── Main entry point ─────────────────────────────────────────────────────────
// Call once from setup() before server.begin().
// Returns when connected (STA mode). Never returns if portal is started —
// it restarts the ESP32 after saving credentials.

static void wifiConnect(AsyncWebServer& server, TFT_eSPI& tft) {
    auto showMsg = [&](const char* line1, const char* line2 = nullptr) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(4, 50);
        tft.print(line1);
        if (line2) { tft.setCursor(4, 65); tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.print(line2); }
    };

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    // Scan once — only attempt networks that are actually visible
    showMsg("Scanning...");
    int found = WiFi.scanNetworks();

    auto isVisible = [&](const char* ssid) {
        for (int i = 0; i < found; i++)
            if (WiFi.SSID(i) == ssid) return true;
        return false;
    };

    // 1. Try LittleFS-saved credential
    String savedSsid, savedPass;
    if (wm_loadCred(savedSsid, savedPass) && isVisible(savedSsid.c_str())) {
        showMsg("Trying saved:", savedSsid.c_str());
        if (wm_tryConnect(savedSsid.c_str(), savedPass.c_str())) return;
    }

    // 2. Try compile-time credentials
    int n = sizeof(WIFI_KNOWN) / sizeof(WIFI_KNOWN[0]);
    for (int i = 0; i < n; i++) {
        if (!isVisible(WIFI_KNOWN[i].ssid)) continue;
        showMsg("Trying:", WIFI_KNOWN[i].ssid);
        if (wm_tryConnect(WIFI_KNOWN[i].ssid, WIFI_KNOWN[i].password)) return;
    }

    // 3. Nothing worked — start AP portal
    WiFi.mode(WIFI_AP_STA);   // AP_STA so we can scan from the portal page
    WiFi.softAP(PORTAL_SSID);

    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(4, 30); tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.println("WiFi Setup");
    tft.setCursor(4, 46); tft.setTextColor(TFT_WHITE,  TFT_BLACK); tft.println("Join network:");
    tft.setCursor(4, 58); tft.setTextColor(TFT_CYAN,   TFT_BLACK); tft.println(PORTAL_SSID);
    tft.setCursor(4, 74); tft.setTextColor(TFT_WHITE,  TFT_BLACK); tft.println("192.168.4.1");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", PORTAL_HTML);
    });

    server.on("/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
        int n = WiFi.scanNetworks();
        String json = "[";
        for (int i = 0; i < n; i++) {
            if (i) json += ",";
            String s = WiFi.SSID(i);
            s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
            json += "{\"ssid\":\"" + s + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
        }
        json += "]";
        req->send(200, "application/json", json);
    });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("ssid") || !req->hasParam("pass")) {
            req->send(400, "text/plain", "Missing params");
            return;
        }
        String ssid = req->getParam("ssid")->value();
        String pass = req->getParam("pass")->value();

        WiFi.mode(WIFI_AP_STA);
        if (wm_tryConnect(ssid.c_str(), pass.c_str())) {
            wm_saveCred(ssid, pass);
            req->send(200, "text/plain", "OK — saved. Restarting…");
            delay(1500);
            ESP.restart();
        } else {
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP(PORTAL_SSID);
            req->send(200, "text/plain", "Failed — check credentials");
        }
    });

    server.begin();

    // Serial config runs alongside the portal
    Serial.println("[WiFi] No known network found.");
    Serial.println("[WiFi] AP portal: connect to \"" PORTAL_SSID "\" → 192.168.4.1");
    Serial.println();
    Serial.println("[WiFi] Nearby networks:");
    for (int i = 0; i < found; i++)
        Serial.printf("  [%2d dBm]  %s\n", WiFi.RSSI(i), WiFi.SSID(i).c_str());
    if (found == 0) Serial.println("  (none found)");
    Serial.println();
    Serial.println("[WiFi] Enter SSID to connect:");
    Serial.print("SSID> ");

    enum { S_SSID, S_PASS, S_CONNECTING } sState = S_SSID;
    String sLine, sSsid, sPass;

    while (true) {
        // Accumulate serial input line by line
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                Serial.println();
                sLine.trim();
                if (sState == S_SSID) {
                    if (sLine.length()) {
                        sSsid = sLine;
                        Serial.print("PASS> ");
                        sState = S_PASS;
                    }
                } else if (sState == S_PASS) {
                    sPass = sLine;
                    sState = S_CONNECTING;
                }
                sLine = "";
            } else {
                Serial.print(c);  // echo
                sLine += c;
            }
        }

        if (sState == S_CONNECTING) {
            Serial.printf("[WiFi] Trying \"%s\"…\n", sSsid.c_str());
            WiFi.mode(WIFI_AP_STA);
            if (wm_tryConnect(sSsid.c_str(), sPass.c_str())) {
                wm_saveCred(sSsid, sPass);
                Serial.println("[WiFi] Connected! Saving and restarting…");
                delay(500);
                ESP.restart();
            } else {
                Serial.println("[WiFi] Failed. Try again:");
                Serial.print("SSID> ");
                WiFi.mode(WIFI_AP_STA);
                WiFi.softAP(PORTAL_SSID);
                sState = S_SSID; sSsid = ""; sPass = "";
            }
        }

        delay(10);
    }
}