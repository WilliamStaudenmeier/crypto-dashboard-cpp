#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <future>
#include <cstdint>

#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

struct CacheEntry {
  json payload;
  std::chrono::steady_clock::time_point expires_at;
};

std::unordered_map<std::string, CacheEntry> g_api_cache;
std::mutex g_api_cache_mutex;
std::mutex g_snapshot_file_mutex;
std::string g_cors_allow_origin = "*";

struct ApiConfig {
  std::string scheme;
  std::string host;
  std::string base_path;
};

ApiConfig parse_api_base_url(const std::string &url) {
  const std::string default_url = "https://api.coingecko.com/api/v3";
  const std::string input = url.empty() ? default_url : url;

  ApiConfig cfg{"https", "api.coingecko.com", "/api/v3"};
  std::size_t scheme_pos = input.find("://");
  std::size_t host_start = 0;
  if (scheme_pos != std::string::npos) {
    cfg.scheme = input.substr(0, scheme_pos);
    host_start = scheme_pos + 3;
  }

  std::size_t path_start = input.find('/', host_start);
  if (path_start == std::string::npos) {
    cfg.host = input.substr(host_start);
    cfg.base_path = "";
  } else {
    cfg.host = input.substr(host_start, path_start - host_start);
    cfg.base_path = input.substr(path_start);
  }

  if (cfg.host.empty()) cfg.host = "api.coingecko.com";
  return cfg;
}

