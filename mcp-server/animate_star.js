const { createCanvas } = require("canvas");

const W = 128, H = 128;
const ESP32_URL = "http://192.168.1.221";

function renderStar(angle) {
  const canvas = createCanvas(W, H);
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, W, H);

  const cx = 64, cy = 64, points = 7;
  const outerR = 55, innerR = 22;
  const step = Math.PI / points;

  ctx.save();
  ctx.translate(cx, cy);
  ctx.rotate(angle);
  ctx.beginPath();
  for (let i = 0; i < 2 * points; i++) {
    const r = i % 2 === 0 ? outerR : innerR;
    const a = i * step - Math.PI / 2;
    const x = r * Math.cos(a);
    const y = r * Math.sin(a);
    i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  }
  ctx.closePath();
  ctx.fillStyle = "#FFD700";
  ctx.fill();
  ctx.strokeStyle = "#FFA500";
  ctx.lineWidth = 2;
  ctx.stroke();
  ctx.restore();

  const pixels = ctx.getImageData(0, 0, W, H).data;
  const buf = Buffer.allocUnsafe(W * H * 2);
  for (let i = 0; i < W * H; i++) {
    const r = pixels[i * 4] >> 3;
    const g = pixels[i * 4 + 1] >> 2;
    const b = pixels[i * 4 + 2] >> 3;
    buf.writeUInt16LE((r << 11) | (g << 5) | b, i * 2);
  }
  return buf;
}

async function sendFrame(buf) {
  const res = await fetch(`${ESP32_URL}/frame`, {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: buf.buffer,
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
}

let angle = 0;
const speed = 0.03; // radians per frame
const delay = 50;   // ms between frames

async function loop() {
  while (true) {
    const buf = renderStar(angle);
    await sendFrame(buf);
    angle += speed;
    await new Promise(r => setTimeout(r, delay));
  }
}

console.log("Animating star — Ctrl+C to stop");
loop().catch(console.error);