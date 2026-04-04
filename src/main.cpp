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

std::optional<json> fetch_json(const ApiConfig &cfg, const std::string &path, const std::string &api_key,
                               int timeout_seconds = 10, int cache_ttl_seconds = 0) {
  const std::string base = cfg.scheme + "://" + cfg.host;
  httplib::Client client(base);

  if (cfg.scheme == "https") {
    client.enable_server_certificate_verification(true);
  }

  client.set_follow_location(true);
  client.set_read_timeout(timeout_seconds, 0);
  client.set_connection_timeout(timeout_seconds, 0);

  httplib::Headers headers;
  if (!api_key.empty()) {
    headers.emplace("x-cg-pro-api-key", api_key);
    headers.emplace("x-cg-demo-api-key", api_key);
  }

  std::string full_path = cfg.base_path + path;
  if (!api_key.empty() && full_path.find("x_cg_demo_api_key=") == std::string::npos) {
    const char separator = full_path.find('?') == std::string::npos ? '?' : '&';
    full_path += separator;
    full_path += "x_cg_demo_api_key=" + api_key;
  }

  if (cache_ttl_seconds > 0) {
    std::lock_guard<std::mutex> lock(g_api_cache_mutex);
    auto it = g_api_cache.find(full_path);
    if (it != g_api_cache.end() && std::chrono::steady_clock::now() < it->second.expires_at) {
      return it->second.payload;
    }
  }

  auto response = client.Get(full_path.c_str(), headers);
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
  const char *api_key_env = std::getenv("COINGECKO_API_KEY");

  const ApiConfig api_cfg = parse_api_base_url(base_url_env ? base_url_env : "");
  const std::string api_key = api_key_env ? api_key_env : "";
  const fs::path static_dir = resolve_static_dir(argc > 0 ? argv[0] : nullptr);

  httplib::Server server;

  server.Get("/health", [](const httplib::Request &, httplib::Response &res) {
    respond_json(res, json{{"ok", true}});
  });

  server.Get("/api/global", [&](const httplib::Request &, httplib::Response &res) {
    auto data = fetch_json(api_cfg, "/global", api_key, 10, 60);
    if (!data) {
      respond_json(res, json{{"error", "Failed to fetch global market data"}}, 502);
      return;
    }
    respond_json(res, *data);
  });

  server.Get("/api/trending", [&](const httplib::Request &, httplib::Response &res) {
    auto data = fetch_json(api_cfg, "/search/trending", api_key, 10, 60);
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

    auto data = fetch_json(api_cfg, path, api_key, 10, 30);
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

    auto data = fetch_json(api_cfg, path, api_key, 10, 300);
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
    res.set_content(html, "text/html; charset=utf-8");
  });

  server.Get("/styles.css", [&](const httplib::Request &, httplib::Response &res) {
    const std::string css = read_text_file((static_dir / "styles.css").string());
    if (css.empty()) {
      res.status = 404;
      return;
    }
    res.set_content(css, "text/css; charset=utf-8");
  });

  server.Get("/app.js", [&](const httplib::Request &, httplib::Response &res) {
    const std::string js = read_text_file((static_dir / "app.js").string());
    if (js.empty()) {
      res.status = 404;
      return;
    }
    res.set_content(js, "application/javascript; charset=utf-8");
  });

  std::cout << "Crypto Dashboard C++ listening on port " << port << std::endl;
  server.listen("0.0.0.0", port);
  return 0;
}
