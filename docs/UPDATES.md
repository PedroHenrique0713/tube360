# Atualizações assinadas

O app embute `client/src/update/rules-default.json`. Após o início saudável e a cada 4 horas na Home/Configurações, um worker procura `rules.json` + `rules.json.sig` e `app.json` + `app.json.sig` em:

```
https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/
```

`updates-url.txt` ao lado do app pode escolher outro diretório/ramo **nesse repositório**, com HTTPS e `/` final. Falha de rede, 404 ou assinatura inválida conserva os dados anteriores. O ramo e os arquivos precisam ser publicados pelo mantenedor; o aplicativo não os cria.

## Regras

A assinatura Ed25519 autentica os **bytes exatos**, incluindo espaços e quebra final. `.sig` contém 128 caracteres hexadecimais minúsculos e uma quebra opcional. A chave pública fica em `client/src/update/public_key.hpp`. Monocypher 4.0.3 verifica Ed25519/RFC 8032; mbedTLS calcula SHA-256. Nenhum JavaScript remoto é executado: o JS do YouTube é lido somente para extrair valores por marcadores literais.

O JSON usa schema 1 e versão inteira crescente. O padrão é também o exemplo completo do schema. Campos desconhecidos, perfis vazios, cabeçalhos não permitidos, origens externas, controles em strings e limites excessivos são recusados. Perfis configuram nome, código, versão, UA, dados do dispositivo, cabeçalhos adicionais e ordem de tentativa; há marcadores de contexto/visitante/player JS/timestamp e identidade TV, chave da API, URLs e limites de rede/retry/paginação. Clientes existentes descartam contextos e sessões ao mudar a versão. O cache de timestamp também inclui a versão das regras.

Origens com credenciais ficam restritas a YouTube e OAuth Google; links de mídia/imagem usam a família YouTube/Google (`youtube.com`, `googlevideo.com`, `ytimg.com`, `ggpht.com`, `googleusercontent.com`, `googleapis.com`, `google.com`). Userinfo, porta explícita, HTTP e nomes que apenas começam com o domínio são recusados. Regras não podem mudar os tetos de segurança, os domínios, o verificador ou executar expressões/regex. Mudanças na estrutura dos renderizadores, algoritmos de decifração ou codecs ainda exigem código novo.

`update-rules-state.json` grava atomicamente regra ativa, última validada, assinaturas, maior versão vista e contadores. Após 3 operações válidas consecutivas, a regra pode servir de reserva para a próxima candidata. A reserva anterior permanece disponível: 3 falhas de compatibilidade consecutivas restauram-na mesmo se as primeiras operações da regra nova funcionaram. Falhas de rede, cancelamentos, TLS, 429 e 5xx não contam como incompatibilidade. Recusa de todos os clientes nos status de fallback conta, mesmo quando o JSON é válido. Repetições HTTP internas contam como uma operação de player. A maior versão vista persiste no rollback: uma correção precisa de número maior. O fallback embutido continua disponível mesmo com cache ilegível.

Os limites de bytes/retries/cards são ajustáveis dentro de tetos compilados. Limites do decodificador, tamanhos de IDs, enumerações do protocolo interno e formatos de áudio/vídeo continuam no código: protegem o hardware e o contrato da interface.

## Chave do mantenedor e assinatura

A chave inicial desta implementação foi criada **fora do Git** em:

```
~/.config/tube360/update-ed25519.pem
```

Guardar backup privado dessa chave: a pública já embutida só confia nela. Nunca publicar o PEM, nem usar a chave descartável dos testes. A ferramenta exige o caminho explícito da chave; ela não procura nem lê a chave por
padrão. Para assinar regras já revisadas, aumente `version` e rode, como mantenedor:

```bash
python3 tools/sign-updates.py --key /caminho/privado/update-ed25519.pem sign rules.json
```

`keygen --header` só serve para iniciar outro projeto com outra identidade de
confiança; não regenere a chave do Tube360. A bancada usa sua própria chave e
configuração de compilação, sem alterar `public_key.hpp`.

Publique **juntos**, no ramo `rules`, `rules.json` e `rules.json.sig`. Não reformate o arquivo depois de assinar. Uma versão recusada/retirada não deve ser reutilizada. O app não tem chave privada e não confia só no TLS do GitHub.

## App: instalação e migração da v0.3.0/v0.3.1

As versões públicas v0.3.0/v0.3.1 usam o aplicativo direto em `default.xex`.
Elas **não conseguem instalar o lançador sozinhas**. O caminho seguro é uma última
cópia manual do pacote completo, com o app fechado. Guarde a pasta anterior como
reserva; na pasta nova, preserve configurações/conta de sua instalação se desejar,
mas não traga `update-app-state.json`, `.next`, `.previous` ou `.failed` antigos.
O pacote não sobrescreve configurações, pois leva apenas arquivos `.example.txt`.

