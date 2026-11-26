`error-report-console.txt` deriva de um log real de reprodução no Xbox, coletado
em 2026-11-24 (`teste-v031-2002/tube360.log`). Foi passado integralmente pelo
scrubber C++ da F54 antes de entrar no Git. IPs, IDs de vídeo, linhas de conta e
Cast e sequências com aparência de credencial foram removidos. O teste combina
esse formato real com canários privados fictícios e também pode receber um log
bruto local por `--real-log` (nunca o envia).
