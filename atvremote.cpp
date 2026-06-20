#include "atvremote.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/bn.h>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cerrno>
#include <string>
#include <iostream>

// ---------------------------------------------------------------------------
// protobuf wire helpers (hand-rolled — only what these two protocols need)
// ---------------------------------------------------------------------------
static void putVarint(std::string& o, uint64_t v) {
    do { uint8_t b = v & 0x7f; v >>= 7; if (v) b |= 0x80; o.push_back((char)b); } while (v);
}
static void pbVarint(std::string& o, int field, uint64_t v) {
    putVarint(o, ((uint64_t)field << 3) | 0); putVarint(o, v);
}
static void pbLen(std::string& o, int field, const std::string& s) {     // wiretype 2
    putVarint(o, ((uint64_t)field << 3) | 2); putVarint(o, s.size()); o += s;
}
static bool getVarint(const uint8_t*& p, const uint8_t* e, uint64_t& v) {
    v = 0; int s = 0;
    while (p < e) { uint8_t b = *p++; v |= (uint64_t)(b & 0x7f) << s; if (!(b & 0x80)) return true; s += 7; if (s > 63) return false; }
    return false;
}
static int firstField(const std::string& m) {
    const uint8_t* p = (const uint8_t*)m.data(); const uint8_t* e = p + m.size();
    uint64_t tag; if (!getVarint(p, e, tag)) return -1; return (int)(tag >> 3);
}
static bool findVarintField(const std::string& m, int field, uint64_t& out) {
    const uint8_t* p = (const uint8_t*)m.data(); const uint8_t* e = p + m.size();
    while (p < e) {
        uint64_t tag; if (!getVarint(p, e, tag)) return false; int f = tag >> 3, wt = tag & 7;
        if (f == field && wt == 0) return getVarint(p, e, out);
        if (wt == 0) { uint64_t t; if (!getVarint(p, e, t)) return false; }
        else if (wt == 2) { uint64_t l; if (!getVarint(p, e, l)) return false; p += l; }
        else if (wt == 5) p += 4; else if (wt == 1) p += 8; else return false;
    }
    return false;
}
static bool findMsgField(const std::string& m, int field, std::string& out) {
    const uint8_t* p = (const uint8_t*)m.data(); const uint8_t* e = p + m.size();
    while (p < e) {
        uint64_t tag; if (!getVarint(p, e, tag)) return false; int f = tag >> 3, wt = tag & 7;
        if (wt == 2) { uint64_t l; if (!getVarint(p, e, l)) return false; if (f == field) { out.assign((const char*)p, l); return true; } p += l; }
        else if (wt == 0) { uint64_t t; if (!getVarint(p, e, t)) return false; }
        else if (wt == 5) p += 4; else if (wt == 1) p += 8; else return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// TLS
// ---------------------------------------------------------------------------
struct Tls {
    SSL_CTX* ctx = nullptr; SSL* ssl = nullptr; int fd = -1;
    ~Tls() { if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); } if (fd >= 0) ::close(fd); if (ctx) SSL_CTX_free(ctx); }
};

