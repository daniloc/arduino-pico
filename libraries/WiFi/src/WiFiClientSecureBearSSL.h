/*
    WiFiClientBearSSL- SSL client/server for esp8266 using BearSSL libraries
    - Mostly compatible with Arduino WiFi shield library and standard
    WiFiClient/ServerSecure (except for certificate handling).

    Copyright (c) 2018 Earle F. Philhower, III

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#pragma once

#include <vector>
#include "WiFiClient.h"
#include <bearssl/bearssl.h>
#include "BearSSLHelpers.h"
#include "CertStoreBearSSL.h"

namespace BearSSL {

class WiFiClientSecureCtx : public WiFiClient {
public:
    WiFiClientSecureCtx();
    WiFiClientSecureCtx(const WiFiClientSecureCtx &rhs) = delete;
    ~WiFiClientSecureCtx() override;

    WiFiClientSecureCtx& operator=(const WiFiClientSecureCtx&) = delete;

    // TODO: usage is invalid b/c of deleted copy, but this will only trigger an error when it is actually used by something
    // TODO: don't remove just yet to avoid including the WiFiClient default implementation and unintentionally causing
    //       a 'slice' that this method tries to avoid in the first place
    std::unique_ptr<WiFiClient> clone() const override {
        return nullptr;
    }

    int connect(IPAddress ip, uint16_t port) override;
    int connect(const String& host, uint16_t port) override;
    int connect(const char* name, uint16_t port) override;

    uint8_t connected() override;
    size_t write(const uint8_t *buf, size_t size) override;
    //    size_t write_P(PGM_P buf, size_t size) override;
    size_t write(Stream& stream); // Note this is not virtual
    int read(uint8_t *buf, size_t size) override;
    int read(char *buf, size_t size) {
        return read((uint8_t*)buf, size);
    }
    int available() override;
    int read() override;
    int peek() override;
    size_t peekBytes(uint8_t *buffer, size_t length) override;
    bool flush(unsigned int maxWaitMs);
    bool stop(unsigned int maxWaitMs);
    void flush() override {
        (void)flush(0);
    }
    void stop() override {
        (void)stop(0);
    }

    int availableForWrite() override;

    // Allow sessions to be saved/restored automatically to a memory area
    void setSession(Session *session) {
        _session = session;
    }

    // Don't validate the chain, just accept whatever is given.  VERY INSECURE!
    void setInsecure() {
        _clearAuthenticationSettings();
        _use_insecure = true;
    }
    // Assume a given public key, don't validate or use cert info at all
    void setKnownKey(const PublicKey *pk, unsigned usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN) {
        _clearAuthenticationSettings();
        _knownkey = pk;
        _knownkey_usages = usages;
    }
    // Only check SHA1 fingerprint of certificate
    bool setFingerprint(const uint8_t fingerprint[20]) {
        _clearAuthenticationSettings();
        _use_fingerprint = true;
        memcpy_P(_fingerprint, fingerprint, 20);
        return true;
    }
    bool setFingerprint(const char *fpStr);
    // Accept any certificate that's self-signed
    void allowSelfSignedCerts() {
        _clearAuthenticationSettings();
        _use_self_signed = true;
    }
    // Install certificates of trusted CAs or specific site
    void setTrustAnchors(const X509List *ta) {
        _clearAuthenticationSettings();
        _ta = ta;
    }
    // In cases when NTP is not used, app must set a time manually to check cert validity
    void setX509Time(time_t now) {
        _now = now;
    }
    // Install a client certificate for this connection, in case the server requires it (i.e. MQTT)
    void setClientRSACert(const X509List *cert, const PrivateKey *sk);
    void setClientECCert(const X509List *cert, const PrivateKey *sk,
                         unsigned allowed_usages, unsigned cert_issuer_key_type);

    // Sets the requested buffer size for transmit and receive
    void setBufferSizes(int recv, int xmit);

    // Returns whether MFLN negotiation for the above buffer sizes succeeded (after connection)
    int getMFLNStatus() {
        return connected() && br_ssl_engine_get_mfln_negotiated(_eng);
    }

    // Return an error code and possibly a text string in a passed-in buffer with last SSL failure
    int getLastSSLError(char *dest = NULL, size_t len = 0);

    // Attach a preconfigured certificate store
    void setCertStore(CertStoreBase *certStore) {
        _certStore = certStore;
    }

    // Select specific ciphers (i.e. optimize for speed over security)
    // These may be in PROGMEM or RAM, either will run properly
    bool setCiphers(const uint16_t *cipherAry, int cipherCount);
    bool setCiphers(const std::vector<uint16_t>& list);
    bool setCiphersLessSecure(); // Only use the limited set of RSA ciphers without EC

    // Limit the TLS versions BearSSL will connect with.  Default is
    // BR_TLS10...BR_TLS12
    bool setSSLVersion(uint32_t min = BR_TLS10, uint32_t max = BR_TLS12);
#if 0
    // peek buffer API is present
    virtual bool hasPeekBufferAPI() const override {
        return true;
    }

    // return number of byte accessible by peekBuffer()
    virtual size_t peekAvailable() override {
        return WiFiClientSecureCtx::available();
    }

    // return a pointer to available data buffer (size = peekAvailable())
    // semantic forbids any kind of read() before calling peekConsume()
    virtual const char* peekBuffer() override;

    // consume bytes after use (see peekBuffer)
    virtual void peekConsume(size_t consume) override;
#endif

    // ESP32 compatibility
    void setCACert(const char *rootCA) {
        if (_esp32_ta) {
            delete _esp32_ta;
        }
        _esp32_ta = new X509List(rootCA);
    }
    void setCertificate(const char *client_ca) {
        if (_esp32_chain) {
            delete _esp32_chain;
        }
        _esp32_chain = new X509List(client_ca);
    }
    void setPrivateKey(const char *private_key) {
        if (_esp32_sk) {
            delete _esp32_sk;
        }
        _esp32_sk = new PrivateKey(private_key);
    }
    bool loadCACert(Stream& stream, size_t size) {
        bool ret = false;
        auto buff = new char[size];
        if (size == stream.readBytes(buff, size)) {
            setCACert(buff);
            ret = true;
        }
        delete[] buff;
        return ret;
    }
    bool loadCertificate(Stream& stream, size_t size) {
        bool ret = false;
        auto buff = new char[size];
        if (size == stream.readBytes(buff, size)) {
            setCertificate(buff);
            ret = true;
        }
        delete[] buff;
        return ret;
    }
    bool loadPrivateKey(Stream& stream, size_t size) {
        bool ret = false;
        auto buff = new char[size];
        if (size == stream.readBytes(buff, size)) {
            setPrivateKey(buff);
            ret = true;
        }
        delete[] buff;
        return ret;
    }
    int connect(IPAddress ip, uint16_t port, int32_t timeout) {
        auto save = _timeout;
        _timeout = timeout * 1000; // timeout is in secs, _timeout in milliseconds
        auto ret = connect(ip, port);
        _timeout = save;
        return ret;
    }
    int connect(const char *host, uint16_t port, int32_t timeout) {
        auto save = _timeout;
        _timeout = timeout * 1000; // timeout is in secs, _timeout in milliseconds
        auto ret = connect(host, port);
        _timeout = save;
        return ret;
    }
    int connect(IPAddress ip, uint16_t port, const char *rootCABuff, const char *cli_cert, const char *cli_key) {
        if (_esp32_ta) {
            delete _esp32_ta;
            _esp32_ta = nullptr;
        }
        if (_esp32_chain) {
            delete _esp32_chain;
            _esp32_chain = nullptr;
        }
        if (_esp32_sk) {
            delete _esp32_sk;
            _esp32_sk = nullptr;
        }
        if (rootCABuff) {
            setCertificate(rootCABuff);
        }
        if (cli_cert && cli_key) {
            setCertificate(cli_cert);
            setPrivateKey(cli_key);
        }
        return connect(ip, port);
    }
    int connect(const char *host, uint16_t port, const char *rootCABuff, const char *cli_cert, const char *cli_key) {
        IPAddress ip;
        if (WiFi.hostByName(host, ip, _timeout)) {
            return connect(ip, port, rootCABuff, cli_cert, cli_key);
        } else {
            return 0;
        }
    }

protected:
    bool _connectSSL(const char *hostName); // Do initial SSL handshake

private:
    void _clear();
    void _clearAuthenticationSettings();
    // Only one of the following two should ever be != nullptr!
    std::shared_ptr<br_ssl_client_context> _sc;
    std::shared_ptr<br_ssl_server_context> _sc_svr;
    inline bool ctx_present() {
        return (_sc != nullptr) || (_sc_svr != nullptr);
    }
    br_ssl_engine_context *_eng; // &_sc->eng, to allow for client or server contexts
    std::shared_ptr<br_x509_minimal_context> _x509_minimal;
    std::shared_ptr<struct br_x509_insecure_context> _x509_insecure;
    std::shared_ptr<br_x509_knownkey_context> _x509_knownkey;
    std::shared_ptr<unsigned char> _iobuf_in;
    std::shared_ptr<unsigned char> _iobuf_out;
    time_t _now;
    const X509List *_ta;
    CertStoreBase *_certStore;
    int _iobuf_in_size;
    int _iobuf_out_size;
    bool _handshake_done;
    bool _oom_err;

    // Optional storage space pointer for session parameters
    // Will be used on connect and updated on close
    Session *_session;

    bool _use_insecure;
    bool _use_fingerprint;
    uint8_t _fingerprint[20];
    bool _use_self_signed;
    const PublicKey *_knownkey;
    unsigned _knownkey_usages;

    // Custom cipher list pointer or NULL if default
    std::shared_ptr<uint16_t> _cipher_list;
    uint8_t _cipher_cnt;

    // TLS ciphers allowed
    uint32_t _tls_min;
    uint32_t _tls_max;

    unsigned char *_recvapp_buf;
    size_t _recvapp_len;

    bool _clientConnected(); // Is the underlying socket alive?
    std::shared_ptr<unsigned char> _alloc_iobuf(size_t sz);
    void _freeSSL();
    int _run_until(unsigned target, bool blocking = true);
    size_t _write(const uint8_t *buf, size_t size, bool pmem);
    bool _wait_for_handshake(); // Sets and return the _handshake_done after connecting

    // Optional client certificate
    const X509List *_chain;
    const PrivateKey *_sk;
    unsigned _allowed_usages;
    unsigned _cert_issuer_key_type;

    // Methods for handling server.available() call which returns a client connection.
    friend class WiFiClientSecure; // access to private context constructors
    WiFiClientSecureCtx(ClientContext *client, const X509List *chain, unsigned cert_issuer_key_type,
                        const PrivateKey *sk, int iobuf_in_size, int iobuf_out_size, ServerSessions *cache,
                        const X509List *client_CA_ta, int tls_min, int tls_max);
    WiFiClientSecureCtx(ClientContext* client, const X509List *chain, const PrivateKey *sk,
                        int iobuf_in_size, int iobuf_out_size, ServerSessions *cache,
                        const X509List *client_CA_ta, int tls_min, int tls_max);

    // RSA keyed server
    bool _connectSSLServerRSA(const X509List *chain, const PrivateKey *sk,
                              ServerSessions *cache, const X509List *client_CA_ta);
    // EC keyed server
    bool _connectSSLServerEC(const X509List *chain, unsigned cert_issuer_key_type, const PrivateKey *sk,
                             ServerSessions *cache, const X509List *client_CA_ta);

    // X.509 validators differ from server to client
    bool _installClientX509Validator(); // Set up X509 validator for a client conn.
    bool _installServerX509Validator(const X509List *client_CA_ta); // Setup X509 client cert validation, if supplied

    uint8_t *_streamLoad(Stream& stream, size_t size);

    // ESP32 compatibility
    X509List *_esp32_ta = nullptr;
    X509List *_esp32_chain = nullptr;
    PrivateKey *_esp32_sk = nullptr;
}; // class WiFiClientSecureCtx


class WiFiClientSecure : public WiFiClient {

    // WiFiClient's "ClientContext* _client" is always nullptr in this class.
    // Instead, all virtual functions call their counterpart in "WiFiClientecureCtx* _ctx"
    //          which also derives from WiFiClient (this parent is the one which is eventually used)

    // TODO: notice that this complicates the implementation by having two distinct ways the client connection is managed, consider:
    // - implementing the secure connection details in the ClientContext
    //   (i.e. delegate the write & read functions there)
    // - simplify the inheritance chain by implementing base wificlient class and inherit the original wificlient and wificlientsecure from it
    // - abstract internals so it's possible to seamlessly =default copy and move with the instance *without* resorting to manual copy and initialization of each member

    // TODO: prefer implementing virtual overrides in the .cpp (or, at least one of them)

public:

    WiFiClientSecure(): _ctx(new WiFiClientSecureCtx()) {
        _owned = _ctx.get();
    }
    WiFiClientSecure(const WiFiClientSecure &rhs): WiFiClient(), _ctx(rhs._ctx) {
        if (_ctx) {
            _owned = _ctx.get();
        }
    }
    ~WiFiClientSecure() override {
        _ctx = nullptr;
    }

    WiFiClientSecure& operator=(const WiFiClientSecure&) = default;

    std::unique_ptr<WiFiClient> clone() const override {
        return std::unique_ptr<WiFiClient>(new WiFiClientSecure(*this));
    }

    uint8_t status() override {
        return _ctx->status();
    }
    int connect(IPAddress ip, uint16_t port) override {
        return _ctx->connect(ip, port);
    }
    int connect(const String& host, uint16_t port) override {
        return _ctx->connect(host, port);
    }
    int connect(const char* name, uint16_t port) override {
        return _ctx->connect(name, port);
    }

    uint8_t connected() override {
        return _ctx->connected();
    }
    size_t write(const uint8_t *buf, size_t size) override {
        return _ctx->write(buf, size);
    }
    //size_t write_P(PGM_P buf, size_t size) override { return _ctx->write_P(buf, size); }
    size_t write(const char *buf) {
        return write((const uint8_t*)buf, strlen(buf));
    }
    //    size_t write_P(const char *buf) { return write_P((PGM_P)buf, strlen_P(buf)); }
    size_t write(Stream& stream) { /* Note this is not virtual */
        return _ctx->write(stream);
    }
    int read(uint8_t *buf, size_t size) override {
        return _ctx->read(buf, size);
    }
    int available() override {
        return _ctx->available();
    }
    int availableForWrite() override {
        return _ctx->availableForWrite();
    }
    int read() override {
        return _ctx->read();
    }
    int peek() override {
        return _ctx->peek();
    }
    size_t peekBytes(uint8_t *buffer, size_t length) override {
        return _ctx->peekBytes(buffer, length);
    }
    bool flush(unsigned int maxWaitMs) {
        return _ctx->flush(maxWaitMs);
    }
    bool stop(unsigned int maxWaitMs) {
        return _ctx->stop(maxWaitMs);
    }
    void flush() override {
        (void)flush(0);
    }
    void stop() override {
        (void)stop(0);
    }

    // Allow sessions to be saved/restored automatically to a memory area
    void setSession(Session *session) {
        _ctx->setSession(session);
    }

    // Don't validate the chain, just accept whatever is given.  VERY INSECURE!
    void setInsecure() {
        _ctx->setInsecure();
    }

    // Assume a given public key, don't validate or use cert info at all
    void setKnownKey(const PublicKey *pk, unsigned usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN) {
        _ctx->setKnownKey(pk, usages);
    }
    // Only check SHA1 fingerprint of certificate
    bool setFingerprint(const uint8_t fingerprint[20]) {
        return _ctx->setFingerprint(fingerprint);
    }
    bool setFingerprint(const char *fpStr) {
        return _ctx->setFingerprint(fpStr);
    }
    // Accept any certificate that's self-signed
    void allowSelfSignedCerts() {
        _ctx->allowSelfSignedCerts();
    }

    // Install certificates of trusted CAs or specific site
    void setTrustAnchors(const X509List *ta) {
        _ctx->setTrustAnchors(ta);
    }
    // In cases when NTP is not used, app must set a time manually to check cert validity
    void setX509Time(time_t now) {
        _ctx->setX509Time(now);
    }
    // Install a client certificate for this connection, in case the server requires it (i.e. MQTT)
    void setClientRSACert(const X509List *cert, const PrivateKey *sk) {
        _ctx->setClientRSACert(cert, sk);
    }
    void setClientECCert(const X509List *cert, const PrivateKey *sk,
                         unsigned allowed_usages, unsigned cert_issuer_key_type) {
        _ctx->setClientECCert(cert, sk, allowed_usages, cert_issuer_key_type);
    }

    // Sets the requested buffer size for transmit and receive
    void setBufferSizes(int recv, int xmit) {
        _ctx->setBufferSizes(recv, xmit);
    }

    // Returns whether MFLN negotiation for the above buffer sizes succeeded (after connection)
    int getMFLNStatus() {
        return _ctx->getMFLNStatus();
    }

    // Return an error code and possibly a text string in a passed-in buffer with last SSL failure
    int getLastSSLError(char *dest = NULL, size_t len = 0) {
        return _ctx->getLastSSLError(dest, len);
    }

    // Attach a preconfigured certificate store
    void setCertStore(CertStoreBase *certStore) {
        _ctx->setCertStore(certStore);
    }

    // Select specific ciphers (i.e. optimize for speed over security)
    // These may be in PROGMEM or RAM, either will run properly
    bool setCiphers(const uint16_t *cipherAry, int cipherCount) {
        return _ctx->setCiphers(cipherAry, cipherCount);
    }
    bool setCiphers(const std::vector<uint16_t> list) {
        return _ctx->setCiphers(list);
    }
    bool setCiphersLessSecure() {
        return _ctx->setCiphersLessSecure();    // Only use the limited set of RSA ciphers without EC
    }

    // Limit the TLS versions BearSSL will connect with.  Default is
    // BR_TLS10...BR_TLS12. Allowed values are: BR_TLS10, BR_TLS11, BR_TLS12
    bool setSSLVersion(uint32_t min = BR_TLS10, uint32_t max = BR_TLS12) {
        return _ctx->setSSLVersion(min, max);
    };

    // Check for Maximum Fragment Length support for given len before connection (possibly insecure)
    static bool probeMaxFragmentLength(IPAddress ip, uint16_t port, uint16_t len);
    static bool probeMaxFragmentLength(const char *hostname, uint16_t port, uint16_t len);
    static bool probeMaxFragmentLength(const String& host, uint16_t port, uint16_t len);
