#include "project_includer.h"

/*----------------------------------------------*
 | 全局变量                                     |
 *----------------------------------------------*/
app_global_var_t g_app_vars = {};

uint8_t gNetStat;
uint32_t gCashboxOpentimes, gCloudC08MsgCount, gSdkInBucks;

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
void init_glbvar(void)
{
    gNetStat          = 0;
    gSdkInBucks       = 0;
    gCashboxOpentimes = 0;

    gCloudC08MsgCount                  = 1;
    g_app_vars.print_id_status         = 1;
    g_app_vars.isNoPaperCleard         = 1;
    g_app_vars.isCloudOrderStatChanged = 1;
}