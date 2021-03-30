#include "project_includer.h"

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define _OK_    0
#define _FAIL_ (-1)

#define DOWNLOAD_TASK_NUM 6
#define DLD_TASK_FILE_MAGIC 0x4B535444
#define DOWNLOAD_TASK_FILENAME "/data/PUB/download_file.zip"
#define DOWNLOAD_TASK_BIN      "/data/PUB/download_task.bin"

#define DOWNLOAD_TTS_TASK            1 // TTS语音
#define DOWNLOAD_FONT_TASK           2 // 字库
#define DOWNLOAD_FW_TASK             3 // 固件
#define DOWNLOAD_APP_TASK            4 // 应用
#define DOWNLOAD_BOOT_TASK           5 // 引导
#define DOWNLOAD_BITMAP_TASK         6 // 位图
#define DOWNLOAD_PUBKEY_TASK         7 // 根证书
#define DOWNLOAD_WNET_TASK           8 // 移动网络模块

#define DOWNLOAD_TASK_ERASE_FAIL    (-3) // 擦写失败
#define DOWNLOAD_TASK_NO_FLASH      (-4) // Flash不足无法下载
#define DOWNLOAD_TASK_DLD_FAIL      (-5) // 下载失败
#define DOWNLOAD_TASK_CHECKSUM_FAIL (-6) // 校验失败
#define DOWNLOAD_TASK_NO_RAM        (-7) // 内存不足
#define DOWNLOAD_TASK_NO_SPACE      (-8) // Flash不足无法安装

#define DOWNLOAD_TASK_IDLE           0   // 索引空闲
#define DOWNLOAD_TASK_LOAD           1   // 索引加载
#define DOWNLOAD_TASK_LOADING        2   // 下载中
#define DOWNLOAD_TASK_ERASE_OK       3   // 擦写成功
#define DOWNLOAD_TASK_DLD_OK         5   // 下载成功
#define DOWNLOAD_TASK_CHECKSUM_OK    6   // 校验成功
#define DOWNLOAD_TASK_INSTALLING     7   // 擦写中

#define UPGRADE_AT_ONCE              0   // 立即更新
#define UPGRADE_AFTER_SECOND         1   // 五秒后更新
#define UPGRADE_AFTER_REBOOT         2   // 重启后更新
#define DOWNLOAD_BY_HTTP             0
#define DOWNLOAD_BY_HTTPS            1

#define DOWNLOAD_CHECK_TIMEGAP      (1000*3)
#define DLD_FAIL_RETRY_TIMEGAP      (60*1000) //下载失败后隔一分钟后重试

#define MySetBit(x, y)    (x |= (1 << abs(y)))
#define MyGetBit(x, y)    (x >> abs(y) & 1)
#define MyClearBit(x, y)  (x &= ~(1 << abs(y)))
#define MyRevertBit(x, y) (x ^= (1 << abs(y)))

#define MyMsync(item) msync(item, sizeof(*item), MS_SYNC);

/*----------------------------------------------*
 | 结构体定义                                   |
 *----------------------------------------------*/
typedef struct _DownloadTaskInfo_t {
	uint32_t magic;
	int taskNum;
	DownloadTask_Item_t taskFile[DOWNLOAD_TASK_NUM];
} DownloadTaskInfo_t;

/*----------------------------------------------*
 | 全局变量                                     |
 *----------------------------------------------*/
uint8_t gMqttTaskReplaced = 0;
uint32_t downloadTimestamp;
char g_netChecked = 0;

static DownloadTaskInfo_t *g_dld_task_info = NULL;
static pthread_mutex_t task_replace_lock = PTHREAD_MUTEX_INITIALIZER;

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
static int Download_Progress_Callback(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	static uint32_t timenow = 0;
	static uint8_t unique_task_reach_full = 0;

	if ( NetGetCurRoute() == NET_NONE ) {
		LogError("no route to download, so shut download action down!");
		return 1;
	}

	if (gMqttTaskReplaced == 1)
	{
		gMqttTaskReplaced = 0;
		LogDbg("download task source was replaced abort current curl performance!");
		return 1;
	}

	if ( dltotal!=dlnow ) {
		unique_task_reach_full = 0;
	}

	if ( unique_task_reach_full==0 && dltotal!=0 && dlnow!=0 && (dltotal==dlnow) ) {
		LogDbg("Downloading from rest total %-10lldbytes to %-10lldbytes===================================================>>progress[100%%]\n", dltotal, dlnow);
		unique_task_reach_full = 1;
		return 0;
	}

	if ( SysTick()-timenow > 7*1000 && dltotal ) { /* show progress every 7 seconds */
		timenow = SysTick();
		LogDbg("Downloading from rest total %-10lldbytes to %-10lldbytes===================================================>>progress[%.0f%%]\n",
				dltotal,dlnow,(((double)dlnow/(double)dltotal)*100));
	}
	return 0;
}