std::optional<json> fetch_json(const ApiConfig &cfg, const std::string &path,
                               int timeout_seconds = 10, int cache_ttl_seconds = 0) {
  const std::string base = cfg.scheme + "://" + cfg.host;
  httplib::Client client(base);

  if (cfg.scheme == "https") {
    client.enable_server_certificate_verification(true);
  }

  client.set_follow_location(true);
  client.set_read_timeout(timeout_seconds, 0);
  client.set_connection_timeout(timeout_seconds, 0);

  std::string full_path = cfg.base_path + path;

  if (cache_ttl_seconds > 0) {
    std::lock_guard<std::mutex> lock(g_api_cache_mutex);
    auto it = g_api_cache.find(full_path);
    if (it != g_api_cache.end() && std::chrono::steady_clock::now() < it->second.expires_at) {
      return it->second.payload;
    }
  }

  auto response = client.Get(full_path.c_str());
  if (!response || response->status != 200) {
    return std::nullopt;
  }

  try {
    auto parsed = json::parse(response->body);
    if (cache_ttl_seconds > 0) {
      std::lock_guard<std::mutex> lock(g_api_cache_mutex);
      g_api_cache[full_path] = CacheEntry{
          parsed,
          std::chrono::steady_clock::now() + std::chrono::seconds(cache_ttl_seconds),
      };
    }
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

void respond_json(httplib::Response &res, const json &payload, int status = 200) {
  res.status = status;
  res.set_header("Access-Control-Allow-Origin", g_cors_allow_origin);
  res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type");
  res.set_header("Content-Type", "application/json; charset=utf-8");
  res.set_content(payload.dump(), "application/json; charset=utf-8");
}

std::string read_text_file(const std::string &file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return "";
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::int64_t now_epoch_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool is_valid_bootstrap_payload(const json &payload) {
  return payload.is_object() && payload.contains("global") && payload.contains("trending") && payload.contains("markets");
}

std::optional<json> read_json_file(const fs::path &file_path) {
  std::lock_guard<std::mutex> lock(g_snapshot_file_mutex);

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return std::nullopt;
  }

  try {
    json parsed;
    file >> parsed;
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

bool write_json_file(const fs::path &file_path, const json &payload) {
  std::lock_guard<std::mutex> lock(g_snapshot_file_mutex);

  std::error_code ec;
  fs::create_directories(file_path.parent_path(), ec);

  const fs::path temp_path = file_path.string() + ".tmp";
  {
    std::ofstream temp_file(temp_path, std::ios::trunc);
    if (!temp_file.is_open()) {
      return false;
    }
    temp_file << payload.dump();
  }

  fs::rename(temp_path, file_path, ec);
  if (ec) {
    fs::remove(file_path, ec);
    ec.clear();
    fs::rename(temp_path, file_path, ec);
  }

  return !ec;
}

fs::path resolve_snapshot_path(const char *argv0) {
  const char *snapshot_env = std::getenv("SNAPSHOT_PATH");
  if (snapshot_env && std::string(snapshot_env).size() > 0) {
    return fs::absolute(snapshot_env);
  }

  std::vector<fs::path> candidates;
  candidates.emplace_back("db.json");

  if (argv0 && std::string(argv0).size() > 0) {
    fs::path exe_path = fs::absolute(fs::path(argv0));
    fs::path exe_dir = exe_path.parent_path();
    candidates.push_back(exe_dir / "db.json");
    candidates.push_back(exe_dir.parent_path() / "db.json");
  }

  for (const auto &candidate : candidates) {
    std::error_code ec;
    if (fs::exists(candidate, ec)) {
      return fs::absolute(candidate);
    }
  }

  return fs::absolute(candidates.front());
}

std::optional<json> fetch_bootstrap_live(const ApiConfig &api_cfg) {
  const std::string markets_path =
      "/coins/markets?vs_currency=usd&order=market_cap_desc&sparkline=false&price_change_percentage=24h&per_page=20&page=1";

  auto global_future = std::async(std::launch::async, [&]() {
    return fetch_json(api_cfg, "/global", 10, 60);
  });
  auto trending_future = std::async(std::launch::async, [&]() {
    return fetch_json(api_cfg, "/search/trending", 10, 60);
  });
  auto markets_future = std::async(std::launch::async, [&]() {
    return fetch_json(api_cfg, markets_path, 10, 30);
  });

  auto global = global_future.get();
  auto trending = trending_future.get();
  auto markets = markets_future.get();

  if (!global || !trending || !markets) {
    return std::nullopt;
  }

  json history = json::object();
  std::vector<std::future<std::optional<json>>> history_futures;
  std::vector<std::string> history_coin_ids;

  const std::size_t top_count = std::min<std::size_t>(5, markets->is_array() ? markets->size() : 0);
  for (std::size_t i = 0; i < top_count; ++i) {
    const auto &coin = (*markets)[i];
    if (!coin.is_object() || !coin.contains("id") || !coin["id"].is_string()) {
      continue;
    }
    const std::string coin_id = coin["id"].get<std::string>();
    const std::string path = "/coins/" + coin_id + "/market_chart?vs_currency=usd&days=365&interval=daily";
    history_coin_ids.push_back(coin_id);
    history_futures.push_back(std::async(std::launch::async, [api_cfg, path]() {
      return fetch_json(api_cfg, path, 10, 300);
    }));
  }

  for (std::size_t i = 0; i < history_futures.size(); ++i) {
    auto history_payload = history_futures[i].get();
    if (history_payload && history_payload->contains("prices")) {
      history[history_coin_ids[i]] = (*history_payload)["prices"];
    }
  }

  return json{{"global", *global},
              {"trending", *trending},
              {"markets", *markets},
              {"history", history},
              {"meta", {{"source", "live"}, {"updated_at_epoch_ms", now_epoch_ms()}}}};
}

fs::path resolve_static_dir(const char *argv0) {
  std::vector<fs::path> candidates;

  const char *static_dir_env = std::getenv("STATIC_DIR");
  if (static_dir_env && std::string(static_dir_env).size() > 0) {
    candidates.emplace_back(static_dir_env);
  }

  candidates.emplace_back("static");

  if (argv0 && std::string(argv0).size() > 0) {
    fs::path exe_path = fs::absolute(fs::path(argv0));
    fs::path exe_dir = exe_path.parent_path();
    candidates.push_back(exe_dir / "static");
    candidates.push_back(exe_dir.parent_path() / "static");
  }

  for (const auto &candidate : candidates) {
    std::error_code ec;
    if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
      return fs::absolute(candidate);
    }
  }

  return fs::absolute("static");
}

int main(int argc, char **argv) {
  const char *port_env = std::getenv("PORT");
  const int port = port_env ? std::stoi(port_env) : 8080;

  const char *base_url_env = std::getenv("COINGECKO_BASE_URL");
  const char *cors_origin_env = std::getenv("FRONTEND_ORIGIN");
  const ApiConfig api_cfg = parse_api_base_url(base_url_env ? base_url_env : "");
  if (cors_origin_env && std::string(cors_origin_env).size() > 0) {
    g_cors_allow_origin = cors_origin_env;
  }
  const fs::path static_dir = resolve_static_dir(argc > 0 ? argv[0] : nullptr);
  const fs::path snapshot_path = resolve_snapshot_path(argc > 0 ? argv[0] : nullptr);

  httplib::Server server;

  server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
    respond_json(res, json{{"ok", true}});
  });

  server.Get("/api/global", [&](const httplib::Request &, httplib::Response &res) {
    auto data = fetch_json(api_cfg, "/global", 10, 60);
    if (!data) {
      respond_json(res, json{{"error", "Failed to fetch global market data"}}, 502);
      return;
    }
    respond_json(res, *data);
  });

  server.Get("/api/bootstrap", [&](const httplib::Request &req, httplib::Response &res) {
    const bool force_refresh =
        req.has_param("refresh") && (req.get_param_value("refresh") == "1" || req.get_param_value("refresh") == "true");

    if (!force_refresh) {
      auto snapshot = read_json_file(snapshot_path);
      if (snapshot && is_valid_bootstrap_payload(*snapshot)) {
        json stale_payload = *snapshot;
        if (!stale_payload.contains("meta") || !stale_payload["meta"].is_object()) {
          stale_payload["meta"] = json::object();
        }
        stale_payload["meta"]["source"] = "snapshot";
        stale_payload["meta"]["served_at_epoch_ms"] = now_epoch_ms();
        res.set_header("Cache-Control", "no-cache");
        respond_json(res, stale_payload);
        return;
      }
    }

    auto live_payload = fetch_bootstrap_live(api_cfg);
    if (live_payload) {
      write_json_file(snapshot_path, *live_payload);
      res.set_header("Cache-Control", "no-cache");
      respond_json(res, *live_payload);
      return;
    }

    auto fallback = read_json_file(snapshot_path);
    if (fallback && is_valid_bootstrap_payload(*fallback)) {
      json fallback_payload = *fallback;
      if (!fallback_payload.contains("meta") || !fallback_payload["meta"].is_object()) {
        fallback_payload["meta"] = json::object();
      }
      fallback_payload["meta"]["source"] = "snapshot-fallback";
      fallback_payload["meta"]["served_at_epoch_ms"] = now_epoch_ms();
      fallback_payload["meta"]["warning"] = "live-refresh-failed";
      res.set_header("Cache-Control", "no-cache");
      respond_json(res, fallback_payload);
      return;
    }

    respond_json(res, json{{"error", "Failed to fetch bootstrap market data"}}, 502);
  });

  server.Get("/api/trending", [&](const httplib::Request &, httplib::Response &res) {
    auto data = fetch_json(api_cfg, "/search/trending", 10, 60);
    if (!data) {
      respond_json(res, json{{"error", "Failed to fetch trending data"}}, 502);
      return;
    }
    respond_json(res, *data);
  });

  server.Get("/api/markets", [&](const httplib::Request &req, httplib::Response &res) {
    const std::string vs_currency = req.has_param("vs_currency") ? req.get_param_value("vs_currency") : "usd";
    const std::string page = req.has_param("page") ? req.get_param_value("page") : "1";
    const std::string per_page = req.has_param("per_page") ? req.get_param_value("per_page") : "20";

    const std::string path = "/coins/markets?vs_currency=" + vs_currency +
                             "&order=market_cap_desc&sparkline=false&price_change_percentage=24h" +
                             "&per_page=" + per_page + "&page=" + page;

    auto data = fetch_json(api_cfg, path, 10, 30);
    if (!data) {
      respond_json(res, json{{"error", "Failed to fetch market list"}}, 502);
      return;
    }
    respond_json(res, *data);
  });

  server.Get("/api/history", [&](const httplib::Request &req, httplib::Response &res) {
    if (!req.has_param("coin_id")) {
      respond_json(res, json{{"error", "coin_id is required"}}, 400);
      return;
    }

    const std::string coin_id = req.get_param_value("coin_id");
    const std::string days = req.has_param("days") ? req.get_param_value("days") : "365";
    const std::string vs_currency = req.has_param("vs_currency") ? req.get_param_value("vs_currency") : "usd";

    const std::string path = "/coins/" + coin_id + "/market_chart?vs_currency=" + vs_currency +
                             "&days=" + days + "&interval=daily";

    auto data = fetch_json(api_cfg, path, 10, 300);
    if (!data) {
      respond_json(res, json{{"error", "Failed to fetch market history"}}, 502);
      return;
    }
    respond_json(res, *data);
  });

  server.Get("/", [&](const httplib::Request &, httplib::Response &res) {
    const std::string html = read_text_file((static_dir / "index.html").string());
    if (html.empty()) {
      res.status = 500;
      res.set_content("Dashboard HTML not found", "text/plain; charset=utf-8");
      return;
    }
    res.set_header("Cache-Control", "no-cache");
    res.set_content(html, "text/html; charset=utf-8");
  });

  server.Get("/styles.css", [&](const httplib::Request &, httplib::Response &res) {
    const std::string css = read_text_file((static_dir / "styles.css").string());
    if (css.empty()) {
      res.status = 404;
      return;
    }
    res.set_header("Cache-Control", "public, max-age=300");
    res.set_content(css, "text/css; charset=utf-8");
  });

  server.Get("/app.js", [&](const httplib::Request &, httplib::Response &res) {
    const std::string js = read_text_file((static_dir / "app.js").string());
    if (js.empty()) {
      res.status = 404;
      return;
    }
    res.set_header("Cache-Control", "public, max-age=300");
    res.set_content(js, "application/javascript; charset=utf-8");
  });

  std::cout << "Crypto Dashboard C++ listening on port " << port << std::endl;
  server.listen("0.0.0.0", port);
  return 0;
}
