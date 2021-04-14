#ifndef __DOWNLOAD_MNG_H__
#define __DOWNLOAD_MNG_H__

/*----------------------------------------------*
 | 结构体定义                                   |
 *----------------------------------------------*/
typedef struct
{
    unsigned int size;         // 文件大小:byte
    char url[256];             // 下载文件服务器地址
    char md5[33];              // 文件md5值
    char ver[33];              // 目标文件版本号
    char type;                 // 文件类型:1-TTSvoice 2-Font 3-Firmware 4-APP 5-BOOT
    char status;               // 任务状态:1-成功 -1-失败
    char upgrade;              // 升级策略:0-立即更新 1-5s后更新 2-下次开机更新
    char netType;              // 升级允许网络类型:1-表示所有网络 2-表示非移动数据
    char isTaskDldNoticed;     // OTA任务开始下载语音提示音播放状况:0-未播放 1-播放过
    char isTaskInstallNoticed; // OTA任务开始安装语音提示音播放状况:0-未播放 1-播放过
    char isUpgradeModeChanged; // 仅用于OTA任务需要重启后更新需求进行置位:upgrade need change frome 2 to 0
    char cReserved[179];       // 预留待扩充:总大小保持512Bytes
} __attribute__((packed)) DownloadTask_Item_t;


void Download_Task_Add_Outside(cJSON *pRoot);
void download_thread_init(void);

#endif /* __DOWNLOAD_MNG_H__ */