static int obtainCache(void)
{
	int iRet = 0, itemSize = 0;
	char buff[256] = {};

	iRet = SystemWithResult("cat /proc/meminfo | grep -e ^Cached", buff, sizeof(buff));
	if(iRet > 0) {
		itemSize = atoi(buff + strlen("Cached: ")) << 10;
	}
	return itemSize;
}

static int obtainMaxfile(const char *zipFilePath)
{
	int maxSize = 0;
	char buff[256] = {};

	snprintf(buff, sizeof(buff), "unzip -l %s | grep /", zipFilePath);
	FILE *pipe = popen(buff, "r");
	while(pipe && fgets(buff, sizeof(buff), pipe)){
		int s = atoi(buff);
		if(s > maxSize) maxSize = s;
	}
	if(pipe) pclose(pipe);

	LogDbg("maxSize=%d", maxSize);
	return maxSize;
}

static int _checkFlashFreeSize(int inputSize, const char *fsdiskPath, void *outputSize)
{
	int iRet = -1;
	struct statfs dataFsStat;
	unsigned long freeFlashSize;

	if((iRet = statfs(fsdiskPath, &dataFsStat))){
		LogError("get data partition accur error %s", strerror(errno));
		return -1;
	}

	freeFlashSize = dataFsStat.f_bfree * dataFsStat.f_bsize;
	if(inputSize > freeFlashSize){
		if(outputSize) {
			*(unsigned long *)outputSize = freeFlashSize;
		}
		return -2;
	}

	return 0;
}

static int _checkDataFreeSize(const char *zipFilePath)
{
	int iRet = -1;
	struct sysinfo dataRomStat;
	unsigned long freeRamSize, cachedSize = 0, dataFreeSize = 0;
	int maxItemSize = 0;

	system("echo 3 > /proc/sys/vm/drop_caches");

	//obtain Cache
	cachedSize = obtainCache();
	if(!cachedSize){
		LogError("can not get cachedSize=%lu", cachedSize);
		return -1;
	}

	if((iRet = sysinfo(&dataRomStat))){
		LogError("get sysinfo accur error %s", strerror(errno));
		return -2;
	}

	//total free ram
	freeRamSize = dataRomStat.freeram + dataRomStat.bufferram + cachedSize;
	LogDbg("printer worked %d seconds since boot, totalfreeRamSize=%lu, freeram=%lu, bufferram=%lu, cachedSize=%lu",
		dataRomStat.uptime, freeRamSize, dataRomStat.freeram, dataRomStat.bufferram, cachedSize);

	//compare data fsdisk free size with cachedSize
	if((_checkFlashFreeSize(freeRamSize, "/tmp/", &dataFreeSize)) == -2) {
		freeRamSize = dataFreeSize;
	}

	//obtain max file size
	maxItemSize = obtainMaxfile(zipFilePath);

	if(maxItemSize > freeRamSize){
		LogDbg("single file=%d in zip larger than freeRamSize=%lu!", maxItemSize, freeRamSize);
		return -3;
	}

	return 0;
}

