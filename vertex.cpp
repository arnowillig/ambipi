#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1   // expose CRTSCTS etc. under strict -std=c++2a
#endif

#include "vertex.h"

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>
#include <sstream>

static speed_t baudConst(int baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B19200;
    }
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Write "#<cmd>\r" on an already-open fd, then read the reply. Stops shortly
// after the first CR/LF (the device sends one line), or on overall timeout.
static std::string txrx(int fd, const std::string& cmd, int timeoutMs)
{
    tcflush(fd, TCIFLUSH);                       // drop any stale bytes
    std::string out = "#" + cmd + "\r";
    if (::write(fd, out.data(), out.size()) < 0) return "";
    tcdrain(fd);

    std::string resp;
    int elapsed = 0, quiet = 0;
    const int step = 50;                          // ms per poll
    bool gotEOL = false;
    while (elapsed < timeoutMs) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = step * 1000;
        int r = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (r > 0 && FD_ISSET(fd, &rfds)) {
            char buf[256];
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0) {
                resp.append(buf, static_cast<size_t>(n));
                if (memchr(buf, '\r', n) || memchr(buf, '\n', n)) gotEOL = true;
                quiet = 0;
                continue;
            }
        }
        elapsed += step;
        if (gotEOL) { quiet += step; if (quiet >= 150) break; }
    }
    return trim(resp);
}

Vertex::Vertex(const std::string& port, int baud) : _port(port), _baud(baud) {}

int Vertex::openConfigured()
{
    int fd = ::open(_port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios t;
    if (tcgetattr(fd, &t) != 0) { ::close(fd); return -1; }
    // Raw mode, spelled out (avoids relying on the non-POSIX cfmakeraw).
    t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY);
    t.c_oflag &= ~OPOST;
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    speed_t sp = baudConst(_baud);
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

std::string Vertex::command(const std::string& cmd, int timeoutMs)
{
    std::lock_guard<std::mutex> lock(_mutex);
    int fd = openConfigured();
    if (fd < 0) return "";
    std::string r = txrx(fd, cmd, timeoutMs);
    ::close(fd);
    return r;
}

std::string Vertex::get(const std::string& key)
{
    std::string r = command("get " + key);
    std::string prefix = key + " ";              // reply is usually "<key> <value>"
    if (r.rfind(prefix, 0) == 0) return trim(r.substr(prefix.size()));
    return r;
}

bool Vertex::set(const std::string& key, const std::string& value)
{
    return !command("set " + key + " " + value).empty();
}

static void jsonField(std::ostringstream& o, bool& first, const char* k, const std::string& v)
{
    if (!first) o << ",";
    first = false;
    o << "\"" << k << "\":\"";
    for (char c : v) { if (c == '"' || c == '\\') o << '\\'; o << c; }
    o << "\"";
}

std::string Vertex::infoJson()
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::ostringstream o;
    o << "{";
    int fd = openConfigured();
    if (fd >= 0) {
        bool first = true;
        jsonField(o, first, "ver", txrx(fd, "get ver", 1200));
        const char* keys[] = { "input", "hdcp", "edidmode", "edidtable", "autosw" };
        for (const char* k : keys) {
            std::string r = txrx(fd, std::string("get ") + k, 800);
            std::string prefix = std::string(k) + " ";
            if (r.rfind(prefix, 0) == 0) r = trim(r.substr(prefix.size()));
            jsonField(o, first, k, r);
        }
        ::close(fd);
    }
    o << "}";
    return o.str();
}
