// Marcadores de abertura, sem SDL/GPU/rede e sem alocar por quadro.
#pragma once
#ifdef __XSDK__
#include <xsdk/terminate.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
void boot_open(const char* directory,const char* role);
void boot_event(const char* phase,const char* step,const char* format,...) __attribute__((format(printf,3,4)));
void boot_close(void);
const char* boot_log_path(void);
void tube360_boot_early(void);
void boot_environment(void);
void boot_network(void);
void boot_session_begin(const char* directory,const char* metadata);
void boot_session_clean(void);
void boot_session_failed(void);
void boot_session_heartbeat(void);
void boot_session_terminated(void);
void boot_session_resume(void);
void boot_session_version(unsigned build,const char* version);
#ifdef __cplusplus
}
#endif
