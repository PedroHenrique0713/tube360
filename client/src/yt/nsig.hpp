// Resolve o desafio `n` (throttling) dos links googlevideo do TVHTML5 com o JS do player, num motor QuickJS embutido.
#pragma once
#include "nsig_reduce.hpp"
#include <memory>
#include <string>

namespace yt::nsig {
class Solver {
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::string start(unsigned timeoutMs);
    std::string finish();
public:
    Solver();
    ~Solver();
    // Avalia o programa reduzido. Devolve "" ou um código curto (eval_failed:<mensagem>, entry_not_callable...).
    std::string load(const Reduced& reduced, unsigned timeoutMs = 20000);
    // Bytecode do QuickJS do mesmo programa: guardar em disco poupa a análise do 1 MB de JS nas próximas aberturas.
    // Vale só para a mesma versão do motor e a mesma arquitetura. Se `loadBytecode` falhar, volte a `load`.
    std::string compile(const Reduced& reduced, std::string& bytecode);
    std::string loadBytecode(const std::string& bytecode, unsigned timeoutMs = 20000);
    // Transforma um `n`. Devolve "" ou um código (solve_failed:<mensagem>, same_value, bad_result).
    std::string solve(const std::string& n, std::string& out, unsigned timeoutMs = 2000);
    size_t memoryBytes() const;
};
}
