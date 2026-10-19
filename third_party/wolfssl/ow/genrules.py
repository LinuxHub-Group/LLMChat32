"""Regenerate ow/makefile completely. Run from third_party/wolfssl/ow/:
    python genrules.py
Writes ./makefile directly (overwrites).
"""
import glob
import os

EXCL_SRC = set("""bio.c conf.c pk.c pk_ec.c pk_rsa.c ssl_api_cert.c
ssl_api_crl_ocsp.c ssl_api_dtls.c ssl_api_ext.c ssl_api_hs.c ssl_api_pk.c
ssl_api_rw.c ssl_asn1.c ssl_bn.c ssl_certman.c ssl_crypto.c ssl_ech.c
ssl_err.c ssl_load.c ssl_misc.c ssl_p7p12.c ssl_sess.c ssl_sk.c ssl_tsp.c
x509.c x509_str.c""".split())

EXCL_WC = set("asn_orig.c asn_tsp.c evp.c evp_pk.c misc.c asm.c".split())

HEADER = """# makefile for wolfSSL static library - Open Watcom wmake
#
# Builds build\\wolfssl.lib (relative to repo root: ..\\..\\..\\build) from
# the wolfSSL v5.9.4-stable sources. Invoked from the repo root makefile
# via `cd third_party\\wolfssl\\ow && wmake`.
#
# This file is generated - edit genrules.py, then run:
#   cd third_party\\wolfssl\\ow && python genrules.py
#
# Ordering constraints (wmake quirks):
#   * The default goal (all) must be the first explicit target - wmake
#     builds the first target it sees, not a target named "all".
#   * Macros in dependency lines are expanded when the line is PARSED:
#     OBJS* are therefore defined before all/$(LIB).
#   * wmake caps the number of "&" continuation lines in one macro, so
#     OBJS is chunked (OBJS0, OBJS1, ...) and combined.
#   * One explicit rule per source object: wmake implicit rules cannot
#     handle ..\\ relative paths.
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

BUILD_DIR = ..\\..\\..\\build
LIB       = $(BUILD_DIR)\\wolfssl.lib

"""

TAIL = """
all: $(LIB) .SYMBOLIC

$(LIB): $(OBJS)
\t@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
\twlib -n $@ $(OBJS)

clean: .SYMBOLIC
\t@if exist ..\\src\\*.obj del ..\\src\\*.obj
\t@if exist ..\\wolfcrypt\\src\\*.obj del ..\\wolfcrypt\\src\\*.obj
\t@if exist $(LIB) del $(LIB)
"""


def pick(pattern, excl):
    return sorted(p.replace("/", "\\") for p in glob.glob(pattern)
                  if os.path.basename(p) not in excl)


sources = pick("..\\src\\*.c", EXCL_SRC) + pick("..\\wolfcrypt\\src\\*.c", EXCL_WC)

out = [HEADER]
chunk = 40
chunks = [sources[i:i + chunk] for i in range(0, len(sources), chunk)]
for c, part in enumerate(chunks):
    out.append("OBJS%d = &" % c)
    for i, path in enumerate(part):
        obj = path[:-2] + ".obj"
        sep = " &" if i < len(part) - 1 else ""
        out.append("    %s%s" % (obj, sep))
    out.append("")
out.append("OBJS = " + " ".join("$(OBJS%d)" % c for c in range(len(chunks))))
out.append(TAIL)
out.append("# object rules - one explicit rule per source. Every object")
out.append("# also depends on user_settings.h so config edits rebuild.")
for path in sources:
    out.append("%s: %s user_settings.h" % (path[:-2] + ".obj", path))
    out.append("\t$(CC) $(CFLAGS) -fo=$@ $*.c")

with open("makefile", "w") as f:
    f.write("\n".join(out))
print("wrote makefile: %d sources" % len(sources))