#if 0
    // peek buffer API is present
    virtual bool hasPeekBufferAPI() const override {
        return true;
    }

    // return number of byte accessible by peekBuffer()
    virtual size_t peekAvailable() override {
        return _ctx->available();
    }

    // return a pointer to available data buffer (size = peekAvailable())
    // semantic forbids any kind of read() before calling peekConsume()
    virtual const char* peekBuffer() override {
        return _ctx->peekBuffer();
    }

    // consume bytes after use (see peekBuffer)
    virtual void peekConsume(size_t consume) override {
        return _ctx->peekConsume(consume);
    }
#endif

    // ESP32 compatibility
    void setCACert(const char *rootCA) {
        return _ctx->setCACert(rootCA);
    }
    void setCertificate(const char *client_ca) {
        return _ctx->setCertificate(client_ca);
    }
    void setPrivateKey(const char *private_key) {
        return _ctx->setPrivateKey(private_key);
    }
    bool loadCACert(Stream& stream, size_t size) {
        return _ctx->loadCACert(stream, size);
    }
    bool loadCertificate(Stream& stream, size_t size) {
        return _ctx->loadCertificate(stream, size);
    }
    bool loadPrivateKey(Stream& stream, size_t size) {
        return _ctx->loadPrivateKey(stream, size);
    }

    int connect(IPAddress ip, uint16_t port, int32_t timeout) {
        return _ctx->connect(ip, port, timeout);
    }
    int connect(const char *host, uint16_t port, int32_t timeout) {
        return _ctx->connect(host, port, timeout);
    }
    int connect(IPAddress ip, uint16_t port, const char *rootCABuff, const char *cli_cert, const char *cli_key) {
        return _ctx->connect(ip, port, rootCABuff, cli_cert, cli_key);
    }
    int connect(const char *host, uint16_t port, const char *rootCABuff, const char *cli_cert, const char *cli_key) {
        return _ctx->connect(host, port, rootCABuff, cli_cert, cli_key);
    }

private:
    std::shared_ptr<WiFiClientSecureCtx> _ctx;

    // Methods for handling server.available() call which returns a client connection.
    friend class WiFiServerSecure; // Server needs to access these constructors
    WiFiClientSecure(ClientContext *client, const X509List *chain, unsigned cert_issuer_key_type,
                     const PrivateKey *sk, int iobuf_in_size, int iobuf_out_size, ServerSessions *cache,
                     const X509List *client_CA_ta, int tls_min, int tls_max):
        _ctx(new WiFiClientSecureCtx(client, chain, cert_issuer_key_type, sk, iobuf_in_size, iobuf_out_size, cache, client_CA_ta, tls_min, tls_max)) {
    }

    WiFiClientSecure(ClientContext* client, const X509List *chain, const PrivateKey *sk,
                     int iobuf_in_size, int iobuf_out_size, ServerSessions *cache,
                     const X509List *client_CA_ta, int tls_min, int tls_max):
        _ctx(new WiFiClientSecureCtx(client, chain, sk, iobuf_in_size, iobuf_out_size, cache, client_CA_ta, tls_min, tls_max)) {
    }

}; // class WiFiClientSecure

}; // namespace BearSSL
