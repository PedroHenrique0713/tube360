#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { TUBE360_SIM_DISPLAY, TUBE360_SIM_OUTPUT, TUBE360_SIM_FONTS, TUBE360_SIM_MAPPED };
void tube360_board_sim_init(void);
int tube360_board_sim_active(void);
int tube360_board_sim_family(void);
// -1: não sobrepor; 0/1: caminho pedido explicitamente pela bancada.
int tube360_board_sim_driver(void);
int tube360_board_sim_display_ok(int native);
const char* tube360_board_sim_name(void);
// 0: desktop normal; 1: contrato simulado; -1: viewport entrelaçado incompatível com safe.
int tube360_board_sim_extent(int requested, int native, int* width, int* height);
int tube360_board_sim_mapped(int native);
uint32_t tube360_board_sim_budget(void);
unsigned tube360_board_sim_frame_delay(void);
int tube360_board_sim_step(unsigned stage);
void tube360_board_sim_log(void);
#ifdef __cplusplus
}
#endif
