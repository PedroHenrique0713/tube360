# Transferência para revive360: compatibilidade e publicação

**Não transferir às cegas.** A transferência pode interromper as regras assinadas
em clientes antigos se o endereço raw responder com um redirect para a organização.
Manter o repositório antigo no lugar enquanto se distribui o pacote completo é a
opção que preserva esse canal sem depender de comportamento não documentado do raw.
Se a transferência tiver de anteceder a 0.3.2, tratar a operação como condicional:
verificar imediatamente o raw antigo **sem `-L`** e desfazer a transferência caso
não continue respondendo 200 com os mesmos bytes assinados. A 0.3.2 resolve isso
para instalações novas; não altera os executáveis já instalados.

## O que cada versão faz

Fontes auditados: `git show v0.3.0:client/src/update/{service,common}.cpp`, o mesmo
para `v0.3.1`, e `544c274` (base rc4 da F48). Também foram examinados
`client/src/yt/https.cpp`, headers e `tools/package-xbox.py` de cada tag.

Todos já consultam `rules.json`/`.sig` **e** `app.json`/`.sig` na base fixa
`https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/`.
O código de candidatos, portanto, antecede a 0.3.2. Porém os pacotes públicos
0.3.0/0.3.1 colocam o app direto em `default.xex`, sem o layout necessário ao
lançador: a instalação de app novo exige uma última cópia manual completa.
A rc4 já usa `default.xex` lançador + `tube360.xex` app.

O libcurl é reinicializado em cada pedido e não habilita `CURLOPT_FOLLOWLOCATION`.
O worker segue explicitamente 301/302/**303**/307/308, com até cinco pedidos no
laço, chamando `updateUrl` **antes de cada pedido**, inclusive na transferência
por Range. Location relativo, HTTP, userinfo, caminhos percent-encoded e outros
hosts/donos não passam nessa allowlist. Não há requisição à API de releases no app;
o manifesto assinado fornece a URL do payload.

| Versão instalada | Regras após a transferência | Atualização do app após a transferência |
|---|---|---|
| v0.3.0 | Sim se raw antigo servir 200 diretamente. Não se houver salto para `revive360`. Cache e regras embutidas continuam. | Cópia manual completa obrigatória. O motor só aceita manifesto com URL antiga e bloqueia um salto para a org. |
| v0.3.1 | Mesmo resultado da v0.3.0. | Mesmo resultado da v0.3.0. |
| 0.3.2-rc4 / build 6 | Sim com raw antigo 200; não com raw antigo → org. Cache e regras embutidas continuam. | Pode instalar no layout novo se o manifesto vier do raw permitido e o download antigo chegar direto ao CDN permitido. URL da org ou salto antigo → org é bloqueado. |
| 0.3.2 final / build 7 | Org primeiro, antigo como reserva; redirects entre ambos permitidos, sempre autenticados. | Manifesto org/antigo, redirects entre ambos e CDN fixo, Range/tamanho/SHA-256 e Ed25519 verificados. Instalação anterior permanece até o próximo lançamento. |

`updates-url.txt` antigo não contorna a restrição: nas tags e na rc4 o dono novo
é recusado também nesse arquivo. Na final, configuração explícita válida substitui
as duas origens padrão. Sem configuração, cada tipo de documento tenta org e
reserva; assinatura e documento precisam formar um par válido. Um par autenticado
com versão já vista encerra a busca sem aceitar downgrade.

## GitHub: garantia documentada e limite da conclusão

O [GitHub documenta a transferência](https://docs.github.com/en/repositories/creating-and-managing-repositories/transferring-a-repository):
conteúdo e releases acompanham o repositório; links antigos redirecionam. A
[documentação REST](https://docs.github.com/en/rest/using-the-rest-api/best-practices-for-using-the-rest-api#follow-redirects)
orienta seguir Location e tratar 301 como mudança permanente. **Recriar um repo ou
fork no endereço antigo apaga permanentemente os redirects.** Não fazer isso como
“reserva”. Transferência pode ainda aposentar o par antigo por critérios do GitHub;
verificar a possibilidade de retorno antes de confirmar a operação.

- Web, `/releases` e `/releases/tag/...`: navegadores/curl com `-L` seguem a mudança.
- API `/repos/PedroHenrique0713/tube360/releases/...`: consumidores precisam seguir
  Location, que pode usar uma URL por ID. O build do site usa urllib e o runtime usa
  fetch; ambos seguem redirects. O site final passa a consultar a org diretamente.
- `/releases/download/...`: pode haver um salto adicional ao dono novo antes do
  CDN assinado. O CDN já é permitido, mas a org não é permitida pelos clientes antigos.
- `raw.githubusercontent.com`: a documentação não especifica status, cadeia,
  propagação ou prazo para esse endpoint. Um alias interno que mantenha 200 funciona;
  um redirect HTTP para a org falha nos clientes antigos. Não afirmar qual ocorrerá
  sem a checagem posterior. `curl -L` com sucesso, sozinho, não prova compatibilidade.

A prova desta entrega não contata o GitHub real. `tests/update/migration-test.py`
extrai snapshots das tags/rc4 por `git archive`, compila seus workers, validadores e
transportes HTTPS reais, troca apenas a chave pública na cópia e injeta CONNECT_TO
para um TLS local com CA descartável. URLs lógicas, validação de certificado/host,
headers, redirects e limites continuam os dos fontes auditados. O servidor registra
cada URL. A prova cobre 301/302/307/308, permite e bloqueia saltos, assinaturas/schema
inválidos, pares separados, reserva independente, loops e payload/hash/Range ruins.
A injeção de loopback/CA só é compilada no binário da prova. Não existe na build pública.

```sh
python3 tests/update/migration-test.py --output .work/f50/migration
```

## Ordem da transferência e da publicação (mantenedor)

1. Integrar esta branch e manter as tags v0.3.0/v0.3.1 intactas. Guardar commit,
   hashes dos pacotes/arquivos do ramo `rules`, visibilidade, permissões e releases.
   Confirmar admin no repo e permissão para criar repo em `revive360`; o destino
   `revive360/tube360` deve estar livre. Não criar um repo vazio nesse destino.
2. Informar aos usuários que v0.3.0/v0.3.1 precisam copiar o pacote completo uma vez.
   Para garantir continuidade independentemente do raw, adiar a transferência até
   essa migração; manter as regras antigas publicadas e assinadas durante a transição.
   Se a transferência vier primeiro, aceitar apenas o resultado raw 200 dos probes
   abaixo; caso contrário, voltar imediatamente ao dono antigo e adiar.
3. No GitHub pessoal: repo → **Settings → General → Danger Zone → Transfer**;
   selecionar **revive360**, preservar o nome **tube360**, revisar os avisos,
   digitar o nome solicitado e confirmar. Não executar esta etapa por automação
   de preparação. Não apagar/renomear o repo como substituto da transferência.
4. Executar todos os probes abaixo, guardar headers/corpos e verificar identidade,
   tags/releases e bytes. Comparar os arquivos raw antigos com o snapshot anterior;
   verificar as assinaturas com a chave pública embutida. Se o raw antigo não
   responder 200 direto, seguir a reversão antes de anunciar/publicar.
5. Atualizar os remotes dos clones de trabalho: `git remote set-url origin
   https://github.com/revive360/tube360.git`. Revisar permissões da org, Actions,
   proteções e integrações. O site é hospedado separadamente; não há deploy automático
   executado nesta entrega. Créditos e licença de Pedro Henrique Quadro permanecem.
6. Gerar a distribuição final de commit limpo: `TUBE360_VERSION=0.3.2`,
   `TUBE360_BUILD_ID=7`, ambiente público e bancadas OFF, lock do Xbox, `-j2`.
   **Se qualquer ID ≥7 já tiver sido oferecido, usar novo ID maior e recompilar.**
   Executar `python3 tools/package-xbox.py --version 0.3.2` e
   `python3 tests/update/package-test.py .work/releases/tube360-v0.3.2-xbox360.zip`.
   Instalar numa pasta temporária e conferir versão/build, Home, fontes, controle e
   vídeo; preservar a pasta anterior. Não usar manifesto de produção no smoke test.
7. Preparar release draft `v0.3.2` no commit integrado. Anexar ZIP, 7z, checksums e
   **o `tube360.xex` gerado pelo empacotador**, além das fontes/relink materiais
   correspondentes exigidos em [RELEASE.md](RELEASE.md). Rodar a análise pública
   do executável e preencher o link e hash verificáveis, sem inventar aprovação.
8. O dono assina o manifesto de produção com sua chave privada, fora do repo:
   `tools/sign-updates.py --key /caminho/privado/... manifest
   .work/releases/tube360.xex --build 7 --version 0.3.2
   --url https://github.com/revive360/tube360/releases/download/v0.3.2/tube360.xex
   --notes-en '...' --notes-pt '...' --output app.json`.
   Não assinar o XEX bruto do build. Publicar primeiro a release/asset e conferir o
   download e hash; só então publicar `app.json` e `.sig` juntos num único commit
   do ramo `rules`. Regras compatíveis e sua assinatura também ficam juntas.
9. Datar o CHANGELOG com o dia real da publicação, rebuildar o site com dados reais
   da release, rodar as provas de site e revisar EN/PT desktop/mobile. Publicar o
   site somente após o asset público resolver. Repetir os probes antigos depois da
   publicação e após a propagação; testar um cliente antigo sem mudar suas regras
   embutidas e um cliente final. Registrar qualquer indisponibilidade observada.

## Probes exatos depois da transferência

Somente o mantenedor executa estes pedidos públicos, poucos e sequenciais. Não
requerem token/cookie. Para cada endereço há um GET sem seguir e outro seguindo;
`--proto-redir` impede downgrade. Guardar os outputs fora da distribuição.

```bash
mkdir -p .work/transfer-probe
probe() {
  local label=$1 address=$2
  curl --proto '=https' --max-time 30 -sS -D ".work/transfer-probe/$label.first.headers" \
    -o ".work/transfer-probe/$label.first.body" -w '%{http_code}\n' "$address" \
    > ".work/transfer-probe/$label.first.status"
  curl --proto '=https' --proto-redir '=https' --max-redirs 5 --max-time 60 -fsSL \
    -D ".work/transfer-probe/$label.chain.headers" \
    -o ".work/transfer-probe/$label.final.body" \
    -w "$label status=%{http_code} final=%{url_effective}\n" "$address"
}
probe web https://github.com/PedroHenrique0713/tube360
probe releases-web https://github.com/PedroHenrique0713/tube360/releases
probe release-tag https://github.com/PedroHenrique0713/tube360/releases/tag/v0.3.1
probe api-repo https://api.github.com/repos/PedroHenrique0713/tube360
probe api-latest https://api.github.com/repos/PedroHenrique0713/tube360/releases/latest
probe api-v031 https://api.github.com/repos/PedroHenrique0713/tube360/releases/tags/v0.3.1
probe download-v031 https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.1/tube360-v0.3.1-xbox360.zip
for name in rules.json rules.json.sig app.json app.json.sig; do
  probe "raw-$name" "https://raw.githubusercontent.com/PedroHenrique0713/tube360/rules/$name"
done
```

Gate mínimo para regras já publicadas:

```bash
test "$(cat .work/transfer-probe/raw-rules.json.first.status)" = 200
test "$(cat .work/transfer-probe/raw-rules.json.sig.first.status)" = 200
```

Verificar os bytes baixados com a **chave pública** do fonte, sem abrir chave privada:

```python
from pathlib import Path
import re, subprocess
folder = Path('.work/transfer-probe')
key = re.search(r'publicKeyHex = "([0-9a-f]{64})"',
                Path('client/src/update/public_key.hpp').read_text())[1]
(folder/'public.der').write_bytes(bytes.fromhex('302a300506032b6570032100'+key))
for name in ('rules.json',):  # Acrescentar app.json somente se já publicado.
    signature = bytes.fromhex((folder/f'raw-{name}.sig.first.body').read_text().strip())
    assert len(signature) == 64
    (folder/'signature.bin').write_bytes(signature)
    subprocess.run(['openssl', 'pkeyutl', '-verify', '-rawin', '-pubin',
                    '-keyform', 'DER', '-inkey', str(folder/'public.der'),
                    '-in', str(folder/f'raw-{name}.first.body'),
                    '-sigfile', str(folder/'signature.bin')], check=True)
```

O snapshot anterior precisa dizer quais arquivos existem: `app.json` ainda ausente
pode continuar 404; não confundir ausência já existente com regressão. Para regras
ativas, exigir **200 no primeiro pedido** e assinatura válida. No API JSON confirmar
`full_name=revive360/tube360`, tag/ID/assets preservados; no ZIP comparar SHA-256 com
o anterior. Guardar Locations da cadeia dos assets: um cliente antigo não pode
atravessar `github.com/revive360/...`. Repetir para `/raw/...` ou cada base explícita
que tenha sido divulgada aos usuários, caso exista.

Depois de publicar a 0.3.2, executar também:

```bash
probe download-final https://github.com/PedroHenrique0713/tube360/releases/download/v0.3.2/tube360.xex
probe org-rules https://raw.githubusercontent.com/revive360/tube360/rules/rules.json
probe org-rules-sig https://raw.githubusercontent.com/revive360/tube360/rules/rules.json.sig
probe org-app https://raw.githubusercontent.com/revive360/tube360/rules/app.json
probe org-app-sig https://raw.githubusercontent.com/revive360/tube360/rules/app.json.sig
sha256sum .work/transfer-probe/download-final.final.body .work/releases/tube360.xex
```

## Reversão

Se o raw antigo redirecionar ou falhar, não publicar o manifesto/release/site.
Em `revive360/tube360` → Settings → Danger Zone → Transfer, transferir o mesmo repo
para **PedroHenrique0713**, preservando nome, branches, tags e releases. Confirmar
que a conta pessoal pode receber o repo e que o GitHub não aposentou/bloqueou o par;
se impedir, solicitar recuperação ao suporte, sem criar um repo substituto.
Executar os probes de novo e exigir raw antigo 200 e bytes assinados anteriores;
a reversão também pode ter propagação/cache, portanto não promete recuperação
instantânea. Atualizar os remotes conforme o dono efetivo. A final aceita ambos.

Se a release já foi publicada, não reutilizar build ID de candidato retirado e não
editar payload sob o mesmo manifesto. Retirar a oferta com um commit coordenado do
ramo `rules` e publicar uma correção com ID maior. Clientes que já viram ID 7 não
aceitam manifesto de ID menor; o rollback local do lançador preserva essa marca.
Manter os pacotes anteriores para recuperação manual com app fechado e backup de
conta/configurações. Preservar os journals para investigação; seguir o procedimento
de instalação limpa de [UPDATES.md](UPDATES.md) ao trocar manualmente de layout.
