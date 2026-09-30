#!/bin/sh
# Makes the key pair that signs bm releases (M19, ECDSA P-256).
#   private: KEY (default ~/.bm/release-key.pem), stays with you and goes
#            into the repository's secret BM_RELEASE_KEY;
#   public:  keys/release-pub.pem, built into the kernel: commit it.
# An existing KEY is never overwritten: its public half is written again.
set -eu
KEY=${1:-$HOME/.bm/release-key.pem}
PUB=keys/release-pub.pem
cd "$(dirname "$0")/.."
if [ -e "$KEY" ]; then
    echo "$KEY exists: kept"
else
    mkdir -p "$(dirname "$KEY")"
    (umask 077 && openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$KEY")
    echo "$KEY: new private key (keep a copy: without it no release can be signed)"
fi
openssl pkey -in "$KEY" -pubout -out "$PUB"
echo "$PUB: public key, built into the kernel"
cat <<MSG

Next:
  1. the secret, with the GitHub CLI:  gh secret set BM_RELEASE_KEY < $KEY
     or on github.com: Settings > Secrets and variables > Actions >
     New repository secret, name BM_RELEASE_KEY, value: the whole file
  2. git add $PUB && git commit -m "Release key" && git push
  3. a release: git tag v0.1.0 && git push origin v0.1.0
MSG
