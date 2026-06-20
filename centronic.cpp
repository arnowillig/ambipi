#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1   // expose CRTSCTS etc. under strict -std=c++2a
#endif

#include "centronic.h"
#include "json.hpp"

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>       // flock
#include <sqlite3.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <iostream>

// --- Becker telegram constants (verbatim from centronic-stick.py) ----------
static const char  STX = '\x02';
static const char  ETX = '\x03';
static const std::string CODE_PREFIX = "0000000002010B";
static const std::string CODE_SUFFIX = "000000";
static const std::string CODE_21     = "021";
static const std::string CODE_REMOTE = "01";   // remote control marker

static const uint8_t COMMAND_HALT = 0x10;
static const uint8_t COMMAND_UP   = 0x20;
static const uint8_t COMMAND_DOWN = 0x40;

static const char* LOCK_FILE = "/tmp/centronic-stick.lock";

static speed_t baudConst(int baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B115200;
    }
}

static std::string hex2(unsigned n) { char b[3]; std::snprintf(b, sizeof b, "%02X", n & 0xFFu);  return b; }
static std::string hex4(unsigned n) { char b[5]; std::snprintf(b, sizeof b, "%04X", n & 0xFFFFu); return b; }

Centronic::Centronic(std::string device, std::string dbPath)
    : _device(std::move(device)), _dbPath(std::move(dbPath)) {}

// Byte-for-byte port of generatecode() + checksum() from centronic-stick.py.
std::string Centronic::buildCode(const std::string& unitId, uint32_t inc,
                                 int channel, uint8_t cmd)
{
    std::string uid = unitId;
    std::transform(uid.begin(), uid.end(), uid.begin(),
                   [](unsigned char c){ return std::toupper(c); });

    std::string code;
    if (channel == 0)
        code = CODE_PREFIX + hex4(inc) + CODE_SUFFIX + uid + CODE_21
             + "00" + hex2(channel) + "00" + hex2(cmd);
    else
        code = CODE_PREFIX + hex4(inc) + CODE_SUFFIX + uid + CODE_21
             + CODE_REMOTE + hex2(channel) + "00" + hex2(cmd);

    // checksum over the 40-char payload (20 byte-pairs)
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < code.size(); i += 2)
        sum += static_cast<unsigned>(std::stoul(code.substr(i, 2), nullptr, 16));

    code += hex2((0x03 - sum) & 0xFFu);   // payload is already uppercase
    return code;
}

// Open + configure the serial port: 115200 8N1, raw. Spelled out (avoids the
// non-POSIX cfmakeraw), mirroring Vertex::openConfigured.
int Centronic::openSerial()
{
    int fd = ::open(_device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios t;
    if (tcgetattr(fd, &t) != 0) { ::close(fd); return -1; }
    t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
    t.c_oflag &= ~OPOST;
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    speed_t sp = baudConst(115200);
    cfsetispeed(&t, sp);
    cfsetospeed(&t, sp);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    t.c_cflag |= CS8;
#ifdef CRTSCTS
    t.c_cflag &= ~CRTSCTS;
#endif
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &t) != 0) { ::close(fd); return -1; }
    return fd;
}

