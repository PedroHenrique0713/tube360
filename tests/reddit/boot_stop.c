#include "boot_log.h"
#include <stdlib.h>
#include <unistd.h>
int main(int argc,char** argv){
    if(argc!=3)return 2;
    boot_open(argv[1],"test");
    boot_event("begin","fixture.stop","saved before abrupt stop; no normal close");
    if(argv[2][0]=='e')_Exit(23);
    for(;;)pause();
}