static int Download_Task_Mmap(void)
{
	int fd = open(DOWNLOAD_TASK_BIN, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
	if (fd < 0) {
		LogFatal("Open(%s) fail(%d),%s", DOWNLOAD_TASK_BIN, errno, strerror(errno));
		return _FAIL_;
	}
	ftruncate(fd, sizeof(DownloadTaskInfo_t));
	g_dld_task_info = (DownloadTaskInfo_t *)mmap(NULL, sizeof(DownloadTaskInfo_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (!g_dld_task_info) {
		LogFatal("mmap fail(%d),%s", errno, strerror(errno));
		return _FAIL_;
	}
	if (g_dld_task_info->magic != DLD_TASK_FILE_MAGIC) {
		memset(g_dld_task_info, 0, sizeof(DownloadTaskInfo_t));
		g_dld_task_info->magic = DLD_TASK_FILE_MAGIC;
		MyMsync(g_dld_task_info);
	}
	return _OK_;
}

static int WhetherNeedDownload(const DownloadTask_Item_t *task)
{
	if (!task || task->type != DOWNLOAD_TTS_TASK) return 1;

	char *pSpeaker = strrchr(task->url, '/');
	if (pSpeaker)
	{
		pSpeaker += 1;
		char *pZip = strstr(pSpeaker, ".zip");
		if (pZip) {
			audio_cfg_t *audiocfg = NULL;
			audiocfg = getTTSInfo();
			if (audiocfg) {
				char speaker[256] = {};
				snprintf(speaker, sizeof(speaker), "%.*s", pZip-pSpeaker, pSpeaker);
				if (!strcmp(speaker, audiocfg->tts.voice_name) && !strcmp(task->ver, audiocfg->tts.version)) {
					LogDbg("current tts speaker:%s, tts version:%s, no need do update", audiocfg->tts.voice_name, audiocfg->tts.version);
					return 0;
				} else {
					return 1;
				}
			} else {
				LogDbg("can't get current TTS info!!!"); return 0;
			}
		} else {
			LogError("tts resource filename has no .zip, check please"); return 0;
		}
	} else {
		LogError("tts resource url has no '/' check please"); return 0;
	}
}

static int Download_Task_Add_Inside(const DownloadTask_Item_t *addTask)
{
	int i, idle_idx = -1;

	if (!WhetherNeedDownload(addTask)) {
		LogDbg("no need do upgrade"); return -1;
	}

	DownloadTask_Item_t *item;
	for (i = 0; i < DOWNLOAD_TASK_NUM; i++)
	{
		item = g_dld_task_info->taskFile+i;
		if (item->status == DOWNLOAD_TASK_IDLE) {
			if (idle_idx < 0)
				idle_idx = i;
			continue;
		}
		if (!strcmp(item->md5, addTask->md5) && !strcmp(item->url, addTask->url)) { /* same task */
			LogInfo("already has the same task so ignore!");
			return 0;
		}

		pthread_mutex_lock(&task_replace_lock);
		if (item->type == addTask->type && item->status != DOWNLOAD_TASK_DLD_OK) { /* same type new task */
			LogDbg("new task[%s] replace old task[%s]", addTask->url, item->url);
			memset(item, 0, sizeof(*item));
			if (access(DOWNLOAD_TASK_FILENAME, F_OK)==0) {
				unlink(DOWNLOAD_TASK_FILENAME);
			}
			if (idle_idx < 0) {
				idle_idx = i;
			}
			gMqttTaskReplaced = 1;
			pthread_mutex_unlock(&task_replace_lock);
			break;
		}
		pthread_mutex_unlock(&task_replace_lock);
	}

	if (idle_idx < 0) {
		LogError("Download Task full!");
		return -1;
	}

	memcpy(g_dld_task_info->taskFile+idle_idx, addTask, sizeof(*addTask));
	item = g_dld_task_info->taskFile+idle_idx;
	item->status = DOWNLOAD_TASK_LOAD;
	MyMsync(g_dld_task_info);

	return 0;
}

/*
 * WNET FOTA process breaks into two steps:
 * step1: bring data from flash to memory
 * step2: real write process
 * so when we got WNET FOTA C04 command, we should do version comparison first.
 */
static int WnetVersionCheck(const char *ver)
{
	char fota_version[16] = {};
	app_setting_get(APP_SETTING_SECTION_WNET, "fota_version", fota_version, sizeof(fota_version));
	return strcmp(ver, fota_version);
}

void Download_Task_Add_Outside(cJSON *pRoot)
{
	int  netType = cJSON_GetValueInt(   pRoot, "netType",  999);
	int  upgrade = cJSON_GetValueInt(   pRoot, "upgrade",   -1);
	int  size    = cJSON_GetValueInt(   pRoot, "size",      -1);
	int  type    = cJSON_GetValueInt(   pRoot, "type",      -1);
	char *md5    = cJSON_GetValueString(pRoot, "md5",     NULL);
	char *ver    = cJSON_GetValueString(pRoot, "ver",     NULL);
	char *url    = cJSON_GetValueString(pRoot, "url",     NULL);

	if ( size<0 ||                                                   /* size        */
	     upgrade<UPGRADE_AT_ONCE || upgrade>UPGRADE_AFTER_REBOOT ||  /* upgrade     */
	     netType<0 || (netType>2 && netType!=999)                ||  /* netType     */
	     type<DOWNLOAD_TTS_TASK || type>DOWNLOAD_WNET_TASK       ||  /* type        */
	     !md5 || !ver || !url) {                                     /* md5 ver url */
		LogError("[ wrong size | upgrade | netType | type | md5 | ver | url ]");
		return;
	}

	if (!WnetVersionCheck(ver)) {
		LogInfo("wnet module is going to update, no need to begin a new round of processes!");
		return;
	}

	DownloadTask_Item_t addTask         = {};
	                    addTask.size    = size;
	                    addTask.type    = type;
	                    addTask.upgrade = upgrade;
	                    addTask.netType = netType;
	snprintf(addTask.md5, sizeof(addTask.md5), "%s", md5);
	snprintf(addTask.ver, sizeof(addTask.ver), "%s", ver);
	snprintf(addTask.url, sizeof(addTask.url), "%s", url);

	Download_Task_Add_Inside(&addTask);
}

static int Download_Space_Check(int *taskIndex)
{
	DownloadTask_Item_t *item;
	for (int i = 0; i < DOWNLOAD_TASK_NUM; i++)
	{
		item = g_dld_task_info->taskFile + i;
		if (item->status == DOWNLOAD_TASK_DLD_OK)
		{
			if (FileSize(DOWNLOAD_TASK_FILENAME) > 0) {
				*taskIndex = i;
				return _OK_;
			} else {
				memset(item, 0, sizeof(*item));
				MyMsync(g_dld_task_info);
				return _FAIL_;
			}
		}
	}
	return _FAIL_;
}

static char *ShowDownloadType(const char type)
{
	switch (type) {
		case DOWNLOAD_TTS_TASK:      return "TTS";
		case DOWNLOAD_FONT_TASK:     return "FONT";
		case DOWNLOAD_FW_TASK:       return "FW";
		case DOWNLOAD_APP_TASK:      return "APP";
		case DOWNLOAD_BOOT_TASK:     return "BOOT";
		case DOWNLOAD_BITMAP_TASK:   return "BITMAP";
		case DOWNLOAD_PUBKEY_TASK:   return "PUBKEY";
		case DOWNLOAD_WNET_TASK:     return "WNET";
		default:                     return "";
	}
}

static void Download_Task_Clean(void)
{
#define CleanDownloadTask(task) \
				({ if(FileSize(DOWNLOAD_TASK_FILENAME) > 0) unlink(DOWNLOAD_TASK_FILENAME); \
						LogDbg("is about to clean up task: type=%d, version=%s, size=%d, url=%s, md5sum=%d", \
								task->type, task->ver, task->size, task->url, task->md5); \
						memset(task, 0, sizeof(*task)); \
						MyMsync(g_dld_task_info); })
	int mainVer, subVer, lastVer;
	int devMainVer, devSubVer, devLastVer;
	DownloadTask_Item_t *item;

	for (int i = 0; i < DOWNLOAD_TASK_NUM; i++)
	{
		item = g_dld_task_info->taskFile + i;
		if (item->status != DOWNLOAD_TASK_IDLE)
		{
			LogDbg("i=%d, status=%d, type=%d, version:%s", i, item->status, item->type, item->ver);
			sscanf(item->ver, "%d.%d.%d", &mainVer, &subVer, &lastVer);
			if (item->type == DOWNLOAD_FW_TASK || item->type == DOWNLOAD_APP_TASK)
			{
				if (item->type == DOWNLOAD_FW_TASK) {
					sscanf(VALUE_IN_SHAREMEM(fw_ver), "%d.%d.%d", &devMainVer, &devSubVer, &devLastVer);
				} else {
					sscanf(APP0_VERSION, "%d.%d.%d", &devMainVer, &devSubVer, &devLastVer);
				}
				devMainVer = (devMainVer << 20) + (devSubVer << 10) + devLastVer;
				mainVer = (mainVer << 20) + (subVer << 10) + lastVer;
				if (devMainVer >= mainVer) {
					CleanDownloadTask(item);
				}
				LogDbg("mainVer=%d, subVer=%d, lastVer=%d, devMainVer=%d, devSubVer=%d, devLastVer=%d.",
					   mainVer, subVer, lastVer, devMainVer, devSubVer, devLastVer);
			} else if (item->type < DOWNLOAD_TTS_TASK || item->type > DOWNLOAD_WNET_TASK) {
				LogError("unrecognized OTA type[%d], index[%d]!", item->type, i);
				CleanDownloadTask(item);
			} else {
				struct stat fileinfo = {};
				stat(DOWNLOAD_TASK_FILENAME, &fileinfo);
				LogInfo("index[%d]|type[%d]|url[%s]|percentage[%.0f%%]", i, ShowDownloadType(item->type), item->url, (((double)fileinfo.st_size/(double)item->size)*100));
			}
		}
	}
	return;
#undef CleanDownloadTask
}

static void Download_Startime_Update(void)
{
	downloadTimestamp = SysTick();
}

static int Download_Task_Get(int *taskIndex)
{
	int i;
	DownloadTask_Item_t *item;

	if ((SysTick() - downloadTimestamp) < DOWNLOAD_CHECK_TIMEGAP) /* no need retry too fast due to network status*/
		return _FAIL_;
	Download_Startime_Update();

	for (i = 0; i < DOWNLOAD_TASK_NUM; i++) {
		item = g_dld_task_info->taskFile + i;
		if (item->status == DOWNLOAD_TASK_DLD_OK) /* download ok but not erase */
			return _FAIL_;
	}

	for (i = 0; i < DOWNLOAD_TASK_NUM; i++) {
		item = g_dld_task_info->taskFile + i;
		if (item->status == DOWNLOAD_TASK_LOADING) { /* task last statu is downloading */
			*taskIndex = i;
			struct stat fileinfo = {};
			stat(DOWNLOAD_TASK_FILENAME, &fileinfo);
			LogInfo("index[%d]|type[%d]|url[%s]| total percentage[%.0f%%]", i,ShowDownloadType(item->type),item->url,(((double)fileinfo.st_size/(double)item->size)*100));
			return _OK_;
		}
	}

	for (i = 0; i < DOWNLOAD_TASK_NUM; i++)
	{
		item = g_dld_task_info->taskFile + i;
		if (item->status == DOWNLOAD_TASK_LOAD) { /* task last statu is load but not download */
			*taskIndex = i;
			if (item->netType == NET_WNET) { /* forbid wnet do download */
				int ret = NetGetCurRoute();
				if (ret==NET_NONE || ret==NET_WNET) {
					if (MyGetBit(g_netChecked,i)==0) {
						AudioPlayFixed(NEED_WIFI_UPDTE, 1, 0);
						MySetBit(g_netChecked, i);
					}
					return _FAIL_;
				}
			}

			if (!_checkFlashFreeSize(item->size, "/data/", NULL)) return _OK_;
			item->status = DOWNLOAD_TASK_NO_FLASH;
			mqtt_publishTopic(UP_UPGRADE_STATUS, item);
			memset(item, 0, sizeof(*item));
			MyMsync(g_dld_task_info);
			return _FAIL_;
		}
	}

	return _FAIL_;
}

static int Download_File_Setup(DownloadTask_Item_t *item)
{
	LogDbg("download type:%s", ShowDownloadType(item->type));
	int iRet = -1;

	if (item->type<DOWNLOAD_TTS_TASK || item->type>DOWNLOAD_WNET_TASK) {
		LogDbg("wrong task type[%d]", item->type);
		return iRet;
	} else {
		iRet = FileInstaller(DOWNLOAD_TASK_FILENAME);
		LogDbg("FileInstaller type[%d] finished", item->type);
	}

	if (iRet == 0)
	{
		if (item->type == DOWNLOAD_TTS_TASK) {
			AudioResUpdate();
		}

		if (item->type == DOWNLOAD_WNET_TASK) {
			//mode is useful, set default mode to 1 for the time being
			NetWnetStartFota(1);
		}

		//Mark not resource tasks
		int notResourceTask = (item->type != DOWNLOAD_BITMAP_TASK &&
		                       item->type != DOWNLOAD_TTS_TASK    &&
		                       item->type != DOWNLOAD_FONT_TASK   &&
		                       item->type != DOWNLOAD_WNET_TASK)?1:0;

		if (item->type == DOWNLOAD_BITMAP_TASK || item->type == DOWNLOAD_FONT_TASK) {
			AudioPlayFixed(RSC_UPDATE_SUCC, 1, 0);
		}

		/* WNET task: FileInstaller return 0 just represent the FOTA binary file has unziped to certain place,
		 * the real finishment of WNET module FOTA may take extra 10 minutes,
		 * so we need to record the version of victoria module, in case of superfluous download process.
		 */
		if (item->type == DOWNLOAD_WNET_TASK) {
			app_setting_set(APP_SETTING_SECTION_WNET, "foat_version", item->ver);
		}

		/* Report task statu to server */
		item->status = DOWNLOAD_TASK_ERASE_OK;
		mqtt_publishTopic(UP_UPGRADE_STATUS, item);
		memset(item, 0, sizeof(*item));
		MyMsync(g_dld_task_info);
		sleep(3);
		unlink(DOWNLOAD_TASK_FILENAME);

		if (notResourceTask) {
			AudioPlayFixed(UPDATE_SUCC_REBOOT, 1, 0); SysDelay(6000); SysReboot();
		}

		return 0;
	} else {
		unlink(DOWNLOAD_TASK_FILENAME);
		item->type = DOWNLOAD_TASK_ERASE_FAIL;
		mqtt_publishTopic(UP_UPGRADE_STATUS, item);
		memset(item, 0, sizeof(*item));
		MyMsync(g_dld_task_info);
		return iRet;
	}
}

static void *upgrade_after_second(void *args)
{
	pthread_detach(pthread_self());

	DownloadTask_Item_t *item = (DownloadTask_Item_t *)args;

	if (item->isTaskInstallNoticed == 0    &&
	    item->type != DOWNLOAD_TTS_TASK    &&
	    item->type != DOWNLOAD_FONT_TASK   &&
	    item->type != DOWNLOAD_BITMAP_TASK &&
	    item->type != DOWNLOAD_WNET_TASK) {
		AudioPlayFixed(UPDATE_AFTER_SECONDS, 1, 0);
		item->isTaskInstallNoticed = 1;
	}

	uint32_t timenow = SysTick();
	while (1) {
		if (SysTick() - timenow > 10*1000)
			break;
		SysDelay(50);
	}

	if (Download_File_Setup(item) < 0) {
		item->status = DOWNLOAD_TASK_DLD_OK;
		item->upgrade = UPGRADE_AT_ONCE;
		MyMsync(g_dld_task_info);
	}
	return NULL;
}

static void Download_Task_Loop(void)
{
	int taskIndex = -1;
	static uint32_t needReboot = 0;
	DownloadTask_Item_t *item;

	if (Download_Space_Check(&taskIndex) == _OK_)
	{
		if (taskIndex < 0) return;
		/* check is request size has been reserved */
		item = g_dld_task_info->taskFile + taskIndex;

		/* switch suitble method to install */
		if (item->upgrade == UPGRADE_AT_ONCE)
		{
			if (item->isTaskInstallNoticed == 0    &&
			    item->type != DOWNLOAD_TTS_TASK    &&
			    item->type != DOWNLOAD_FONT_TASK   &&
			    item->type != DOWNLOAD_BITMAP_TASK &&
			    item->type != DOWNLOAD_WNET_TASK) {
			    AudioPlayFixed(NEW_VERSION_NOW_UPDATE, 1, 0);
			    item->isTaskInstallNoticed = 1;
			}
			if (needReboot == 0) item->isUpgradeModeChanged = 0;
			if (item->isUpgradeModeChanged == 0) {
				if (Download_File_Setup(item) < 0) {
					item->status = DOWNLOAD_TASK_DLD_OK;
					MyMsync(g_dld_task_info);
				}
			}
		} else if (item->upgrade == UPGRADE_AFTER_SECOND) {
			pthread_t tid;
			if(pthread_create(&tid, NULL, upgrade_after_second, item)<0){
				LogError("pthread_create upgrade_after_second failed");
			}
		} else if (item->upgrade == UPGRADE_AFTER_REBOOT) {
			if (!item->isTaskInstallNoticed) {
				AudioPlayFixed(UPDATE_AFTER_REBOOT, 1, 0);
				item->isTaskInstallNoticed = 1;
				item->isUpgradeModeChanged = 1;
				needReboot = 1;
			}
			item->upgrade = UPGRADE_AT_ONCE;
			item->status = DOWNLOAD_TASK_DLD_OK;
			MyMsync(g_dld_task_info);
		} else {
			/* this should not happen */
			LogError("undefined upgrade mode[%d]", item->upgrade);
		}
	}
	return;
}

static void Download_Task_Init(void)
{
	Download_Startime_Update();
	if (Download_Task_Mmap() == _FAIL_) {
		LogError("download task file:%s mmap fail", DOWNLOAD_TASK_BIN);
	} else {
		Download_Task_Clean();
	}
}

static size_t MyFwrite(char *buff, size_t size, size_t nmemb, FILE *fp)
{
	return fwrite(buff, size, nmemb, fp);
}

static int File_Downloader(const char *url,uint32_t file_size,const char httpType)
{
	int iRet = -1;
	uint8_t currrentNet = 0;
	uint32_t currentSize = 0;
	struct curl_slist *headers = NULL;

	CURL *curl = curl_easy_init();
	FILE *fp = fopen(DOWNLOAD_TASK_FILENAME, "a");
	if ( !fp ) {
		LogError("fopen %s file fail", DOWNLOAD_TASK_FILENAME);
		goto EXIT;
	} else {
		currentSize = ftell(fp);
		LogInfo("currentSize[%d]--->taskSize[%d]", currentSize, file_size);
	}

	/* set curl option */
	curl_easy_setopt(curl, CURLOPT_URL, url);
	headers = curl_slist_append(headers, "Content-Type: application/binary");
	headers = curl_slist_append(headers, "Expect:");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	if (httpType == DOWNLOAD_BY_HTTPS) {
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	currrentNet = NetGetCurRoute();
	if ( currrentNet==NET_WIFI || currrentNet==NET_LAN ) {
		/* complete connection within 1000*30 milliseconds */
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000*30L);
	} else if ( currrentNet==NET_WNET ) {
		/* complete connection within 1000*50 milliseconds */
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000*50L);
	} else {
		/* this will not happen */
		LogDbg("current network[%d] check please", currrentNet);iRet = -2;goto EXIT;
	}

	if ( currrentNet==NET_WIFI ||
	     currrentNet==NET_WNET ||
	     currrentNet==NET_LAN ) {
		LogInfo("currrent using %s net", currrentNet==NET_WIFI?"WIFI":(currrentNet==NET_WNET?"WNET":"LAN"));

		/* abort if slower than 20 bytes/sec during 120 seconds */
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,  120L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 20L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT,         0L);

		char curl_option[32] = {};
		snprintf(curl_option, sizeof(curl_option), "%d-", currentSize);
		curl_easy_setopt(curl, CURLOPT_RANGE,             curl_option);
		curl_easy_setopt(curl, CURLOPT_BUFFERSIZE,        1024*10);
		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,  (curl_off_t)(file_size));
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,     MyFwrite);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA,         fp);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,  Download_Progress_Callback);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS,        0L);
		curl_easy_setopt(curl, CURLOPT_VERBOSE,           1L);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL,          1L);
		iRet = curl_easy_perform(curl);

		if ( iRet != CURLE_OK ) {
			LogError("curl error[%d] error string[%s]", iRet, curl_easy_strerror(iRet));goto EXIT;
		} else {
			LogInfo("[CURL EASY PERFORM SUCCESS]");
		}
	}

