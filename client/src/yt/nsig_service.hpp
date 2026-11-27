// O desafio `n` dos links do TVHTML5 (vídeo de membros e live de membros), resolvido com o JS do player num QuickJS embutido.
// Um serviço por processo: o programa reduzido fica em memória (cerca de 5 MB) e o bytecode no cache, por versão do player.
#pragma once
#include "client.hpp"
#include <string>
#include <vector>

namespace updates { struct Rules; }
namespace yt::nsig {

// Reescreve o parâmetro `n` de cada URL (cache por valor). Devolve "" ou um código curto: disabled, player_id,
// download_<status>, reduce_<erro>, load_<erro>, solve_<erro>. Em falha nenhuma URL muda.
std::string rewrite(Transport& net, const updates::Rules& rules, const std::string& cacheBase, const std::vector<std::string*>& urls);
// signatureTimestamp do player TV atual (0 se o serviço não carregou). Carrega o programa se preciso.
int timestamp(Transport& net, const updates::Rules& rules, const std::string& cacheBase);

// Partes puras, testáveis sem rede.
// Valor do parâmetro `n` (vazio se não tem).
std::string nValue(const std::string& url);
// Troca o valor de `n`; devolve a URL igual se não há `n`.
std::string withN(const std::string& url, const std::string& value);
// Versão do player anunciada no iframe_api (8 caracteres) ou "".
std::string playerIdFrom(const std::string& iframeApi);
// Esquece o estado em memória (testes).
void resetForTest();
// Contadores para o log e os testes.
struct Counters { unsigned downloads = 0, bytecodeLoads = 0, solves = 0, memoHits = 0; };
Counters counters();
}
