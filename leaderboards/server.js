/**
 OpenMBU leaderboards server
 - Rate-limiting
 - Input validation/sanitization
 - Simple anti-cheat checks
 - API Key and optional HMAC signature verification
 - Optional HTTPS when TLS cert/key files provided via env
 - Returns plain text (default) or JSON (?format=json or Accept: application/json)
 - Keeps best time per player per level (upsert with unique constraint)
*/

require('dotenv').config();

const fs = require('fs');
const http = require('http');
const https = require('https');
const express = require('express');
const helmet = require('helmet');
const bodyParser = require('body-parser');
const sqlite3 = require('sqlite3').verbose();
const path = require('path');
const cors = require('cors');
const rateLimit = require('express-rate-limit');
const crypto = require('crypto');
const validator = require('validator');

const app = express();
const PORT = process.env.PORT || 3000;
const DB_PATH = path.join(__dirname, 'leaderboards.db');

// Configuration from env
const LB_API_KEY = process.env.LB_API_KEY || '';
const LB_HMAC_SECRET = process.env.LB_HMAC_SECRET || '';
const LB_ALLOWED_ORIGINS = process.env.LB_ALLOWED_ORIGINS || 'http://localhost:3000';
const LB_MIN_TIME = parseFloat(process.env.LB_MIN_TIME || '0.01'); // seconds
const LB_MAX_TIME = parseFloat(process.env.LB_MAX_TIME || '36000'); // 10 hours default
const LB_MIN_SUBMIT_INTERVAL = parseInt(process.env.LB_MIN_SUBMIT_INTERVAL || '3'); // seconds between same-player submissions

// CORS config (comma separated origins)
const allowedOrigins = LB_ALLOWED_ORIGINS.split(',').map(s => s.trim());
const corsOptions = {
  origin: function (origin, callback) {
    if (!origin) return callback(null, true); // allow non-browser clients
    if (allowedOrigins.indexOf('*') !== -1 || allowedOrigins.indexOf(origin) !== -1) {
      callback(null, true);
    } else {
      callback(new Error('Not allowed by CORS'), false);
    }
  }
};

app.use(helmet());
app.use(cors(corsOptions));

// We need the raw body for HMAC verification
app.use(bodyParser.json({
  verify: function (req, res, buf) {
    req.rawBody = buf;
  },
  limit: '64kb'
}));
app.use(bodyParser.urlencoded({ extended: true }));

// Basic rate limiting for all routes
const globalLimiter = rateLimit({
  windowMs: 60 * 1000, // 1 minute
  max: 300, // limit each IP to 300 requests per windowMs
  standardHeaders: true,
  legacyHeaders: false
});
app.use(globalLimiter);

// Stronger rate limit for score submissions
const scoreLimiter = rateLimit({
  windowMs: 60 * 1000,
  max: 30,
  message: { error: 'too_many_requests' },
  standardHeaders: true,
  legacyHeaders: false
});

// In-memory basic anti-cheat tracking (per-player last submission timestamp)
// Note: This is volatile and resets on server restart. For production use Redis or DB.
const lastSubmissionAt = Object.create(null);

// Open/create DB
const db = new sqlite3.Database(DB_PATH, (err) => {
  if (err) {
    console.error('Failed to open DB', err);
    process.exit(1);
  }
});

// Create scores table with unique constraint on (name, level) to keep best only
db.serialize(() => {
  db.run(`
    CREATE TABLE IF NOT EXISTS scores (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT NOT NULL,
      time REAL NOT NULL,
      level TEXT NOT NULL,
      created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
      UNIQUE(name, level)
    );
  `);
});

// Utility: verify API key or HMAC signature (if configured)
function verifyAuth(req) {
  // If no auth configured, allow
  if (!LB_API_KEY && !LB_HMAC_SECRET) return true;

  // API key via header or query param
  const keyHeader = (req.get('x-api-key') || '').trim();
  const keyQuery = (req.query.key || '').trim();
  if (LB_API_KEY && (keyHeader === LB_API_KEY || keyQuery === LB_API_KEY)) return true;

  // HMAC signature: header 'x-signature' hex hmac-sha256 of raw body
  if (LB_HMAC_SECRET && req.rawBody) {
    const sig = (req.get('x-signature') || '').trim();
    if (sig) {
      const h = crypto.createHmac('sha256', LB_HMAC_SECRET).update(req.rawBody).digest('hex');
      if (crypto.timingSafeEqual(Buffer.from(h, 'hex'), Buffer.from(sig, 'hex'))) {
        return true;
      }
    }
  }

  return false;
}

// Simple sanitization helpers
function sanitizeName(name) {
  name = String(name || '').trim().slice(0, 50);
  // remove disallowed characters
  name = name.replace(/[\r\n|]/g, '');
  if (!name.length) name = 'Player';
  return name;
}
function sanitizeLevel(level) {
  level = String(level || '').trim().slice(0, 100);
  level = level.replace(/[\r\n|]/g, '');
  if (!level.length) level = 'default';
  return level;
}

// Health check
app.get('/', (req, res) => {
  res.json({
    ok: true,
    version: '0.2.0',
    service: 'openmbu-leaderboards',
    features: {
      apiKey: !!LB_API_KEY,
      hmac: !!LB_HMAC_SECRET,
      https: !!(process.env.LB_TLS_CERT && process.env.LB_TLS_KEY)
    }
  });
});

