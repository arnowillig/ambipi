// File: main.cpp
// Build: your existing CMakeLists.txt works.
// Run:   ./wled_rest --port 9080 --threads 2
//
// What this does:
// - Keeps the two API endpoints:
//     POST /api/v1/wled/dnrgb-range
//     POST /api/v1/wled/ranges-color
// - Adds CORS (simple + preflight).
// - Uses REUSE_ADDR on the HTTP server.
// - Serves static files from disk (no embedded strings):
//     GET /            => ./public/index.html
//     GET /swagger.json => ./public/swagger.json
//
// Notes:
// - All comments and logs are in English.
// - Adjust PUBLIC_DIR via environment variable if needed.

#include <pistache/endpoint.h>
#include <pistache/router.h>
#include <pistache/http.h>
#include <pistache/net.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <csignal>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

// ---- UDP sender (original function, logs in English) ----
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

bool sendWledDnRgbRange(const char* host, const char* port, uint16_t startIndex,
                        const std::vector<uint8_t>& rgb_values, uint8_t timeout_seconds = 1)
{
    if (rgb_values.size() % 3 != 0) {
        std::cerr << "[ERROR] rgb_values length must be a multiple of 3.\n";
        return false;
    }
    size_t ledCount = rgb_values.size() / 3;
    if (ledCount == 0) {
        std::cerr << "[WARN] No LEDs to send.\n";
        return true;
    }

    // Safe UDP payload size; chunk when necessary
    const size_t max_payload = 512; // conservative
    size_t needed = 1 + 1 + 2 + 3 * ledCount; // mode + timeout + start hi/lo + RGBs
    if (needed > max_payload) {
        std::cerr << "[INFO] Packet too large (" << needed << " bytes), splitting into chunks.\n";
        size_t max_leds_per = (max_payload - 4) / 3;
        for (size_t offset = 0; offset < ledCount; offset += max_leds_per) {
            size_t chunk = std::min(max_leds_per, ledCount - offset);
            std::vector<uint8_t> sub(rgb_values.begin() + offset * 3,
                                     rgb_values.begin() + (offset + chunk) * 3);
            if (!sendWledDnRgbRange(host, port, startIndex + static_cast<uint16_t>(offset), sub, timeout_seconds)) {
                return false;
            }
        }
        return true;
    }

    std::vector<uint8_t> packet;
    packet.reserve(needed);
    packet.push_back(4); // DNRGB mode
    packet.push_back(timeout_seconds);
    packet.push_back(static_cast<uint8_t>((startIndex >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(startIndex & 0xFF));
    packet.insert(packet.end(), rgb_values.begin(), rgb_values.end());

    struct addrinfo hints{};
    struct addrinfo* res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        std::cerr << "[ERROR] getaddrinfo failed: " << gai_strerror(rc) << "\n";
        return false;
    }

    int sock = -1;
    struct addrinfo* p;
    for (p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == -1) continue;
        break;
    }
    if (p == nullptr || sock == -1) {
        std::cerr << "[ERROR] Could not create socket.\n";
        freeaddrinfo(res);
        return false;
    }

    ssize_t sent = sendto(sock, packet.data(), packet.size(), 0, p->ai_addr, p->ai_addrlen);
    if (sent != static_cast<ssize_t>(packet.size())) {
        std::cerr << "[ERROR] sendto failed: " << strerror(errno)
                  << " sent: " << sent << " of " << packet.size() << "\n";
        close(sock);
        freeaddrinfo(res);
        return false;
    }

    close(sock);
    freeaddrinfo(res);
    return true;
}
// ---- end UDP sender ----

using json = nlohmann::json;
using namespace Pistache;

