// Desligamento (#42): o app chama SDL_Quit com o gravador da Biblioteca vivo. A ordem antiga (SDL_Quit antes de destruir o Writer)
// abortava de forma intermitente; a ordem do main agora é localUI.shutdown() (Writer::stop) e só depois SDL_Quit.
// uso: writer-quit-test <pasta> [rodadas]. Modo "old" (3º argumento) reproduz a ordem antiga e serve para provar que o laço pega o defeito.
#include "local_library_worker.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
int main(int argc,char** argv){
  if(argc<2)return 2;
  std::string dir=argv[1]; int rounds=argc>2?std::atoi(argv[2]):3000; bool old=argc>3 && !std::strcmp(argv[3],"old");
  std::filesystem::create_directories(dir);
  for(int round=0;round<rounds;++round){
    SDL_Init(0);
    local::Library library; auto* writer=new local::Writer(dir+"/quit.json");
    for(int i=0;i<(round%5)+1;++i){char id[16];std::snprintf(id,sizeof(id),"quit%07d",i);library.toggle({id,"Fixture","Demo",120});writer->submit(library);}
    if(!old)writer->stop();
    SDL_Quit();
    delete writer;
  }
  std::printf("%d SDL_Init/SDL_Quit rounds with a live writer survived (%s order)\n",rounds,old?"old":"new");
}