O formato novo mantém juntos:

- `default.xex`: lançador estável, entrada do Aurora.
- `tube360.xex`: aplicativo, único executável substituído por atualização.
- As quatro fontes `inter-*.ttf` e as licenças do pacote.

Abra sempre **default.xex**. Não renomeie o payload para `default.xex` e não
substitua o lançador pelo aplicativo. No layout antigo a tela explica a migração,
sem tentar trocar o executável em execução. Reserve espaço gravável para o app,
o candidato e a reserva (aproximadamente três vezes o tamanho do payload).

## Publicar uma versão nova (somente o mantenedor)

Escolha `TUBE360_BUILD_ID` inteiro maior que TODOS os já oferecidos, inclusive
candidatos retirados ou que sofreram rollback, e uma `TUBE360_VERSION` de até
40 caracteres. O ID e a versão devem ser os mesmos no build e no manifesto.
Os números abaixo são exemplos; confira o maior ID publicado antes de escolhê-los.
Uma build pública precisa estar sem `TUBE360_UPDATE_TEST_CONFIG` nas flags:

```bash
# Em cache já configurado, limpa a configuração descartável da bancada, se houve.
cmake -S client/x360 -B client/build-x360 '-DCMAKE_CXX_FLAGS=-mcpu=cell -fno-pic' \
  -DTUBE360_BUILD_ID=3 -DTUBE360_VERSION=0.3.2
TUBE360_BUILD_ID=3 TUBE360_VERSION=0.3.2 tools/build-xbox.sh
python3 tools/package-xbox.py --version 0.3.2
python3 tools/sign-updates.py --key /caminho/privado/update-ed25519.pem manifest \
  .work/releases/tube360.xex --build 3 --version 0.3.2 \
  --url https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.2/tube360.xex \
  --notes-en 'Playback fixes.' --notes-pt 'Correções de reprodução.' --output app.json
```

Assine **o payload do empacotador** `.work/releases/tube360.xex`: ele remove caminhos
do host do ELF e reconverte, portanto seu hash pode diferir do build bruto. O ZIP/7z
contém exatamente esse payload, além do lançador. O manifesto do pacote registra
os dois hashes, o layout e `test_only=false`. O empacotador recusa uma build de teste
sem a opção explícita `--test-package`; pacotes com essa opção nunca são públicos.
O script de assinatura valida schema/URL/notas e produz `app.json.sig`; não publica.

Anexe `.work/releases/tube360.xex` e os pacotes à release. Publique **app.json e
app.json.sig juntos**, sem reformatação, no ramo **rules** do repositório público.
A chave privada não sai do computador do mantenedor. A bancada F26 não lê nem usa
essa chave. O manifesto assinado contém tamanho, SHA-256, URL, ID, versão e notas
pt/en. A assinatura Ed25519 autentica também o hash do payload.

## Baixar, instalar e recuperar

**Configurações → Atualizações do app → A** mostra versão instalada/ID, versão
oferecida, tamanho em MiB e notas. **A Atualizar** baixa e verifica; **B** cancela
ou volta, inclusive durante uma requisição. **X** verifica novamente quando não
há download ativo. A Home mostra um aviso discreto; **LB** também abre essa tela.
O desenho não espera por rede, disco nem hash: o worker publica apenas o estado.
Um candidato já preparado permanece pronto após reiniciar o app.

Downloads públicos aceitam apenas o repositório e assets de releases do GitHub,
com TLS validado e redirects revalidados. O payload vem em blocos de 512 KiB com
`Content-Range` exato (teto total 64 MiB). Só XEX2 com tamanho/hash corretos e
manifesto Ed25519 válido é preparado. Erros/cancelamentos removem o arquivo
parcial; o aplicativo instalado permanece intacto. Uma oferta válida já conhecida
é preservada se a próxima checagem falhar.

A troca ocorre na próxima abertura pelo lançador. `tube360.previous.xex` guarda o
anterior, `tube360.next.xex` o candidato, `update-app-state.json` o journal. O
lançador revalida assinatura e hash, grava o estado antes dos renames e marca
`trial`. O app confirma `good` só depois de renderizar por pelo menos três segundos,
com fontes carregadas, ID de build e hash correspondentes. Essa confirmação
antecede consultas de rede, para não depender da disponibilidade do GitHub.

