#ifndef __BASIC_SETTING_H__
#define __BASIC_SETTING_H__

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define VALUE_IN_SHAREMEM(KEY) sys_global_var()->KEY
#define VALUELEN_IN_SHAREMEM(KEY) strlen(VALUE_IN_SHAREMEM(KEY))

#define APP_SYS_IMEI    "/data/SYS/imei"
#define APP_SYS_ICCID   "/data/SYS/iccid"
#define APP_SYS_LBS     "/data/SYS/lbs"
#define APP_SYS_WNET    "/data/SYS/wnet_ver"

#define EXECUTE_SUCCESS 0x00
#define EXECUTE_FAIL 0x01

/*----------------------------------------------*
 | 可供外部使用函数                              |
 *----------------------------------------------*/
void get_basic_info(void);
void check_hw_info(void);
char *get_bt_devname(char *buf,int len);
audio_cfg_t *getTTSInfo(void);

#endif /* __BASIC_SETTING_H__ */