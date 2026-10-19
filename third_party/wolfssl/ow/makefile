# makefile for wolfSSL static library - Open Watcom wmake
#
# Builds build\wolfssl.lib (relative to repo root: ..\..\..\build) from
# the wolfSSL v5.9.4-stable sources. Invoked from the repo root makefile
# via `cd third_party\wolfssl\ow && wmake`.
#
# This file is generated - edit genrules.py, then run:
#   cd third_party\wolfssl\ow && python genrules.py
#
# Ordering constraints (wmake quirks):
#   * The default goal (all) must be the first explicit target - wmake
#     builds the first target it sees, not a target named "all".
#   * Macros in dependency lines are expanded when the line is PARSED:
#     OBJS* are therefore defined before all/$(LIB).
#   * wmake caps the number of "&" continuation lines in one macro, so
#     OBJS is chunked (OBJS0, OBJS1, ...) and combined.
#   * One explicit rule per source object: wmake implicit rules cannot
#     handle ..\ relative paths.
#   * Files that are #included into other .c files (src/ssl*.c fragments,
#     wolfcrypt/src/{misc,asn_orig,asn_tsp,evp,evp_pk,asm}.c) must NOT be
#     compiled standalone - see src/ssl.c and wolfcrypt/src/random.c for
#     the #include sites. Excluded in genrules.py.
#   * user_settings.h lives in this directory; -DWOLFSSL_USER_SETTINGS is
#     required for wolfssl/wolfcrypt/settings.h to include it, and -i=.
#     makes the quoted #include "user_settings.h" resolve.
#   * Object files land next to the sources; covered by the repo-wide
#     *.obj gitignore.

CC       = wcc386
CFLAGS   = -bt=nt -bm -wx -os -DWOLFSSL_USER_SETTINGS -DBUILDING_WOLFSSL -i=.. -i=.

BUILD_DIR = ..\..\..\build
LIB       = $(BUILD_DIR)\wolfssl.lib


OBJS0 = &
    ..\src\crl.obj &
    ..\src\dtls.obj &
    ..\src\dtls13.obj &
    ..\src\internal.obj &
    ..\src\keys.obj &
    ..\src\ocsp.obj &
    ..\src\quic.obj &
    ..\src\sniffer.obj &
    ..\src\ssl.obj &
    ..\src\tls.obj &
    ..\src\tls13.obj &
    ..\src\wolfio.obj &
    ..\wolfcrypt\src\aes.obj &
    ..\wolfcrypt\src\arc4.obj &
    ..\wolfcrypt\src\argon2.obj &
    ..\wolfcrypt\src\ascon.obj &
    ..\wolfcrypt\src\asn.obj &
    ..\wolfcrypt\src\async.obj &
    ..\wolfcrypt\src\blake2b.obj &
    ..\wolfcrypt\src\blake2s.obj &
    ..\wolfcrypt\src\camellia.obj &
    ..\wolfcrypt\src\chacha.obj &
    ..\wolfcrypt\src\chacha20_poly1305.obj &
    ..\wolfcrypt\src\cmac.obj &
    ..\wolfcrypt\src\coding.obj &
    ..\wolfcrypt\src\compress.obj &
    ..\wolfcrypt\src\cpuid.obj &
    ..\wolfcrypt\src\cryptocb.obj &
    ..\wolfcrypt\src\curve25519.obj &
    ..\wolfcrypt\src\curve448.obj &
    ..\wolfcrypt\src\des3.obj &
    ..\wolfcrypt\src\dh.obj &
    ..\wolfcrypt\src\dsa.obj &
    ..\wolfcrypt\src\ecc.obj &
    ..\wolfcrypt\src\ecc_fp.obj &
    ..\wolfcrypt\src\eccsi.obj &
    ..\wolfcrypt\src\ed25519.obj &
    ..\wolfcrypt\src\ed448.obj &
    ..\wolfcrypt\src\error.obj &
    ..\wolfcrypt\src\falcon.obj

