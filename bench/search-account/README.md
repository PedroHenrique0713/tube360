# F47 — busca com conta, diagnóstico e apresentação

A busca de vídeos é pública mesmo com conta conectada e sem filtros. O grant de
TV continua na conta e na Música; não é enviado a `WEB/search`, nem nas
continuações. O bootstrap e a busca usam o handle sem cookies. Assim, um WEB que
responde 400/401/403 ou 200 com erro a um Bearer TV não bloqueia a busca. Não há
personalização da busca nessa rota. Um erro real do pedido anônimo continua sendo
erro; não se inventa uma lista nem se descarta o filtro numa reserva PC.

`test.cpp` usa o cliente de produção com transporte adversarial fechado. São 16
combinações (4 respostas ao Bearer × com/sem conta × com/sem filtro), mais
continuações, preservação dos params, erros HTTP/JSON/rede e ausência de dados
privados nos logs. O cliente de mídia também recebe uma resposta sintética
1920×800/24: a altura real fica 800; a oferta do perfil 1080p aparece no menu.
A política de seleção, limites, deblocking e recuperação continua usando a altura
real. Alturas fixas continuam exigindo correspondência exata.

`prepare.py` cria mídia AVC/AAC e arte sintéticas locais. Reaproveita somente
formatos de DOM das fixtures públicas sanitizadas de `bench/search` e
`bench/catalog`. Os dados da conta são inventados, com token
`console-fixture-token`. Todo HTTPS do app/player é substituído no link; pedidos
sem fixture falham fechado. Nenhum teste acessa YouTube, console, conta, Nest ou
servidor reais. HOME, XDG, assets, settings e cache ficam sob o diretório da prova;
o Cast fica desabilitado. O arquivo `audit.txt` registra somente identidade e
flags dos pedidos, sem token, URL, query ou corpo.

## Reproduzir

```sh
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j2 --target tube360 search-test search-worker-test manual-quality-test
cmake -S bench/catalog -B .work/catalog-build
cmake --build .work/catalog-build -j2
ctest --test-dir .work/catalog-build --output-on-failure
client/build/search-test bench/search/fixtures
client/build/search-worker-test
client/build/manual-quality-test "$PWD/.work/provas/f47/manual-quality"
python3 bench/catalog/check-oauth-tls.py .work/provas/f47/tls
python3 bench/search-account/prepare.py .work/provas/f47/ui
python3 bench/search-account/e2e.py .work/provas/f47/ui
python3 bench/search-account/baseline.py .work/provas/f47
```

O último comando compila a rc3 `af792a2` por `git archive` em pasta própria, sem
trocar de branch, e exige falha da mesma prova (33 falhas) e três reproduções
visuais anteriores: busca sem itens, atalhos sem arte, player 800p24. A prova atual
tem 157 verificações sem falha. `--reuse-objects` serve somente para religar
objetos rc3 já compilados no mesmo diretório.

Dependências das capturas: FFmpeg com libx264, ImageMagick e Pillow. A suíte atual
captura busca com conta, filtros, Música, player, menu Y e aviso real de sobrecarga
manual nos quatro idiomas, além de Música 4:3 com texto grande, convidados,
recusas adversariais, servidor PC simulado e erro HTTP 200 no pedido anônimo. O aviso é provocado por
atraso de decodificação somente no executável de fixture. Confere que a fonte fixa
não mudou. As três artes recebem verificações de pixels além da revisão visual.
Os PNGs e o manifesto `screenshots.json` são artefatos da execução, não provas de
autorização de conta real ou desempenho do Xbox.

## Diagnóstico de falhas

O catálogo usa um logger ligado ao `LOG` do app; fora dele escreve no stderr.
Exemplos:

```text
catalogo falha: rota=WEB/search http=400 codigo=HTTP 400 api_code=400
catalogo falha: rota=WEB/search http=200 codigo=youtube_unavailable api_code=400
catalogo falha: rota=WEB/bootstrap http=0 codigo=network_error transport=60 failure=3 tls=-9984 verify=-1 api_code=0
```

Rotas são nomes internos constantes. Códigos HTTP/API/TLS/transporte são números;
o texto remoto e o texto bruto do erro de transporte nunca entram no log. Erros
de bootstrap, DOM, JSON, cursores ou navegação também têm códigos locais. A linha
final `/api/nav` pode acompanhar o detalhe `WEB/search`: a primeira descreve a
falha que chegou à navegação, a segunda identifica a operação remota que falhou.
`http=0` indica que não houve HTTP nessa operação. Uma recusa WEB_REMIX recuperada
pela TV também permanece registrada antes do sucesso `music=tv`.

O log rc3 fornecido pelo coordenador mostra 40 bytes e zero itens, sem status do
YouTube. Ele não permite afirmar qual dessas respostas foi recebida naquele
console. A falha de contrato (Bearer de TV na busca WEB) e as lacunas 400/200 são
reproduzíveis offline; o HTTP exato daquela execução continua desconhecido.

Roteiro de console, resultados e riscos de integração: [RELATORIO.md](RELATORIO.md).