bool Centronic::send(uint8_t cmd, const std::string& channelArg)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // Parse "[unit:]channel" exactly like the Python tool.
    std::string unitSel = "1";          // default unit index 1 (rowid 1 == "1737b")
    std::string chStr   = channelArg;
    auto colon = channelArg.find(':');
    if (colon != std::string::npos) {
        unitSel = channelArg.substr(0, colon);
        chStr   = channelArg.substr(colon + 1);
    }
    int channel;
    try { channel = std::stoi(chStr); }
    catch (...) { std::cerr << "[shutter] bad channel '" << chStr << "'\n"; return false; }
    if (!((channel >= 1 && channel <= 7) || channel == 15 || channel == 0)) {
        std::cerr << "[shutter] channel out of range (1-7, 15 or 0): " << channel << "\n";
        return false;
    }

    // Cross-process lock shared with centronic-stick.py.
    int lockfd = ::open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (lockfd >= 0) flock(lockfd, LOCK_EX);

    auto unlock = [&]() {
        if (lockfd >= 0) { flock(lockfd, LOCK_UN); ::close(lockfd); }
    };

    sqlite3* db = nullptr;
    if (sqlite3_open(_dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[shutter] cannot open db " << _dbPath << ": "
                  << (db ? sqlite3_errmsg(db) : "?") << "\n";
        if (db) sqlite3_close(db);
        unlock();
        return false;
    }

    // Resolve unit: 5-char code -> by code, otherwise 1-based index == rowid.
    long rowid = -1, increment = 0; int configured = 0; std::string code;
    sqlite3_stmt* st = nullptr;
    if (unitSel.size() == 5) {
        sqlite3_prepare_v2(db, "SELECT rowid,code,increment,configured FROM unit WHERE code=?", -1, &st, nullptr);
        sqlite3_bind_text(st, 1, unitSel.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_prepare_v2(db, "SELECT rowid,code,increment,configured FROM unit WHERE rowid=?", -1, &st, nullptr);
        sqlite3_bind_int(st, 1, std::atoi(unitSel.c_str()));
    }
    if (st && sqlite3_step(st) == SQLITE_ROW) {
        rowid      = sqlite3_column_int(st, 0);
        const unsigned char* c = sqlite3_column_text(st, 1);
        code       = c ? reinterpret_cast<const char*>(c) : "";
        increment  = sqlite3_column_int(st, 2);
        configured = sqlite3_column_int(st, 3);
    }
    if (st) sqlite3_finalize(st);

    bool ok = false;
    if (rowid < 0) {
        std::cerr << "[shutter] unit not found: " << unitSel << "\n";
    } else if (!configured) {
        std::cerr << "[shutter] unit " << code << " not configured (pair it first)\n";
    } else {
        std::string telegram = buildCode(code, static_cast<uint32_t>(increment), channel, cmd);
        int fd = openSerial();
        if (fd < 0) {
            std::cerr << "[shutter] cannot open serial " << _device << "\n";
        } else {
            std::string wire;
            wire.push_back(STX);
            wire += telegram;
            wire.push_back(ETX);
            ssize_t n = ::write(fd, wire.data(), wire.size());
            if (n == static_cast<ssize_t>(wire.size())) {
                tcdrain(fd);
                ok = true;
            } else {
                std::cerr << "[shutter] serial write failed (wrote " << n << "/" << wire.size() << ")\n";
            }
            ::close(fd);
        }

        // Persist N+1 only on success — don't burn a counter value on failure.
        if (ok) {
            sqlite3_stmt* up = nullptr;
            sqlite3_prepare_v2(db, "UPDATE unit SET increment=?, executed=? WHERE rowid=?", -1, &up, nullptr);
            sqlite3_bind_int(up, 1, static_cast<int>(increment + 1));
            sqlite3_bind_int(up, 2, static_cast<int>(time(nullptr)));
            sqlite3_bind_int(up, 3, static_cast<int>(rowid));
            if (sqlite3_step(up) != SQLITE_DONE)
                std::cerr << "[shutter] db update failed: " << sqlite3_errmsg(db) << "\n";
            sqlite3_finalize(up);
        }
    }

    sqlite3_close(db);
    unlock();
    return ok;
}

bool Centronic::open (const std::string& channel) { return send(COMMAND_DOWN, channel); }  // screen down
bool Centronic::close(const std::string& channel) { return send(COMMAND_UP,   channel); }  // screen up
bool Centronic::halt (const std::string& channel) { return send(COMMAND_HALT, channel); }

std::string Centronic::statusJson()
{
    std::lock_guard<std::mutex> lock(_mutex);
    nlohmann::json j;
    j["units"] = nlohmann::json::array();

    sqlite3* db = nullptr;
    if (sqlite3_open(_dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        j["error"] = "cannot open db";
        return j.dump();
    }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT rowid,code,increment,configured,executed FROM unit ORDER BY rowid",
                           -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            nlohmann::json u;
            u["index"]      = sqlite3_column_int(st, 0);
            const unsigned char* c = sqlite3_column_text(st, 1);
            u["code"]       = c ? reinterpret_cast<const char*>(c) : "";
            u["increment"]  = sqlite3_column_int(st, 2);
            u["configured"] = sqlite3_column_int(st, 3) ? true : false;
            u["executed"]   = sqlite3_column_int(st, 4);
            j["units"].push_back(u);
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return j.dump();
}
