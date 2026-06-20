#ifndef ATVREMOTE_H
#define ATVREMOTE_H

#include <string>

// Android TV Remote v2 client (the protocol used by the JMGO projector, ports
// 6467 pairing / 6466 remote). Pure C++ over OpenSSL TLS with hand-rolled
// protobuf — no Python, no protobuf lib. The pairing authorizes a self-signed
// client certificate (stored in certPath); the remote connection reuses it to
// inject key events (power on/off). See atvremote.cpp for the wire details.
class AtvRemote
{
public:
    AtvRemote(const std::string& host = "192.168.178.118",
              const std::string& certPath = "/var/lib/ambipi/atv_client.pem");

    bool isPaired() const;       // client cert exists
    bool pair();                 // interactive (reads 6-hex code from stdin) — CLI use
    bool powerOn();              // KEYCODE_POWER, only if currently off (idempotent)
    bool powerOff();             // KEYCODE_POWER, only if currently on  (idempotent)
    bool sendKey(int keycode);   // generic key inject on port 6466 (remote/command)

private:
    std::string _host;
    std::string _certPath;
    // Open the remote channel, run the handshake, wait for remote_start (which
    // carries the live power state), then inject keyCode. If injectAlways is
    // false (power mode), the key (KEYCODE_POWER, a toggle) is only sent when
    // the reported state differs from desiredOn — making on/off idempotent.
    bool runRemote(int keyCode, bool injectAlways, bool desiredOn);
};

#endif // ATVREMOTE_H
