#include "curl_setup.h"
#include "curlx/strparse.h"
#include "urldata.h"
#include "progress.h"
#include <stdint.h>
#include <stdio.h>
volatile int64_t results[10];
static void ck(int i,int64_t got,int64_t expected){results[0]++;if(got!=expected){results[1]|=INT64_C(1)<<i;results[2]=got;results[3]=expected;}}
int main(void){
 const char* text[]={"2147483647","2147483648","2357786511","3723217268","4294967295","4294967296","6442450945"};
 int64_t sizes[]={2147483647LL,2147483648LL,2357786511LL,3723217268LL,4294967295LL,4294967296LL,6442450945LL};
 struct Curl_easy data={0};
 for(int i=0;i<7;i++){
  const char* p=text[i];curl_off_t n=-1;
  ck(0,curlx_str_numblanks(&p,&n),0);ck(1,n,sizes[i]);ck(2,*p,0);
  p=text[i];n=-1;ck(3,curlx_str_number(&p,&n,CURL_OFF_T_MAX),0);ck(4,n,sizes[i]);
  Curl_pgrsSetDownloadSize(&data,sizes[i]);ck(5,data.progress.dl_size_known,1);ck(6,data.progress.dl.total_size,sizes[i]);
 }
 const char* p="9223372036854775808";curl_off_t n;ck(7,curlx_str_numblanks(&p,&n),STRE_OVERFLOW);
 p="-1";ck(8,curlx_str_numblanks(&p,&n),STRE_NO_NUM);
 Curl_pgrsSetDownloadSize(&data,-1);ck(9,data.progress.dl_size_known,0);ck(10,data.progress.dl.total_size,0);
 results[9]=0x463436;for(;;)__asm__ volatile("nop");
}
