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
  local::Library restored;
  if(restored.load(path)!=local::Library::Loaded || !restored.later.empty()){std::cerr<<"FAIL clear raced with older snapshot\n";return 1;}
  std::cout<<"301 snapshots submitted; final clear persisted after worker join\n";
}