static bool ensureCert(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r"); if (f) { fclose(f); return true; }
    EVP_PKEY* pkey = EVP_PKEY_new(); RSA* rsa = RSA_new(); BIGNUM* e = BN_new(); BN_set_word(e, RSA_F4);
    if (!RSA_generate_key_ex(rsa, 2048, e, nullptr)) { BN_free(e); RSA_free(rsa); EVP_PKEY_free(pkey); return false; }
    EVP_PKEY_assign_RSA(pkey, rsa); BN_free(e);
    X509* x = X509_new(); X509_set_version(x, 2); ASN1_INTEGER_set(X509_get_serialNumber(x), 1000);
    X509_gmtime_adj(X509_get_notBefore(x), 0); X509_gmtime_adj(X509_get_notAfter(x), (long)3650 * 24 * 3600);
    X509_set_pubkey(x, pkey);
    X509_NAME* nm = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(nm, "CN", MBSTRING_ASC, (const unsigned char*)"ambipi", -1, -1, 0);
    X509_set_issuer_name(x, nm);
    // Android/conscrypt (the beamer's TLS stack) rejects self-signed client certs
    // that lack these extensions with "certificate unknown" — match the reference
    // androidtvremote2 client cert exactly: CA basic-constraints + a SAN.
    { X509V3_CTX vc; X509V3_set_ctx_nodb(&vc); X509V3_set_ctx(&vc, x, x, nullptr, nullptr, 0);
      X509_EXTENSION* ext;
      if ((ext = X509V3_EXT_conf_nid(nullptr, &vc, NID_basic_constraints, (char*)"critical,CA:TRUE,pathlen:0"))) { X509_add_ext(x, ext, -1); X509_EXTENSION_free(ext); }
      if ((ext = X509V3_EXT_conf_nid(nullptr, &vc, NID_subject_alt_name, (char*)"DNS:ambipi"))) { X509_add_ext(x, ext, -1); X509_EXTENSION_free(ext); } }
    X509_sign(x, pkey, EVP_sha256());
    FILE* o = fopen(path.c_str(), "w"); if (!o) { X509_free(x); EVP_PKEY_free(pkey); return false; }
    PEM_write_PrivateKey(o, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    PEM_write_X509(o, x);
    fclose(o); X509_free(x); EVP_PKEY_free(pkey);
    return true;
}

static bool tlsConnect(const std::string& host, int port, const std::string& cert, Tls& t, int timeoutSec = 5) {
    struct addrinfo hints; memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr; char ports[16]; snprintf(ports, sizeof(ports), "%d", port);
    if (getaddrinfo(host.c_str(), ports, &hints, &res) != 0 || !res) return false;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    struct timeval tv; tv.tv_sec = timeoutSec; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) { freeaddrinfo(res); ::close(fd); return false; }
    freeaddrinfo(res);
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    if (SSL_CTX_use_certificate_file(ctx, cert.c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, cert.c_str(), SSL_FILETYPE_PEM) != 1) { SSL_CTX_free(ctx); ::close(fd); return false; }
    SSL* ssl = SSL_new(ctx); SSL_set_fd(ssl, fd);
    if (SSL_connect(ssl) != 1) { SSL_free(ssl); SSL_CTX_free(ctx); ::close(fd); return false; }
    t.ctx = ctx; t.ssl = ssl; t.fd = fd; return true;
}

static bool readN(SSL* ssl, uint8_t* buf, int n) {
    int got = 0; while (got < n) { int r = SSL_read(ssl, buf + got, n - got); if (r <= 0) return false; got += r; } return true;
}
static bool g_atvDebug = false;
static bool readMsg(SSL* ssl, std::string& out) {   // varint length prefix
    uint64_t len = 0; int shift = 0;
    while (true) {
        uint8_t b; int r = SSL_read(ssl, &b, 1);
        if (r <= 0) {
            if (g_atvDebug) std::cerr << "[atv] readMsg fail: SSL_read=" << r
                << " sslerr=" << SSL_get_error(ssl, r) << " errno=" << errno << "\n";
            return false;
        }
        len |= (uint64_t)(b & 0x7f) << shift; if (!(b & 0x80)) break; shift += 7; if (shift > 63) return false;
    }
    if (len > 65536) return false; out.resize(len);
    if (len && !readN(ssl, (uint8_t*)&out[0], (int)len)) return false;
    if (g_atvDebug) std::cerr << "[atv] readMsg ok: " << len << " bytes, firstField=" << firstField(out) << "\n";
    return true;
}
static bool writeMsg(SSL* ssl, const std::string& m) {
    std::string f; putVarint(f, m.size()); f += m; int w = SSL_write(ssl, f.data(), f.size()); return w == (int)f.size();
}

static bool rsaBytes(X509* x, std::string& mod, std::string& exp) {
    EVP_PKEY* pk = X509_get_pubkey(x); if (!pk) return false;
    RSA* rsa = EVP_PKEY_get1_RSA(pk); EVP_PKEY_free(pk); if (!rsa) return false;
    const BIGNUM* n = nullptr; const BIGNUM* e = nullptr; RSA_get0_key(rsa, &n, &e, nullptr);
    mod.resize(BN_num_bytes(n)); BN_bn2bin(n, (unsigned char*)&mod[0]);
    exp.resize(BN_num_bytes(e)); BN_bn2bin(e, (unsigned char*)&exp[0]);
    RSA_free(rsa); return true;
}
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1;
}

