/*
 * net.h - TCP connection layer: DNS, connect with timeouts, send/recv
 * with timeouts, optional HTTP proxy via CONNECT, URL parsing.
 *
 * The caller must have initialized Winsock (WSAStartup) before
 * net_connect; the layer does not manage WSAStartup/WSACleanup itself.
 *
 * Timeouts: timeout_ms >= 0 waits up to that many milliseconds
 * (0 = poll, return immediately); timeout_ms < 0 blocks forever.
 * The standard limits are NET_TIMEOUT_CONNECT_MS (net_connect) and
 * NET_TIMEOUT_RW_MS (net_send/net_recv) below; pass them explicitly
 * unless a scenario needs something shorter.
 *
 *   net_connect  - on success returns 0 and stores a new NetConn in *out
 *                  (caller frees with net_close). On failure returns
 *                  non-zero, *out is NULL, and err holds a readable
 *                  message (errcap should be >= 128). When
 *                  use_proxy && proxy->host[0], the TCP connection goes
 *                  to the proxy and the target is requested with
 *                  "CONNECT host:port HTTP/1.1"; only a 2xx status line
 *                  is accepted. With proxy->username[0], a
 *                  "Proxy-Authorization: Basic base64(user:pass)" header
 *                  is sent. If use_proxy is set but proxy->host is
 *                  empty, the connection is direct.
 *   net_send     - sends the whole buffer; returns 0 only when every
 *                  byte was written, -1 on timeout/error.
 *   net_recv     - returns bytes read (>0), 0 when the peer closed the
 *                  connection, -1 on timeout/error.
 *   net_close    - closes and frees; NULL-safe.
 *   net_parse_url- parses "https://host[:port]/path" (use_tls=1,
 *                  default 443) or "http://host[:port]/path"
 *                  (use_tls=0, default 80) into caller buffers.
 *                  The path defaults to "/"; a raw space in the path
 *                  (must be percent-encoded by the caller) or any
 *                  malformed component is rejected with non-zero.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_NET_H
#define LLMCHAT32_NET_H

#include "config.h"   /* ProxyCfg */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetConn NetConn;

/* Standard timeout limits, in milliseconds, for callers of this layer:
 * net_connect uses NET_TIMEOUT_CONNECT_MS; net_send and net_recv use
 * NET_TIMEOUT_RW_MS. They are conveniences for callers only - every
 * function still takes an explicit timeout_ms (see the semantics above). */
#define NET_TIMEOUT_CONNECT_MS 10000
#define NET_TIMEOUT_RW_MS      30000

int net_connect(NetConn **out, const char *host, unsigned short port,
                int timeout_ms, const ProxyCfg *proxy, int use_proxy,
                char *err, int errcap);
int net_send(NetConn*, const void *data, int len, int timeout_ms);
int net_recv(NetConn*, void *buf, int cap, int timeout_ms);
void net_close(NetConn*);
int  net_parse_url(const char *url, char *host, int hostcap, unsigned short *port,
                   char *path, int pathcap, int *use_tls);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_NET_H */
