#include "project_includer.h"
#include "partner.h"


/*----------------------------------------------*
 | 全局变量                                     |
 *----------------------------------------------*/
static pthread_mutex_t sdk_in_bucks_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cloud_order_lock = PTHREAD_MUTEX_INITIALIZER;


int gMqttOrderDataSignal      = IDLE_STATUS_ORDER;  //此值用于下载控制
int gMqttPrintOrderDataSignal = IDLE_STATUS_ORDER;  //此值用于打印控制

TRANS_TERM_PARAM glb_termParm = {};

/*----------------------------------------------*
 | 引用变量                                     |
 *----------------------------------------------*/
extern HTTPS_ORDER_LIST_TASK glb_HttpsOrderListTask;        //实时订单列表任务
extern HTTPS_ORDER_LIST_TASK glb_HttpsHistoryOrderListTask; //历史订单列表任务
extern HTTPS_ORDER_TASK glb_HttpsOrderTask;                 //实时打印任务
extern HTTPS_ORDER_TASK glb_HttpsHistoryOrderTask;          //历史打印任务

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
int mqtt_saveOrderContent(const uint8_t *OrderId, const uint8_t *OrderContent, int orderLen, int orderCnt)
{
	int iRet = -1;
	for (int i = 0; i < ORDER_NUM; i++)
	{
		if (!g_app_vars.isReprintOldOrderContent)
		{
			if (glb_HttpsOrderTask.orderContent[i].status == ORDER_IDLE)
			{
				snprintf((char *)glb_HttpsOrderTask.orderContent[i].OrderNo, sizeof(glb_HttpsOrderTask.orderContent[i].OrderNo), "%s", OrderId);
				glb_HttpsOrderTask.orderContent[i].printData = (unsigned char *)malloc(orderLen);
				memcpy(glb_HttpsOrderTask.orderContent[i].printData, OrderContent, orderLen);
				glb_HttpsOrderTask.orderContent[i].orderLen = orderLen;
				glb_HttpsOrderTask.orderContent[i].orderCnt = orderCnt;
				glb_HttpsOrderTask.orderNum = i;
				glb_HttpsOrderTask.orderContent[i].status = ORDER_READY;
				gMqttOrderDataSignal = IDLE_STATUS_ORDER;
				iRet = 0;
				LogDbg("orderId[%s]", glb_HttpsOrderTask.orderContent[i].OrderNo);
				break;
			}
		} else {
			snprintf((char *)glb_HttpsHistoryOrderTask.orderContent[i].OrderNo, sizeof(glb_HttpsHistoryOrderTask.orderContent[i].OrderNo), "%s", OrderId);
			glb_HttpsHistoryOrderTask.orderContent[i].printData = (unsigned char *)malloc(orderLen);
			memcpy(glb_HttpsHistoryOrderTask.orderContent[i].printData, OrderContent, orderLen);
			glb_HttpsHistoryOrderTask.orderContent[i].orderLen = orderLen;
			glb_HttpsHistoryOrderTask.orderContent[i].orderCnt = orderCnt;
			glb_HttpsHistoryOrderTask.orderNum = i;
			glb_HttpsHistoryOrderTask.orderContent[i].status = ORDER_READY;
			gMqttOrderDataSignal = IDLE_STATUS_ORDER;
			g_app_vars.isReprintListUpdate = 1;
			iRet = 0;
			break;
		}
	}
	return iRet;
}

int mqtt_saveHistoryOrderId(const char *OrderId,int len)
{
	int iRet = -1;
	for(int i = 0; i < ORDER_NUM; i++)
	{
		if(glb_HttpsHistoryOrderListTask.orderList[i].status == ORDER_IDLE)
		{
			snprintf((char *)glb_HttpsHistoryOrderListTask.orderList[i].OrderNo, sizeof(glb_HttpsHistoryOrderListTask.orderList[i].OrderNo), "%s", OrderId);
			glb_HttpsHistoryOrderListTask.orderList[i].status = ORDER_READY;
			gMqttOrderDataSignal = READY_DOWN_ORDER_CONT;
			iRet = 0;
			break;
		}
	}

	return iRet;
}

int mqtt_getOrderId(char *OrderNo)
{
	int iRet = -1;
	for(int i=0; i < ORDER_LIST_NUM; i++)
	{
		if(glb_HttpsOrderListTask.orderList[i].status == ORDER_READY)
		{
			strcpy(OrderNo,(char*)glb_HttpsOrderListTask.orderList[i].OrderNo);
			glb_HttpsOrderListTask.orderListNum = i;
			glb_HttpsOrderListTask.orderList[i].status = ORDER_BUSY;
			iRet = 0;
			LogInfo("orderNo[%s]",OrderNo);
			break;
		}
	}

	return iRet;
}

static void *https_get_print_sample(void *args)
{
	pthread_detach(pthread_self());

	int iRet = CarryoutHttpsOptions(GET_PRINT_SAMPLE);
	if(iRet) LogError("GET_PRINT_SAMPLE error return=%d.", iRet);

	return NULL;
}

