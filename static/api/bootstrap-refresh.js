const fs = require("node:fs/promises");
const path = require("node:path");

const RENDER_BASE = process.env.RENDER_API_BASE || "https://crypto-dashboard-cpp.onrender.com";

async function readStaleSnapshot() {
  try {
    const snapshotPath = path.join(process.cwd(), "db.json");
    const raw = await fs.readFile(snapshotPath, "utf8");
    return JSON.parse(raw);
  } catch {
    return null;
  }
}

module.exports = async (req, res) => {
  res.setHeader("Content-Type", "application/json; charset=utf-8");
  res.setHeader("Cache-Control", "no-store");

  try {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 8000);

    const response = await fetch(`${RENDER_BASE}/api/bootstrap?refresh=1`, {
      signal: controller.signal,
      headers: {
        Accept: "application/json",
      },
    });

    clearTimeout(timeout);

    if (!response.ok) {
      throw new Error(`Render bootstrap failed: ${response.status}`);
    }

    const payload = await response.json();
    res.statusCode = 200;
    res.end(JSON.stringify(payload));
    return;
  } catch {
    const stale = await readStaleSnapshot();
    if (stale) {
      if (!stale.meta || typeof stale.meta !== "object") {
        stale.meta = {};
      }
      stale.meta.source = "vercel-snapshot-fallback";
      res.statusCode = 200;
      res.end(JSON.stringify(stale));
      return;
    }

    res.statusCode = 502;
    res.end(JSON.stringify({ error: "Failed to refresh bootstrap data" }));
  }
};
