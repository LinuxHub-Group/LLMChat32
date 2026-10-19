"""Convert tools/certs/gts_root_{r4,r1}.pem into src/tests/certs_google.h.

Usage (from repo root):
    python tools/pem2hdr.py
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SRC = [os.path.join(HERE, "certs", "gts_root_r4.pem"),
       os.path.join(HERE, "certs", "gts_root_r1.pem")]
NAMES = ["gts_root_r4_pem", "gts_root_r1_pem"]
OUT = os.path.join(ROOT, "src", "tests", "certs_google.h")

for path in SRC:
    if not os.path.exists(path):
        sys.exit("missing %s - extract GTS Root R4/R1 from a Mozilla CA "
                 "bundle first" % path)

out = io.StringIO()
out.write("""/* certs_google.h - public CA roots for the TLS smoke test.
 *
 * Sources (Mozilla CA bundle, curl project / Git for Windows ca-bundle.crt,
 * extracted 2026-10-19):
 *   GTS Root R4 - anchors www.gstatic.com's current ECDSA chain (WE2)
 *   GTS Root R1 - anchors Google's RSA chains (fallback)
 * These are public root certificates, not secret credentials.
 * Regenerate with tools/pem2hdr.py after replacing the PEM files in
 * tools/certs/.
 */
#ifndef LLMCHAT32_TEST_CERTS_GOOGLE_H
#define LLMCHAT32_TEST_CERTS_GOOGLE_H
""")

for fname, ident in zip(SRC, NAMES):
    lines = [l.rstrip("\n") for l in open(fname) if l.strip()]
    assert lines[0].startswith("-----BEGIN") and lines[-1].startswith("-----END")
    out.write("\nstatic const char %s[] =\n" % ident)
    for l in lines:
        out.write('    "%s\\n"\n' % l)
    out.write("    ;\n")

out.write("\n#endif /* LLMCHAT32_TEST_CERTS_GOOGLE_H */\n")

open(OUT, "w").write(out.getvalue())
print("wrote", OUT)