int mqtt_saveOrderId(const char *OrderId, int len)
{
	int iRet = -1;
	int i;

	for(i=0; i < ORDER_LIST_NUM; i++)
	{
		if(glb_HttpsOrderListTask.orderList[i].status == ORDER_IDLE)
		{
			snprintf((char *)glb_HttpsOrderListTask.orderList[i].OrderNo, sizeof(glb_HttpsOrderListTask.orderList[i].OrderNo), "%s", OrderId);
			glb_HttpsOrderListTask.orderList[i].status = ORDER_READY;
			iRet = 0;
			gMqttOrderDataSignal = READY_DOWN_ORDER_CONT;
			LogDbg("orderId[%s]", glb_HttpsOrderListTask.orderList[i].OrderNo);
			break;
		}
	}

	return iRet;
}

int  mqtt_getHistoryOrderId(char *OrderNo)
{
	int iRet = -1;
	int i;

	for(i=0; i < ORDER_LIST_NUM; i++)
	{
		if(glb_HttpsHistoryOrderListTask.orderList[i].status == ORDER_READY)
		{
			strcpy(OrderNo, (char*)glb_HttpsHistoryOrderListTask.orderList[i].OrderNo);
			glb_HttpsHistoryOrderListTask.orderList[i].status = ORDER_BUSY;
			glb_HttpsHistoryOrderListTask.orderListNum = i;
			iRet = 0;
			LogDbg("OrderNo:%s", OrderNo);
			break;
		}
	}

	return iRet;
}

static int get_tph_temperature(void)
{
	int fd = open(TPH_TEMPERATURE_NODE, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		LogError("open:%s error code=%d", TPH_TEMPERATURE_NODE, fd);
		return 0;
	}
	char buff[64] = {};
	int iRet = read(fd, buff, sizeof(buff));
	close(fd);
	if (iRet < 0) {
		LogError("read:%s error code=%d", TPH_TEMPERATURE_NODE, iRet);
		return iRet;
	}
	iRet = atoi(buff);
	return iRet;
}

static char *getCurrentNetname(void)
{
	uint8_t index = NetGetCurRoute();
	switch (index)
	{
		case NET_WIFI:
			return "wifi";
		case NET_WNET:
			return "mobile";
		case NET_LAN:
			return "lan";
		default:
			return "";
	}
}

typedef struct
{
	int orderType;
	char *orderId;
} T_specificOrder;

static void *SpecificOrderHandler(void *args)
{
	T_specificOrder *orderInfo = (T_specificOrder *)args;
	pthread_detach(pthread_self());
	if (orderInfo->orderId && strlen(orderInfo->orderId)>0) {
		if (orderInfo->orderType == 0) {
			if (GetSpecificOrderInfo(orderInfo->orderId)) LogError("GetSpecificOrderInfo fail!");
		} else if (orderInfo->orderType == 1)
			libam_handler()->notify(AM_NOTIFY_TYPE_SPECIFIC_ORDER, orderInfo->orderId, strlen(orderInfo->orderId));
		else
			LogError("unsupport order type[%d]!", orderInfo->orderType);
	}
	if (orderInfo->orderId) free(orderInfo->orderId);
	if (orderInfo) free(orderInfo);
	return NULL;
}

static int MqttProc_C01(cJSON *pRoot)
{
	T_specificOrder *order_info = (T_specificOrder *)malloc(sizeof(T_specificOrder));
	int orderType = cJSON_GetValueInt(pRoot, "orderType", -1);
	char *orderId = cJSON_GetValueString(pRoot, "orderId", NULL);
	if (orderType>=0 && orderId && strlen(orderId)>0) {
		order_info->orderType = orderType;
		order_info->orderId = strdup(orderId);
	} else {
		LogDbg("wrong orderId or orderType item!"); return -1;
	}
	pthread_t tid;
	if (pthread_create(&tid, NULL, SpecificOrderHandler, order_info) < 0) {
		LogError("MqttProc_C01 pthread_create SpecificOrderHandler fail!");
		return -1;
	}
	return 0;
}

static int MqttProc_C02(cJSON *pRoot)
{
	return 0;
}

static int MqttProc_C03(cJSON *pRoot)
{
	int exptime = cJSON_GetValueInt(pRoot, "expTimestamp", 0x7fffffff);
	time_t timenow  = time(0);
	if (exptime < 0 || timenow > exptime) {
		LogDbg("wrong exptime or exptime[%d] expired timenow[%d]", exptime, timenow);
		return 0;
	}

	int cycle = cJSON_GetValueInt(pRoot, "cycle", 1);
	int delay = cJSON_GetValueInt(pRoot, "delay", 0);
	char *voice = cJSON_GetValueString(pRoot, "voice", NULL);
	if (voice && strlen(voice)>0 && cycle>0) {
		AudioPlayText(voice, cycle, delay);
		return 0;
	}
	char *voiceUrl = cJSON_GetValueString(pRoot, "voiceUrl", NULL);
	if (voiceUrl && strlen(voiceUrl)>0 && cycle>0) {
		LogDbg("voiceUrl:%s", voiceUrl);
		NewVoiceFile(cycle, delay, strlen(voiceUrl)+1, voiceUrl);
	}
	return 0;
}

static int MqttProc_C04(cJSON *pRoot)
{
	Download_Task_Add_Outside(pRoot);
	return 0;
}

