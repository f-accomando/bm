#!/bin/sh
# Makes the key pair that signs the catalog of the bm Market (M25, ECDSA
# P-256). It is not the release key: whoever had this one could not install
# a kernel, only list games (each checked with its SHA-256).
#   private: KEY (default ~/.bm/market-key.pem), stays with you and goes
#            into the secret BM_MARKET_KEY of f-accomando/bm-market;
#   public:  keys/market-pub.pem, built into the kernel: commit it.
# An existing KEY is never overwritten: its public half is written again.
set -eu
KEY=${1:-$HOME/.bm/market-key.pem}
PUB=keys/market-pub.pem
cd "$(dirname "$0")/.."
if [ -e "$KEY" ]; then
    echo "$KEY exists: kept"
else
    mkdir -p "$(dirname "$KEY")"
    (umask 077 && openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$KEY")
    echo "$KEY: new private key (keep a copy: without it the catalog cannot be signed)"
fi
openssl pkey -in "$KEY" -pubout -out "$PUB"
echo "$PUB: public key, built into the kernel"
cat <<MSG

Next (scripts/market.sh does all of it, and puts the games in the Market:
./easy_install.sh market, or m in its menu):
  1. the secret, with the GitHub CLI:  gh secret set BM_MARKET_KEY -R f-accomando/bm-market < $KEY
     or on github.com: f-accomando/bm-market > Settings > Secrets and variables >
     Actions > New repository secret, name BM_MARKET_KEY, value: the whole file
  2. git add $PUB && git commit -m "Market key" && git push
  3. make, and kernel.img on the SD card: the Market tab checks the catalog
MSG
