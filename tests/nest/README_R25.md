# R25 — reprodução da revisão adversarial

Tudo abaixo usa receptores locais e respostas sintéticas. Não precisa do console, do
Nest da casa, do YouTube ou de credenciais. Os targets de falhas usam o código de
produção; somente as syscalls indicadas recebem injeções no link, em Linux.

```bash
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3 --target tube360 nest-test nest-discovery-test cast-test-app cast-protocol-test cast-lan-test nest-scan-fault-test nest-live-clock-test
python3 bench/standalone/cast_test.py client/build/nest-test
python3 tests/nest/discovery_test.py
python3 tests/nest/review_probes.py
python3 tests/cast/lan_faults_test.py
LP_NUM_THREADS=2 python3 tests/cast/ssdp_test.py
LP_NUM_THREADS=2 python3 tests/cast/e2e.py --work .work/r25/cast-e2e
client/build/cast-protocol-test
python3 tests/nest/wifi_player_test.py
python3 tests/nest/live_clock_test.py
```

As bancadas SSDP/DIAL compartilham 8134/1934/8234 e precisam rodar em sequência.
As duas bancadas de descoberta também compartilham os IPs/portas das fixtures.
`review_probes.py` mede os limites de /24 e de muitos serviços TLS mudos; esses
diagnósticos não são asserções de aceitação da descoberta em redes maiores.

Perda permanente depois do LOAD, no player real:

```bash
python3 - <<'PY'
import sys
sys.path.insert(0, 'tests/nest')
from wifi_player_test import run
run('permanent-silence', silence=True, permanent=True)
PY
```

Esse último comando é uma **reprodução de limitação**, não aprovação do produto:
confirma tentativa de liberação após 25 s e TV ainda muda por falta de `quiet`.
Desativar o requisito de silêncio confirmado muda a política de posse e pode tocar
nas duas saídas se o Nest continuar com áudio armazenado durante uma partição.

No hardware, somente o coordenador deve executar o roteiro de `README_F25.md`.
Acrescentar: renovar o IP com o app aberto e conferir LOCATION/NOTIFY e M-SEARCH
com o IP novo; testar uma falha transitória de membership; desligar o Nest depois
de PLAYING e observar se a política escolhida pelo dono permite voltar à TV.