namespace {
std::atomic<bool> g_running{true};
void handleSignal(int) { g_running = false; }

// --- Helpers: color parsing ---
static inline bool parseHexByte(char hi, char lo, uint8_t& out) {
    auto hex = [](char c)->int {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        return -1;
    };
    int a = hex(hi), b = hex(lo);
    if (a < 0 || b < 0) return false;
    out = static_cast<uint8_t>((a << 4) | b);
    return true;
}
static bool parseHexColor(const std::string& s, uint8_t& r, uint8_t& g, uint8_t& b) {
    std::string v = s;
    if (!v.empty() && v[0] == '#') v.erase(0, 1);
    if (v.rfind("0x", 0) == 0 || v.rfind("0X", 0) == 0) v.erase(0, 2);
    if (v.size() != 6) return false;
    return parseHexByte(v[0], v[1], r) &&
           parseHexByte(v[2], v[3], g) &&
           parseHexByte(v[4], v[5], b);
}

// --- Small static file loader ---
static std::string readFile(const std::string& path, bool* ok) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) { if (ok) *ok = false; return {}; }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    if (ok) *ok = true;
    return oss.str();
}
static std::string guessMime(const std::string& path) {
    static const std::unordered_map<std::string, std::string> m = {
        {".html","text/html"}, {".htm","text/html"}, {".json","application/json"},
        {".js","application/javascript"}, {".css","text/css"},
        {".svg","image/svg+xml"}, {".png","image/png"}, {".jpg","image/jpeg"},
        {".jpeg","image/jpeg"}, {".ico","image/x-icon"}, {".txt","text/plain"}
    };
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    auto it = m.find(path.substr(dot));
    return (it != m.end()) ? it->second : "application/octet-stream";
}

struct WledApiServer {
    explicit WledApiServer(Address addr, int threads)
        : httpEndpoint_(std::make_shared<Http::Endpoint>(addr)), threads_(threads) {

        const char* env = std::getenv("PUBLIC_DIR");
        publicDir_ = env ? std::string(env) : std::string("public");
    }

    void init() {
        // Ensure REUSE_ADDR is set
        auto opts = Http::Endpoint::options()
                        .threads(static_cast<unsigned int>(std::max(1, threads_)))
                        .flags(Tcp::Options::ReuseAddr);
        httpEndpoint_->init(opts);
        setupRoutes();
    }

    void start() {
        httpEndpoint_->setHandler(router_.handler());
        httpEndpoint_->serveThreaded();
    }
    void shutdown() { httpEndpoint_->shutdown(); }

private:
    // --- CORS helpers ---
    static void addCors(Http::ResponseWriter& resp) {
        using namespace Http;
        resp.headers()
            .add<Http::Header::AccessControlAllowOrigin>("*")
            .add<Http::Header::AccessControlAllowMethods>("GET, POST, OPTIONS")
            .add<Http::Header::AccessControlAllowHeaders>("Content-Type, Authorization");
    }
    static void sendJson(Http::ResponseWriter& resp, Http::Code code, const json& j) {
        addCors(resp);
        resp.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        resp.send(code, j.dump());
    }
    static void sendHtml(Http::ResponseWriter& resp, const std::string& body, Http::Code code = Http::Code::Ok) {
        addCors(resp);
        resp.headers().add<Http::Header::ContentType>(MIME(Text, Html));
        resp.send(code, body);
    }
    static void sendRaw(Http::ResponseWriter& resp, const std::string& body, const std::string& mime, Http::Code code = Http::Code::Ok) {
        addCors(resp);
        resp.headers().add<Http::Header::ContentType>(Http::Mime::MediaType(mime));
        resp.send(code, body);
    }

    void setupRoutes() {
        using namespace Rest;

        // REST endpoints
        Routes::Post(router_, "/api/v1/wled/dnrgb-range",
                     Routes::bind(&WledApiServer::postDnRgbRange, this));
        Routes::Post(router_, "/api/v1/wled/ranges-color",
                     Routes::bind(&WledApiServer::postRangesColor, this));

        // CORS preflight
        Routes::Options(router_, "/api/v1/wled/dnrgb-range",
                        Routes::bind(&WledApiServer::optionsAny, this));
        Routes::Options(router_, "/api/v1/wled/ranges-color",
                        Routes::bind(&WledApiServer::optionsAny, this));

        // Static files from disk
        Routes::Get(router_, "/",                    Routes::bind(&WledApiServer::getIndex, this));
        Routes::Get(router_, "/swagger.json",        Routes::bind(&WledApiServer::getSwagger, this));

        // Optional: serve additional assets under /public (e.g., /public/app.js, /public/app.css)
        Routes::Get(router_, "/public/:file",        Routes::bind(&WledApiServer::getPublicFile, this));
        Routes::Options(router_, "/",                Routes::bind(&WledApiServer::optionsAny, this));
        Routes::Options(router_, "/swagger.json",    Routes::bind(&WledApiServer::optionsAny, this));
        Routes::Options(router_, "/public/:file",    Routes::bind(&WledApiServer::optionsAny, this));
    }

    // --- Static handlers ---
    void getIndex(const Rest::Request&, Http::ResponseWriter resp) {
        const std::string path = publicDir_ + "/index.html";
        bool ok=false; auto data = readFile(path, &ok);
        if (!ok) return sendRaw(resp, "index.html not found", "text/plain", Http::Code::Not_Found);
        return sendRaw(resp, data, guessMime(path), Http::Code::Ok);
    }