EXIT:
	if (fp) fclose(fp);
	if (curl) curl_easy_cleanup(curl);
	if (headers) curl_slist_free_all(headers);
	return iRet;
}

static int Download_Enterance(int taskIndex)
{
#define OTA_RETRY_TIMES 3
	int iRet = -1;
	static uint8_t failTimes = 0;
	static char lastTaskMd5[64] = {};
	DownloadTask_Item_t *item, temp = {};

	item = g_dld_task_info->taskFile+taskIndex;

	/* record task parameters in case of global task parameters changed in other place */
	snprintf(temp.url, sizeof(temp.url), "%s", item->url);
	snprintf(temp.md5, sizeof(temp.md5), "%s", item->md5);
	temp.isTaskDldNoticed = item->isTaskDldNoticed;
	temp.size             = item->size;
	temp.type             = item->type;

	/* use parameter md5sum to determine whether clear failTimes */
	if (strncmp(lastTaskMd5, temp.md5, sizeof(temp.md5))) {
		snprintf(lastTaskMd5, sizeof(lastTaskMd5), "%s", item->md5);
		failTimes = 0;
	}

	/* need net to proc download */
	if (NetGetCurRoute()==NET_NONE) {
		LogDbg("no access to network!");
		return iRet;
	}

	item->status = DOWNLOAD_TASK_LOADING;
	if (temp.isTaskDldNoticed == 0) {
		if (temp.type != DOWNLOAD_TTS_TASK &&
		    temp.type != DOWNLOAD_FONT_TASK &&
		    temp.type != DOWNLOAD_WNET_TASK &&
		    temp.type != DOWNLOAD_BITMAP_TASK) {
		    AudioPlayFixed(OTAFILE_BEGINE_DLD, 1, 0);
		} else {
			AudioPlayFixed(RSCFILE_BEGINE_DLD, 1, 0);
		}
		item->isTaskDldNoticed = 1;
		MyMsync(g_dld_task_info);
	}

	/* same type mqtt task replacement happend before curl_easy_perform, the gMqttTaskReplaced should be clear up here */
	if (gMqttTaskReplaced == 1) gMqttTaskReplaced = 0;
	iRet = File_Downloader(temp.url, temp.size, DOWNLOAD_BY_HTTPS);
	LogDbg("File_Downloader url[%s] size[%d] ret[%d]", temp.url, temp.size, iRet);

	if (iRet == CURLE_OK)
	{
		pthread_mutex_lock(&task_replace_lock);
		struct stat st = {};
		if ( stat(DOWNLOAD_TASK_FILENAME, &st) == 0 && st.st_size == temp.size )
		{
			char md5txt[36] = {};
			failTimes = 0;
			if (util_md5file(DOWNLOAD_TASK_FILENAME, md5txt) < 0 || strcmp(md5txt, temp.md5)) {
				/* md5sum check fail */
				unlink(DOWNLOAD_TASK_FILENAME);
				item->status = DOWNLOAD_TASK_CHECKSUM_FAIL;
				mqtt_publishTopic(UP_UPGRADE_STATUS, item);
				memset(item, 0, sizeof(*item));
				MyMsync(g_dld_task_info);
			} else {
				item->status = DOWNLOAD_TASK_CHECKSUM_OK;
				mqtt_publishTopic(UP_UPGRADE_STATUS, item);
			}

			/* download ok */
			item->status = DOWNLOAD_TASK_DLD_OK;
			mqtt_publishTopic(UP_UPGRADE_STATUS, item);
			MyMsync(g_dld_task_info);
		} else {
			/* curl perform successfully, however current url is invalid, so we need clean this task */
			LogError("CURL OK BUT URL MAKE NO SENSE!!!");
			unlink(DOWNLOAD_TASK_FILENAME);
			item->status = DOWNLOAD_TASK_CHECKSUM_FAIL;
			mqtt_publishTopic(UP_UPGRADE_STATUS, item);
			memset(item, 0, sizeof(*item));
			MyMsync(g_dld_task_info);
		}
		pthread_mutex_unlock(&task_replace_lock);
	} else {
		failTimes++;
		/* CURLE_ABORTED_BY_CALLBACK used under two circumstances:
		 * 1.download task was replaced by a same type but new task
		 * 2.download procedure burst with network outage
		 * we set OTA_RETRY_TIMES for the 1st circumstance, and set OTA_RETRY_TIMES plus one for the 2nd circumstance.
		 */
		if ((failTimes == OTA_RETRY_TIMES && iRet != CURLE_ABORTED_BY_CALLBACK) ||
		     failTimes > OTA_RETRY_TIMES) {
			unlink(DOWNLOAD_TASK_FILENAME);
			item->status = DOWNLOAD_TASK_DLD_FAIL;
			mqtt_publishTopic(UP_UPGRADE_STATUS, item);
			memset(item, 0, sizeof(*item));
			MyMsync(g_dld_task_info);
			failTimes = 0;
			if (temp.isTaskDldNoticed == 0) AudioPlayFixed(UPDATE_FAILED, 1, 0);
		}
	}

	return iRet;
#undef OTA_RETRY_TIMES
}

