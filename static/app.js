const formatMoney = (value) =>
  new Intl.NumberFormat("en-US", {
    style: "currency",
    currency: "USD",
    maximumFractionDigits: value > 100 ? 0 : 2,
  }).format(value || 0);

const formatPercent = (value) => `${(value || 0).toFixed(2)}%`;
const EXPLICIT_API_BASE = (window.__API_BASE_URL__ || "").replace(/\/$/, "");
const DEFAULT_RENDER_API_BASE = "https://crypto-dashboard-cpp.onrender.com";
const API_BASE_URL =
  EXPLICIT_API_BASE ||
  (window.location.hostname === "localhost" || window.location.hostname === "127.0.0.1" ? "" : DEFAULT_RENDER_API_BASE);

function apiUrl(path) {
  return `${API_BASE_URL}${path}`;
}

function refreshUrl() {
  const isLocal = window.location.hostname === "localhost" || window.location.hostname === "127.0.0.1";
  return isLocal ? apiUrl("/api/bootstrap?refresh=1") : "/api/bootstrap-refresh";
}

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Request failed: ${url}`);
  }
  return response.json();
}

function renderGlobal(globalData) {
  const usd = globalData?.data?.total_market_cap?.usd || 0;
  const volume = globalData?.data?.total_volume?.usd || 0;
  const btcDom = globalData?.data?.market_cap_percentage?.btc || 0;
  const ethDom = globalData?.data?.market_cap_percentage?.eth || 0;

  document.getElementById("market-cap").textContent = formatMoney(usd);
  document.getElementById("volume").textContent = formatMoney(volume);
  document.getElementById("btc-dom").textContent = formatPercent(btcDom);
  document.getElementById("eth-dom").textContent = formatPercent(ethDom);
}

function renderTrending(payload) {
  const list = document.getElementById("trending-list");
  list.innerHTML = "";
  const coins = payload?.coins || [];
  coins.slice(0, 7).forEach(({ item }) => {
    const li = document.createElement("li");
    li.innerHTML = `<span>${item.name} (${item.symbol})</span><span>#${item.market_cap_rank || "-"}</span>`;
    list.appendChild(li);
  });
}

function renderMarkets(markets) {
  const body = document.getElementById("coins-body");
  body.innerHTML = "";

  markets.forEach((coin) => {
    const tr = document.createElement("tr");
    const change = coin.price_change_percentage_24h || 0;
    const changeClass = change >= 0 ? "change-up" : "change-down";

    tr.innerHTML = `
      <td>${coin.name} (${coin.symbol.toUpperCase()})</td>
      <td>${formatMoney(coin.current_price)}</td>
      <td class="${changeClass}">${formatPercent(change)}</td>
      <td>${formatMoney(coin.market_cap)}</td>
      <td>${coin.market_cap_rank || "-"}</td>
    `;

    body.appendChild(tr);
  });
}


async function bootstrap() {
  try {
    let bootstrapData;
    try {
      // Stale-first: render snapshot instantly.
      bootstrapData = await fetchJson("/db.json");
    } catch {
      // Fallback to backend bootstrap if snapshot is unavailable.
      bootstrapData = await fetchJson(apiUrl("/api/bootstrap"));
    }

    renderGlobal(bootstrapData?.global);
    renderTrending(bootstrapData?.trending);
    renderMarkets(bootstrapData?.markets || []);

    // Refresh right after initial render and replace stale data with live data.
    setTimeout(async () => {
      try {
        const refreshedData = await fetchJson(refreshUrl());
        renderGlobal(refreshedData?.global);
        renderTrending(refreshedData?.trending);
        renderMarkets(refreshedData?.markets || []);
      } catch (error) {
        console.error(error);
      }
    }, 0);
  } catch (error) {
    console.error(error);
  }
}

bootstrap();
