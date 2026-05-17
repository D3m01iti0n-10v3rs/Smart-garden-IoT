const express = require("express");
const sqlite3 = require("sqlite3").verbose();

const app = express();
app.use(express.json());

const db = new sqlite3.Database("./database.db");

db.serialize(() => {
  db.run(`
    CREATE TABLE IF NOT EXISTS sensors (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      type TEXT,
      value REAL,
      timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);

  db.run(`
    CREATE TABLE IF NOT EXISTS devices (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      deviceID TEXT UNIQUE,
      state INTEGER DEFAULT 0,
      updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  `);

  for (let i = 1; i <= 8; i++) {
    db.run(`INSERT OR IGNORE INTO devices (deviceID, state) VALUES (?, 0)`, [String(i)]);
  }
});

// ---- SENSOR DATA ----

app.post("/api/sensors", (req, res) => {
  const { type, value } = req.body;
  db.run("INSERT INTO sensors (type, value) VALUES (?, ?)", [type, value], (err) => {
    if (err) return res.status(500).json(err);
    res.json({ status: "ok" });
  });
});

app.get("/api/sensors", (req, res) => {
  db.all("SELECT * FROM sensors ORDER BY timestamp DESC LIMIT 50", [], (err, rows) => {
    if (err) return res.status(500).json(err);
    res.json(rows);
  });
});

// ---- DEVICE STATE ----

app.get("/api/devices", (req, res) => {
  db.all("SELECT * FROM devices ORDER BY deviceID DESC", [], (err, rows) => {
    if (err) return res.status(500).json(err);
    res.json(rows);
  });
});

app.post("/api/devices", (req, res) => {
  const { deviceID, value } = req.body;  // device → deviceID
  db.run(
    "UPDATE devices SET state = ?, updated_at = CURRENT_TIMESTAMP WHERE deviceID = ?",
    [value, deviceID],  // device → deviceID
    (err) => {
      if (err) return res.status(500).json(err);
      res.json({ status: "ok" });
    }
  );
});

app.listen(3000, () => {
  console.log("Server running on http://localhost:3000");
});