static int MqttProc_C05(cJSON *pRoot)
{
	cJSON *pItem = cJSON_GetObjectItem(pRoot, "srv_timestamp");
	
	if( !pItem ) return -1;

	int t = pItem->valueint;
	stime((__const time_t *)&t);
	LogDbg("Sync time OK");

	return 0;
}

static void AudioSlienceOption(const char *name, int option)
{
#define SLIENT 0
#define NOTICE 1
	if (option != SLIENT && option != NOTICE) {
		LogError("unsupport option[%d] please check", option);
		return;
	}

	char buf1[1024] = {}, buf2[1024] = {};
	app_setting_get("tts", "slient", buf1, sizeof(buf1));

	if (strlen(buf1)>0) {
		char *p = strstr(buf1, name);
		if (option == SLIENT) {
			if (p) return;
			else snprintf(buf2, sizeof(buf2), "%s,%s", buf1, name);
		} else {
			if (!p) return;
			else snprintf(buf2, sizeof(buf2), "%.*s%s", p-buf1, buf1, p+strlen(name)+1);
		}
	} else {
		if (option == NOTICE) return;
		snprintf(buf2, sizeof(buf2), "%s", name);
	}

	if (app_setting_set("tts", "slient", buf2) < 0)
		LogError("app_setting_set(tts, slient) error");
#undef SLIENT
#undef NOTICE
}

static void AppSetTTSParam(int pitch, int speed)
{
	if( (pitch>=0 && pitch<=100) || (speed>=0 && speed<=100) ) {
		char tts_param[128] = {};
		char str_pitch[60] = {},str_speed[60] ={};
		int nums = 0;
		if( pitch>=0 && pitch<=100 ) {
			snprintf(str_pitch, sizeof(str_pitch), "\"pitch\":%d",pitch);
			nums ++;
		}
		if( speed>=0 && speed<=100 ) {
			snprintf(str_speed, sizeof(str_speed), "\"speed\":%d",speed);
			nums ++;
		}
		snprintf(tts_param,sizeof(tts_param),"{%s%s%s}",str_pitch,nums>1?",":"",str_speed);
		AudioSetTTSParam(tts_param);
	} else {
		LogError("unsupport pitch[%d], speed[%d]", pitch, speed);
	}
}

static int MqttProc_C06(cJSON *pRoot)
{
	char *testPageLng = cJSON_GetValueString(pRoot, "testPageLng", NULL);
	if ( testPageLng && strlen(testPageLng) ) {
		app_setting_set("global", "language", testPageLng);
	}

	int netConnectVoice = cJSON_GetValueInt(pRoot, "netConnectVoice", -1);
	if ( netConnectVoice != -1 ) {
		AudioSlienceOption("Mobileconnected", netConnectVoice);
		AudioSlienceOption("WIFIconnected", netConnectVoice);
		AudioSlienceOption("LANconnected", netConnectVoice);
	}

	int netDisconnectVoice = cJSON_GetValueInt(pRoot, "netDisconnectVoice", -1);
	if ( netDisconnectVoice != -1 ) {
		AudioSlienceOption("NETdisconnected", netDisconnectVoice);
	}

	int paperTakenVoice = cJSON_GetValueInt(pRoot, "paperTakenVoice", -1);
	if ( paperTakenVoice >= 0 ) {
		uint8_t flag = get_paper_not_taken_actions();
		if ( paperTakenVoice ) {
			flag |= PAPER_NOT_TAKEN_ACTION_PLAY_AUDIO;
		} else {
			flag &= ~PAPER_NOT_TAKEN_ACTION_PLAY_AUDIO;
		}
		set_paper_not_taken_actions(flag);
	}

	int black = cJSON_GetValueInt(pRoot, "black", -1);
	if ( black >= 0 ) {
		print_set_density(black, 1);
	}

	int pitch = cJSON_GetValueInt(pRoot, "voice_pitch", -1);
	int speed = cJSON_GetValueInt(pRoot, "voice_speed", -1);
	if ( pitch!=-1 && speed!=-1 ) {
		AppSetTTSParam(pitch, speed);
	}

	int uploadData = cJSON_GetValueInt(pRoot, "uploadData", -1);
	if ( uploadData>=0 && uploadData!=app_setting_get_int("switch","data_report",-1) ) {
		app_setting_set_int("switch", "data_report", uploadData);
		set_data_collection_state(uploadData);
	}

	int paperTakenPrinting = cJSON_GetValueInt(pRoot, "paperTakenPrinting", -1);
	if ( paperTakenPrinting >= 0 ) {
		uint8_t flag = get_paper_not_taken_actions();
		if ( paperTakenPrinting ) {
			flag |= PAPER_NOT_TAKEN_ACTION_STOP_PRINTING;
		} else {
			flag &= ~PAPER_NOT_TAKEN_ACTION_STOP_PRINTING;
		}
		set_paper_not_taken_actions(flag);
	}

	return 0;
}

static int MqttProc_C07(cJSON *pRoot)
{
	cJSON *pItem = cJSON_GetObjectItem(pRoot, "restart");
	if( pItem && pItem->valueint == 1)
		system("(sleep 2;reboot) &");
	return 0;
}

