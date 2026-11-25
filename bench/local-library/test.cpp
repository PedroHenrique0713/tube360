#include "local_library.hpp"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <limits>

static int checks=0, failWrite=-1; static bool failSync=false,failRename=false;
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_rename(const char*,const char*);
extern "C" ssize_t __wrap_write(int fd,const void* data,size_t n){if(failWrite==0){errno=ENOSPC;return -1;}if(failWrite>0){n=std::min(n,size_t(failWrite));failWrite-=int(n);}return __real_write(fd,data,n);}
extern "C" int __wrap_fsync(int fd){if(failSync){errno=EIO;return -1;}return __real_fsync(fd);}
extern "C" int __wrap_rename(const char* a,const char* b){if(failRename){errno=EACCES;return -1;}return __real_rename(a,b);}
void check(bool ok,const char* what){++checks;if(!ok){std::cerr<<"FAIL "<<what<<'\n';std::exit(1);}}
std::string read(const std::string& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
std::string id(int n){char b[16];std::snprintf(b,sizeof(b),"test%07d",n);return b;}
local::Entry video(int n,double position=30){return {id(n),"Fixture vídeo · العربية", "Canal de bancada",120,position};}
int main(int argc,char** argv){
  if(argc!=2)return 2;
  std::filesystem::create_directories(argv[1]);std::string path=std::string(argv[1])+"/library.json";
  std::filesystem::remove(path);std::filesystem::remove(path+".tmp");
  local::Library a,b;
  check(a.load(path)==local::Library::Missing,"first use");
  check(!a.toggle({"../bad","",""}),"invalid identifier");
  check(a.toggle(video(1)) && a.contains(id(1)),"add");
  check(a.toggle(video(1)) && !a.contains(id(1)),"remove");
  a.toggle(video(1));a.record(video(1));
  check(a.save(path),"atomic initial write");auto original=read(path);
  check(b.load(path)==local::Library::Loaded && b.contains(id(1)) && b.position(id(1))==30,"roundtrip");
  a.toggle(video(2));failWrite=40;check(!a.save(path),"partial write then ENOSPC");failWrite=-1;
  check(read(path)==original && !std::filesystem::exists(path+".tmp"),"ENOSPC preserves committed file and removes temp");
  failSync=true;check(!a.save(path),"sync error");failSync=false;check(read(path)==original,"sync error preserves file");
  failRename=true;check(!a.save(path),"rename failure");failRename=false;check(read(path)==original,"rename error preserves file");
  check(a.save(path) && b.load(path)==local::Library::Loaded && b.contains(id(2)),"retry after space restored");
  for(int i=0;i<220;++i)a.toggle(video(100+i));
  check(a.later.size()==200 && a.later.front().id==id(319) && !a.contains(id(1)),"list evicts oldest");
  for(int i=0;i<520;++i)a.record(video(1000+i));
  check(a.history.size()==500 && a.position(id(1000))==0 && a.position(id(1519))==30,"history evicts least recent");
  a.record(video(1020,45));check(a.history.front().id==id(1020),"revisit refreshes LRU");
  a.record(video(1020,3));check(a.position(id(1020))==0,"under 10 seconds erased");
  for(double p:{0.0,9.9,110.0,119.0,120.0,130.0,std::numeric_limits<double>::infinity()}){a.record(video(9,p));check(a.position(id(9))==0,"invalid or finished position excluded");}
  a.record(video(9,10));check(a.position(id(9))==10,"10 second boundary");
  a.record(video(9,10),true);check(a.position(id(9))==0,"natural end erased");
  auto live=video(10);live.duration=0;a.record(live);check(a.position(id(10))==0,"live excluded");
  auto longVideo=video(11,950);longVideo.duration=1000;a.record(longVideo);check(a.position(id(11))==0,"95 percent excluded");
  longVideo.position=949;a.record(longVideo);check(a.position(id(11))==949,"before 95 percent resumes");
  check(a.save(path) && read(path).size()<local::Library::byteLimit,"bounded full library saves");
  check(b.load(path)==local::Library::Loaded && b.later.size()==200 && b.history.size()==500,"full library roundtrip");
  auto before=b.encode();for(auto raw:{std::string("oops"),std::string("{\"version\":1}"),std::string(100,'{'),std::string(local::Library::byteLimit+1,'x'),before+"garbage"})check(!b.decode(raw) && b.encode()==before,"corrupt input leaves state unchanged");
  std::ofstream(path)<<"{bad";check(b.load(path)==local::Library::Corrupt && read(path)=="{bad","corrupt file retained");
  std::ofstream(path)<<std::string(local::Library::byteLimit+1,'x');check(b.load(path)==local::Library::Corrupt,"oversize file rejected");
  b.clear(true);check(b.history.empty() && b.later.size()==200,"clear history only");b.clear(false);check(b.later.empty(),"clear watch later only");
  check(b.save(path) && a.load(path)==local::Library::Loaded && a.later.empty() && a.history.empty(),"clear persists on reopen");
  std::cout<<checks<<" checks passed (real file I/O; injected ENOSPC, fsync and rename failures)\n";
}
