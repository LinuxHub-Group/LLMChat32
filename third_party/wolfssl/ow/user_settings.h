/* user_settings.h - wolfSSL build configuration for LLMChat32.
 *
 * Target: Open Watcom 2.0, Win32 (Windows 98 compatible), static library.
 * Language: C89. Library sources compiled with -DWOLFSSL_USER_SETTINGS
 * (required so that wolfssl/wolfcrypt/settings.h picks up this file).
 *
 * Minimal client feature set per LLMChat32 plan:
 *   TLS 1.2 + TLS 1.3, AEAD ciphers (AES-GCM, ChaCha20-Poly1305),
 *   SHA-256/SHA-384, ECDSA/ECDH (P-256), RSA verify, SNI,
 *   certificate time validation, no filesystem (memory buffers only).
 */
#ifndef LLMCHAT32_USER_SETTINGS_H
#define LLMCHAT32_USER_SETTINGS_H

/* --------------------------------------------------------------------------
 * Toolchain / platform
 * ------------------------------------------------------------------------ */
/* Mark this as a user-settings build. Also passed via -D on the command
 * line (required, otherwise wolfssl/wolfcrypt/settings.h never includes
 * this file) - hence the #ifndef guard against E1100 redefinition. */
#ifndef WOLFSSL_USER_SETTINGS
#define WOLFSSL_USER_SETTINGS
#endif

/* Open Watcom port. Upstream 5.9.x detects the compiler via __WATCOMC__;
 * this macro documents the port and is available for downstream checks. */
#define WOLFSSL_OPENWATCOM

/* Upstream types.h maps WOLFSSL_THREAD_NO_JOIN to _WCCALLBACK on Watcom,
 * but defines _WCCALLBACK itself only for __WATCOMC__ < 1300 (Open Watcom
 * 1.9). Open Watcom 2.0 (__WATCOMC__ == 1300) defines it nowhere, so the
 * THREAD_CB_NOJOIN typedef fails to parse. Empty matches upstream's own
 * fallback choice for older Watcom versions. */
#ifndef _WCCALLBACK
#define _WCCALLBACK
#endif

/* Static library: no dllexport/dllimport attribute games. */
#define WOLFSSL_STATIC

/* Win32 platform layer (winsock, windows.h, CryptoAPI seeding). */
#define USE_WINDOWS_API

/* C89 translation unit; keep declarations at block starts where the
 * library honors it. */
#define WOLF_C89

/* --------------------------------------------------------------------------
 * Library shape: client-only, no OS file access
 * ------------------------------------------------------------------------ */
#define NO_WOLFSSL_SERVER      /* client-only, halves handshake code */
#define NO_FILESYSTEM          /* certs/keys loaded from memory buffers */
#define NO_WRITEV              /* WinSock has no writev */
#define NO_WOLFSSL_DIR         /* no directory traversal */

/* --------------------------------------------------------------------------
 * Protocol versions
 * ------------------------------------------------------------------------ */
/* TLS 1.3 client. TLS 1.2 stays enabled (default). TLS 1.0/1.1 are off by
 * default since wolfSSL 5.x. */
#define WOLFSSL_TLS13
#define HAVE_HKDF              /* TLS 1.3 KDF */

/* --------------------------------------------------------------------------
 * Crypto primitives
 * ------------------------------------------------------------------------ */
#define HAVE_AESGCM            /* AES-128/256-GCM AEAD suites */
#define HAVE_CHACHA            /* ChaCha20-Poly1305 AEAD suites */
#define HAVE_POLY1305
#define WOLFSSL_SHA512         /* SHA-512 */
#define WOLFSSL_SHA384         /* SHA-384 is a separate opt-in in 5.9.x
                                * (not implied by WOLFSSL_SHA512); TLS 1.3
                                * TLS_AES_256_GCM_SHA384 handshakes and
                                * GTS ECDSA chains fail with HASH_TYPE_E
                                * (-232) without it */
#define HAVE_ECC               /* ECDSA/ECDH, includes P-256 */
#define ECC_TIMING_RESISTANT   /* constant-time ECC */
#define WC_RSA_BLINDING        /* constant-time RSA (upstream hardening
                                * default; without it settings.h emits a
                                * #warning directive that Watcom cannot
                                * parse) */
#define WC_RSA_PSS             /* RSA-PSS signatures (TLS 1.3 RSA chains) */

/* Minimum accepted RSA key size for certificates. */
#define WOLFSSL_MIN_RSA_BITS 2048

/* Legacy ciphers not needed for the minimal set. */
#define NO_DES3
#define NO_RC4
#define NO_RABBIT
#define NO_IDEA
#define NO_DSA                 /* cert sigs we need are RSA/ECDSA */
#define NO_DH                  /* kx via ECDHE only */

/* --------------------------------------------------------------------------
 * TLS extensions
 * ------------------------------------------------------------------------ */
#define HAVE_TLS_EXTENSIONS
#define HAVE_SNI               /* Server Name Indication (virtual hosting) */
#define HAVE_ALPN              /* HTTP/2-style protocol negotiation, cheap */
/* Watcom's string.h declares no strtok_r prototype (types.h would pick it
 * for __WATCOMC__ >= 1300), so use wolfSSL's own thread-safe strtok. */
#define USE_WOLF_STRTOK
#define HAVE_SUPPORTED_CURVES  /* supported_groups ext: ECDHE-only clients
                                * are rejected by modern servers without it
                                * (BoringSSL answered: fatal handshake_failure) */
#define HAVE_EXTENDED_MASTER   /* extended master secret for TLS 1.2 sessions */
/* Servers commonly send a cross-signed root (e.g. GTS Root R4 signed by
 * GlobalSign) while we trust the self-signed root; without alternate
 * chain building wolfSSL fails with ASN_NO_SIGNER_E (-188) - its own
 * debug output recommends this flag. */
#define WOLFSSL_ALT_CERT_CHAINS

/* --------------------------------------------------------------------------
 * Certificate handling
 * ------------------------------------------------------------------------ */
/* Date validation (before/after) stays ENABLED: do not define
 * WOLFSSL_NO_ASN_TIME or NO_ASN_TIME. */
/* Relaxed ASN.1 strictness only if the build requires it - added below. */

#endif /* LLMCHAT32_USER_SETTINGS_H */
