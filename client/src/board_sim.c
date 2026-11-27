#include "board_sim.h"
#include "boot_log.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { const char *name,*chip; int family,hdmi,analog; } Profile;
static const Profile profiles[]={
    {"xenon","ANA",0,0,1},{"zephyr","HANA",1,1,1},
    {"falcon","HANA",2,1,1},{"opus","HANA",2,0,1},
    {"jasper","HANA",3,1,1},{"trinity","HANA",4,1,1},
    {"corona","KSB",5,1,1},{"corona-e","KSB",5,1,0},
    {"winchester","KSB",6,1,0},{"unknown","unknown",-1,0,0}
};
static const char* stages[]={"display-open","output","fonts","mapped-alloc"};
static const Profile* profile;
static int initialized,active,invalid,driver=-1,mode,cable,mapped_off,fault=-1;
static unsigned delay_ms,frame_delay_ms,after,hits[4],fired[4],budget_mib=160;
static const char* origin="none";
static const char* modes[]={"1080p","720p","480p","480w","480i","576i"};
static const char* cables[]={"hdmi","component","vga","composite"};

static int index_of(const char* value,const char* const* values,unsigned count) {
    for(unsigned i=0;i<count;++i)if(!strcmp(value,values[i]))return (int)i;
    return -1;
}
static const Profile* find_profile(const char* name) {
    for(unsigned i=0;i<sizeof(profiles)/sizeof(profiles[0]);++i)
        if(!strcmp(name,profiles[i].name))return &profiles[i];
    return NULL;
}
static int number(const char* value,unsigned max,unsigned* out) {
    if(!*value)return 0;
    unsigned n=0;
    for(const unsigned char* p=(const unsigned char*)value;*p;++p) {
        if(!isdigit(*p) || n>(max-(unsigned)(*p-'0'))/10 || (unsigned)(*p-'0')>max)return 0;
        n=n*10+(*p-'0');if(n>max)return 0;
    }
    *out=n;return 1;
}
static char* trim(char* s) {
    while(isspace((unsigned char)*s))++s;
    char* end=s+strlen(s);while(end>s && isspace((unsigned char)end[-1]))--end;
    *end=0;return s;
}
void tube360_board_sim_init(void) {
    if(initialized)return;
    initialized=1;
    const char* env=NULL;
#ifndef __XSDK__
    env=getenv("XT_BOARD");
    if(env){active=1;origin="XT_BOARD";profile=find_profile(env);if(!profile)invalid=1;}
#endif
    char path[1024];const char* log=boot_log_path();const char* slash=NULL;
    for(const char* p=log;*p;++p)if(*p=='/' || *p=='\\')slash=p;
    size_t parent=slash?(size_t)(slash-log+1):0;
    if(parent+sizeof("board-sim.txt")>sizeof(path)){if(active)invalid=1;}
    else {
        memcpy(path,log,parent);strcpy(path+parent,"board-sim.txt");
        FILE* f=fopen(path,"rb");
        if(f) {
            active=1;origin="board-sim.txt";driver=0;
            char text[1025];size_t n=fread(text,1,sizeof(text)-1,f);
            int extra=fgetc(f),failed=ferror(f);fclose(f);text[n]=0;
            if(failed || extra!=EOF || memchr(text,0,n)){invalid=1;}
            else {
                unsigned seen=0;
                for(char* line=text;line && *line;) {
                    char* next=strchr(line,'\n');if(next)*next++=0;
                    char* key=trim(line);line=next;
                    if(!*key || *key=='#')continue;
                    char* eq=strchr(key,'=');if(!eq){invalid=1;break;}
                    *eq++=0;key=trim(key);char* value=trim(eq);unsigned bit=0;
                    if(!strcmp(key,"board")) {
                        bit=1;const Profile* p=find_profile(value);
                        if(!p || (env && strcmp(env,value)))invalid=1;else profile=p;
                    } else if(!strcmp(key,"driver")) {
                        bit=2;if(!strcmp(value,"safe"))driver=0;else if(!strcmp(value,"native"))driver=1;else invalid=1;
                    } else if(!strcmp(key,"mode")) {
                        bit=4;mode=index_of(value,modes,6)+1;if(!mode)invalid=1;
                    } else if(!strcmp(key,"cable")) {
                        bit=8;cable=index_of(value,cables,4)+1;if(!cable)invalid=1;
                    } else if(!strcmp(key,"mapped")) {
                        bit=16;if(!strcmp(value,"off"))mapped_off=1;else if(strcmp(value,"auto"))invalid=1;
                    } else if(!strcmp(key,"delay_ms")) {
                        bit=32;if(!number(value,250,&delay_ms))invalid=1;
                    } else if(!strcmp(key,"fault")) {
                        bit=64;fault=index_of(value,stages,4);if(fault<0 && strcmp(value,"none"))invalid=1;
                    } else if(!strcmp(key,"fault_after")) {
                        bit=128;if(!number(value,50,&after))invalid=1;
                    } else if(!strcmp(key,"mapped_limit_mib")) {
                        bit=256;if(!number(value,160,&budget_mib) || !budget_mib)invalid=1;
                    } else if(!strcmp(key,"frame_delay_ms")) {
                        bit=512;if(!number(value,50,&frame_delay_ms))invalid=1;
                    } else invalid=1;
                    if(seen&bit)invalid=1;
                    seen|=bit;
                }
            }
        } else if(errno!=ENOENT){active=1;invalid=1;origin="unreadable board-sim.txt";}
    }
    if(active) {
        if(!profile)invalid=1;
        if(!invalid) {
            if(!cable)cable=profile->family<0 || profile->hdmi?1:profile->analog?2:4;
            if(!mode)mode=profile->family<0?2:cable==4?5:profile->hdmi?1:2;
            if((profile->family>=0 && cable==1 && !profile->hdmi) || (profile->family>=0 && (cable==2 || cable==3) && !profile->analog) ||
               (cable==4 && mode!=5 && mode!=6) || (cable==3 && (mode==5 || mode==6)))invalid=1;
        }
        if(invalid) {
            // Arquivo inválido só reduz recursos: nunca executa falha/atraso parcialmente lidos.
            profile=&profiles[9];driver=0;mode=2;cable=4;mapped_off=1;fault=-1;delay_ms=frame_delay_ms=0;budget_mib=160;
        }
    }
}
int tube360_board_sim_active(void){return active;}
int tube360_board_sim_family(void){return profile?profile->family:-1;}
int tube360_board_sim_driver(void){return driver;}
int tube360_board_sim_display_ok(int native){(void)native;return 1;}
int tube360_board_sim_sd(void){return active && !invalid && mode>=3;}
float tube360_board_sim_aspect(void){return tube360_board_sim_sd()?(mode==4?16.f/9:4.f/3):0;}
const char* tube360_board_sim_name(void){return profile?profile->name:"none";}
int tube360_board_sim_extent(int requested,int native,int* width,int* height) {
    if(!active)return 0;
    if(tube360_board_sim_sd()) {
        *width=mode==4?864:640;*height=mode==6?576:480;
    } else if(!native) { *width=1280;*height=720; }
    else if(requested==480) { *width=640;*height=480; }
    else { *height=requested==1080 && mode==1?1080:720;*width=*height*16/9; }
    boot_event("detect","board.sim.extent","profile=%s requested=%d native=%d contract=%dx%d; real signal/clocks/MMIO not emulated, kernel limits still apply",tube360_board_sim_name(),requested,native,*width,*height);
    return 1;
}
int tube360_board_sim_mapped(int native){return !active || (native && !mapped_off);}
uint32_t tube360_board_sim_budget(void){return budget_mib<<20;}
unsigned tube360_board_sim_frame_delay(void){return frame_delay_ms;}
int tube360_board_sim_step(unsigned stage) {
    if(!active || stage>=4)return 0;
    if(delay_ms) {
        boot_event("begin","board.sim.delay","stage=%s delay_ms=%u synthetic=1",stages[stage],delay_ms);
        struct timespec wait={0,(long)delay_ms*1000000};
        while(nanosleep(&wait,&wait) && errno==EINTR){}
        boot_event("end","board.sim.delay","stage=%s",stages[stage]);
    }
    if((int)stage==fault && hits[stage]++>=after && !fired[stage]) {
        fired[stage]=1;
        boot_event("error","board.sim.inject","stage=%s after=%u synthetic=1 one_shot=1; not a measured board failure",stages[stage],after);
        return 1;
    }
    return 0;
}
void tube360_board_sim_log(void) {
    if(!active)return;
    boot_event("detect","board.sim","active=1 source=%s valid=%d profile=%s family=%d driver=%s; real detection retained separately",origin,!invalid,tube360_board_sim_name(),tube360_board_sim_family(),driver<0?"policy":driver?"native":"safe");
    boot_event("detect","board.sim.hardware","synthetic=1 encoder=%s mode=%s cable=%s mapped=%s mapped_limit_mib=%u delay_ms=%u fault=%s after=%u; retail=512MiB CPU=3200MHz GPU=500MHz eDRAM=10MiB; no kernel patches, thermal or speed emulation",profile->chip,modes[mode-1],cables[cable-1],mapped_off?"off":"policy",budget_mib,delay_ms,fault<0?"none":stages[fault],after);
    boot_event("detect","board.sim.latency","frame_delay_ms=%u synthetic=1; injected pacing, not a board clock measurement",frame_delay_ms);
}
