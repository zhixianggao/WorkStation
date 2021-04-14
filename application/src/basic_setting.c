#include "project_includer.h"

audio_cfg_t *getTTSInfo(void)
{
    return get_audio_cfg_mmap();
}

void get_basic_info(void)
{
    LogDbg("init");

    audio_cfg_t *audiocfg = NULL;
    char fontVersion[16]={}, imei[32]={}, iccid[32]={};

    print_get_fontlib_version(fontVersion);
    util_read2buff(APP_SYS_IMEI, imei, sizeof(imei));
    util_read2buff(APP_SYS_ICCID, iccid, sizeof(iccid));

    audiocfg = getTTSInfo();

    LogDbg("Iccid:%s", iccid);
    LogDbg("Imei:%s", imei);
    LogDbg("APP0 Verison:%s", APP0_VERSION);
    LogDbg("APP1 Version:%s", APP1_VERSION);
    LogDbg("MiniAPP Version:%s", MINIAPP_VERSION);
    LogDbg("Font Version:%s", fontVersion);
    LogDbg("Boot Version:%s", VALUE_IN_SHAREMEM(boot_ver));
    LogDbg("Hardware Version:%s", VALUE_IN_SHAREMEM(hw_ver));
    LogDbg("Firmware Version:%s", VALUE_IN_SHAREMEM(fw_ver));
    LogDbg("Hardware Tag:%s", VALUE_IN_SHAREMEM(hw_tag));
    LogDbg("Model:%s", VALUE_IN_SHAREMEM(model));
    LogDbg("Serial Number:%s", VALUE_IN_SHAREMEM(sn));
    LogDbg("Wifi mac:%s", VALUE_IN_SHAREMEM(wifi_mac));
    LogDbg("Bt mac:%s", VALUE_IN_SHAREMEM(bt_mac));
    if (audiocfg) LogDbg("TTSVersion:%s, TTSLanguage:%s", audiocfg->tts.version, audiocfg->tts.lang);
}

void check_hw_info(void)
{
    while(strlen(sys_global_var()->sn) == 0 || strlen(sys_global_var()->model) == 0)
    {
        LogDbg("Model | Sn Checking");
        SysDelay(5000);
    }
}
