#include "local_library_worker.hpp"
#include <filesystem>
#include <iostream>
int main(int argc,char** argv){
  if(argc!=2)return 2;
  std::filesystem::create_directories(argv[1]);std::string path=std::string(argv[1])+"/writer.json";
  local::Library state;
  {
    local::Writer worker(path);
    for(int i=0;i<300;++i){char id[16];std::snprintf(id,sizeof(id),"test%07d",i);state.toggle({id,"Fixture","Demo",120});worker.submit(state);}
    // Um clear precisa vencer tanto um snapshot em escrita quanto a vaga pendente.
    state.clear(false);worker.submit(state);
  }
  // Corrida no desligamento (#42): abrir e fechar o gravador com escrita pendente, muitas vezes; stop() duas vezes não pode quebrar.
  for(int round=0;round<3000;++round){
    local::Library l;local::Writer w(path+".stress");
    for(int i=0;i<(round%5);++i){char id[16];std::snprintf(id,sizeof(id),"stres%06d",i);l.toggle({id,"Fixture","Demo",120});w.submit(l);}
    if(round%2)w.stop();
    if(round%7==0)w.stop();
  }
  local::Library restored;
  if(restored.load(path)!=local::Library::Loaded || !restored.later.empty()){std::cerr<<"FAIL clear raced with older snapshot\n";return 1;}
  std::cout<<"3000 open/close rounds survived; 301 snapshots submitted; final clear persisted after worker join\n";
}
