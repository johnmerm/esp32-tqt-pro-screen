# ESP32 T-QT Pro Screen

A PlatformIO firmware for the **Lilygo T-QT Pro (ESP32-S3)** that turns the 128×128 GC9107 display into a Wi-Fi-connected canvas — drawable from any browser or from an AI agent via MCP.

## Features

- **Browser canvas** — draw on a 512×512 canvas in any browser; frames are downsampled to 128×128 RGB565 and pushed to the display in real time.
- **Frame persistence** — the last frame is written to LittleFS and restored on reboot.
- **Wi-Fi manager** — tries compile-time credentials first, then a saved credential from flash. If nothing works it opens an AP captive portal (`ESP32-Setup` / `192.168.4.1`) where you can enter your network from a browser or via serial.
- **MCP server** — a TypeScript Node.js server that exposes `draw_on_screen`, `clear_screen`, and `set_url` tools so an AI agent (e.g. Claude) can draw directly on the display.

## Hardware

| Item | Value |
|------|-------|
| Board | Lilygo T-QT Pro (ESP32-S3-FN4R2) |
| Display | 0.85" GC9107, 128×128, SPI |
| Flash | 4 MB (LittleFS filesystem) |
| PSRAM | 2 MB |

## Project Structure

```
src/
  main.cpp          # Firmware: web server, display, frame I/O
  wifi_manager.h    # Wi-Fi connection logic and captive portal
mcp-server/
  src/index.ts      # MCP server (draw_on_screen, clear_screen, set_url tools)
  package.json
lib/
  TFT_eSPI/         # Patched TFT_eSPI with T-QT Pro pin config
board/
  esp32-s3-t-qt-pro.json   # PlatformIO board definition
platformio.ini
wifi_credentials.h  # Not committed — see wifi_credentials.h.example
```

## Getting Started

### 1. Clone and configure Wi-Fi

```bash
git clone <repo-url>
cd screen
cp wifi_credentials.h.example wifi_credentials.h
# Edit wifi_credentials.h with your network(s)
```

### 2. Flash the firmware

```bash
pio run --target upload
pio run --target uploadfs   # upload LittleFS partition
```

After boot the display shows the IP address. Open it in a browser to draw.

### 3. MCP server (optional)

The MCP server lets an AI agent draw on the screen programmatically.

```bash
cd mcp-server
npm install
npm run build

# Set the ESP32 IP (default: http://192.168.1.221)
export ESP32_URL=http://<your-esp32-ip>
npm start
```

Register it in your MCP client config (e.g. Claude Desktop `claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "esp32-screen": {
      "command": "node",
      "args": ["/absolute/path/to/mcp-server/dist/index.js"],
      "env": { "ESP32_URL": "http://<your-esp32-ip>" }
    }
  }
}
```

#### Available MCP tools

| Tool | Description |
|------|-------------|
| `draw_on_screen` | Run JavaScript canvas drawing code on the 128×128 display |
| `clear_screen` | Fill the screen with a solid color (default: black) |
| `set_url` | Change the ESP32 target URL for the current session |

**`draw_on_screen` example** — the code receives a `ctx` (Canvas 2D context) and an async `loadImage` helper:

```js
ctx.fillStyle = '#ff6600';
ctx.font = 'bold 48px sans-serif';
ctx.textAlign = 'center';
ctx.textBaseline = 'middle';
ctx.fillText('Hi!', 64, 64);
```

## Wi-Fi Credential Priority

1. Credential saved in LittleFS (`/wifi.txt`) from a previous portal login
2. Compile-time credentials in `wifi_credentials.h`
3. AP captive portal — join `ESP32-Setup` and open `192.168.4.1`

## HTTP API

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/` | Browser canvas UI |
| `GET` | `/frame` | Download current frame (RGB565 binary, 32 768 bytes) |
| `POST` | `/frame` | Upload new frame (RGB565 binary, 32 768 bytes) |

## Dependencies

| Library | Purpose |
|---------|---------|
| `mathieucarbou/ESPAsyncWebServer` | Async HTTP server |
| `mathieucarbou/AsyncTCP` | Async TCP layer |
| TFT_eSPI (local, patched) | Display driver |
| `@modelcontextprotocol/sdk` | MCP server framework |
| `canvas` (Node.js) | Server-side canvas rendering |

## License

MIT
