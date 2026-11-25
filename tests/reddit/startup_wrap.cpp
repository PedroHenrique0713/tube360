#include "startup.hpp"
#include <cstdio>
#include <cstdlib>
int main(){
  for(const auto& text:{std::string(180,'x'),std::string("Falha na conexão: ")+std::string(100,'x')+" ação próxima"}) {
    auto wrapped=startup::wrap(text,40);
    size_t line=0;
    for(unsigned char c:wrapped){if(c=='\n')line=0;else if((c&0xc0)!=0x80)++line;if(line>40){std::fputs("FAIL emergency text exceeds safe width\n",stderr);return 1;}}
  }
  std::puts("PASS emergency wrap: long paths, word boundaries and UTF-8 stay inside safe width");
}
