const http = require("http");
const { WebSocketServer } = require("ws");
const sqlite3 = require("sqlite3").verbose();
const os = require("os");

// ── Database ──────────────────────────────────────────────────────────────────
const db = new sqlite3.Database("./database.db");

db.serialize(() => {
  db.run(`CREATE TABLE IF NOT EXISTS sensors (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    type       TEXT,
    value      REAL,
    timestamp DATETIME
  )`);

  db.run(`CREATE TABLE IF NOT EXISTS devices (
    deviceID   TEXT UNIQUE,
    state      INTEGER DEFAULT 0,
    updated_at DATETIME
  )`);

  db.run(`CREATE TABLE IF NOT EXISTS commands (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    cmd        TEXT,
    value      REAL,
    timestamp DATETIME
  )`);

  for (let i = 1; i <= 8; i++) {
    db.run(`INSERT OR IGNORE INTO devices (deviceID, state, updated_at) VALUES (?, 0, datetime('now', 'localtime'))`, [String(i)]);
  }
});

// ── State ─────────────────────────────────────────────────────────────────────
let latestFrame = null;
let camSocket   = null;
let iotSocket   = null;

// Changed from Set to Map to store client metadata (IP, connection time)
const uiClients = new Map(); 

// ── Helpers ───────────────────────────────────────────────────────────────────
function send(ws, obj) {
  if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
}

function broadcastUI(obj) {
  const msg = JSON.stringify(obj);
  for (const client of uiClients.keys()) {
    if (client.readyState === client.OPEN) client.send(msg);
  }
}

// Helper to neatly display remaining frontend connections
function logRemainingUiClients() {
  if (uiClients.size === 0) {
    console.log("[UI] No UI clients remaining.");
  } else {
    console.log(`[UI] Active UI Clients (${uiClients.size}):`);
    let idx = 1;
    for (const info of uiClients.values()) {
      console.log(`  ${idx++}. IP: ${info.ip} | Connected at: ${info.connectedAt}`);
    }
  }
}

// ── WebSocket servers ────────────────────────────────────────────────────────

const wss_cam = new WebSocketServer({ noServer: true });
const wss_iot = new WebSocketServer({ noServer: true });
const wss_ui  = new WebSocketServer({ noServer: true });

// /cam — ESP32-CAM
wss_cam.on("connection", (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  console.log(`[CAM] Connected from ${clientIp}`);
  camSocket = ws;

  ws.on("message", (data, isBinary) => {
      if (!isBinary) return;
      latestFrame = data;
      
      for (const client of uiClients.keys()) {
        // Only send if the client's buffer is empty (meaning they finished receiving the last frame)
        if (client.readyState === client.OPEN && client.bufferedAmount === 0) {
          client.send(data);
        }
      }
    });

  ws.on("close", () => {
    console.log(`[CAM] Disconnected: ${clientIp}`);
    camSocket   = null;
    latestFrame = null;
    broadcastUI({ type: "camera.disconnected" });
  });

  ws.on("error", (err) => console.error(`[CAM] error (${clientIp}):`, err.message));
});

// /iot_nodes — ESP32 gateway
wss_iot.on("connection", (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  console.log(`[IOT] Connected from ${clientIp}`);
  iotSocket = ws;

  db.all("SELECT * FROM commands ORDER BY timestamp ASC", [], (err, rows) => {
    if (err) return console.error("[IOT] Failed to fetch pending commands:", err.message);
    if (rows.length === 0) return;
    console.log(`[IOT] Sending ${rows.length} pending command(s) to ${clientIp}`);
    for (const row of rows) {
      send(ws, {
        type:  "commands.new",
        id:    row.id,
        cmd:   row.cmd,
        value: row.value
      });
    }
  });

  ws.on("message", (data, isBinary) => {
    if (isBinary) return;

    let msg;
    try { msg = JSON.parse(data.toString()); }
    catch { return send(ws, { type: "error", message: "Invalid JSON" }); }

    switch (msg.type) {
      case "sensors.insert": {
        const client = ws;
        db.run(
          "INSERT INTO sensors (type, value, timestamp) VALUES (?, ?, datetime('now', 'localtime'))",
          [msg.sensorType, msg.value],
          (err) => err ? send(client, { type: "error", message: err.message })
                       : send(client, { type: "ok" })
        );
        break;
      }

      case "devices.update": {
        const client = ws;
        db.run(
          "UPDATE devices SET state = ?, updated_at = datetime('now', 'localtime') WHERE deviceID = ?",
          [msg.value, msg.deviceID],
          (err) => {
            if (err) return send(client, { type: "error", message: err.message });
            send(client, { type: "ok" });
          }
        );
        break;
      }

      case "commands.complete": {
        const client = ws;
        db.run(
          "DELETE FROM commands WHERE id = ?",
          [msg.id],
          (err) => err ? send(client, { type: "error", message: err.message })
                       : send(client, { type: "ok" })
        );
        break;
      }

      default:
        send(ws, { type: "error", message: `Unknown type: ${msg.type}` });
    }
  });

  ws.on("close", () => {
    console.log(`[IOT] Disconnected: ${clientIp}`);
    iotSocket = null;
  });

  ws.on("error", (err) => console.error(`[IOT] error (${clientIp}):`, err.message));
});

