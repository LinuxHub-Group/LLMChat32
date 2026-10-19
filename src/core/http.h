/*
 * http.h - HTTP/1.1 request/response layer on top of a byte-stream
 * transport. The transport is either the net connection layer (plain
 * TCP, NetConn) or the TLS layer (TlsConn) - the implementation only
 * talks to it through IoSend/IoRecv function pointers (typedefs from
 * tls.h, net_send/net_recv semantics).
 *
 *   http_send_request - serializes "METHOD path HTTP/1.1" plus the
 *       caller's headers ({"K: V", NULL} list, sent in order) and the
 *       body, then reads and parses the response status line and
 *       headers into *out. Content-Length is appended automatically
 *       when body_len > 0 and the caller did not supply one. Any
 *       status code (404 included) is a transport success: the return
 *       value is 0 and err stays empty; only send/IO/parse failures
 *       return non-zero with a readable err. Interim 1xx heads (100
 *       Continue) are skipped. The response head is read byte at a
 *       time so body bytes following in the same segment are never
 *       swallowed by the head reader.
 *   http_read_body    - decodes the framing described by *out into a
 *       continuous byte stream delivered to fn in arrival-sized pieces
 *       (chunked decoding is transparent: hex chunk-size lines may
 *       carry extensions, CRLF or bare-LF line ends are accepted, the
 *       0-chunk and its trailers end the body). On success returns 0
 *       after exactly one final fn(ctx, "", 0, last=1) call. Framing
 *       or IO failure returns -1 with err set and no final call.
 *       *cancel != 0 aborts promptly with -2 and no final call (err
 *       stays empty; -2 is the whole message). Recv timeouts are
 *       NET_TIMEOUT_RW_MS.
 *   http_send_request2/http_read_body2 - the same implementation over
 *       an explicit transport: io is passed through to every IoSend/
 *       IoRecv call (a NetConn*, a TlsConn*, ...). The plain
 *       NetConn-taking originals are thin wrappers around these with
 *       net-backed IO functions.
 *
 * All functions are C89. Strings are narrow UTF-8.
 */
#ifndef LLMCHAT32_HTTP_H
#define LLMCHAT32_HTTP_H

#include "tls.h"   /* net.h (NetConn, NET_TIMEOUT_RW_MS) + IoSend/IoRecv */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int  status;                    /* 状态码 */
  int  chunked;                   /* Transfer-Encoding: chunked */
  long content_length;            /* -1 = 未知（按连接关闭） */
  char content_type[64];
} HttpResult;
typedef void (*HttpBodyFn)(void *ctx, const char *data, int len, int last);
int http_send_request(NetConn*, const char *method, const char *path,
                      const char *const *headers,   /* {"K: V", NULL} */
                      const char *body, int body_len,
                      HttpResult *out, char *err, int errcap);
int http_read_body(NetConn*, const HttpResult*, const volatile int *cancel,
                   HttpBodyFn fn, void *ctx, char *err, int errcap);
/* 读取并解码 chunked/content-length 成连续字节流；cancel 非 0 时中止返回 -2；结束时 fn(...,last=1) */

int http_send_request2(void *io, IoSend send_fn, IoRecv recv_fn,
                       const char *method, const char *path,
                       const char *const *headers,
                       const char *body, int body_len,
                       HttpResult *out, char *err, int errcap);
int http_read_body2(void *io, IoRecv recv_fn, const HttpResult*,
                    const volatile int *cancel,
                    HttpBodyFn fn, void *ctx, char *err, int errcap);

#ifdef __cplusplus
}
#endif

#endif /* LLMCHAT32_HTTP_H */