static int MqttProc_C08(cJSON *pRoot)
{
	return updateRealtimeOrderCount(1);
}

static int MqttProc_C09(cJSON *pRoot)
{
	cJSON *hasTest = cJSON_GetObjectItem(pRoot, "hasTest");
	if(NULL != hasTest){
		if(hasTest->valueint == 1){
			pthread_t tid;
			if (pthread_create(&tid, NULL, https_get_print_sample, NULL) < 0) {
				LogError("pthread_create https_get_print_sample failed.");
			}
		}else if(hasTest->valueint == 2){
			LogDbg("hasTest 2");
			g_app_vars.isGotNetOrder = 1;
		}
	}
	return 0;
}

static int MqttProc_C10(cJSON *pRoot)
{
	libam_handler()->notify(AM_NOTIFY_TYPE_ORDER, NULL, 0);
	return 0;
}

static int MqttProc_C11(cJSON *pRoot)
{
	char *appNo	= cJSON_GetValueString(pRoot, "appNo",NULL);
	char *appId	= cJSON_GetValueString(pRoot, "appId",NULL);
	char *appKey= cJSON_GetValueString(pRoot, "appKey",NULL);
	char *url 	= cJSON_GetValueString(pRoot, "url",NULL);
	if( !appNo || !appId || !appKey || !url ) {
		LogError("appNo,appId,appKey or url is empty");
		return 0;
	}

	if( SavePartnerParams(appNo,appId,appKey,url)>=0 ) {
		T_PartnerParam partner = {};
		if( GetPartnerParam(&partner)>=0 ) {
			libam_handler()->notify(AM_NOTIFY_TYPE_DEVINFO, &partner, sizeof(partner));
		} else {
			LogError("get partner params fail");
		}
	} else {
		LogError("save partner params fail");
	}

	return 0;
}

static int MqttProc_C12(cJSON *pRoot)
{
	int uploadLog = cJSON_GetValueInt(pRoot, "uploadLog",-1);
	if( uploadLog != 0 && uploadLog != 1 ) return 0;

	if( uploadLog ) {
		int stopTimestamp = cJSON_GetValueInt(pRoot,"stopTimestamp",-1);
		int fileSplitTime = cJSON_GetValueInt(pRoot,"fileSplitTime",60);
		int intervalTime = cJSON_GetValueInt(pRoot,"intervalTime",60);
		if( stopTimestamp<=0 || fileSplitTime<=0 || intervalTime<=0 ) return 0;
		app_setting_set_int("log","stopTimestamp",stopTimestamp);
		app_setting_set_int("log","fileSplitTime",fileSplitTime);
		app_setting_set_int("log","intervalTime",intervalTime);
	}
	app_setting_set_int("log","uploadLog",uploadLog);
	int ret = LogSetDebugMode(uploadLog);
	LogInfo("log set debug mode to %d %s!",uploadLog,ret<0?"fail":"OK");

	return 0;
}

#define toHex(c) (((c)>='0'&&(c)<='9')?((c)-'0'):(((c)>='a'&&(c)<='f')?((c)-'a'+10):(((c)>='A'&&(c)<='F')?((c)-'A'+10):0x10)))
static void str2hex(const char *src, int len, FILE *fp)
{
	for ( int i=0; i<len; i+=2 )
	{
		uint8_t high = toHex(src[i]);
		uint8_t low = toHex(src[i+1]);
		if ( high>0xf || low>0xf) {
			LogError("src[%s] contains invalid character", src);
		}
		char c = ( (high<<4)|low );
		if ( fwrite(&c, 1, 1, fp)<=0 ) {
			LogError("fwrite error[%d]:%s", errno, strerror(errno));
		}
	}
}

typedef struct
{
	int taskId;
	char *sequence;
} T_supplementalInfo;

static void *UpdateSupplementalStat(void *args)
{
#define NO_SUPPLEMENT_PRINTED  0
#define SUPPLEMENT_PRINTED     1
#define SEQ_NOT_SAME          (-1)
#define OUT_OF_DATE           (-2)
#define TASKID_NOT_SAME       (-3)
	T_supplementalInfo *p = (T_supplementalInfo *)args;
	pthread_detach(pthread_self());
	if ( p->taskId == NO_SUPPLEMENT_PRINTED ||
	     p->taskId == SEQ_NOT_SAME          ||
	     p->taskId == OUT_OF_DATE           ||
	     p->taskId == TASKID_NOT_SAME ) {
		mqtt_publishTopic(UP_SUPPLEMENT_STATUS, p);
		if (p->sequence) free(p->sequence);
		if (p) free(p);
		return NULL;
	}

	while (1) {
		if ( print_task_is_complete(p->taskId)==SUPPLEMENT_PRINTED ) {
			p->taskId = SUPPLEMENT_PRINTED;
			mqtt_publishTopic(UP_SUPPLEMENT_STATUS, p);
			if (p->sequence) free(p->sequence);
			if (p) free(p);
			return NULL;
		}
		SysDelay(30);
	}
	return NULL;

#undef NO_SUPPLEMENT_PRINTED
#undef SUPPLEMENT_PRINTED
#undef SEQ_NOT_SAME
#undef OUT_OF_DATE
#undef TASKID_NOT_SAME
}

