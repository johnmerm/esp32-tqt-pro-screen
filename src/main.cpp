#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <LittleFS.h>

#include "wifi_manager.h"

// ─── Display ─────────────────────────────────────────────────────────────────
// Pins and driver (GC9A01, 128x128) are defined in:
//   lib/TFT_eSPI/User_Setups/Setup211_LilyGo_T_QT_Pro_S3.h
// Backlight is active-LOW (TFT_BACKLIGHT_ON = 0), handled by tft.init().

TFT_eSPI tft = TFT_eSPI();

#define DISP_W      128
#define DISP_H      128
#define FRAME_BYTES (DISP_W * DISP_H * 2)   // RGB565, 32 768 bytes
#define FRAME_PATH  "/frame.bin"

// ─── Web server ───────────────────────────────────────────────────────────────
AsyncWebServer server(80);

static uint8_t frameBuf[FRAME_BYTES];

// ─── HTML page ───────────────────────────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>T-QT Pro Canvas</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: #12121f;
    color: #ddd;
    font-family: system-ui, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 20px;
    gap: 14px;
    min-height: 100dvh;
  }
  h2 { font-size: 1.1rem; color: #a8b4ff; letter-spacing: .05em; }
  #canvas {
    display: block;
    cursor: crosshair;
    image-rendering: pixelated;
    border: 2px solid #334;
    border-radius: 6px;
    touch-action: none;
  }
  .controls {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
    justify-content: center;
    align-items: center;
  }
  button {
    padding: 7px 16px;
    border: 1px solid #445;
    border-radius: 5px;
    cursor: pointer;
    font-size: 13px;
    background: #1e2035;
    color: #cdd;
    transition: background .15s;
  }
  button:hover { background: #2a2d4a; }
  button.active { background: #3a4080; border-color: #a8b4ff; color: #fff; }
  input[type=color] {
    width: 40px; height: 34px;
    border: 1px solid #445; border-radius: 5px;
    padding: 2px; cursor: pointer; background: #1e2035;
  }
  .row { display: flex; align-items: center; gap: 6px; font-size: 13px; }
  input[type=range] { width: 90px; accent-color: #a8b4ff; }
  #status { font-size: 11px; color: #778; height: 16px; }
  #status.ok  { color: #5c8; }
  #status.err { color: #e66; }
</style>
</head>
<body>
<h2>T-QT Pro &mdash; 128 &times; 128 Canvas</h2>
<canvas id="canvas" width="512" height="512"></canvas>
<div class="controls">
  <button id="btnPen"    class="active" onclick="setTool('pen')">Pen</button>
  <button id="btnEraser"             onclick="setTool('eraser')">Eraser</button>
  <button id="btnFill"               onclick="setTool('fill')">Fill</button>
  <button onclick="clearCanvas()">Clear</button>
  <div class="row">
    <label>Color</label>
    <input type="color" id="colorPick" value="#ffffff">
  </div>
  <div class="row">
    <label>Size&nbsp;<strong id="sizeVal">4</strong></label>
    <input type="range" id="brushSize" min="1" max="20" value="4"
           oninput="sizeVal.textContent=this.value">
  </div>
</div>
<div id="status">Draw something and release to send</div>

<script>
const W = 128, H = 128, SCALE = 4;
const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d', { willReadFrequently: true });
ctx.fillStyle = '#000';
ctx.fillRect(0, 0, 512, 512);

// Restore last saved frame from ESP32 flash
fetch('/frame').then(r => r.ok ? r.arrayBuffer() : null).then(buf => {
  if (!buf || buf.byteLength !== W * H * 2) return;
  const dv = new DataView(buf);
  const img = ctx.createImageData(512, 512);
  const d = img.data;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const px = dv.getUint16((y * W + x) * 2, true);
    const r = ((px >> 11) & 0x1f) << 3;
    const g = ((px >>  5) & 0x3f) << 2;
    const b =  (px        & 0x1f) << 3;
    for (let dy = 0; dy < SCALE; dy++) for (let dx = 0; dx < SCALE; dx++) {
      const i = ((y * SCALE + dy) * 512 + (x * SCALE + dx)) * 4;
      d[i] = r; d[i+1] = g; d[i+2] = b; d[i+3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}).catch(() => {});

let drawing = false, tool = 'pen', sending = false, sendQueued = false;

function setTool(t) {
  tool = t;
  document.getElementById('btnPen').classList.toggle('active', t === 'pen');
  document.getElementById('btnEraser').classList.toggle('active', t === 'eraser');
  document.getElementById('btnFill').classList.toggle('active', t === 'fill');
}

function setStatus(msg, cls) {
  const el = document.getElementById('status');
  el.textContent = msg;
  el.className = cls || '';
}

function clearCanvas() {
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, 512, 512);
  sendFrame();
}

function getColor() {
  return tool === 'eraser' ? '#000000' : document.getElementById('colorPick').value;
}

function getPos(e) {
  const r = canvas.getBoundingClientRect();
  const sx = canvas.width / r.width, sy = canvas.height / r.height;
  const src = e.touches ? e.touches[0] : e;
  return { x: (src.clientX - r.left) * sx, y: (src.clientY - r.top) * sy };
}

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}
function floodFill(px, py) {
  const imgData = ctx.getImageData(0, 0, 512, 512);
  const d = imgData.data;
  const [fr, fg, fb] = hexToRgb(document.getElementById('colorPick').value);
  const [sr, sg, sb] = [d[(py*512+px)*4], d[(py*512+px)*4+1], d[(py*512+px)*4+2]];
  if (sr===fr && sg===fg && sb===fb) return;
  const stack = [[px, py]];
  while (stack.length) {
    const [x, y] = stack.pop();
    if (x < 0 || x >= 512 || y < 0 || y >= 512) continue;
    const i = (y*512+x)*4;
    if (d[i]!==sr || d[i+1]!==sg || d[i+2]!==sb) continue;
    d[i]=fr; d[i+1]=fg; d[i+2]=fb; d[i+3]=255;
    stack.push([x+1,y],[x-1,y],[x,y+1],[x,y-1]);
  }
  ctx.putImageData(imgData, 0, 0);
}

function drawAt(pos) {
  if (tool === 'fill') { floodFill(Math.floor(pos.x), Math.floor(pos.y)); return; }
  const r = parseInt(document.getElementById('brushSize').value) * SCALE / 2;
  ctx.beginPath();
  ctx.fillStyle = getColor();
  ctx.arc(pos.x, pos.y, r, 0, Math.PI * 2);
  ctx.fill();
}

function scheduleSend() {
  if (sendQueued) return;
  sendQueued = true;
  requestAnimationFrame(() => { sendFrame(); sendQueued = false; });
}

// Encode 512x512 canvas → 128x128 RGB565 little-endian → POST /frame
function sendFrame() {
  if (sending) return;
  sending = true;
  setStatus('Sending…');
  const off = document.createElement('canvas');
  off.width = W; off.height = H;
  off.getContext('2d').drawImage(canvas, 0, 0, W, H);
  const px = off.getContext('2d').getImageData(0, 0, W, H).data;
  const buf = new ArrayBuffer(W * H * 2);
  const dv = new DataView(buf);
  for (let i = 0; i < W * H; i++) {
    const r = px[i*4] >> 3, g = px[i*4+1] >> 2, b = px[i*4+2] >> 3;
    dv.setUint16(i * 2, (r << 11) | (g << 5) | b, true /* little-endian */);
  }
  fetch('/frame', { method: 'POST', body: buf,
                    headers: { 'Content-Type': 'application/octet-stream' } })
    .then(() => setStatus('OK', 'ok'))
    .catch(() => setStatus('Send failed', 'err'))
    .finally(() => { sending = false; });
}

canvas.addEventListener('pointerdown', e => {
  drawing = true;
  canvas.setPointerCapture(e.pointerId);
  drawAt(getPos(e));
  scheduleSend();
});
canvas.addEventListener('pointermove', e => {
  if (!drawing) return;
  drawAt(getPos(e));
  scheduleSend();
});
canvas.addEventListener('pointerup',     () => { drawing = false; sendFrame(); });
canvas.addEventListener('pointercancel', () => { drawing = false; });
</script>
</body>
</html>
)rawliteral";

// ─── Display update ───────────────────────────────────────────────────────────
static void pushFrameToDisplay() {
    tft.pushImage(0, 0, DISP_W, DISP_H, (uint16_t*)frameBuf);
}

static void saveFrameToFlash() {
    File f = LittleFS.open(FRAME_PATH, "w");
    if (!f) return;
    f.write(frameBuf, FRAME_BYTES);
    f.close();
}

static bool loadFrameFromFlash() {
    File f = LittleFS.open(FRAME_PATH, "r");
    if (!f || f.size() != FRAME_BYTES) return false;
    f.read(frameBuf, FRAME_BYTES);
    f.close();
    pushFrameToDisplay();
    return true;
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    LittleFS.begin(true);   // true = format if mount fails

    tft.init();
    tft.setRotation(0);

    // Color test — red / green / blue flash confirms display is alive
    tft.fillScreen(TFT_RED);   delay(500);
    tft.fillScreen(TFT_GREEN); delay(500);
    tft.fillScreen(TFT_BLUE);  delay(500);
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 55);
    tft.print("WiFi...");

    wifiConnect(server, tft);   // scans, tries known networks, portal if none work
    Serial.printf("[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());

    if (!loadFrameFromFlash()) {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 44);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.println("Open browser:");
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.println(WiFi.localIP().toString());
    }

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/frame", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, FRAME_PATH, "application/octet-stream");
    });

    server.on("/frame", HTTP_POST,
        [](AsyncWebServerRequest* req) { req->send(200); },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total != FRAME_BYTES) return;
            if (index + len <= FRAME_BYTES) memcpy(frameBuf + index, data, len);
            if (index + len == total) { pushFrameToDisplay(); saveFrameToFlash(); }
        }
    );

    server.begin();
    Serial.println("[HTTP] Server started");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {}