# Error reports / Relatórios de erro

## English

Starting in 0.3.3, automatic error reports are **on by default**. A persistent
session marker is written before the launcher hands off to the app and when the
app starts directly. A normal app exit marks it clean. A crash, hang, startup
failure or interrupted power can leave it open. On the next launch, `default.xex`
collects diagnostics before opening `tube360.xex`. This also reaches failures
before the app can draw a screen, where an in-app report button cannot help.
A sudden power loss can look like a crash; a marker cannot distinguish them.

The report contains app version/build, model/board and unlock information **only
as already known by the boot logs** (often `exploit=unknown`), last logged screen,
error or last startup checkpoint, and up to about 300 recent diagnostic lines.
There is no console serial number, persistent device identifier or account-file
upload. Account names, login details, tokens/cookies, video IDs (including watch
history identifiers), IPv4 addresses and Cast device names are removed before
sending. Lines mentioning account/login/search data, Cast/Nest or credentials
are dropped conservatively. The launcher reads diagnostic logs and the reporting
preference; it never reads account, history, library or Cast identity files.
Do not attach raw local logs to a public issue without checking them yourself.

The destination is **https://tube360.hypermind.space/api/report**, operated for
Tube360 by the project owner. HTTPS verifies certificates; uploads have a 64 KiB
ceiling and a short network deadline. An offline report stays sanitized in
`error-report.json` beside the app and retries on the next enabled launcher start.
Only one pending report is retained locally. A short `T360-...` receipt code is
shown for about four seconds after acceptance. This feature adds at most about
six seconds under normal storage/display operation; no work runs per frame.

The receiver sees the network source IP to enforce five requests per hour. It
keeps only a salted hash and timestamps **in memory**, never an IP or its hash on
disk. It stores sanitized diagnostics for **30 days**, at most 200 MiB total,
removing the oldest first, and accepts at most 1,000 unique reports per UTC day.
Identical errors and versions create or update one **public GitHub issue** with
the `crash-report` label. Those issues/comments can remain longer than 30 days;
the receiver's expiry does not delete GitHub content. Contact the project owner
through [the repository](https://github.com/revive360/tube360) to request removal
of an accidentally disclosed detail. The receipt code helps find the report.

To disable **before the app can open**, create an empty **`no-error-reports.txt`**
next to **`default.xex`**. You can also turn off **Settings → Error reports**, or
set **`error_reports=off`** in `settings.txt` in that folder. The file override wins;
a missing setting means on. Disabling prevents all uploads, including a pending
report. Delete local `error-report.json` too if you want to discard that pending
copy permanently. Normal logs and the session marker remain local for diagnosis.

Open source: [launcher collection and scrubber](../client/src/update/launcher_reports.hpp),
[send policy](../client/src/update/launcher.cpp), [session marker](../client/src/boot_log.c),
[receiver and deployment instructions](../tools/error-reports/README.md).
The receiver repeats scrubbing as a second check. Neither the launcher nor
receiver sends emails or other notifications to users.

## Português

A partir da 0.3.3, os **relatórios automáticos de erro vêm ligados**. O launcher
abre um marcador persistente antes de chamar o app; abrir o app diretamente também
cria o marcador. Uma saída normal o marca limpo. Falha de partida, crash,
travamento ou falta de energia podem deixá-lo aberto. Na próxima abertura, o
`default.xex` envia o diagnóstico antes do app. Assim é possível receber erros que
acontecem antes da primeira tela, quando um botão dentro do app não funcionaria.
Uma queda de energia pode ser confundida com crash.

Vão a versão/build, modelo/placa e desbloqueio que os boot logs já souberem
(desconhecido continua desconhecido), última tela registrada, erro ou último passo
de partida e cerca de 300 linhas recentes de diagnóstico. **Não vão** conta,
credenciais, tokens/cookies, IDs de vídeos/histórico, IPv4 ou nomes de aparelhos
Cast. A limpeza remove esses dados antes do envio e descarta linhas de conta,
login, busca e Cast/Nest por precaução. Não são lidos arquivos de conta, histórico,
biblioteca ou identidade Cast. Nunca publique logs brutos sem revisá-los.

O destino é **https://tube360.hypermind.space/api/report**, mantido para o Tube360
pelo dono do projeto. Há HTTPS validado, teto de 64 KiB e prazo curto. Sem rede,
um relatório já limpo fica em `error-report.json` e tenta de novo na próxima
abertura com o recurso ligado. Só uma cópia pendente é mantida. O código de
recibo `T360-...` aparece por cerca de quatro segundos; o acréscimo na partida é
de até aproximadamente seis segundos com disco e vídeo funcionando normalmente.
Não há trabalho por quadro.

O receptor usa o IP de origem só para limitar cinco pedidos por hora: hash com
sal e horários ficam **apenas na memória**, nunca no disco. Guarda diagnósticos
limpos por **30 dias**, até **200 MiB**, apagando os mais antigos; aceita até
1.000 relatórios únicos por dia UTC. Erro + versão são agrupados em uma **issue
pública no GitHub**, com rótulo `crash-report`. As issues/comentários podem durar
mais de 30 dias; o prazo do receptor não os apaga. Peça remoção de um dado exposto
por engano ao dono pelo repositório, mencionando o código de recibo.

Para desligar **sem abrir o app**, crie **`no-error-reports.txt`** vazio ao lado
de **`default.xex`**. Também pode desligar em **Configurações → Relatórios de erro**
ou salvar **`error_reports=off`** no `settings.txt` da mesma pasta. O arquivo tem
prioridade; sem a chave, fica ligado. Desligado não envia nem a cópia pendente;
apague `error-report.json` se quiser descartá-la permanentemente. Logs e marcador
continuam só no aparelho. O código da coleta, da limpeza, do envio e do receptor
está nos links acima. O receptor repete a limpeza antes de armazenar.
