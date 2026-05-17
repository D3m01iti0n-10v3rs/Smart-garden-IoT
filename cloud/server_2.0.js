const http = require("http");
const { WebSocketServer } = require("ws");
const sqlite3 = require("sqlite3").verbose();

// ── Database ──────────────────────────────────────────────────────────────────

const db = new sqlite3.Database("./database.db");

db.serialize(() => {
  db.run(`CREATE TABLE IF NOT EXISTS sensors (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    type      TEXT,
    value     REAL,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  db.run(`CREATE TABLE IF NOT EXISTS devices (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    deviceID   TEXT UNIQUE,
    state      INTEGER DEFAULT 0,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
  )`);

  for (let i = 1; i <= 8; i++) {
    db.run(`INSERT OR IGNORE INTO devices (deviceID, state) VALUES (?, 0)`, [String(i)]);
  }
});

// ── State ─────────────────────────────────────────────────────────────────────

let latestFrame = null;
let camSocket   = null;
const wsClients = new Set();

// ── Helpers ───────────────────────────────────────────────────────────────────

function send(ws, obj) {
  if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
}

function broadcast(obj) {
  const msg = JSON.stringify(obj);
  for (const client of wsClients) {
    if (client.readyState === client.OPEN) client.send(msg);
  }
}

// ── WebSocket servers (noServer = true, routed manually below) ────────────────

const wss_cam = new WebSocketServer({ noServer: true });
const wss_ws  = new WebSocketServer({ noServer: true });

// /cam — ESP32-CAM
wss_cam.on("connection", (ws, req) => {
  console.log("[CAM] Connected from", req.socket.remoteAddress);
  camSocket = ws;

  ws.on("message", (data, isBinary) => {
    if (!isBinary) return;
    latestFrame = data;
    for (const client of wsClients) {
      if (client.readyState === client.OPEN) client.send(data);
    }
  });

  ws.on("close", () => {
    console.log("[CAM] Disconnected");
    camSocket   = null;
    latestFrame = null;
    broadcast({ type: "camera.disconnected" });
  });

  ws.on("error", (err) => console.error("[CAM] error:", err.message));
});

// /ws — GUI clients + sensor ESP
wss_ws.on("connection", (ws) => {
  console.log("[WS] Client connected, total:", wsClients.size + 1);
  wsClients.add(ws);

  if (latestFrame) ws.send(latestFrame);

  ws.on("message", (data, isBinary) => {
    if (isBinary) return;

    let msg;
    try { msg = JSON.parse(data.toString()); }
    catch { return send(ws, { type: "error", message: "Invalid JSON" }); }

    switch (msg.type) {

      case "sensors.insert":
        db.run("INSERT INTO sensors (type, value) VALUES (?, ?)",
          [msg.sensorType, msg.value],
          (err) => err ? send(ws, { type: "error", message: err.message })
                       : send(ws, { type: "ok" }));
        break;

      case "sensors.get":
        db.all("SELECT * FROM sensors ORDER BY timestamp DESC LIMIT 50", [],
          (err, rows) => err ? send(ws, { type: "error", message: err.message })
                             : send(ws, { type: "sensors.data", rows }));
        break;

      case "devices.get":
        db.all("SELECT * FROM devices ORDER BY deviceID ASC", [],
          (err, rows) => err ? send(ws, { type: "error", message: err.message })
                             : send(ws, { type: "devices.data", rows }));
        break;

      case "devices.update":
        db.run("UPDATE devices SET state = ?, updated_at = CURRENT_TIMESTAMP WHERE deviceID = ?",
          [msg.value, msg.deviceID],
          (err) => {
            if (err) return send(ws, { type: "error", message: err.message });
            broadcast({ type: "devices.update", deviceID: msg.deviceID, value: msg.value });
          });
        break;

      case "camera.settings": {
        if (!camSocket || camSocket.readyState !== camSocket.OPEN)
          return send(ws, { type: "error", message: "Camera not connected" });
        const allowed = ["framesize","quality","vflip","hmirror","brightness","contrast","saturation"];
        const settings = {};
        for (const k of allowed) if (msg[k] !== undefined) settings[k] = msg[k];
        if (!Object.keys(settings).length)
          return send(ws, { type: "error", message: "No valid settings" });
        camSocket.send(JSON.stringify(settings), (err) =>
          err ? send(ws, { type: "error", message: "Failed to send to camera" })
              : send(ws, { type: "ok", applied: settings }));
        break;
      }

      case "frame.get":
        if (!latestFrame) return send(ws, { type: "error", message: "No frame yet" });
        ws.send(latestFrame);
        break;

      default:
        send(ws, { type: "error", message: `Unknown type: ${msg.type}` });
    }
  });

  ws.on("close", () => {
    wsClients.delete(ws);
    console.log("[WS] Client disconnected, total:", wsClients.size);
  });

  ws.on("error", (err) => console.error("[WS] error:", err.message));
});

// ── HTTP server — routes upgrades to the correct WSS ─────────────────────────

const server = http.createServer();

server.on("upgrade", (req, socket, head) => {
  if (req.url === "/cam") {
    wss_cam.handleUpgrade(req, socket, head, (ws) => wss_cam.emit("connection", ws, req));
  } else if (req.url === "/ws") {
    wss_ws.handleUpgrade(req, socket, head, (ws) => wss_ws.emit("connection", ws, req));
  } else {
    socket.destroy();
  }
});

server.listen(3000, () => {
  console.log("Listening on port 3000");
  console.log("Camera  : ws://localhost:3000/cam");
  console.log("Clients : ws://localhost:3000/ws");
});