// pairing OuterMessage: protocol_version(1)=2, status(2)=200 (OK), + payload field
static std::string pairingOuter(int field, const std::string& inner) {
    std::string o; pbVarint(o, 1, 2); pbVarint(o, 2, 200); pbLen(o, field, inner); return o;
}

AtvRemote::AtvRemote(const std::string& host, const std::string& certPath)
    : _host(host), _certPath(certPath) {}

bool AtvRemote::isPaired() const {
    FILE* f = fopen(_certPath.c_str(), "r"); if (f) { fclose(f); return true; } return false;
}

bool AtvRemote::pair() {
    g_atvDebug = true;
    ::remove(_certPath.c_str());   // re-pair => fresh cert (picks up the current cert format)
    if (!ensureCert(_certPath)) { std::cerr << "[atv] cert generation failed\n"; return false; }
    Tls t; if (!tlsConnect(_host, 6467, _certPath, t)) { std::cerr << "[atv] TLS connect to " << _host << ":6467 failed\n"; return false; }   // 6467 = PAIRING port
    { X509* sc = SSL_get_peer_certificate(t.ssl);
      std::cerr << "[atv] TLS up, cipher=" << SSL_get_cipher(t.ssl) << ", peer_cert=" << (sc ? "yes" : "no") << "\n";
      if (sc) X509_free(sc); }
    std::string m;
    // 1) PairingRequest { service_name=1, client_name=2 }
    { std::string pr; pbLen(pr, 1, std::string("atvremote")); pbLen(pr, 2, std::string("ambipi"));
      std::string om = pairingOuter(10, pr);
      if (!writeMsg(t.ssl, om)) { std::cerr << "[atv] send pairing_request failed\n"; return false; }
      std::cerr << "[atv] pairing_request sent (" << om.size() << " bytes)\n"; }
    if (!readMsg(t.ssl, m)) { std::cerr << "[atv] no pairing_request_ack\n"; return false; }
    // 2) Options { input_encodings=[{type=HEX(3), symbol_length=6}], preferred_role=INPUT(1) }
    { std::string enc; pbVarint(enc, 1, 3); pbVarint(enc, 2, 6);
      std::string opt; pbLen(opt, 1, enc); pbVarint(opt, 3, 1);
      if (!writeMsg(t.ssl, pairingOuter(20, opt))) { std::cerr << "[atv] send options failed\n"; return false; } }
    readMsg(t.ssl, m);
    // 3) Configuration { encoding={type=HEX,symbol_length=6}, client_role=INPUT }
    { std::string enc; pbVarint(enc, 1, 3); pbVarint(enc, 2, 6);
      std::string cfg; pbLen(cfg, 1, enc); pbVarint(cfg, 2, 1);
      if (!writeMsg(t.ssl, pairingOuter(30, cfg))) { std::cerr << "[atv] send configuration failed\n"; return false; } }
    readMsg(t.ssl, m);   // ConfigurationAck → the TV now displays a 6-hex code

    std::cout << "\n>>> Code vom Beamer-Bildschirm eingeben (6 Hex-Zeichen): " << std::flush;
    std::string line; std::getline(std::cin, line);
    std::string c; for (char ch : line) if (!isspace((unsigned char)ch)) c.push_back(ch);
    if (c.size() < 6) { std::cerr << "[atv] code too short\n"; return false; }

    X509* server = SSL_get_peer_certificate(t.ssl); if (!server) { std::cerr << "[atv] no server cert\n"; return false; }
    FILE* cf = fopen(_certPath.c_str(), "r"); X509* client = cf ? PEM_read_X509(cf, nullptr, nullptr, nullptr) : nullptr; if (cf) fclose(cf);
    std::string cn, ce, sn, se;
    if (!client || !rsaBytes(client, cn, ce) || !rsaBytes(server, sn, se)) { std::cerr << "[atv] RSA extract failed\n"; if (client) X509_free(client); X509_free(server); return false; }
    X509_free(client); X509_free(server);

    unsigned char codeBytes[2];
    codeBytes[0] = (hexval(c[2]) << 4) | hexval(c[3]);
    codeBytes[1] = (hexval(c[4]) << 4) | hexval(c[5]);
    unsigned char check = (hexval(c[0]) << 4) | hexval(c[1]);
    SHA256_CTX sc; SHA256_Init(&sc);
    SHA256_Update(&sc, cn.data(), cn.size()); SHA256_Update(&sc, ce.data(), ce.size());
    SHA256_Update(&sc, sn.data(), sn.size()); SHA256_Update(&sc, se.data(), se.size());
    SHA256_Update(&sc, codeBytes, 2);
    unsigned char hash[32]; SHA256_Final(hash, &sc);
    if (hash[0] != check) { std::cerr << "[atv] code checksum mismatch (hash[0]=" << (int)hash[0] << " vs " << (int)check << ") — wrong code or hash inputs?\n"; return false; }

    std::string secretBytes((const char*)hash, 32);
    std::string sec; pbLen(sec, 1, secretBytes);
    if (!writeMsg(t.ssl, pairingOuter(40, sec))) { std::cerr << "[atv] send secret failed\n"; return false; }
    std::string ack; if (!readMsg(t.ssl, ack)) { std::cerr << "[atv] no secret_ack\n"; return false; }
    uint64_t st = 0; findVarintField(ack, 2, st);
    if (st == 0 || st == 200) { std::cout << "\n[atv] ✅ Pairing erfolgreich!\n"; return true; }
    std::cerr << "[atv] pairing failed, status=" << st << "\n"; return false;
}