OBJS1 = &
    ..\wolfcrypt\src\fe_448.obj &
    ..\wolfcrypt\src\fe_low_mem.obj &
    ..\wolfcrypt\src\fe_operations.obj &
    ..\wolfcrypt\src\ge_448.obj &
    ..\wolfcrypt\src\ge_low_mem.obj &
    ..\wolfcrypt\src\ge_operations.obj &
    ..\wolfcrypt\src\hash.obj &
    ..\wolfcrypt\src\hmac.obj &
    ..\wolfcrypt\src\hpke.obj &
    ..\wolfcrypt\src\integer.obj &
    ..\wolfcrypt\src\kdf.obj &
    ..\wolfcrypt\src\logging.obj &
    ..\wolfcrypt\src\md2.obj &
    ..\wolfcrypt\src\md4.obj &
    ..\wolfcrypt\src\md5.obj &
    ..\wolfcrypt\src\memory.obj &
    ..\wolfcrypt\src\pkcs12.obj &
    ..\wolfcrypt\src\pkcs7.obj &
    ..\wolfcrypt\src\poly1305.obj &
    ..\wolfcrypt\src\puf.obj &
    ..\wolfcrypt\src\pwdbased.obj &
    ..\wolfcrypt\src\random.obj &
    ..\wolfcrypt\src\rc2.obj &
    ..\wolfcrypt\src\ripemd.obj &
    ..\wolfcrypt\src\rng_bank.obj &
    ..\wolfcrypt\src\rsa.obj &
    ..\wolfcrypt\src\sakke.obj &
    ..\wolfcrypt\src\sha.obj &
    ..\wolfcrypt\src\sha256.obj &
    ..\wolfcrypt\src\sha3.obj &
    ..\wolfcrypt\src\sha512.obj &
    ..\wolfcrypt\src\signature.obj &
    ..\wolfcrypt\src\siphash.obj &
    ..\wolfcrypt\src\sm2.obj &
    ..\wolfcrypt\src\sm3.obj &
    ..\wolfcrypt\src\sm4.obj &
    ..\wolfcrypt\src\sp_arm32.obj &
    ..\wolfcrypt\src\sp_arm64.obj &
    ..\wolfcrypt\src\sp_armthumb.obj &
    ..\wolfcrypt\src\sp_c32.obj

OBJS2 = &
    ..\wolfcrypt\src\sp_c64.obj &
    ..\wolfcrypt\src\sp_cortexm.obj &
    ..\wolfcrypt\src\sp_dsp32.obj &
    ..\wolfcrypt\src\sp_int.obj &
    ..\wolfcrypt\src\sp_riscv64.obj &
    ..\wolfcrypt\src\sp_sm2_arm32.obj &
    ..\wolfcrypt\src\sp_sm2_arm64.obj &
    ..\wolfcrypt\src\sp_sm2_armthumb.obj &
    ..\wolfcrypt\src\sp_sm2_c32.obj &
    ..\wolfcrypt\src\sp_sm2_c64.obj &
    ..\wolfcrypt\src\sp_sm2_cortexm.obj &
    ..\wolfcrypt\src\sp_sm2_riscv64.obj &
    ..\wolfcrypt\src\sp_sm2_x86_64.obj &
    ..\wolfcrypt\src\sp_x86_64.obj &
    ..\wolfcrypt\src\srp.obj &
    ..\wolfcrypt\src\tfm.obj &
    ..\wolfcrypt\src\tsp.obj &
    ..\wolfcrypt\src\wc_dsp.obj &
    ..\wolfcrypt\src\wc_encrypt.obj &
    ..\wolfcrypt\src\wc_frodokem.obj &
    ..\wolfcrypt\src\wc_frodokem_mat.obj &
    ..\wolfcrypt\src\wc_keystore.obj &
    ..\wolfcrypt\src\wc_lms.obj &
    ..\wolfcrypt\src\wc_lms_impl.obj &
    ..\wolfcrypt\src\wc_mldsa.obj &
    ..\wolfcrypt\src\wc_mlkem.obj &
    ..\wolfcrypt\src\wc_mlkem_poly.obj &
    ..\wolfcrypt\src\wc_pkcs11.obj &
    ..\wolfcrypt\src\wc_port.obj &
    ..\wolfcrypt\src\wc_she.obj &
    ..\wolfcrypt\src\wc_slhdsa.obj &
    ..\wolfcrypt\src\wc_xmss.obj &
    ..\wolfcrypt\src\wc_xmss_impl.obj &
    ..\wolfcrypt\src\wolfentropy.obj &
    ..\wolfcrypt\src\wolfevent.obj &
    ..\wolfcrypt\src\wolfmath.obj