Sem confirmação, a próxima abertura de `default.xex` restaura automaticamente o
anterior. Erros de launch que retornam ao lançador restauram imediatamente e
fazem uma única tentativa de abrir a reserva. O lançador não pode reiniciar um
console/kernel travado. A retomada cobre interrupção entre renames; falta de
energia real e durabilidade FATX ainda precisam de medição no console.

O ID rejeitado permanece no high water e não é oferecido novamente: publique ID
maior para corrigir. Journal ilegível abre a reserva instalada, se existir, ou o
app já instalado; nunca promove `.next`. Atualizar permanece bloqueado até reparar
o journal local. Guarde os arquivos antes de uma intervenção manual. O arquivo
`tube360.failed.xex` conserva o rejeitado até o próximo download. O SDK implementa
rename com substituição, mas o FATX não tem as garantias de fsync/ACL POSIX.

Logs: `launcher.log` é exclusivo do lançador e recebe flush por linha; `update.log`
registra oferta, download, preparo e confirmação do app. No Xbox ambos também
chegam ao logger/UDP do xsdk. Não contêm chave privada, conta nem cookies.

## Bancada F26 sem publicação

[Passo a passo de dez minutos](../tests/update/CONSOLE.md). O gerador
`tests/update/console-kit.py build` prepara N, N+1 e um candidato que deliberadamente
não confirma o boot, assinados com chave descartável dentro de `.work/`.
O servidor local é `http://192.168.101.27:8266/`; a chave pública e a exceção HTTP
só entram nas builds com header de teste passado explicitamente ao compilador.
Nenhuma edição da chave pública de produção, configuração do console real ou
publicação é necessária. O teste de reserva usa saída normal para o Aurora,
sem provocar travamento do console.

Para o coordenador sem controle, `tests/update/console-run.py --host <IP-XBDM>`
executa e coleta o roteiro automaticamente usando `update-auto.txt`, lido **só**
na build de teste. O gatilho chama o mesmo download da interface. O runner foi
exercitado por TCP contra XBDM falso local, com falhas e limpeza por PID; sua
execução contra o Xbox real é exclusiva do coordenador. Veja o roteiro acima.

## Lançador instrumentado e limite da prova em console

`launcher.log` é exclusivo do lançador, acrescenta uma sessão a cada abertura e recebe `fflush` por linha. Os mesmos eventos passam por `xsdk_log_write` e pelo UDP configurado em `net_log.txt` (sequência de 4 bytes em ordem de rede, seguida de UTF-8). O log do SDK usa o nome do XEX: `default.log` para o lançador e `tube360.log` para o app no pacote de teste. Eles não substituem `launcher.log`.

Em 2026-11-24 o lançador instrumentado de `374f246` abriu o app no console real em cerca de 0,2 s, sem journal. Isso valida o primeiro handoff; troca, confirmação e rollback ainda dependem da bancada F26. A causa do travamento antigo não foi isolada.

O lançador desenha um quadro com “Tube360 – abrindo…” (pt/en/es, conforme `settings.txt`) usando o texto embutido do xsdk, registra journal, prepare, progresso de SHA-256 por MiB, renames e cada chamada/retorno de launch. A primeira execução sem journal abre o app instalado sem calcular hash. GPU e apresentação são fechadas antes de lançar `game:\tube360.xex`. Caminhos relativos também são resolvidos pelo xsdk: não há prova de que fossem a causa do travamento.

Um erro que retorna tenta a reserva uma vez quando existe um trial e encerra o título ao falhar. Após devolver a GPU ao XAM, o handoff arma um worker de 20 s para pedir `XamLoaderTerminateTitle` se o título continuar vivo. Esse pedido não recupera um travamento do kernel/XAM nem garante qual dashboard será retomado; tela, watchdog e transição real continuam pendentes de teste no console. Não há espera por rede ou entrada do usuário no lançador.

## Provas reproduzíveis

```bash
tools/build-standalone.sh pc
# inclui signed-updates: chaves descartáveis OpenSSL, verificador C++, cache, rollback e recuperação de boot
.work/standalone-pc/yt-update-test --github  # uma leitura pública: valida a cadeia TLS real do GitHub
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3
flock ~/.cache/tube360-xbox.lock systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G tools/build-xbox.sh
```

Para QA em PC: `python3 tests/update/service-test.py` exercita oito cenários HTTP, gera chave descartável e um binário visual separado. `tests/update/ui-fixture.py <diretório> --key <chave descartável>` prepara respostas assinadas locais (payload falso, nunca enviar ao console). Use `XT_YT_REPLAY=<diretório>`, `XT_ASSETS=<diretório>` e `server=off`; a rede do YouTube não é usada. A chave de assinatura não é copiada para o fixture.