    void getSwagger(const Rest::Request&, Http::ResponseWriter resp) {
        const std::string path = publicDir_ + "/swagger.json";
        bool ok=false; auto data = readFile(path, &ok);
        if (!ok) return sendRaw(resp, "swagger.json not found", "text/plain", Http::Code::Not_Found);
        return sendRaw(resp, data, guessMime(path), Http::Code::Ok);
    }

    void getPublicFile(const Rest::Request& req, Http::ResponseWriter resp) {
        const auto fname = req.param(":file").as<std::string>();
        const std::string path = publicDir_ + "/" + fname;
        bool ok=false; auto data = readFile(path, &ok);
        if (!ok) return sendRaw(resp, "file not found", "text/plain", Http::Code::Not_Found);
        return sendRaw(resp, data, guessMime(path), Http::Code::Ok);
    }

    void optionsAny(const Rest::Request&, Http::ResponseWriter resp) {
        addCors(resp);
        resp.send(Http::Code::No_Content);
    }

    // --- POST /api/v1/wled/dnrgb-range ---
    void postDnRgbRange(const Rest::Request& req, Http::ResponseWriter resp) {
        json body;
        try { body = json::parse(req.body()); }
        catch (const std::exception& e) {
            return sendJson(resp, Http::Code::Bad_Request,
                            {{"ok", false}, {"error", std::string("Invalid JSON: ") + e.what()}});
        }

        std::string host, port;
        uint32_t startIndex32 = 0;
        uint8_t timeoutSeconds = 1;
        std::vector<uint8_t> rgb;

        if (auto it = body.find("host"); it != body.end() && it->is_string()) host = *it;
        else return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "Field 'host' (string) is required"}});

        if (auto it = body.find("port"); it != body.end() && it->is_string()) port = *it;
        else return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "Field 'port' (string) is required"}});

        if (auto it = body.find("startIndex"); it != body.end() && (it->is_number_unsigned() || it->is_number_integer())) {
            startIndex32 = it->get<uint32_t>();
            if (startIndex32 > 0xFFFF)
                return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "startIndex must be in [0, 65535]"}});
        } else {
            return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "Field 'startIndex' (uint16) is required"}});
        }

        if (auto it = body.find("timeoutSeconds"); it != body.end()) {
            if (!(it->is_number_unsigned() || it->is_number_integer()))
                return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "timeoutSeconds must be an integer [0..255]"}})
            ;
            uint32_t ts = it->get<uint32_t>();
            if (ts > 255)
                return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "timeoutSeconds must be in [0, 255]"}});
            timeoutSeconds = static_cast<uint8_t>(ts);
        }

        if (auto it = body.find("rgb"); it != body.end() && it->is_array()) {
            rgb.reserve(it->size());
            for (const auto& v : *it) {
                if (!(v.is_number_unsigned() || v.is_number_integer()))
                    return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "All 'rgb' values must be integers [0..255]"}});

                int val = v.get<int>();
                if (val < 0 || val > 255)
                    return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "All 'rgb' values must be in [0..255]"}});

                rgb.push_back(static_cast<uint8_t>(val));
            }
        } else {
            return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "Field 'rgb' (array of bytes) is required"}});
        }

        if (rgb.empty())
            return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "'rgb' must not be empty"}});
        if (rgb.size() % 3 != 0)
            return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "'rgb' length must be a multiple of 3 (R,G,B)"}});

        bool ok = sendWledDnRgbRange(host.c_str(), port.c_str(),
                                     static_cast<uint16_t>(startIndex32),
                                     rgb, timeoutSeconds);

        if (!ok)
            return sendJson(resp, Http::Code::Internal_Server_Error, {{"ok", false}, {"error", "Sending failed. See server logs for details."}});

        return sendJson(resp, Http::Code::Ok, {{"ok", true}});
    }

    // --- POST /api/v1/wled/ranges-color ---
    void postRangesColor(const Rest::Request& req, Http::ResponseWriter resp) {
        json body;
        try { body = json::parse(req.body()); }
        catch (const std::exception& e) {
            return sendJson(resp, Http::Code::Bad_Request,
                            {{"ok", false}, {"error", std::string("Invalid JSON: ") + e.what()}});
        }

        std::string host, port;
        uint8_t timeoutSeconds = 1;

        if (auto it = body.find("host"); it != body.end() && it->is_string()) host = *it;
        else return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "Field 'host' (string) is required"}});

        if (auto it = body.find("port"); it != body.end() && it->is_string()) port = *it;
        else return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "Field 'port' (string) is required"}});

        if (auto it = body.find("timeoutSeconds"); it != body.end()) {
            if (!(it->is_number_unsigned() || it->is_number_integer()))
                return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "timeoutSeconds must be an integer [0..255]"}});

            uint32_t ts = it->get<uint32_t>();
            if (ts > 255)
                return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "timeoutSeconds must be in [0,255]"}});

            timeoutSeconds = static_cast<uint8_t>(ts);
        }

        if (!body.contains("ranges") || !body["ranges"].is_array() || body["ranges"].empty())
            return sendJson(resp, Http::Code::Bad_Request, {{"ok", false}, {"error", "Field 'ranges' (non-empty array) is required"}});

        std::vector<json> failed;
        uint32_t applied = 0;

        for (const auto& rj : body["ranges"]) {
            if (!rj.is_object()) {
                failed.push_back({{"reason","Invalid range object"}});
                continue;
            }

            if (!rj.contains("startIndex") || !(rj["startIndex"].is_number_unsigned() || rj["startIndex"].is_number_integer())) {
                failed.push_back({{"range", rj}, {"reason","'startIndex' missing/invalid"}});
                continue;
            }
            if (!rj.contains("count") || !(rj["count"].is_number_unsigned() || rj["count"].is_number_integer())) {
                failed.push_back({{"range", rj}, {"reason","'count' missing/invalid"}});
                continue;
            }
            if (!rj.contains("color") || !rj["color"].is_string()) {
                failed.push_back({{"range", rj}, {"reason","'color' missing/invalid"}});
                continue;
            }

            uint32_t startIndex32 = rj["startIndex"].get<uint32_t>();
            uint32_t count32      = rj["count"].get<uint32_t>();
            if (startIndex32 > 0xFFFF) { failed.push_back({{"range", rj}, {"reason","startIndex out of range [0..65535]"}}); continue; }
            if (count32 == 0)          { failed.push_back({{"range", rj}, {"reason","count must be > 0"}}); continue; }
            if (startIndex32 + count32 - 1 > 0xFFFF) {
                failed.push_back({{"range", rj}, {"reason","startIndex+count-1 exceeds 65535"}});
                continue;
            }

            uint8_t rr=0, gg=0, bb=0;
            const std::string color = rj["color"].get<std::string>();
            if (!parseHexColor(color, rr, gg, bb)) {
                failed.push_back({{"range", rj}, {"reason","color must be RRGGBB (accepts #RRGGBB or 0xRRGGBB)"}});
                continue;
            }

            // Build contiguous RGB block filled with the color and send
            std::vector<uint8_t> payload;
            payload.reserve(count32 * 3);
            for (uint32_t i = 0; i < count32; ++i) {
                payload.push_back(rr);
                payload.push_back(gg);
                payload.push_back(bb);
            }

            bool ok = sendWledDnRgbRange(host.c_str(), port.c_str(),
                                         static_cast<uint16_t>(startIndex32),
                                         payload, timeoutSeconds);
            if (!ok) {
                failed.push_back({{"range", rj}, {"reason","UDP send failed (see server logs)"}});
            } else {
                ++applied;
            }
        }

        if (!failed.empty()) {
            return sendJson(resp, failed.size() == body["ranges"].size()
                                     ? Http::Code::Bad_Request
                                     : Http::Code::Partial_Content,
                            {{"ok", false}, {"applied", applied}, {"failed", failed}});
        }

        return sendJson(resp, Http::Code::Ok, {{"ok", true}, {"applied", applied}});
    }

    std::shared_ptr<Http::Endpoint> httpEndpoint_;
    Rest::Router router_;
    int threads_;
    std::string publicDir_;
};
} // namespace

int main(int argc, char* argv[]) {
    int port = 9080;
    int threads = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-p" || a == "--port") && i + 1 < argc)      port = std::stoi(argv[++i]);
        else if ((a == "-t" || a == "--threads") && i + 1 < argc) threads = std::stoi(argv[++i]);
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: " << argv[0] << " [--port N] [--threads N]\n";
            return 0;
        }
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    Pistache::Address addr(Pistache::Ipv4::any(), Pistache::Port(port));
    WledApiServer server(addr, threads);
    server.init();
    server.start();

    while (g_running.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    server.shutdown();
    std::cout << "Server stopped.\n";
    return 0;
}
