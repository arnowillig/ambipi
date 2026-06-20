#ifndef VERTEX_H
#define VERTEX_H

#include <string>
#include <mutex>

// Serial control for an HDFury (4K Vertex) over the FTDI cable on its 3.5mm
// RS232 jack. Protocol: "#<cmd>\r" at 19200 8N1; the device replies with a
// single line. Port defaults to /dev/ttyUSB0 (the FTDI adapter). Each public
// call opens, talks, and closes the port, so manual use / other tools can
// share it. All access is mutex-serialized.
class Vertex
{
public:
    explicit Vertex(const std::string& port = "/dev/ttyUSB0", int baud = 19200);

    // Send "#<cmd>\r" and return the trimmed reply (empty on error/timeout).
    std::string command(const std::string& cmd, int timeoutMs = 1200);
    // "#get <key>" -> value (text after the leading "<key> "), or full reply.
    std::string get(const std::string& key);
    // "#set <key> <value>" -> true if the device replied (non-empty).
    bool set(const std::string& key, const std::string& value);
    // JSON object with the most useful status fields, for the web UI.
    std::string infoJson();

private:
    std::string _port;
    int _baud;
    std::mutex _mutex;
    int openConfigured();   // open + configure the port; -1 on failure
};

#endif // VERTEX_H
