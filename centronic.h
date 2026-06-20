#ifndef CENTRONIC_H
#define CENTRONIC_H

#include <string>
#include <mutex>
#include <cstdint>

// Native C++ port of centronic-stick.py (ole1986/centronic-py v0.9): drives
// Becker CC11/CC51 roller-shutter receivers through the BECKER CDC-RS232 USB
// stick. It sends ASCII-hex telegrams wrapped in <STX> .. <ETX> at 115200 8N1.
//
// Each telegram carries a per-unit rolling counter ("increment") that the
// receiver requires to increase monotonically, otherwise the telegram is
// ignored. The counter is persisted in the SQLite DB shared with the Python
// tool (/var/lib/centronics/centronic-stick.db) and access is serialized via
// the same /tmp/centronic-stick.lock flock, so the daemon and the old
// .py/.sh helpers can coexist without ever desyncing the rolling code.
//
// Mirrors the project's Vertex serial controller: each public call opens,
// talks and closes the port; all access is mutex-serialized.
class Centronic
{
public:
    explicit Centronic(
        std::string device = "/dev/serial/by-id/usb-BECKER-ANTRIEBE_GmbH_CDC_RS232_v125_Centronic-if00",
        std::string dbPath = "/var/lib/centronics/centronic-stick.db");

    // channel is "[unit:]channel" (Python form). Bare channel -> unit index 1
    // (rowid 1 == code "1737b"). channel range 1-7, 15 (all) or 0 (none).
    bool open (const std::string& channel = "1");   // COMMAND_DOWN -> screen rolls down ("open")
    bool close(const std::string& channel = "1");   // COMMAND_UP   -> screen rolls up   ("close")
    bool halt (const std::string& channel = "1");   // COMMAND_HALT -> stop

    // {"units":[{"index","code","increment","configured","executed"},...]}
    std::string statusJson();

    // Pure telegram builder (no I/O) — public/static so the byte-for-byte
    // parity with centronic-stick.py can be unit-tested. Returns the 42-char
    // uppercase code (40 payload chars + 2 checksum), without <STX>/<ETX>.
    static std::string buildCode(const std::string& unitId, uint32_t increment,
                                 int channel, uint8_t cmd);

private:
    bool send(uint8_t cmd, const std::string& channel);
    int  openSerial();          // termios 115200 8N1 raw; -1 on failure

    std::mutex  _mutex;
    std::string _device;
    std::string _dbPath;
};

#endif // CENTRONIC_H
