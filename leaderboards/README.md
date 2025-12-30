# OpenMBU Leaderboards (improved)

This repository contains a small leaderboard server for OpenMBU with the following improvements:
- Rate-limiting and basic anti-cheat heuristics
- Input validation and sanitization
- API key and optional HMAC signature verification
- Optional HTTPS support (provide TLS cert/key paths)
- CORS restriction configurable via env
- Keeps only best time per player per level (upsert)
- Returns plain-text (default) or JSON (`?format=json` or `Accept: application/json`)
- Dockerfile + docker-compose for easy deployment

## Quick start (local)

1. Install dependencies:

```bash
cd leaderboards
npm install
```

2. Start server:

```bash
LB_API_KEY=mysupersecretkey npm start
# or with HMAC signing:
# LB_HMAC_SECRET=verysecret npm start
```

By default the server listens on `http://localhost:3000`.

## Environment variables

- `PORT` (default 3000)
- `LB_API_KEY` - (optional) pre-shared API key. Clients may send `x-api-key` header or `?key=` query param.
- `LB_HMAC_SECRET` - (optional) HMAC secret to verify request body. Clients must send `x-signature` header containing hex HMAC-SHA256 of the raw request body.
- `LB_ALLOWED_ORIGINS` - comma-separated list of allowed CORS origins (default `http://localhost:3000`)
- `LB_MIN_TIME` / `LB_MAX_TIME` - min/max plausible times in seconds (defaults: 0.01, 36000)
- `LB_MIN_SUBMIT_INTERVAL` - minimum seconds between submissions for the same player to help rate-limit (default: 3)
- `LB_TLS_KEY` - path to TLS key file (if provided, server attempts HTTPS)
- `LB_TLS_CERT` - path to TLS cert file

## API

- POST /score
  - Body (JSON): `{ "name": "Player", "time": 123.45, "level": "level1" }`
  - Auth: either `?key=...` or header `x-api-key: ...` OR provide HMAC `x-signature` (hex) computed with `LB_HMAC_SECRET` over the raw JSON body.
  - Returns JSON: `{ ok: true, improved: true, savedTime: 123.45 }`

- GET /leaderboard?level=<level>&limit=<n>&format=json
  - Returns top scores for the level. Default returns pipe-separated text lines (`name|time|level`).
  - Add `format=json` or `Accept: application/json` to receive JSON array.

- GET /submissions (authorized)
  - Returns recent entries (for audits). Requires auth (API key or HMAC).

## Security & production notes

- The built-in anti-cheat and rate-limiting are minimal. For production:
  - Use HTTPS and strong credentials.
  - Use a server-side anti-cheat system, trusted client signing, and stricter validation.
  - Use persistent stores (Redis) for rate limits / request tracking.
  - Add request logging, IP blocking, and monitoring.
  - Consider server-side replay detection and signed client builds.

## Docker

Build & run:

```bash
cd leaderboards
docker build -t openmbu-leaderboards:latest .
docker run -e LB_API_KEY=mysupersecretkey -p 3000:3000 openmbu-leaderboards:latest
```

Or use `docker-compose` (example provided).