// Submit a score: { name: string, time: number, level: string }
// Auth: are provided by LB_API_KEY OR LB_HMAC_SECRET (optional). Clients may use query param key= or header x-api-key, or x-signature for HMAC.
app.post('/score', scoreLimiter, (req, res) => {
  if (!verifyAuth(req)) {
    return res.status(401).json({ error: 'unauthorized' });
  }

  const payload = req.body || {};
  let name = sanitizeName(payload.name);
  let level = sanitizeLevel(payload.level);
  let time = parseFloat(payload.time);

  if (!validator.isFloat(String(time))) {
    return res.status(400).json({ error: 'invalid_time' });
  }
  if (!validator.isLength(name, { min: 1, max: 50 })) {
    return res.status(400).json({ error: 'invalid_name' });
  }

  // Anti-cheat: plausible checks
  if (!isFinite(time) || time < LB_MIN_TIME || time > LB_MAX_TIME) {
    return res.status(400).json({ error: 'implausible_time' });
  }

  // Anti-cheat: submission rate per player
  const now = Date.now();
  const last = lastSubmissionAt[name] || 0;
  if (now - last < LB_MIN_SUBMIT_INTERVAL * 1000) {
    return res.status(429).json({ error: 'submit_too_soon' });
  }
  lastSubmissionAt[name] = now;

  // Upsert to keep only best time per (name, level).
  // If existing time is better, keep it. If new time is better, update.
  const sql = `
    INSERT INTO scores (name, time, level, created_at)
    VALUES (?, ?, ?, CURRENT_TIMESTAMP)
    ON CONFLICT(name, level) DO UPDATE SET
      time = CASE WHEN excluded.time < time THEN excluded.time ELSE time END,
      created_at = CASE WHEN excluded.time < time THEN CURRENT_TIMESTAMP ELSE created_at END
    ;
  `;
  db.run(sql, [name, time, level], function (err) {
    if (err) {
      console.error('DB insert/upsert error', err);
      return res.status(500).json({ error: 'db_error' });
    }
    // Return whether the operation improved or not by fetching saved value
    db.get('SELECT time FROM scores WHERE name = ? AND level = ?', [name, level], (err2, row) => {
      if (err2) {
        console.error('DB select after upsert', err2);
        return res.status(500).json({ error: 'db_error' });
      }
      const savedTime = row ? row.time : null;
      const improved = savedTime === time;
      res.json({ ok: true, improved: !!improved, savedTime });
    });
  });
});

// Get leaderboard
// Query params: level (optional), limit (optional), format=json|text (optional)
app.get('/leaderboard', (req, res) => {
  const level = req.query.level ? sanitizeLevel(req.query.level) : null;
  const limit = Math.max(1, Math.min(100, parseInt(req.query.limit) || 10));
  const format = (req.query.format || '').toLowerCase();
  const wantsJson = format === 'json' || (req.get('accept') || '').includes('application/json');

  let sql = 'SELECT name, time, level FROM scores';
  const params = [];
  if (level) {
    sql += ' WHERE level = ?';
    params.push(level);
  }
  sql += ' ORDER BY time ASC, created_at ASC LIMIT ?';
  params.push(limit);

  db.all(sql, params, (err, rows) => {
    if (err) {
      console.error('DB select error', err);
      return res.status(500).send(wantsJson ? JSON.stringify([]) : '');
    }
    if (wantsJson) {
      return res.json(rows);
    }
    // Build plain-text pipe-separated lines
    const lines = rows.map(r => `${r.name}|${r.time}|${r.level}`);
    res.set('Content-Type', 'text/plain; charset=utf-8');
    res.send(lines.join('\n'));
  });
});

// Optional: endpoint to return raw submission history for audits (authorized only)
app.get('/submissions', (req, res) => {
  if (!verifyAuth(req)) {
    return res.status(401).json({ error: 'unauthorized' });
  }
  const limit = Math.max(1, Math.min(1000, parseInt(req.query.limit) || 100));
  db.all('SELECT id, name, time, level, created_at FROM scores ORDER BY created_at DESC LIMIT ?', [limit], (err, rows) => {
    if (err) {
      console.error('DB select error', err);
      return res.status(500).json([]);
    }
    res.json(rows);
  });
});

// Start HTTP or HTTPS server depending on environment
function startServer() {
  const tlsKeyPath = process.env.LB_TLS_KEY || '';
  const tlsCertPath = process.env.LB_TLS_CERT || '';

  if (tlsKeyPath && tlsCertPath) {
    try {
      const key = fs.readFileSync(tlsKeyPath);
      const cert = fs.readFileSync(tlsCertPath);
      const httpsServer = https.createServer({ key, cert }, app);
      httpsServer.listen(PORT, () => {
        console.log(`OpenMBU leaderboards server listening (HTTPS) on port ${PORT}`);
      });
      return;
    } catch (e) {
      console.error('Failed to start HTTPS server, falling back to HTTP:', e);
    }
  }

  // Fallback to HTTP
  http.createServer(app).listen(PORT, () => {
    console.log(`OpenMBU leaderboards server listening (HTTP) on port ${PORT}`);
  });
}

startServer();