bool AtvRemote::runRemote(int keyCode, bool injectAlways, bool desiredOn) {
    if (!isPaired()) return false;
    Tls t; if (!tlsConnect(_host, 6466, _certPath, t, 8)) return false;   // 6466 = REMOTE/command port

    // Feature flags we advertise (must match a real remote, mirrors the
    // reference androidtvremote2): PING|KEY|POWER|VOLUME|IME|APP_LINK.
    static const uint64_t ACTIVE_FEATURES = 615;
    std::string m;
    bool started = false, curOn = false;

    // Handshake: answer remote_configure(1) / remote_set_active(2) /
    // remote_ping_request(8) until the device sends remote_start(40), which
    // carries the live power state (started, field 1) and means the session
    // is up.
    for (int i = 0; i < 40 && !started; i++) {
        if (!readMsg(t.ssl, m)) break;
        switch (firstField(m)) {
        case 1: {   // remote_configure -> echo config (NO model/vendor, like the ref)
            std::string di; pbVarint(di, 3, 1); pbLen(di, 4, std::string("1"));
            pbLen(di, 5, std::string("atvremote")); pbLen(di, 6, std::string("1.0.0"));
            std::string rc; pbVarint(rc, 1, ACTIVE_FEATURES); pbLen(rc, 2, di);
            std::string out; pbLen(out, 1, rc); writeMsg(t.ssl, out);
            break; }
        case 2: {   // remote_set_active { active }
            std::string sa; pbVarint(sa, 1, ACTIVE_FEATURES);
            std::string out; pbLen(out, 2, sa); writeMsg(t.ssl, out);
            break; }
        case 8: {   // remote_ping_request -> remote_ping_response(9){ val1 }
            std::string inner; findMsgField(m, 8, inner); uint64_t v1 = 0; findVarintField(inner, 1, v1);
            std::string pr; pbVarint(pr, 1, v1); std::string out; pbLen(out, 9, pr); writeMsg(t.ssl, out);
            break; }
        case 40: {  // remote_start { started }
            std::string inner; findMsgField(m, 40, inner); uint64_t st = 0; findVarintField(inner, 1, st);
            curOn = (st != 0); started = true;
            break; }
        }
    }
    if (!started) return false;
    if (!injectAlways && curOn == desiredOn) return true;   // already in desired state

    // The device drops keys injected immediately after remote_start; it needs a
    // short moment to become ready (empirically ~1s). Pings only come every ~5s,
    // so a plain wait is safe here.
    usleep(1500000);
    std::string ki; pbVarint(ki, 1, keyCode); pbVarint(ki, 2, 3);   // key_code, direction=SHORT
    std::string out; pbLen(out, 10, ki); writeMsg(t.ssl, out);
    usleep(400000);
    return true;
}

bool AtvRemote::sendKey(int keycode) { return runRemote(keycode, true, false); }
bool AtvRemote::powerOn()  { return runRemote(26, false, true); }    // KEYCODE_POWER (toggle), only if off
bool AtvRemote::powerOff() { return runRemote(26, false, false); }   // KEYCODE_POWER (toggle), only if on