OBJS = $(OBJS0) $(OBJS1) $(OBJS2)

all: $(LIB) .SYMBOLIC

$(LIB): $(OBJS)
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	wlib -n $@ $(OBJS)

clean: .SYMBOLIC
	@if exist ..\src\*.obj del ..\src\*.obj
	@if exist ..\wolfcrypt\src\*.obj del ..\wolfcrypt\src\*.obj
	@if exist $(LIB) del $(LIB)

# object rules - one explicit rule per source. Every object
# also depends on user_settings.h so config edits rebuild.
..\src\crl.obj: ..\src\crl.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\dtls.obj: ..\src\dtls.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\dtls13.obj: ..\src\dtls13.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\internal.obj: ..\src\internal.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\keys.obj: ..\src\keys.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\ocsp.obj: ..\src\ocsp.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\quic.obj: ..\src\quic.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\sniffer.obj: ..\src\sniffer.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\ssl.obj: ..\src\ssl.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\tls.obj: ..\src\tls.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\tls13.obj: ..\src\tls13.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\src\wolfio.obj: ..\src\wolfio.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\aes.obj: ..\wolfcrypt\src\aes.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\arc4.obj: ..\wolfcrypt\src\arc4.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\argon2.obj: ..\wolfcrypt\src\argon2.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ascon.obj: ..\wolfcrypt\src\ascon.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\asn.obj: ..\wolfcrypt\src\asn.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\async.obj: ..\wolfcrypt\src\async.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\blake2b.obj: ..\wolfcrypt\src\blake2b.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\blake2s.obj: ..\wolfcrypt\src\blake2s.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\camellia.obj: ..\wolfcrypt\src\camellia.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\chacha.obj: ..\wolfcrypt\src\chacha.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\chacha20_poly1305.obj: ..\wolfcrypt\src\chacha20_poly1305.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\cmac.obj: ..\wolfcrypt\src\cmac.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\coding.obj: ..\wolfcrypt\src\coding.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\compress.obj: ..\wolfcrypt\src\compress.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\cpuid.obj: ..\wolfcrypt\src\cpuid.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\cryptocb.obj: ..\wolfcrypt\src\cryptocb.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\curve25519.obj: ..\wolfcrypt\src\curve25519.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\curve448.obj: ..\wolfcrypt\src\curve448.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\des3.obj: ..\wolfcrypt\src\des3.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\dh.obj: ..\wolfcrypt\src\dh.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\dsa.obj: ..\wolfcrypt\src\dsa.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ecc.obj: ..\wolfcrypt\src\ecc.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ecc_fp.obj: ..\wolfcrypt\src\ecc_fp.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\eccsi.obj: ..\wolfcrypt\src\eccsi.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ed25519.obj: ..\wolfcrypt\src\ed25519.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ed448.obj: ..\wolfcrypt\src\ed448.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\error.obj: ..\wolfcrypt\src\error.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\falcon.obj: ..\wolfcrypt\src\falcon.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\fe_448.obj: ..\wolfcrypt\src\fe_448.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\fe_low_mem.obj: ..\wolfcrypt\src\fe_low_mem.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\fe_operations.obj: ..\wolfcrypt\src\fe_operations.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ge_448.obj: ..\wolfcrypt\src\ge_448.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ge_low_mem.obj: ..\wolfcrypt\src\ge_low_mem.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ge_operations.obj: ..\wolfcrypt\src\ge_operations.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\hash.obj: ..\wolfcrypt\src\hash.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\hmac.obj: ..\wolfcrypt\src\hmac.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\hpke.obj: ..\wolfcrypt\src\hpke.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\integer.obj: ..\wolfcrypt\src\integer.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\kdf.obj: ..\wolfcrypt\src\kdf.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\logging.obj: ..\wolfcrypt\src\logging.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\md2.obj: ..\wolfcrypt\src\md2.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\md4.obj: ..\wolfcrypt\src\md4.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\md5.obj: ..\wolfcrypt\src\md5.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\memory.obj: ..\wolfcrypt\src\memory.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\pkcs12.obj: ..\wolfcrypt\src\pkcs12.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\pkcs7.obj: ..\wolfcrypt\src\pkcs7.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\poly1305.obj: ..\wolfcrypt\src\poly1305.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\puf.obj: ..\wolfcrypt\src\puf.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\pwdbased.obj: ..\wolfcrypt\src\pwdbased.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\random.obj: ..\wolfcrypt\src\random.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\rc2.obj: ..\wolfcrypt\src\rc2.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\ripemd.obj: ..\wolfcrypt\src\ripemd.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\rng_bank.obj: ..\wolfcrypt\src\rng_bank.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\rsa.obj: ..\wolfcrypt\src\rsa.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sakke.obj: ..\wolfcrypt\src\sakke.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sha.obj: ..\wolfcrypt\src\sha.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sha256.obj: ..\wolfcrypt\src\sha256.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sha3.obj: ..\wolfcrypt\src\sha3.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sha512.obj: ..\wolfcrypt\src\sha512.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\signature.obj: ..\wolfcrypt\src\signature.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\siphash.obj: ..\wolfcrypt\src\siphash.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sm2.obj: ..\wolfcrypt\src\sm2.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sm3.obj: ..\wolfcrypt\src\sm3.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sm4.obj: ..\wolfcrypt\src\sm4.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_arm32.obj: ..\wolfcrypt\src\sp_arm32.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_arm64.obj: ..\wolfcrypt\src\sp_arm64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_armthumb.obj: ..\wolfcrypt\src\sp_armthumb.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_c32.obj: ..\wolfcrypt\src\sp_c32.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_c64.obj: ..\wolfcrypt\src\sp_c64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_cortexm.obj: ..\wolfcrypt\src\sp_cortexm.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_dsp32.obj: ..\wolfcrypt\src\sp_dsp32.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_int.obj: ..\wolfcrypt\src\sp_int.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_riscv64.obj: ..\wolfcrypt\src\sp_riscv64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_arm32.obj: ..\wolfcrypt\src\sp_sm2_arm32.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_arm64.obj: ..\wolfcrypt\src\sp_sm2_arm64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_armthumb.obj: ..\wolfcrypt\src\sp_sm2_armthumb.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_c32.obj: ..\wolfcrypt\src\sp_sm2_c32.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_c64.obj: ..\wolfcrypt\src\sp_sm2_c64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_cortexm.obj: ..\wolfcrypt\src\sp_sm2_cortexm.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_riscv64.obj: ..\wolfcrypt\src\sp_sm2_riscv64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_sm2_x86_64.obj: ..\wolfcrypt\src\sp_sm2_x86_64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\sp_x86_64.obj: ..\wolfcrypt\src\sp_x86_64.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\srp.obj: ..\wolfcrypt\src\srp.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\tfm.obj: ..\wolfcrypt\src\tfm.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\tsp.obj: ..\wolfcrypt\src\tsp.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_dsp.obj: ..\wolfcrypt\src\wc_dsp.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_encrypt.obj: ..\wolfcrypt\src\wc_encrypt.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_frodokem.obj: ..\wolfcrypt\src\wc_frodokem.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_frodokem_mat.obj: ..\wolfcrypt\src\wc_frodokem_mat.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_keystore.obj: ..\wolfcrypt\src\wc_keystore.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_lms.obj: ..\wolfcrypt\src\wc_lms.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_lms_impl.obj: ..\wolfcrypt\src\wc_lms_impl.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_mldsa.obj: ..\wolfcrypt\src\wc_mldsa.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_mlkem.obj: ..\wolfcrypt\src\wc_mlkem.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_mlkem_poly.obj: ..\wolfcrypt\src\wc_mlkem_poly.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_pkcs11.obj: ..\wolfcrypt\src\wc_pkcs11.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_port.obj: ..\wolfcrypt\src\wc_port.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_she.obj: ..\wolfcrypt\src\wc_she.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_slhdsa.obj: ..\wolfcrypt\src\wc_slhdsa.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_xmss.obj: ..\wolfcrypt\src\wc_xmss.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wc_xmss_impl.obj: ..\wolfcrypt\src\wc_xmss_impl.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wolfentropy.obj: ..\wolfcrypt\src\wolfentropy.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wolfevent.obj: ..\wolfcrypt\src\wolfevent.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c
..\wolfcrypt\src\wolfmath.obj: ..\wolfcrypt\src\wolfmath.c user_settings.h
	$(CC) $(CFLAGS) -fo=$@ $*.c