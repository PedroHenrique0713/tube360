> **v0.3.0:** só as regras assinadas estão ativas. O launcher e a troca do executável descritos abaixo não vão no pacote: o launcher travou num console real (caminho relativo no `xsdk_launch`) e volta corrigido na v0.3.1.

# Atualizações assinadas

O app embute `client/src/update/rules-default.json`. Ao abrir e a cada 4 horas na Home, um worker procura `rules.json` + `rules.json.sig` e `app.json` + `app.json.sig` em:

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

Guardar backup privado dessa chave: a pública já embutida só confia nela. Nunca publicar o PEM, nem usar a chave descartável dos testes. Para uma instalação nova, antes do primeiro build:

```bash
python3 tools/sign-updates.py keygen --header client/src/update/public_key.hpp
```

O comando falha se a chave já existir. Para publicar regras, copie o padrão para `rules.json`, aumente `version`, edite e assine:

```bash
python3 tools/sign-updates.py sign rules.json
```

Publique **juntos**, no ramo `rules`, `rules.json` e `rules.json.sig`. Não reformate o arquivo depois de assinar. Uma versão recusada/retirada não deve ser reutilizada. O app não tem chave privada e não confia só no TLS do GitHub.

## App: instalação inicial e release

A recuperação de um executável que nem carrega exige um launcher que permaneça íntegro. O pacote inicial deve conter, na mesma pasta com as quatro fontes:

- `default.xex`: cópia de `client/build-x360/tube360-launcher.xex`; entrada do Aurora, permanece estável.
- `tube360.xex`: `client/build-x360/tube360.xex`; aplicativo atualizado.

Quem usa o antigo `default.xex` único precisa instalar esse pacote completo uma vez. Regras funcionam no layout antigo; atualização do executável mostra a orientação de instalação se faltar `tube360.xex`. Não substituir o launcher por um payload.

Cada release recebe `TUBE360_BUILD_ID` inteiro crescente (começa em 1) e `TUBE360_VERSION`. O **mesmo ID** deve estar no build e no manifesto:

```bash
TUBE360_BUILD_ID=2 TUBE360_VERSION=0.3.1 tools/build-xbox.sh
python3 tools/sign-updates.py manifest client/build-x360/tube360.xex \
  --build 2 --version 0.3.1 \
  --url https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.1/tube360.xex \
  --notes-en 'Playback fixes.' --notes-pt 'Correções de reprodução.' --output app.json
```

Anexe o payload à release e publique `app.json`/`app.json.sig` no ramo de regras. O manifesto assina tipo/schema, build, versão, tamanho, SHA-256, URL e notas pt/en. Downloads aceitam só o repositório público e os hosts de assets do GitHub, com TLS validado e até 5 redirects revalidados. O payload é baixado em blocos de 512 KiB com Content-Range conferido (teto total 64 MiB); só um XEX2 com tamanho/hash corretos é preparado.

Home: **LB** abre versão e novidades, **A** baixa, **B** volta/cancela. A troca acontece na próxima abertura pelo launcher. `tube360.previous.xex` guarda o anterior, `tube360.next.xex` o candidato e `update-app-state.json` o journal. O launcher revalida assinatura e hash, grava o estado antes dos renames e marca tentativa de boot. O app só confirma depois de renderizar por pelo menos 3 s, com fontes carregadas, ID de build e hash correspondentes.

Sem confirmação (travamento, encerramento precoce ou XAM recusando o arquivo), **a próxima abertura pelo Aurora restaura automaticamente o anterior**. Erros de caminho que retornam ao launcher restauram imediatamente. O launcher não pode reiniciar um console travado por conta própria. Interrupção entre os renames é retomada. O ID rejeitado não é oferecido novamente; publique outro ID. Um journal ilegível abre diretamente a reserva instalada, se existir, ou o app já instalado; nunca promove `.next`. Nesse caso, a atualização permanece bloqueada até reparar/remover o journal local. Um arquivo `tube360.failed.xex` de falha anterior é substituído antes de outra restauração, inclusive em FATX. Backup/candidato ocupam até duas cópias extras do app. FATX não oferece fsync/ACL POSIX; falta de energia real precisa ser medida no console.

## Provas reproduzíveis

```bash
tools/build-standalone.sh pc
# inclui signed-updates: chaves descartáveis OpenSSL, verificador C++, cache, rollback e recuperação de boot
.work/standalone-pc/yt-update-test --github  # uma leitura pública: valida a cadeia TLS real do GitHub
cmake -S client -B client/build -DCMAKE_BUILD_TYPE=Release
cmake --build client/build -j3
flock /home/pedrohenriquequadro/.cache/tube360-frota-xbox.lock systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=1G tools/build-xbox.sh
```

Para QA em PC: `tests/update/ui-fixture.py <diretório> --key <chave do mantenedor>` prepara respostas assinadas locais (payload falso, nunca enviar ao console). Use `XT_YT_REPLAY=<diretório>`, `XT_ASSETS=<diretório>` e `server=off`; a rede do YouTube não é usada. A chave de assinatura não é copiada para o fixture.
