// Executa o wrapper C do produto. Stub só para as fronteiras kernel/pthread.
#include <xsdk/thread.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
static unsigned count,places,priorities,hardware[20];
static int fail;
HANDLE xsdk_pthread_handle(pthread_t id) {return (unsigned)id;}
NTSTATUS xsdk_thread_place(HANDLE id,unsigned hw) {assert(id>100);hardware[places++]=hw;return 0;}
NTSTATUS xsdk_thread_set_priority(HANDLE id,int32_t priority,int32_t* old) {assert(id>100 && priority==0 && !old);++priorities;return 0;}
void xsdk_log_write(const char* format,...) {(void)format;}
int __real_pthread_create(pthread_t* id,const pthread_attr_t* attr,void* (*fn)(void*),void* data) {
  (void)attr;(void)fn;(void)data;*id=101+count++;return fail?11:0;
}
void ffmpeg_workers(int);
int __wrap_pthread_create(pthread_t*,const pthread_attr_t*,void* (*)(void*),void*);
static void create(void){pthread_t id;assert(__wrap_pthread_create(&id,0,0,0)==(fail?11:0));}
int main(void) {
  create();assert(!places);ffmpeg_workers(2);
  for(int i=0;i<6;++i)create();
  unsigned expected[]={0,2,4,1,3,5};
  for(unsigned i=0;i<6;++i)assert(hardware[i]==expected[i]);
  assert(priorities==6);ffmpeg_workers(0);create();assert(places==6);
  ffmpeg_workers(1);for(int i=0;i<6;++i)create();
  unsigned legacy[]={2,4,3,5,2,4};
  for(unsigned i=0;i<6;++i)assert(hardware[i+6]==legacy[i]);
  fail=1;create();assert(places==12);ffmpeg_workers(0);
  puts("PASS product pthread wrapper: six unique HW, legacy order, reset, non-video threads and create failure");
}