static void PrintSupplementalResult(T_supplementalInfo *p)
{
	if (p) {
		pthread_t tid;
		if ( pthread_create(&tid, NULL, UpdateSupplementalStat, p)<0 ) {
			if (p->sequence) free(p->sequence);
			if (p) free(p);
		}
	}
}

static int MqttProc_C13(cJSON *pRoot)
{
	/* basic param check */
	time_t timeStamp;
	time(&timeStamp);
	int ts = cJSON_GetValueInt(pRoot, "validTimeStamp", -1);
	if ( ts<0 || ts<timeStamp ) {
		LogError("timeStamp invalid or out of date!");
		return -1;
	}

	char *sequence = cJSON_GetValueString(pRoot, "seq", NULL);
	if ( !sequence || !strlen(sequence) ) {
		LogError("no sequence or sequence invalid!");
		return -1;
	}

	/* handle voice */
	int voiceCnt   = cJSON_GetValueInt(pRoot, "voiceCnt",  -1);
	int voiceDelay = cJSON_GetValueInt(pRoot, "voiceDelay", 0);
	if ( voiceCnt>0 ) {
		char *voiceData = cJSON_GetValueString(pRoot, "voiceData", NULL);
		if ( voiceData && strlen(voiceData) ) {
			AudioPlayText(voiceData, voiceCnt, voiceDelay);
		} else {
			char *voiceUrl = cJSON_GetValueString(pRoot, "voiceUrl", NULL);
			if ( voiceUrl && strlen(voiceUrl) ) {
				NewVoiceFile(voiceCnt, voiceDelay, strlen(voiceUrl)+1, voiceUrl);
			}
		}
	}

	/* handle orderdata */
	int orderCnt = cJSON_GetValueInt(pRoot, "orderCnt", -1);
	if ( orderCnt>0 ) {
		char *orderData = cJSON_GetValueString(pRoot, "orderData", NULL);
		if ( orderData && strlen(orderData) ) {
			size_t size = 0;
			char *realData = NULL;
			FILE *fp = open_memstream(&realData, &size);
			if ( !fp ) {
				LogError("open_memstream fail!"); return 0;
			}
			str2hex(orderData, strlen(orderData), fp);
			fclose(fp);
			if ( size>0 ) {
				T_supplementalInfo *p = (T_supplementalInfo *)malloc(sizeof(T_supplementalInfo));
				if ( !p ) return -1;
				p->taskId = print_supplemental_data(sequence, ts, realData, size, orderCnt);
				p->sequence = strdup(sequence);
				PrintSupplementalResult(p);
			} else {
				LogError("unusual orderSize[%d]", size);
			}
			if (realData) free(realData);
		}
	} else {
		LogError("unusual orderCnt[%d]", orderCnt);
	}
	return 0;
}

static int MqttProc_T01(cJSON *pRoot)
{
	int mqtt_rw = cJSON_GetValueInt(pRoot,"mqtt_rw",0);
	if( mqtt_rw ) g_app_vars.isMqttRwOK = 1;
	return 0;
}

static int CalcDatafsFreesize(void)
{
	int iRet;
	struct statfs stat;

	if ((iRet = statfs("/data/", &stat))) {
		LogError("get data partition accur error %s", strerror(errno)); return -1;
	}

	return stat.f_bfree*stat.f_bsize;//Byte
}

static char *_pack_msg_s01()
{
	cJSON *pRoot = cJSON_CreateObject();
	if( !pRoot ) return NULL;
	cJSON *pItem = cJSON_CreateObject();
	if( !pItem ) {
		cJSON_Delete(pRoot); return NULL;
	}

	char fontVersion[16] = {}, fontLang[64] = {}, imei[32] = {}, iccid[32] = {};
	T_PartnerParam partner = {};
	GetPartnerParam(&partner);
	audio_cfg_t *audiocfg = getTTSInfo();
	print_get_fontlib_version(fontVersion);
	print_get_fontlib_language(fontLang);
	wnet_ini_get(WNET_KEY_IMEI,imei,sizeof(imei));
	wnet_ini_get(WNET_KEY_ICCID,iccid,sizeof(iccid));
	cJSON_AddStringToObject(pItem, "imei",  imei);
	cJSON_AddStringToObject(pItem, "iccid", iccid);
	cJSON_AddStringToObject(pItem, "mm",    VALUE_IN_SHAREMEM(model));
	cJSON_AddStringToObject(pItem, "msn",   VALUE_IN_SHAREMEM(sn));
	cJSON_AddStringToObject(pItem, "btm",   VALUE_IN_SHAREMEM(bt_mac));
	cJSON_AddStringToObject(pItem, "fv",    VALUE_IN_SHAREMEM(fw_ver));
	cJSON_AddStringToObject(pItem, "btv",   VALUE_IN_SHAREMEM(boot_ver));
	cJSON_AddStringToObject(pItem, "hv",    VALUE_IN_SHAREMEM(hw_ver));
	cJSON_AddStringToObject(pItem, "appv",  APP0_VERSION);
	cJSON_AddStringToObject(pItem, "flv",   fontVersion);
	cJSON_AddStringToObject(pItem, "flang", fontLang);
	cJSON_AddStringToObject(pItem, "an",    partner.appNo);
	cJSON_AddStringToObject(pItem, "pu",    partner.parterURL);
	if ( audiocfg ) {
		cJSON_AddStringToObject(pItem, "ttsv", audiocfg->tts.version);
		cJSON_AddStringToObject(pItem, "ttslang", audiocfg->tts.voice_name);
	}
	if ( access("/tmp/abnormal.exit", F_OK) == 0 ) {
		cJSON_AddNumberToObject(pItem, "ae", 1);
		unlink("/tmp/abnormal.exit");
	} else {
		cJSON_AddNumberToObject(pItem, "ae", 0);
	}
	cJSON_AddNumberToObject(pItem, "fsize", CalcDatafsFreesize());
	cJSON_AddItemToObject(pRoot, "s01", pItem);

	char *jstr = cJSON_PrintUnformatted(pRoot);
	cJSON_Delete(pRoot);

	return jstr;
}

