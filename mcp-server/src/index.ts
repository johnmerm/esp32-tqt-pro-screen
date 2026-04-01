import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { createCanvas, loadImage } from "canvas";

const DISP_W = 128;
const DISP_H = 128;
let ESP32_URL = process.env.ESP32_URL ?? "http://192.168.1.221";

async function renderToRgb565(code: string): Promise<Buffer> {
  const canvas = createCanvas(DISP_W, DISP_H);
  const ctx = canvas.getContext("2d");

  // Black background
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, DISP_W, DISP_H);

  // eslint-disable-next-line no-new-func
  const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;
  await new AsyncFunction("ctx", "loadImage", code)(ctx, loadImage);

  const imageData = ctx.getImageData(0, 0, DISP_W, DISP_H);
  const pixels = imageData.data; // RGBA

  const buf = Buffer.allocUnsafe(DISP_W * DISP_H * 2);
  for (let i = 0; i < DISP_W * DISP_H; i++) {
    const r = pixels[i * 4] >> 3;
    const g = pixels[i * 4 + 1] >> 2;
    const b = pixels[i * 4 + 2] >> 3;
    buf.writeUInt16LE((r << 11) | (g << 5) | b, i * 2);
  }
  return buf;
}

async function sendFrameToESP32(frame: Buffer): Promise<void> {
  const response = await fetch(`${ESP32_URL}/frame`, {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: frame.buffer as ArrayBuffer,
  });
  if (!response.ok) {
    throw new Error(`ESP32 returned HTTP ${response.status}`);
  }
}

// ─── MCP Server ──────────────────────────────────────────────────────────────

const server = new McpServer({
  name: "esp32-screen",
  version: "1.0.0",
});

server.tool(
  "draw_on_screen",
  [
    "Render JavaScript canvas drawing code on the ESP32 T-QT Pro 128×128 screen.",
    "The code runs with a 2D canvas context named 'ctx' on a 128×128 canvas.",
    "Black background is pre-filled. Use coordinates 0–127.",
    "Example: ctx.fillStyle='#fff'; ctx.font='bold 96px sans-serif'; ctx.textAlign='center'; ctx.textBaseline='middle'; ctx.fillText('8', 64, 64);",
    "You can load images from URLs using: const img = await loadImage('https://...'); ctx.drawImage(img, 0, 0, 128, 128);",
  ].join(" "),
  {
    code: z
      .string()
      .describe("JavaScript that draws on a 128×128 canvas 2D context named 'ctx'"),
  },
  async ({ code }) => {
    let frame: Buffer;
    try {
      frame = await renderToRgb565(code);
    } catch (err) {
      return {
        content: [{ type: "text", text: `Canvas render error: ${err}` }],
        isError: true,
      };
    }

    try {
      await sendFrameToESP32(frame);
    } catch (err) {
      return {
        content: [{ type: "text", text: `Failed to send to ESP32 at ${ESP32_URL}: ${err}` }],
        isError: true,
      };
    }

    return {
      content: [{ type: "text", text: "Frame sent to screen." }],
    };
  }
);

server.tool(
  "clear_screen",
  "Fill the ESP32 screen with a solid color (default: black).",
  {
    color: z
      .string()
      .optional()
      .describe("CSS hex color, e.g. '#ff0000'. Defaults to '#000000'."),
  },
  async ({ color = "#000000" }) => {
    const code = `ctx.fillStyle='${color}'; ctx.fillRect(0,0,128,128);`;
    let frame: Buffer;
    try {
      frame = await renderToRgb565(code);
    } catch (err) {
      return {
        content: [{ type: "text", text: `Render error: ${err}` }],
        isError: true,
      };
    }
    try {
      await sendFrameToESP32(frame);
    } catch (err) {
      return {
        content: [{ type: "text", text: `Failed to send to ESP32 at ${ESP32_URL}: ${err}` }],
        isError: true,
      };
    }
    return { content: [{ type: "text", text: "Screen cleared." }] };
  }
);

server.tool(
  "set_url",
  "Change the ESP32 target URL for this session.",
  {
    url: z.string().describe("Base URL of the ESP32, e.g. 'http://192.168.1.221'"),
  },
  async ({ url }) => {
    ESP32_URL = url;
    return { content: [{ type: "text", text: `ESP32 URL set to ${ESP32_URL}` }] };
  }
);

// ─── Start ────────────────────────────────────────────────────────────────────

const transport = new StdioServerTransport();
await server.connect(transport);