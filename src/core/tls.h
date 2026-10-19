/*
 * tls.h - TLS connection layer: wolfSSL wrapped on top of the net
 * connection layer (NetConn). HTTPS is then just http.c over a TlsConn
 * via the IoSend/IoRecv function-pointer transport (see below).
 *
 *   tls_connect - performs the TLS handshake over an already-connected
 *                 NetConn (from net_connect, possibly via a proxy
 *                 CONNECT tunnel). *out receives a new TlsConn on
 *                 success (caller frees with tls_close). The NetConn is
 *                 borrowed, NOT owned: the caller keeps it open until
 *                 after tls_close and closes it with net_close itself.
 *                 sni_host, when non-empty, is sent as the SNI server
 *                 name and checked against the certificate when
 *                 verify != 0. ca_file is reserved for future use: this
 *                 build verifies against the CA roots embedded in
 *                 certs_roots.h (the wolfSSL library is built with
 *                 NO_FILESYSTEM), so the argument may be NULL or name a
 *                 nonexistent file. verify != 0 enables peer
 *                 verification (chain, validity, host name); verify == 0
 *                 skips it entirely (plaintext-grade security, for
 *                 testing only). On failure returns non-zero, *out is
 *                 NULL, and err holds a readable message (errcap should
 *                 be >= 128). Handshake IO uses NET_TIMEOUT_RW_MS.
 *   tls_send    - encrypts and sends the whole buffer; returns 0 only
 *                 when every byte was written, -1 on timeout/error.
 *   tls_recv    - returns decrypted bytes (>0), 0 when the peer closed
 *                 the connection (close_notify or TCP FIN), -1 on
 *                 timeout/error. Same contract as net_recv.
 *   tls_close   - tears down the TLS session and frees the TlsConn;
 *                 does not close the borrowed NetConn. NULL-safe.
 *
 * IoSend/IoRecv abstract a byte stream with net_send/net_recv
 * semantics (send: 0 ok / -1 error; recv: >0 bytes, 0 peer closed,
 * -1 error). http.c's http_send_request2/http_read_body2 consume them
 * so the same HTTP implementation runs over plain NetConn and over
 * TlsConn without knowing either.
 *
 * The caller must have initialized Winsock (WSAStartup) before
 * tls_connect. wolfSSL_Init is handled internally, once per process
 * (tls_close never calls wolfSSL_Cleanup).
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_TLS_H
#define LLMCHAT32_TLS_H

#include "net.h"   /* NetConn, NET_TIMEOUT_RW_MS */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TlsConn TlsConn;
int tls_connect(TlsConn **out, NetConn *tcp, const char *sni_host,
                const char *ca_file, int verify, char *err, int errcap);
int tls_send(TlsConn*, const void *data, int len, int timeout_ms);
int tls_recv(TlsConn*, void *buf, int cap, int timeout_ms);   /* 同 net_recv 语义 */
void tls_close(TlsConn*);
/* 供 http 复用：http 层经函数指针收发（见下） */
typedef int (*IoSend)(void *io, const void*, int, int);
typedef int (*IoRecv)(void *io, void*, int, int);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_TLS_H */