static char *_pack_msg_s02()
{
	cJSON *pRoot = cJSON_CreateObject();
	if( !pRoot ) return NULL;
	cJSON *pItem = cJSON_CreateObject();
	if( !pItem ) {
		cJSON_Delete(pRoot);
		return NULL;
	}

	cJSON_AddStringToObject(pItem, "net", getCurrentNetname());
	cJSON_AddItemToObject(pRoot, "s02", pItem);

	char *jstr = cJSON_PrintUnformatted(pRoot);
	cJSON_Delete(pRoot);

	return jstr;
}

void update_sdk_in_bucks_count(uint8_t option)
{
#define RESET    0
#define INCREASE 1
	if ( option!=RESET && option!=INCREASE ) return;
	pthread_mutex_lock(&sdk_in_bucks_lock);
	if ( option==RESET ) {
		gSdkInBucks=0;
	} else {
		gSdkInBucks++;
	}
	pthread_mutex_unlock(&sdk_in_bucks_lock);
#undef RESET
#undef INCREASE
}

static uint32_t PrinteDistanceReport(void)
{
    static uint8_t isfirst = 1;

    uint32_t value = app_setting_get_int(APP_SETTING_PRINT_DISTANCE, "latest", 0) - app_setting_get_int(APP_SETTING_PRINT_DISTANCE, "last", 0);
    if (isfirst) {
        app_setting_set_int(APP_SETTING_PRINT_DISTANCE, "latest", 0);
        app_setting_set_int(APP_SETTING_PRINT_DISTANCE, "last",   0);
        isfirst = 0;
    } else {
        app_setting_set_int(APP_SETTING_PRINT_DISTANCE, "last", app_setting_get_int(APP_SETTING_PRINT_DISTANCE, "latest", 0));
    }

    return value;
}

static char *_pack_msg_s03()
{
	cJSON *pRoot = cJSON_CreateObject();
	if( !pRoot ) return NULL;
	cJSON *pItem = cJSON_CreateObject();
	if( !pItem ) {
		cJSON_Delete(pRoot);
		return NULL;
	}


	unsigned char buf[128] = {};
	int len = badpoint_info_from_file(buf,sizeof(buf),NULL);
	if( len<0 ) len = sys_global_var()->prt_dots_per_line/8;
	char bpt[512]={};
	SysBcd2Asc((uint8_t *)bpt, buf, len*2);
	cJSON_AddStringToObject(pItem, "bpt", bpt);
	cJSON_AddNumberToObject(pItem, "pkm", PrinteDistanceReport());
	cJSON_AddNumberToObject(pItem, "mbt", 0);
	int wifiRssidb=0, wnetRssidb=0;
	net_wifi_get_rssi(&wifiRssidb);
	wnetRssidb = wnet_ini_get_int(WNET_KEY_RSSI_DB, 0);
	cJSON_AddNumberToObject(pItem, "wifil", wifiRssidb);
	cJSON_AddNumberToObject(pItem, "gprsl", wnetRssidb);
	cJSON_AddNumberToObject(pItem, "temp", get_tph_temperature());

	cJSON *paycnt = cJSON_CreateObject();
	if ( paycnt ) {
		char vendor[64] = {};
		trade_ini_get(TRADE_KEY_VEN, vendor, sizeof(vendor));
		cJSON_AddStringToObject(paycnt, TRADE_KEY_VEN, vendor);
		cJSON_AddNumberToObject(paycnt, TRADE_KEY_TTT, gSdkInBucks);
		cJSON_AddItemToObject(pItem, "paycnt", paycnt);
		update_sdk_in_bucks_count(0);
	}

	cJSON_AddItemToObject(pRoot, "s03", pItem);

	char *jstr = cJSON_PrintUnformatted(pRoot);
	cJSON_Delete(pRoot);

	return jstr;
}