// /ui_clients — GUI clients (Added 'req' parameter to extract IP)
wss_ui.on("connection", (ws, req) => {
  const clientIp = req.socket.remoteAddress;
  
  // Store connection data inside our Map
  uiClients.set(ws, {
    ip: clientIp,
    connectedAt: new Date().toLocaleTimeString()
  });

  console.log(`\n[UI] Client connected from ${clientIp}. Total: ${uiClients.size}`);
  logRemainingUiClients();

  if (latestFrame) ws.send(latestFrame);

  ws.on("message", (data, isBinary) => {
    if (isBinary) return;

    let msg;
    try { msg = JSON.parse(data.toString()); }
    catch { return send(ws, { type: "error", message: "Invalid JSON" }); }

    switch (msg.type) {
      case "sensors.get":
        db.all(
          "SELECT * FROM sensors ORDER BY timestamp DESC LIMIT 50", [],
          (err, rows) => err ? send(ws, { type: "error", message: err.message })
                             : send(ws, { type: "sensors.data", rows })
        );
        break;

      case "devices.get":
        db.all(
          "SELECT * FROM devices ORDER BY deviceID ASC", [],
          (err, rows) => err ? send(ws, { type: "error", message: err.message })
                             : send(ws, { type: "devices.data", rows })
        );
        break;

      case "commands.insert": {
        const client = ws;
        db.run(
          "INSERT INTO commands (cmd, value, timestamp) VALUES (?, ?, datetime('now', 'localtime'))",
          [msg.cmd, msg.value],
          function (err) {
            if (err) return send(client, { type: "error", message: err.message });

            send(client, { type: "ok", id: this.lastID });

            if (iotSocket && iotSocket.readyState === iotSocket.OPEN) {
              send(iotSocket, {
                type:  "commands.new",
                id:    this.lastID,
                cmd:   msg.cmd,
                value: msg.value
              });
            }
          }
        );
        break;
      }

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
              : send(ws, { type: "ok", applied: settings })
        );
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
    uiClients.delete(ws);
    console.log(`\n[UI] Client disconnected: ${clientIp}. Total remaining: ${uiClients.size}`);
    logRemainingUiClients();
  });

  ws.on("error", (err) => console.error(`[UI] error (${clientIp}):`, err.message));
});

// ── HTTP server — routes upgrades to the correct WSS ─────────────────────────
const server = http.createServer();

server.on("upgrade", (req, socket, head) => {
  if (req.url === "/cam") {
    wss_cam.handleUpgrade(req, socket, head, (ws) => wss_cam.emit("connection", ws, req));
  } else if (req.url === "/iot_nodes") {
    wss_iot.handleUpgrade(req, socket, head, (ws) => wss_iot.emit("connection", ws, req));
  } else if (req.url === "/ui_clients") {
    wss_ui.handleUpgrade(req, socket, head, (ws) => wss_ui.emit("connection", ws, req));
  } else {
    socket.destroy();
  }
});

server.listen(3000, () => {
  console.log("Listening on port 3000");
  console.log("Camera     : ws://localhost:3000/cam");
  console.log("IoT nodes  : ws://localhost:3000/iot_nodes");
  console.log("UI clients : ws://localhost:3000/ui_clients");

  const mdns = require('multicast-dns')();

  mdns.on('query', function(query) {
    query.questions.forEach((q) => {
      if (q.name === 'gloriainexcelsisdeo.local' && q.type === 'A') {
        const ip = Object.values(os.networkInterfaces()).flat().find(i => i.family === "IPv4" && !i.internal).address;
        mdns.respond({
          answers: [{
            name: 'gloriainexcelsisdeo.local',
            type: 'A',
            ttl:  300,
            data: ip
          }]
        });
        console.log('Responded to mDNS query');
      }
    });
  });
});