static void *Download_thread(void *arg)
{
	int    chk_dld_ret         = -1;  //chk_dld接口返回值判断
	static char timenowDefined = 0;   //WIFI下载失败时记录当前时刻是否已经记录:0 未记录
	static char isDownloaded   = 0;   //0 未下载过 1 下载过
	static uint32_t timenow    = 0;   //下载失败时间戳
	Download_Task_Init();
	while (1)
	{
		int taskIndex = -1;
		if (NetGetCurRoute()==NET_NONE) {
			SysDelay(100); continue;
		}
		if (Download_Task_Get(&taskIndex) == _OK_) {
			if (taskIndex < 0) {
				SysDelay(50); continue;
			}
			if (SysTick()-timenow>DLD_FAIL_RETRY_TIMEGAP || isDownloaded==0) {
				chk_dld_ret    = Download_Enterance(taskIndex);
				timenowDefined = 0;
				isDownloaded   = 1;
			}
			if ((chk_dld_ret != CURLE_OK && chk_dld_ret != CURLE_ABORTED_BY_CALLBACK) && timenowDefined == 0) {
				timenow        = SysTick();
				timenowDefined = 1;
			}
			Download_Startime_Update();
		}
		Download_Task_Loop();
		SysDelay(100);
	}
	LogDbg("OTATask_thread exit now");
	pthread_exit(0);
}

void download_thread_init(void)
{
	int iRet = -1;
	pthread_t tid;
	if ((iRet = pthread_create(&tid, NULL, Download_thread, NULL)))
		LogError("error return %d", iRet);
	pthread_detach(tid);
}