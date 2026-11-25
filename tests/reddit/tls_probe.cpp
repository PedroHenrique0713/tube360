#include "yt/client.hpp"
#include "boot_log.h"
#include <cstdlib>
#include <cstdio>
int main(int argc,char** argv){
  if(argc!=3)return 2;
  if(const char* directory=std::getenv("XT_BOOT_PROBE"))boot_open(directory,"tls-test");
  auto net=yt::https();auto response=net->request(argv[1]);
  auto failure=yt::failureName(response.failure);
  std::printf("TLS probe: %s %s HTTP %ld body %zu\n",failure,response.error.c_str(),response.status,response.body.size());
  return failure==std::string(argv[2]) && response.body.empty() && response.status==0 ? 0:1;
}
