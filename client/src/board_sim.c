// Contrato de display compatível com board/driver/mode/cable da F38; não emula hardware.
#include "board_sim.h"
#include "boot_log.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int initialized,active,invalid,driver=-1,family=-1,mode,cable;
static const char* board="none";
static const char* profiles[]={"xenon","zephyr","falcon","jasper","trinity","corona","winchester","corona-e","opus","unknown"};
static const char* modes[]={"inherit","1080p","720p","480p","480w","480i","576i"};
static const char* cables[]={"inherit","hdmi","component","vga","composite"};
static char* trim(char* s){while(isspace((unsigned char)*s))++s;char* e=s+strlen(s);while(e>s && isspace((unsigned char)e[-1]))--e;*e=0;return s;}
static int index_of(const char* s,const char* const* values,int n){for(int i=0;i<n;++i)if(!strcmp(s,values[i]))return i;return -1;}
void tube360_board_sim_init(void){
    if(initialized)return;initialized=1;
    char path[1024];const char* log=boot_log_path();const char* slash=NULL;
    for(const char* p=log;*p;++p)if(*p=='/' || *p=='\\')slash=p;
    size_t parent=slash?(size_t)(slash-log+1):0;
    if(parent+sizeof("board-sim.txt")>sizeof(path)){active=invalid=1;driver=0;return;}
    memcpy(path,log,parent);strcpy(path+parent,"board-sim.txt");
    boot_event("begin","board.sim.file","path=%s; read once before GPU",path);
    FILE* file=fopen(path,"rb");if(!file){int error=errno;if(error!=ENOENT){active=invalid=1;driver=0;}boot_event("end","board.sim.file","present=0 errno=%d",error);return;}
    active=1;driver=0;
    char text[1025];size_t n=fread(text,1,sizeof(text)-1,file);int more=fgetc(file),failed=ferror(file);fclose(file);text[n]=0;
    if(failed || more!=EOF || memchr(text,0,n))invalid=1;
    unsigned seen=0;
    for(char* line=text;!invalid && line && *line;){
        char* next=strchr(line,'\n');if(next)*next++=0;
        char* key=trim(line);line=next;if(!*key || *key=='#')continue;
        char* value=strchr(key,'=');if(!value){invalid=1;break;}*value++=0;key=trim(key);value=trim(value);unsigned bit=0;
        if(!strcmp(key,"board")){
            bit=1;int i=index_of(value,profiles,10);if(i<0)invalid=1;else{board=profiles[i];family=i<7?i:i==7?5:i==8?2:-1;}
        }else if(!strcmp(key,"driver")){bit=2;driver=!strcmp(value,"native")?1:!strcmp(value,"safe")?0:-1;if(driver<0)invalid=1;
        }else if(!strcmp(key,"mode")){bit=4;mode=index_of(value,modes,7);if(mode<=0)invalid=1;
        }else if(!strcmp(key,"cable")){bit=8;cable=index_of(value,cables,5);if(cable<=0)invalid=1;
        }else invalid=1;
        if(seen&bit)invalid=1;seen|=bit;
    }
    if(!(seen&1))invalid=1;
    if((cable==1 && (!strcmp(board,"xenon") || !strcmp(board,"opus"))) ||
       ((cable==2 || cable==3) && (!strcmp(board,"corona-e") || !strcmp(board,"winchester"))) ||
       (cable==4 && mode && mode!=5 && mode!=6) || (cable==3 && (mode==5 || mode==6)))invalid=1;
    if(invalid){driver=0;family=-1;board="unknown";mode=cable=0;}
    boot_event("end","board.sim.file","present=1 valid=%d",!invalid);
}
int tube360_board_sim_active(void){return active;}
int tube360_board_sim_driver(void){return driver;}
int tube360_board_sim_family(void){return family;}
const char* tube360_board_sim_name(void){return board;}
int tube360_board_sim_sd(void){return active && !invalid && mode>=3;}
float tube360_board_sim_aspect(void){return tube360_board_sim_sd()?(mode==4?16.f/9:4.f/3):0;}
int tube360_board_sim_extent(int requested,int native,int* width,int* height){
    if(!active)return 0;
    if(tube360_board_sim_sd()){*width=mode==4?848:640;*height=mode==6?576:480;}
    else{*height=native && requested==1080 && mode==1?1080:720;*width=*height*16/9;}
    return 1;
}
int tube360_board_sim_display_ok(int native){(void)native;return 1;}
void tube360_board_sim_log(void){
    if(!active)return;
    boot_event("detect","board.sim","active=1 source=board-sim.txt valid=%d profile=%s family=%d driver=%s mode=%s cable=%s simulated=1; real detection and signal unchanged",!invalid,board,family,driver?"native":"safe",modes[mode],cables[cable]);
}
