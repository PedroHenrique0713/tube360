#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void tube360_board_sim_init(void);
int tube360_board_sim_active(void);
int tube360_board_sim_driver(void);
int tube360_board_sim_family(void);
const char* tube360_board_sim_name(void);
int tube360_board_sim_sd(void);
float tube360_board_sim_aspect(void);
int tube360_board_sim_extent(int requested,int native,int* width,int* height);
int tube360_board_sim_display_ok(int native);
void tube360_board_sim_log(void);
#ifdef __cplusplus
}
#endif
