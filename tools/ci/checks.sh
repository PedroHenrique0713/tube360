#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
python3 -m unittest discover -s tests/ci -v
# Contratos locais; os testes de integração de rede ficam fora deste conjunto.
cmake --build client/build -j2 --target search-test display-layout-test arabic-text-test reddit-contract-test cast-protocol-test
client/build/search-test bench/search/fixtures
client/build/display-layout-test bench
for test in arabic-text-test reddit-contract-test cast-protocol-test; do
    "client/build/$test"
done
JOBS=2 tools/build-standalone.sh pc
