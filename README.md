# Crypto Dashboard C++

A lightweight cryptocurrency market dashboard built with C++.

## Overview

- C++17 HTTP server using `cpp-httplib`
- JSON parsing with `nlohmann/json`
- Server-side CoinGecko API integration
- Lightweight static dashboard UI served by the C++ app
- Render free-tier deployment via Docker

## Project Structure

- `src/main.cpp`: C++ server and API proxy routes
- `static/index.html`: Dashboard layout
- `static/styles.css`: Styling
- `static/app.js`: Client-side rendering logic
- `CMakeLists.txt`: C++ build config
- `Dockerfile`: Containerized deploy/runtime
- `render.yaml`: Render Blueprint

## Local Development

### 1. Configure environment

```bash
cp .env.example .env
```

Optional variables:

- `COINGECKO_BASE_URL` (default: `https://api.coingecko.com/api/v3`)
- `COINGECKO_API_KEY` (optional)
- `PORT` (default: `8080`)

### 2. Build

```bash
cmake -S . -B build
cmake --build build
```

### 3. Run

```bash
PORT=8080 ./build/crypto_dashboard_cpp
```

Open http://localhost:8080

## API Endpoints

- `GET /health`
- `GET /api/global`
- `GET /api/trending`
- `GET /api/markets?vs_currency=usd&per_page=20&page=1`