static char *_pack_msg_s04()
{
	cJSON *pRoot = cJSON_CreateObject();
	if( !pRoot ) return NULL;
	cJSON *pItem = cJSON_CreateObject();
	if( !pItem ) {
		cJSON_Delete(pRoot); return NULL;
	}
	char longitude[32] = {}, latitude[32] = {};
	sys_get_lbs(longitude, latitude);
	cJSON_AddStringToObject(pItem, "lat", latitude);
	cJSON_AddStringToObject(pItem, "lng", longitude);
	cJSON_AddNumberToObject(pItem, "mbs", gCashboxOpentimes);
	if (gt_printerStatus.no_paper && g_app_vars.isNoPaperCleard) {
		cJSON_AddNumberToObject(pItem, "np", 1);
	} else {
		cJSON_AddNumberToObject(pItem, "np", 0);
	}

	char vendor[64] = {};
	trade_ini_get(TRADE_KEY_VEN, vendor, sizeof(vendor));
	if (strlen(vendor)) {
		cJSON *payb = cJSON_CreateObject();
		if (payb) {
			cJSON_AddStringToObject(payb, TRADE_KEY_VEN, vendor);
			cJSON_AddNumberToObject(payb, TRADE_KEY_BID, trade_ini_get_int(TRADE_KEY_BID, 0));
			cJSON_AddItemToObject(pItem, "payb", payb);
		}
	}
	cJSON_AddItemToObject(pRoot, "s04", pItem);
	if (gCashboxOpentimes) gCashboxOpentimes = 0;
	if (g_app_vars.isNoPaperCleard) g_app_vars.isNoPaperCleard = 0;
	if (g_app_vars.isNeedSendLBsByHttp) g_app_vars.isNeedSendLBsByHttp = 0;
	char *jstr = cJSON_PrintUnformatted(pRoot);
	cJSON_Delete(pRoot);
	return jstr;
}

static char *_pack_msg_s08(void *topicBuff)
{
	cJSON *pRoot = cJSON_CreateObject();
	if( !pRoot ) return NULL;
	cJSON *pItem = cJSON_CreateObject();
	if( !pItem ) {
		cJSON_Delete(pRoot); return NULL;
	}
	DownloadTask_Item_t *item = (DownloadTask_Item_t *)topicBuff;
	cJSON_AddNumberToObject(pItem, "type",   item->type);
	cJSON_AddStringToObject(pItem, "ver",    item->ver);
	cJSON_AddNumberToObject(pItem, "status", item->status);
	cJSON_AddItemToObject  (pRoot, "s08",    pItem);
	char *jstr = cJSON_PrintUnformatted(pRoot);
	cJSON_Delete(pRoot);
	return jstr;
}

static char *_pack_msg_s09(void *topicBuff)
{
	cJSON *pRoot = cJSON_CreateObject();
	if( !pRoot ) return NULL;
	cJSON *pItem = cJSON_CreateObject();
	if( !pItem ) {
		cJSON_Delete(pRoot); return NULL;
	}
	T_supplementalInfo *p = (T_supplementalInfo *)topicBuff;
	time_t ts;
	time(&ts);
	cJSON_AddStringToObject(pItem, "seq",          p->sequence);
	cJSON_AddNumberToObject(pItem, "status",       p->taskId);
	cJSON_AddNumberToObject(pItem, "prtTimeStamp", ts);
	cJSON_AddItemToObject  (pRoot, "s09",          pItem);
	char *jstr = cJSON_PrintUnformatted(pRoot);
	cJSON_Delete(pRoot);
	return jstr;
}

