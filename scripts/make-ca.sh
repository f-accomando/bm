#!/bin/sh
# Builds boot/ca.pem: the root certificates bm trusts for https (M19),
# taken from the Mozilla list shipped by Debian/Ubuntu (ca-certificates).
# A short list on purpose: the roots behind GitHub (github.com, api,
# release downloads), Let's Encrypt, Google, Amazon, Cloudflare/DigiCert.
set -e
SRC=${1:-/usr/share/ca-certificates/mozilla}
OUT=${2:-boot/ca.pem}
ROOTS="
USERTrust_ECC_Certification_Authority
USERTrust_RSA_Certification_Authority
Sectigo_Public_Server_Authentication_Root_E46
Sectigo_Public_Server_Authentication_Root_R46
DigiCert_Global_Root_CA
DigiCert_Global_Root_G2
DigiCert_Global_Root_G3
ISRG_Root_X1
ISRG_Root_X2
GTS_Root_R1
GTS_Root_R2
GTS_Root_R3
GTS_Root_R4
GlobalSign_Root_CA
GlobalSign_Root_CA_-_R3
Amazon_Root_CA_1
Amazon_Root_CA_2
Amazon_Root_CA_3
Amazon_Root_CA_4
"
: > "$OUT"
for r in $ROOTS; do
    f="$SRC/$r.crt"
    [ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
    echo "# $r" >> "$OUT"
    cat "$f" >> "$OUT"
done
echo "$OUT: $(grep -c 'BEGIN CERTIFICATE' "$OUT") root certificates"
