// Antes de pthread/newlib: só buffer de pilha e chamadas de arquivo do kernel.
#include <stddef.h>
#include <stdint.h>
#ifdef __XSDK__
#include <xsdk/kernel.h>
static HANDLE raw_file;
static int attempted;
static void add(char* out,size_t* used,size_t capacity,const char* value) {
    while(*value && *used+1<capacity){char c=*value++;out[(*used)++]=(c=='\n'||c=='\r')?' ':c;}
    out[*used]=0;
}
void tube360_boot_raw(const char* phase,const char* step) {
    if(!attempted) {
        attempted=1;
        char path[1024];size_t n=0;
        const char* image=ExLoadedImageName?ExLoadedImageName:"";const char* last=NULL;
        for(const char* p=image;*p;++p)if(*p=='\\')last=p;
        if(last && (size_t)(last-image+1)+16>=sizeof(path))return;
        if(last)for(const char* p=image;p<=last && n+1<sizeof(path);++p)path[n++]=*p;
        else add(path,&n,sizeof(path),"game:\\");
        path[n]=0;add(path,&n,sizeof(path),"boot-crt.log");
        // Caminho curto perto do limite do resolvedor; nunca escreve em outro drive.
        if(n>=260 && last){n=(size_t)(last-image+1);path[n]=0;add(path,&n,sizeof(path),"bc.log");}
        const ANSI_STRING name={.length=(uint16_t)n,.maximum_length=(uint16_t)(n+1),.pointer=path};
        const OBJECT_ATTRIBUTES attributes={.root_directory=path[0]=='\\'?0:OB_DOS_DEVICES,.name=&name};
        IO_STATUS_BLOCK io;
        NTSTATUS status=NtCreateFile(&raw_file,GENERIC_WRITE|SYNCHRONIZE,&attributes,&io,NULL,
            FILE_ATTRIBUTE_NORMAL,FILE_SHARE_READ|FILE_SHARE_WRITE,FILE_OVERWRITE_IF,
            FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE);
        if(!NT_SUCCESS(status))raw_file=0;
    }
    if(!raw_file)return;
    char line[1536];size_t used=0;
    add(line,&used,sizeof(line),"boot raw ");add(line,&used,sizeof(line),phase);add(line,&used,sizeof(line)," ");
    add(line,&used,sizeof(line),step);add(line,&used,sizeof(line)," image=");
    add(line,&used,sizeof(line),ExLoadedImageName?ExLoadedImageName:"unknown");
    if(used+1<sizeof(line))line[used++]='\n';
    IO_STATUS_BLOCK io;
    NtWriteFile(raw_file,0,NULL,NULL,&io,line,(uint32_t)used,NULL);
    NtFlushBuffersFile(raw_file,&io);
}
void tube360_boot_raw_close(void){if(raw_file){NtClose(raw_file);raw_file=0;}}
#else
void tube360_boot_raw(const char* phase,const char* step){(void)phase;(void)step;}
void tube360_boot_raw_close(void){}
#endif