static void app_mqtt_msg_proc(void **unused, struct mqtt_response_publish *published)
{
	char topic[128];

	snprintf(topic,sizeof(topic),"/%s/%s/sub",sys_global_var()->project,sys_global_var()->sn);
	if( published->topic_name_size != strlen(topic) || strncmp(topic,published->topic_name,published->topic_name_size) ) {
		LogWarn("Topic[%.*s] invalid",published->topic_name_size,published->topic_name);
		return;
	}

	if( published->application_message_size<=0  ) return;
	char *data = malloc(published->application_message_size+1);
	memcpy(data,published->application_message,published->application_message_size);
	data[published->application_message_size] = 0;
	LogInfo("%s",data);
	cJSON *root = cJSON_Parse(data);
	free(data);
	if( root ) {
		cJSON *item = NULL;
		if( NULL != (item = cJSON_GetObjectItem(root,"C01")) )
			MqttProc_C01(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C02")) )
			MqttProc_C02(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C03")) )
			MqttProc_C03(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C04")) )
			MqttProc_C04(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C05")) )
			MqttProc_C05(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C06")) )
			MqttProc_C06(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C07")) )
			MqttProc_C07(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C08")) )
			MqttProc_C08(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C09")) )
			MqttProc_C09(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C10")) )
			MqttProc_C10(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C11")) )
			MqttProc_C11(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C12")) )
			MqttProc_C12(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"C13")) )
			MqttProc_C13(item);
		else if( NULL != (item = cJSON_GetObjectItem(root,"T01")) )
			MqttProc_T01(item);
		cJSON_Delete(root);
	} else {
		LogError("cJSON_Parse fail");
	}
}

static void app_mqtt_conn_success_cb(mqtt_client_t *client,int is_first_fime)
{
	if( is_first_fime ) {
		mqtt_publishTopic(UP_DEV_STATUS, NULL);
	}
	mqtt_publishTopic(UP_NET_TYPE, NULL);
}

int mqtt_publishTopic(int type, void *topicBuff)
{
	char *msg = NULL;
	switch( type ) {
		case UP_DEV_STATUS:
			msg =  _pack_msg_s01();
			break;
		case UP_NET_TYPE:
			msg = _pack_msg_s02();
			break;
		case UP_INTERVAL:
			msg = _pack_msg_s03();
			break;
		case UP_RIGHTNOW:
			msg =  _pack_msg_s04();
			break;
		case UP_UPGRADE_STATUS:
			msg = _pack_msg_s08(topicBuff);
			break;
		case UP_SUPPLEMENT_STATUS:
			msg = _pack_msg_s09(topicBuff);
			break;
		default:
			LogError("invalid msgType:%d", type);
		    return -1;
    }

	if( !msg ) {
		LogError("Pack msg(%d) fail!",type);
		return -1;
	}

	MQTTErrors_t err = sunmi_mqtt_publish(msg);
	LogInfo("%s",msg);
	free(msg);
	return err==MQTT_OK?0:-1;
}

void mqtt_report_rightnow(void)
{
#define WNET_GET_VALUE(k,v) wnet_ini_get(k, v, sizeof(v))
#define CJSON_ADD_WNET_ITEM(o,n,k,v) cJSON_AddStringToObject(o, n, WNET_GET_VALUE(k,v))
	cJSON *root   = cJSON_CreateObject();
	cJSON *common = cJSON_CreateObject();
	cJSON *wnet   = cJSON_CreateObject();
	if ( !root || !common || !wnet ) {
		if (root)   cJSON_Delete(root);
		if (wnet)   cJSON_Delete(wnet);
		if (common) cJSON_Delete(common);
		return;
	}
	Network_Info_t info = {};
	CJSON_ADD_WNET_ITEM(wnet, "mnc",   WNET_KEY_MNC,   info.mnc);
	CJSON_ADD_WNET_ITEM(wnet, "mcc",   WNET_KEY_MCC,   info.mcc);
	CJSON_ADD_WNET_ITEM(wnet, "isp",   WNET_KEY_ISP,   info.isp);
	CJSON_ADD_WNET_ITEM(wnet, "ver",   WNET_KEY_VER,   info.ver);
	CJSON_ADD_WNET_ITEM(wnet, "apn",   WNET_KEY_APN,   info.apn);
	CJSON_ADD_WNET_ITEM(wnet, "type",  WNET_KEY_TYPE,  info.type);
	CJSON_ADD_WNET_ITEM(wnet, "imei",  WNET_KEY_IMEI,  info.imei);
	CJSON_ADD_WNET_ITEM(wnet, "imsi",  WNET_KEY_IMSI,  info.imsi);
	CJSON_ADD_WNET_ITEM(wnet, "cpsi",  WNET_KEY_CPSI,  info.cpsi);
	CJSON_ADD_WNET_ITEM(wnet, "dial",  WNET_KEY_DIAL,  info.dial);
	CJSON_ADD_WNET_ITEM(wnet, "iccid", WNET_KEY_ICCID, info.iccid);
	char longitude[32] = {}, latitude[32] = {};
	sys_get_lbs(longitude, latitude);
	cJSON_AddStringToObject(common, "lat",  latitude);
	cJSON_AddStringToObject(common, "lng",  longitude);
	cJSON_AddNumberToObject(common, "mbs",  0);
	cJSON_AddNumberToObject(common, "np",   0);
	cJSON_AddItemToObject  (common, "wnet", wnet);
	cJSON_AddItemToObject  (root,   "s04",  common);
	char *publish_msg = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!publish_msg) {
		LogError("publish msg null!"); return;
	}
	sunmi_mqtt_publish(publish_msg);
	LogInfo("%s", publish_msg);
	free(publish_msg);
#undef WNET_GET_VALUE
#undef CJSON_ADD_WNET_ITEM
}

void mqtt_report_trade_bindstatus(uint8_t bindStatus)
{
#define BIND   1
#define UNBIND 0
	if ( bindStatus!=BIND && bindStatus!=UNBIND ) return;
	cJSON *root   = cJSON_CreateObject();
	cJSON *payb   = cJSON_CreateObject();
	cJSON *common = cJSON_CreateObject();
	if ( !root || !payb || !common ) {
		if (root)   cJSON_Delete(root);
		if (payb)   cJSON_Delete(payb);
		if (common) cJSON_Delete(common);
		return;
	}
	char vendor[64] = {};
	trade_ini_get(TRADE_KEY_VEN,vendor,sizeof(vendor));
	cJSON_AddStringToObject(payb,TRADE_KEY_VEN,vendor);
	cJSON_AddNumberToObject(payb,TRADE_KEY_BID,bindStatus);
	cJSON_AddItemToObject(common,"payb",payb);
	cJSON_AddItemToObject(root,"s04",common);
	char *publish_msg = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if ( !publish_msg ) {
		LogError("publish msg null!"); return;
	}
	sunmi_mqtt_publish(publish_msg);
	LogInfo("%s", publish_msg);
	free(publish_msg);
#undef BIND
#undef UNBIND
}

void mqtt_thread_init(void)
{
	sunmi_mqtt_init(app_mqtt_msg_proc, app_mqtt_conn_success_cb,NULL);
}
