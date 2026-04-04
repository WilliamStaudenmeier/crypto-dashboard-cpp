const formatMoney = (value) =>
  new Intl.NumberFormat("en-US", {
    style: "currency",
    currency: "USD",
    maximumFractionDigits: value > 100 ? 0 : 2,
  }).format(value || 0);

const formatPercent = (value) => `${(value || 0).toFixed(2)}%`;
let topFiveCoins = [];
const HISTORY_DAYS = 365;
let chartsLoaded = false;

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Request failed: ${url}`);
  }
  return response.json();
}

function setupSidebarToggle() {
  const button = document.getElementById("sidebar-toggle");
  if (!button) {
    return;
  }

  button.addEventListener("click", () => {
    const collapsed = document.body.classList.toggle("sidebar-collapsed");
    button.textContent = collapsed ? "Show Charts" : "Hide Charts";
    button.setAttribute("aria-expanded", String(!collapsed));

    if (!collapsed && !chartsLoaded && topFiveCoins.length) {
      renderLineCharts(topFiveCoins, HISTORY_DAYS)
        .then(() => {
          chartsLoaded = true;
        })
        .catch((error) => {
          console.error(error);
        });
    }
  });
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

function buildSparkline(prices) {
  if (!prices.length) {
    return "";
  }

  const width = 375;
  const height = 100;
  const min = Math.min(...prices);
  const max = Math.max(...prices);
  const span = Math.max(max - min, 0.0001);

  const points = prices
    .map((price, index) => {
      const x = (index / (prices.length - 1 || 1)) * width;
      const y = height - ((price - min) / span) * height;
      return `${x.toFixed(2)},${y.toFixed(2)}`;
    })
    .join(" ");

  const end = prices[prices.length - 1] || 0;
  const start = prices[0] || 0;
  const up = end >= start;
  const stroke = up ? "#50f0a4" : "#ff6b6b";

  return `
    <svg class="sparkline" viewBox="0 0 ${width} ${height}" preserveAspectRatio="none" aria-hidden="true">
      <polyline fill="none" stroke="${stroke}" stroke-width="2.2" points="${points}" />
    </svg>
  `;
}

async function renderLineCharts(coins, days) {
  const host = document.getElementById("line-charts");
  host.innerHTML = "<p class=\"chart-meta\">Loading chart history...</p>";

  const results = await Promise.all(
    coins.map(async (coin) => {
      try {
        const data = await fetchJson(`/api/history?coin_id=${encodeURIComponent(coin.id)}&days=${days}&vs_currency=usd`);
        const prices = (data?.prices || []).map((entry) => entry[1]);
        return { coin, prices };
      } catch {
        return { coin, prices: [] };
      }
    }),
  );

  host.innerHTML = "";

  results.forEach(({ coin, prices }) => {
    const card = document.createElement("article");
    card.className = "chart-card";

    if (!prices.length) {
      card.innerHTML = `
        <h3 class="chart-title">${coin.name}</h3>
        <p class="chart-meta">No data available.</p>
      `;
      host.appendChild(card);
      return;
    }

    const delta = prices[prices.length - 1] - prices[0];
    const deltaPct = prices[0] ? (delta / prices[0]) * 100 : 0;
    const deltaClass = delta >= 0 ? "change-up" : "change-down";

    card.innerHTML = `
      <h3 class="chart-title">${coin.name}</h3>
      ${buildSparkline(prices)}
      <p class="chart-meta ${deltaClass}">${formatPercent(deltaPct)} over 1Y</p>
    `;
    host.appendChild(card);
  });
}

async function bootstrap() {
  try {
    setupSidebarToggle();

    const bootstrapData = await fetchJson("/api/bootstrap");
    const globalData = bootstrapData?.global;
    const trending = bootstrapData?.trending;
    const markets = bootstrapData?.markets || [];

    renderGlobal(globalData);
    renderTrending(trending);
    renderMarkets(markets);

    topFiveCoins = markets.slice(0, 5).map((coin) => ({ id: coin.id, name: coin.name }));
    if (!document.body.classList.contains("sidebar-collapsed")) {
      await renderLineCharts(topFiveCoins, HISTORY_DAYS);
      chartsLoaded = true;
    }
  } catch (error) {
    console.error(error);
  }
}

bootstrap();
