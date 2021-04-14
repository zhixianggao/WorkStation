#include "project_includer.h"
#include "http_info_log.h"

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define MEM_COLLECTION_DIR        "/tmp/collection/"
#define FLASH_COLLECTION_DIR      "/data/PUB/collection/"
#define HTTP_DATA_MAX_SIZE        (1024 * 1024 * 2)    //2MB
#define HTTP_FILE_MAX_SIZE        (1024 * 1024 * 5)    //5MB
#define DATA_COLLECTION_LIMIT     (1024 * 450)         //450KB
#define toHex(c) (((c) >= '0' && (c) <= '9') ? ((c) - '0') : (((c) >= 'a' && (c) <= 'f') ? ((c) - 'a' + 10) : (((c) >= 'A' && (c) <= 'F') ? ((c) - 'A' + 10) : 0x10)))
#define RETURN_WITH_NOTICE(label, key, value, notice, args...) \
	{                                                          \
		key = value;                                           \
		LogError(notice, ##args);                              \
		goto label;                                            \
	}

/*----------------------------------------------*
 | 全局变量                                     |
 *----------------------------------------------*/
static char finishedOrderID[50][64]      = {};  //已打印完成的订单ID号
static uint8_t orderListFailTimes        = 0;   //请求订单列表失败次数

HTTPS_ORDER_LIST_TASK glb_HttpsOrderListTask        = {};  //实时订单列表任务
HTTPS_ORDER_LIST_TASK glb_HttpsHistoryOrderListTask = {};  //历史订单列表任务
HTTPS_ORDER_TASK      glb_HttpsOrderTask            = {};  //实时打印任务
HTTPS_ORDER_TASK      glb_HttpsHistoryOrderTask     = {};  //历史打印任务

/*----------------------------------------------*
 | 引用外部变量                                 |
 *----------------------------------------------*/
extern pthread_mutex_t cloud_order_lock;

/*----------------------------------------------*
 | 函数声明                                     |
 *----------------------------------------------*/
static void AbnormalOrderStatusHandler(const char *orderId, int status);

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
static int WhetherOrderPrinted(const char *orderId)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(finishedOrderID); i++) {
		if (!strncmp(finishedOrderID[i], orderId, sizeof(finishedOrderID[i]))) {
			LogDbg("this order has been printed orderId[%s]", finishedOrderID[i]); return 1;
		}
	}
	return 0;
}

static int HttpsSendReqDownOrderList(void)
{
	int iRet = -1;
	gMqttOrderDataSignal = READY_DOWN_ORDER_LIST;
	if(gMqttOrderDataSignal == READY_DOWN_ORDER_LIST)
		iRet = CarryoutHttpsOptions(GET_ORDER_LIST);
	return iRet;
}

static int HttpsSendReqDownOrderCont(void)
{
#define ORDER_FINISHED 1
	int iRet = -1;
	if (gMqttOrderDataSignal == READY_DOWN_ORDER_CONT)
	{
		memset(glb_termParm.cur_lst_order_no, 0, sizeof(glb_termParm.cur_lst_order_no));
		if (!g_app_vars.isReprintOldOrderContent)
		{
			if (mqtt_getOrderId(glb_termParm.cur_lst_order_no) < 0)
				return iRet;
			if (WhetherOrderPrinted(glb_termParm.cur_lst_order_no))
				AbnormalOrderStatusHandler(glb_termParm.cur_lst_order_no, ORDER_FINISHED);
			else {
				iRet = CarryoutHttpsOptions(GET_ORDER_CONT);
				if (iRet == 0) g_app_vars.isCloudOrderStatChanged = 0;
			}
		} else {
			if (mqtt_getHistoryOrderId(glb_termParm.cur_lst_order_no) < 0)
				return iRet;
			if (CarryoutHttpsOptions(GET_ORDER_CONT) == UNUSUAL_ORDER_CNT) {
				cleanHistoryOrderMsg();
				iRet = 0;
			}
		}
	}
	return iRet;
#undef ORDER_FINISHED
}

static int HttpsSendPrtFinishResult(void)
{
	if(gMqttPrintOrderDataSignal != READY_PRINT_ORDER_FINISH) return -1;
	int iRet = -1, i;
	iRet = CarryoutHttpsOptions(SEND_ORDER_STATUS);
	if(iRet == CURL_OK_WITH_RESPONSE) {
		gMqttPrintOrderDataSignal = IDLE_STATUS_ORDER;
		for(i = 0; i < ORDER_LIST_NUM; i++) {
			if(glb_HttpsOrderListTask.orderList[i].status == ORDER_READY)
				gMqttOrderDataSignal = READY_DOWN_ORDER_CONT;
		}
	}
	return iRet;
}


static int _WaitingOrderTask_(void)
{
	for ( int i=0; i<ORDER_NUM; i++) {
		if ( glb_HttpsOrderTask.orderContent[i].status==ORDER_READY ) {
			LogDbg("ORDER_READY index[%d]", i);
			gMqttPrintOrderDataSignal = READY_PRINTING_ORDER;
			return i;
		}
	}
	return -1;
}

static size_t httpWriteFunc(char *buff, size_t size, size_t nmemb, FILE *fp)
{
	if (ftell(fp) + size * nmemb >= HTTP_DATA_MAX_SIZE) {
		LogError("beyond data limit"); return 0;
	}

	return fwrite(buff, size, nmemb, fp);
}

static size_t httpDldFileFunc(char *buff, size_t size, size_t nmemb, FILE *fp)
{
	if (ftell(fp) + size * nmemb >= HTTP_FILE_MAX_SIZE) {
		LogError("beyond file limit"); return 0;
	}
	return fwrite(buff, size, nmemb, fp);
}

static void HttpsPostDldFileReq(const char *url, CURLcode *errCode, const char *filename)
{
	LogDbg("download filename[%s]", filename);
	CURL *curl = curl_easy_init();
	if (!curl) {
		*errCode = CURLE_FAILED_INIT; return;
	}

	FILE *fp = fopen(filename, "w");
	if (!fp) {
		LogError("create file %s fail", filename);
		if (curl) curl_easy_cleanup(curl);
		*errCode = CURLE_FAILED_INIT; return;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpDldFileFunc);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000 * 40);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 40L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	fclose(fp);
	*errCode = res;
	return;
}

int DldCertainAudio(void *arg)
{
	LogDbg("init");
	T_VoiceFile *temp = (T_VoiceFile *)arg;
	LogDbg("voice info:cycle[%d]|delay[%d]|voiceUrlLen[%d]|voiceUrl[%s]", temp->cycle, temp->delay, temp->voiceUrlLen, temp->voiceUrl);

	static uint8_t i = 0;
	CURLcode res = CURLE_OK;
	char temp_filename[256] = {}, real_filename[256] = {};

	char *p = strrchr(temp->voiceUrl, '.');
	if (!p) {
		LogError("abnormal audiofile[%s]", temp->voiceUrl); return -1;
	}

	if (i >= 9) i = 0;
	else i++;

	uint32_t timeStamp;
	time((time_t *)&timeStamp);
	if (!strcmp(p, ".mp3"))
		snprintf(temp_filename, sizeof(temp_filename), "/tmp/audiofile/%08x%d.mp3.tmp", timeStamp, i);
	else if (!strcmp(p, ".wav"))
		snprintf(temp_filename, sizeof(temp_filename), "/tmp/audiofile/%08x%d.wav.tmp", timeStamp, i);
	else {
		LogError("current dont support audiofile[%s]", temp->voiceUrl); return -2;
	}

	HttpsPostDldFileReq(temp->voiceUrl, &res, temp_filename);
	if (res != CURLE_OK) {
		LogError("curl error[%d], error string[%s], unlink file[%s]", res, curl_easy_strerror(res), temp_filename);
		unlink(temp_filename);
		return -3;
	}

	char *p1 = strrchr(temp_filename, '.');
	if (p1) {
		snprintf(real_filename, sizeof(real_filename), "%.*s", p1-temp_filename, temp_filename);
		rename(temp_filename, real_filename);
		AudioPlayFile(real_filename, temp->cycle, temp->delay);
		KeepAudioTotalsize(real_filename);
	}
	LogDbg("finish");
	return 0;
}

static char *HttpsPostReq(const char *url, CURLcode *errCode)
{
	char *buff = NULL;
	size_t size = 0;
	CURL *curl = curl_easy_init();
	if (!curl) {
		if( errCode ) *errCode = CURLE_FAILED_INIT;
		return buff;
	}
	FILE *fp = open_memstream(&buff, &size);
	if (!fp) {
		if( errCode ) *errCode = CURLE_FAILED_INIT;
		return buff;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpWriteFunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000 * 40);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 40L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	fclose(fp);
	if (size <= 0) {
		if( errCode ) *errCode = res;
		if( buff ) free(buff);
		return NULL;
	}

	return buff;
}

static int HttpsOrderResponseParse(const char *response)
{
	LogDbg("response[%s]", response);
	int iRet = 0;
	cJSON *pRoot = NULL;

	if (strlen(response))
	{
		if (util_chk_json_format(response) < 0) RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");
		pRoot = cJSON_Parse(response);
		if (!pRoot) RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");

		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if (code == RESPONSE_ERROR_CODE || (code == RESPONSE_ERROR_CODE && msg && strlen(msg) > 0))
			RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");

		char *data = cJSON_GetValueString(pRoot, "data", NULL);
		if (!data) {
			RETURN_WITH_NOTICE(OUT, iRet, -5, "absence of data!");
		} else if (strcmp(data, "success")) {
			RETURN_WITH_NOTICE(OUT, iRet, -6, "cloud reply[%s] not success", data);
		}
	OUT:
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogError("ZERO response!"); return CLOUD_NO_RESPONSE;
	}
}

static int HttpsOrderListResponseParse(const char *response)
{
	LogDbg("response[%s]", response);

	int iRet = 0;
	int iLen = 0;
	unsigned char cmdData[1024] = {};
	cJSON *pRoot = NULL;

	if (strlen(response) > 0)
	{
		if (util_chk_json_format(response) < 0)
			RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");
		pRoot = cJSON_Parse(response);
		if (!pRoot)
			RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");
		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if (code == RESPONSE_ERROR_CODE || (code == RESPONSE_ERROR_CODE && msg && strlen(msg) > 0))
			RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");

		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if (!pData) {
			updateRealtimeOrderCount(0); iRet = -5; goto OUT;
		} else {
			int iCount = cJSON_GetArraySize(pData);
			if (iCount == 0)
			{
				LogInfo("[NULL DATA]");
				LogInfo("[Reprint:%d, NeedReprint:%d, RelistGot:%d, RelistUpdate:%d]",
						g_app_vars.isReprintOldOrderContent, g_app_vars.isNeedReprintOldOrder, g_app_vars.isReprintListGot, g_app_vars.isReprintListUpdate);
				if (!g_app_vars.isReprintOldOrderContent) {
					updateRealtimeOrderCount(0); iRet = 0; goto OUT;
				} else if (g_app_vars.isReprintOldOrderContent) {
					//当回打历史订单模式中，得到的data数据为空时，需要清除回打历史订单相关flag
					g_app_vars.isReprintOldOrderContent = 0; g_app_vars.isNeedReprintOldOrder = 0; g_app_vars.isReprintListGot = 0; g_app_vars.isReprintListUpdate = 0;
				}
				iRet = 0; goto OUT;
			}

			for (int i = 0; i < iCount; ++i)
			{
				cJSON *orderId = cJSON_GetArrayItem(pData, i);
				if (orderId && orderId->valuestring && strlen(orderId->valuestring) > 0)
				{
					if (!g_app_vars.isReprintOldOrderContent) {
						if (mqtt_saveOrderId(orderId->valuestring, strlen(orderId->valuestring)) < 0) {
							RETURN_WITH_NOTICE(OUT, iRet, -6, "no free task index!");
						}
					} else if (g_app_vars.isReprintOldOrderContent) {
						if (mqtt_saveHistoryOrderId(orderId->valuestring, strlen(orderId->valuestring)) < 0) {
							RETURN_WITH_NOTICE(OUT, iRet, -6, "no free task index!");
						}
						g_app_vars.isReprintListGot = 1;//回打历史订单标志着历史订单列表已经获取到
					}

					memcpy((char *)&cmdData[iLen], orderId->valuestring, strlen(orderId->valuestring));
					iLen += strlen(orderId->valuestring);
				}
			}
		}
	OUT:
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogDbg("ZERO response!"); return CLOUD_NO_RESPONSE;
	}
}

static void _string2Hex_(const char *src, int length, FILE *fp)
{
	int i;
	for (i = 0; i < length; i += 2)
	{
		unsigned char high = toHex(src[i]);
		unsigned char low = toHex(src[i + 1]);
		if (high > 0xf || low > 0xf)
			LogError("buf[%s] contains invalid char", src);
		char c = ((high << 4) | low);
		if (fwrite(&c, 1, 1, fp) <= 0) LogError("fwrite error[%d]:%s", errno, strerror(errno));
	}
}

static int HttpsOrderInfoResponseParse(const char *response)
{
	LogDbg("response[%s]", response);
	int iRet = 0, defOrderType = 1, defOrderCnt = 1;
	size_t size = 0;
	unsigned char orderNo[64] = {};
	char *cmdData = NULL;
	cJSON *pRoot = NULL;

	if (strlen(response) > 0)
	{
		if (util_chk_json_format(response) < 0)
			RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");
		pRoot = cJSON_Parse(response);
		if (!pRoot)
			RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");
		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if (code == RESPONSE_ERROR_CODE || (code == RESPONSE_ERROR_CODE && msg && strlen(msg) > 0))
			RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");

		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if (!pData) {
			RETURN_WITH_NOTICE(OUT, iRet, -5, "absence of data!");
		} else {
			int voiceCnt = cJSON_GetValueInt(pData, "voiceCnt", 1);
			int delay = cJSON_GetValueInt(pData, "delay", 0);
			char *voice = cJSON_GetValueString(pData, "voice", NULL);
			char *voiceUrl = cJSON_GetValueString(pData, "voiceUrl", NULL);
			if (voice && strlen(voice)>0 && voiceCnt>0)
				AudioPlayText(voice, voiceCnt, delay);
			else if (voiceUrl && strlen(voiceUrl)>0 && voiceCnt>0)
				NewVoiceFile(voiceCnt, delay, strlen(voiceUrl)+1, voiceUrl);

			char *orderId = cJSON_GetValueString(pData, "orderId", NULL);
			if (orderId && strlen(orderId)>0)
			{
				if (!g_app_vars.isReprintOldOrderContent) {
					if (strcmp((const char *)orderId, (const char *)glb_HttpsOrderListTask.orderList[glb_HttpsOrderListTask.orderListNum].OrderNo)) {
						RETURN_WITH_NOTICE(OUT, iRet, -6, "not the same orderNo: responseNo[%s], requestNo[%s]!", orderId, glb_HttpsOrderListTask.orderList[glb_HttpsOrderListTask.orderListNum].OrderNo);
					}
				} else if (g_app_vars.isReprintOldOrderContent) {
					if (strcmp((const char *)orderId, (const char *)glb_HttpsHistoryOrderListTask.orderList[glb_HttpsHistoryOrderListTask.orderListNum].OrderNo)) {
						RETURN_WITH_NOTICE(OUT, iRet, -6, "not the same orderNo!");
					}
				}
				snprintf((char *)orderNo, sizeof(orderNo), orderId);
			}

			defOrderCnt = cJSON_GetValueInt(pData, "orderCnt", 1);
			if (defOrderCnt>0)
			{
				defOrderType = cJSON_GetValueInt(pData, "type", 1);
				if (defOrderType == 1) /* orginal SUNMI order type: command & data */
				{
					cJSON *pSubData = cJSON_GetObjectItem(pData, "data");
					if (pSubData)
					{
						int iCount = cJSON_GetArraySize(pSubData);
						if (iCount == 0) {
							RETURN_WITH_NOTICE(OUT, iRet, -7, "ZERO arry!");
						}

						FILE *fp = open_memstream(&cmdData, &size);
						if (!fp) {
							RETURN_WITH_NOTICE(OUT, iRet, -9, "open_memstream fail!");
						}

						for (int i = 0; i < iCount; ++i)
						{
							cJSON *lineData = cJSON_GetArrayItem(pSubData, i);
							if (lineData)
							{
								char *c = cJSON_GetValueString(lineData, "c", NULL);
								if (c && strlen(c)>0) _string2Hex_(c, strlen(c), fp);
								char *d = cJSON_GetValueString(lineData, "d", NULL);
								if (d && strlen(d)>0) fwrite(d, strlen(d), 1, fp);
							}
						}
						fclose(fp);
					} else {
						RETURN_WITH_NOTICE(OUT, iRet, -8, "absence of orderData!");
					}
				}
				else if (defOrderType == 2) /* new SUNMI order type: hexadecimal */
				{
					char *orderData = cJSON_GetValueString(pData, "orderData", NULL);
					if (orderData && strlen(orderData)>0)
					{
						FILE *fp = open_memstream(&cmdData, &size);
						if (!fp) {
							RETURN_WITH_NOTICE(OUT, iRet, -9, "open_memstream fail!");
						}
						_string2Hex_(orderData, strlen(orderData), fp);
						fclose(fp);
					} else {
						RETURN_WITH_NOTICE(OUT, iRet, -8, "absence of orderData!");
					}
				}
				else
				{
					RETURN_WITH_NOTICE(OUT, iRet, -10, "wrong order type[%d]!", defOrderType);
				}
			}
		}

		if (defOrderCnt>0 && size>0)
			mqtt_saveOrderContent(orderNo, (uint8_t *)cmdData, size, defOrderCnt);
		else {
			LogDbg("unusual orderCnt[%d] or orderSize[%d]", defOrderCnt, size);
			iRet = UNUSUAL_ORDER_CNT;
		}

	OUT:
		if (cmdData) free(cmdData);
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogDbg("ZERO response!");
		return CLOUD_NO_RESPONSE;
	}
}

static int NewsampleResponseParser(const char *response)
{
	LogDbg("response[%s]", response);
	int    iRet     = 0, count = 0, orderCnt = 1, taskId = -1;
	size_t size     = 0;
	char   *cmdData = NULL;
	cJSON  *pRoot   = NULL;

	for (int i=0; i<ORDER_NUM; i++) {
		if ( glb_HttpsOrderTask.orderContent[i].status==ORDER_IDLE && glb_HttpsOrderListTask.orderList[i].status==ORDER_IDLE )
			count++;
	}

	/* StatChanged: set to 1 when previous order status uploaded
	 * C08Count: current number of realtime order
	 * RePrint: feed key double click print lastest history order
	 * RePrintListGot: lastest history order list loaded
	 * RePrintContentGot: lastest history order info loaded
	 * IdleCount: free task index in the ORDER_NUM task
	 */
	LogDbg("StatChanged[%d], C08Count[%d], RePrint[%d], RePrintListGot[%d], RePrintContentGot[%d], IdleCount[%d]", g_app_vars.isCloudOrderStatChanged, gCloudC08MsgCount, g_app_vars.isNeedReprintOldOrder, g_app_vars.isReprintListGot, g_app_vars.isReprintOldOrderContent, count);
	if( g_app_vars.isCloudOrderStatChanged && gCloudC08MsgCount<=0 && !g_app_vars.isNeedReprintOldOrder && !g_app_vars.isReprintOldOrderContent && count==ORDER_NUM && strlen(response))
	{
		if ( util_chk_json_format(response)<0 ) RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");
		pRoot = cJSON_Parse(response);
		if ( !pRoot ) RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");
		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if ( code == RESPONSE_ERROR_CODE || !msg ) RETURN_WITH_NOTICE(OUT, iRet, -3, "absence of msg or code!");
		if ( code == RESPONSE_ERROR_CODE && strlen(msg) ) RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");

		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if ( !pData ) {
			RETURN_WITH_NOTICE(OUT, iRet, -5, "absence of data!");
		} else {
			     orderCnt = cJSON_GetValueInt(pData, "orderCnt", 1);
			int  voiceCnt = cJSON_GetValueInt(pData, "voiceCnt", 1);
			int  delay    = cJSON_GetValueInt(pData, "delay", 0);
			char *voice   = cJSON_GetValueString(pData, "voice", NULL);
			if ( voice && strlen(voice) && voiceCnt>0 ) AudioPlayText(voice, voiceCnt, delay);

			cJSON *pSubInfo = cJSON_GetObjectItem(pData, "data");
			if ( pSubInfo && orderCnt>0 ) {
				FILE *fp = open_memstream(&cmdData, &size);
				if ( !fp ) RETURN_WITH_NOTICE(OUT, iRet, -8, "open_memstream fail!");
				int iCount = cJSON_GetArraySize(pSubInfo);
				if ( iCount==0 ) RETURN_WITH_NOTICE(OUT, iRet, -6, "ZERO arry!");
				for (int i=0; i<iCount; ++i) {
					cJSON *lineData = cJSON_GetArrayItem(pSubInfo, i);
					if ( lineData ) {
						char *c = cJSON_GetValueString(lineData, "c", NULL);
						if ( c && strlen(c) ) _string2Hex_(c, strlen(c), fp);
						char *d = cJSON_GetValueString(lineData, "d", NULL);
						if ( d && strlen(d) ) fwrite(d, strlen(d), 1, fp);
					}
				}
				fclose(fp);
			} else {
				RETURN_WITH_NOTICE(OUT, iRet, -7, "no data or unusual orderCnt[%d]!", orderCnt);
			}
		}

		if ( orderCnt>0 && size>0) {
			if ( (taskId = print_channel_send_data(PRINT_CHN_ID_HTTPS, (uint8_t *)cmdData, size, orderCnt) )<0) LogError("add printsample task fail!");
		} else {
			LogDbg("unusual orderCnt[%d] or orderSize[%d]", orderCnt, size);
		}

	OUT:
		if (cmdData) free(cmdData);
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		if (!strlen(response)) {
			LogError("ZERO response!"); return CLOUD_NO_RESPONSE;
		} else {
			LogError("ignore sample print task!"); return 0;
		}
	}
}

static int HttpsLBsParse(const char *response)
{
	LogDbg("response[%s]", response);
	int iRet = 0;
	cJSON *pRoot = NULL;

	if ( strlen(response) )
	{
		if (util_chk_json_format(response) < 0) RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");
		pRoot = cJSON_Parse(response);
		if (!pRoot) RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");
		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if (code == RESPONSE_ERROR_CODE || (code == RESPONSE_ERROR_CODE && msg && strlen(msg) > 0))
			RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");
		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if (!pData) {
			RETURN_WITH_NOTICE(OUT, iRet, -5, "absence of data!");
		} else {
			if ((cJSON_GetArraySize(pData)) == 0) RETURN_WITH_NOTICE(OUT, iRet, -6, "ZERO arry!");
			char *lat = cJSON_GetValueString(pData, "lat", NULL);
			char *lng = cJSON_GetValueString(pData, "lng", NULL);
			if ( lat && lng ) {
				app_set_lbs(lng, lat);
				g_app_vars.isLBsGotByHttp = 1;
				g_app_vars.isNeedSendLBsByHttp = 1;
			}
		}

	OUT:
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogDbg("ZERO response!");
		return CLOUD_NO_RESPONSE;
	}
}

static int HttpsTimestampParse(const char *response)
{
	LogDbg("response[%s]", response);
	int iRet = 0;
	cJSON *pRoot = NULL;

	if ( strlen(response) )
	{
		if (util_chk_json_format(response) < 0) RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");
		pRoot = cJSON_Parse(response);
		if (!pRoot) RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");
		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if (code == RESPONSE_ERROR_CODE || (code == RESPONSE_ERROR_CODE && msg && strlen(msg) > 0))
			RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");

		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if (!pData) {
			RETURN_WITH_NOTICE(OUT, iRet, -5, "absence of data!");
		} else {
			if ((cJSON_GetArraySize(pData)) == 0) RETURN_WITH_NOTICE(OUT, iRet, -6, "ZERO arry!");
			int timestamp = cJSON_GetValueInt(pData, "timestamp", 0);
			if ( timestamp>0 ) {
				time_t t = timestamp;
				stime(&t);
				LogDbg("time sync OK");
				g_app_vars.isSystimeSynced = 1;
			}
		}
	OUT:
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogDbg("ZERO response!");
		return CLOUD_NO_RESPONSE;
	}
}

static int HttpsGetServerTimestamp(void)
{
	if ( g_app_vars.isSystimeSynced ) return 0;
	if ( NetGetCurRoute() == NET_NONE || !sys_global_var()->cloud_url[0] ) return -1;
	LogDbg("init");
	int iRet = -1;
	CURLcode res = CURLE_OK;
	char param[1024] = {};
	char *response = NULL;
	snprintf(param, sizeof(param), "%s%s",sys_global_var()->cloud_url, CLOUD_GET_SERVER_TIMESTAMP);
	char *matchPtr = NULL;
	if ((matchPtr = strstr(param, "https"))) {
		matchPtr += strlen("https");
		snprintf(param, sizeof(param), "http%s", matchPtr);
	}
	LogDbg("param[%s]", param);
	response = HttpsPostReq(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res));
		return CURL_PERFORM_FAIL;
	}
	iRet = HttpsTimestampParse(response);
	free(response);
	return iRet;
}

static int HttpsGetLBs(void)
{
	static uint32_t tickCount = 0;
	if ( g_app_vars.isLBsGotByHttp || is_lbs_got || (SysTick()-tickCount<3*1000)) return 0;
	if ( NetGetCurRoute()==NET_NONE ) return -1;
	tickCount = SysTick();

	LogDbg("init");
	int iRet = -1;
	CURLcode res = CURLE_OK;
	char param[1024] = {};
	char *response = NULL;
	snprintf(param, sizeof(param), "%s%s", VALUE_IN_SHAREMEM(cloud_url), CLOUD_GET_LBS_WITHOUT_GPRS);
	char *matchPtr = NULL;
	if ( (matchPtr = strstr(param, "https")) ) {
		matchPtr += strlen("https");
		snprintf(param, sizeof(param), "http%s", matchPtr);
	}
	LogDbg("param[%s]", param);
	response = HttpsPostReq(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res)); return CURL_PERFORM_FAIL;
	}
	iRet = HttpsLBsParse(response);
	free(response);
	return iRet;
}

static int HttpsReportOrderStat(void)
{
	LogDbg("init");
	int  iRet = -1;
	CURLcode res = CURLE_OK;
	time_t timeStamp;
	char sign[64] = {};
	char param[1024] = {};
	char *response = NULL;

	time(&timeStamp);
	snprintf(param, sizeof(param), "%s%s%s%s%lu", VALUE_IN_SHAREMEM(cloud_token), VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), VALUE_IN_SHAREMEM(model), timeStamp*2);
	util_md5sum(param, strlen(param), sign);
	snprintf(param, sizeof(param), "%s%sparams={\"orderId\":\"%s\",\"status\":\"%d\",\"msn\":\"%s\",\"timeStamp\":\"%lu\",\"sign\":\"%s\"}",
			 VALUE_IN_SHAREMEM(cloud_url), CLOUD_ORDER_STAT_API, glb_termParm.cur_prt_order_no, glb_termParm.cur_prt_status, VALUE_IN_SHAREMEM(sn), timeStamp, sign);
	LogDbg("param[%s]", param);
	response = HTTP_REQ_CALC_TIME(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res));
		return CURL_PERFORM_FAIL;
	}

	iRet = HttpsOrderResponseParse(response);
	free(response);
	return iRet;
}

static int HttpsGetOrderId(void)
{
	LogDbg("init");
	int iRet = -1;
	CURLcode res = CURLE_OK;
	time_t timeStamp;
	char sign[64] = {};
	char param[1024] = {};
	char *response = NULL;

	time(&timeStamp);
	snprintf(param, sizeof(param), "%s%s%s%s%lu", VALUE_IN_SHAREMEM(cloud_token), VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), VALUE_IN_SHAREMEM(model), timeStamp*2);
	util_md5sum(param, strlen(param), sign);

	if (!g_app_vars.isReprintOldOrderContent)
		snprintf(param, sizeof(param), "%s%sparams={\"msn\":\"%s\",\"model\":\"%s\",\"timeStamp\":\"%lu\",\"sign\":\"%s\"}",
				 VALUE_IN_SHAREMEM(cloud_url), CLOUD_GET_ORDERID_API, VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), timeStamp, sign);
	else
		snprintf(param, sizeof(param), "%s%sparams={\"msn\":\"%s\",\"model\":\"%s\",\"timeStamp\":\"%lu\",\"sign\":\"%s\"}",
				 VALUE_IN_SHAREMEM(cloud_url), CLOUD_REGET_ORDERID_API, VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), timeStamp, sign);
	LogDbg("param[%s]", param);

	response = HTTP_REQ_CALC_TIME(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res));
		return CURL_PERFORM_FAIL;
	}

	iRet = HttpsOrderListResponseParse(response);
	free(response);
	return iRet;
}

static int HttpsGetOrderInfo(void)
{
	LogDbg("init");
	int iRet = -1;
	CURLcode res = CURLE_OK;
	time_t timeStamp;
	char sign[64] = {};
	char param[1024] = {};
	char *response = NULL;

	time(&timeStamp);
	snprintf(param, sizeof(param), "%s%s%s%s%lu", VALUE_IN_SHAREMEM(cloud_token), VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), VALUE_IN_SHAREMEM(model), timeStamp*2);
	util_md5sum(param, strlen(param), sign);
	snprintf(param, sizeof(param), "%s%sparams={\"orderId\":\"%s\",\"timeStamp\":\"%lu\",\"sign\":\"%s\"}",
			 VALUE_IN_SHAREMEM(cloud_url), CLOUD_GET_ORDERINFO_API, glb_termParm.cur_lst_order_no, timeStamp, sign);
	LogDbg("param[%s]", param);

	response = HTTP_REQ_CALC_TIME(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res));
		return CURL_PERFORM_FAIL;
	}

	iRet = HttpsOrderInfoResponseParse(response);
	free(response);
	return iRet;
}

static int SpecificOrderParser(const char *specificId, const char *response)
{
	LogDbg("id[%s], response[%s]", specificId, response);
	int iRet = 0, defOrderCnt = 1, printStatus = -1;
	size_t size = 0;
	char *cmdData = NULL;
	cJSON *pRoot = NULL;

	if (strlen(response)>0)
	{
		if (util_chk_json_format(response) < 0)
			RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");

		pRoot = cJSON_Parse(response);
		if (!pRoot)
			RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");

		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if (code == RESPONSE_ERROR_CODE || (code == RESPONSE_ERROR_CODE && msg && strlen(msg) > 0))
			RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");

		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if (!pData) {
			RETURN_WITH_NOTICE(OUT, iRet, -5, "absence of data!");
		} else {
			char *voice = cJSON_GetValueString(pData, "voice", NULL);
			char *voiceUrl = cJSON_GetValueString(pData, "voiceUrl", NULL);
			int voiceCnt = cJSON_GetValueInt(pData, "voiceCnt", 1);
			int delay = cJSON_GetValueInt(pData, "delay", 0);
			if (voiceCnt>0 && voice && strlen(voice)>0)
				AudioPlayText(voice, voiceCnt, delay);
			else if (voiceCnt>0 && voiceUrl && strlen(voiceUrl)>0)
				NewVoiceFile(voiceCnt, delay, strlen(voiceUrl)+1, voiceUrl);

			char *orderId = cJSON_GetValueString(pData, "orderId", NULL);
			if (orderId && strlen(orderId)>0) {
				if (strcmp(specificId, orderId)) {
					RETURN_WITH_NOTICE(OUT, iRet, -6, "request id[%s] response id[%s] not same!", specificId, orderId);
				}
			}

			defOrderCnt = cJSON_GetValueInt(pData, "orderCnt", 1);
			int type = cJSON_GetValueInt(pData, "type", 0);
			if (type == 1) { //this order should come from sunmi cloud order
				cJSON *pSubData = cJSON_GetObjectItem(pData, "data");
				if (pSubData) {
					int iCount = cJSON_GetArraySize(pSubData);
					if (iCount == 0) {
						RETURN_WITH_NOTICE(OUT, iRet, -7, "ZERO arry!");
					}

					FILE *fp = open_memstream(&cmdData, &size);
					if (!fp) {
						RETURN_WITH_NOTICE(OUT, iRet, -9, "open_memstream fail!");
					}

					for (int i = 0; i < iCount; ++i) {
						cJSON *lineData = cJSON_GetArrayItem(pSubData, i);
						if (lineData) {
							char *c = cJSON_GetValueString(lineData, "c", NULL);
							if (c && strlen(c)>0) _string2Hex_(c, strlen(c), fp);
							char *d = cJSON_GetValueString(lineData, "d", NULL);
							if (d && strlen(d)>0) fwrite(d, strlen(d), 1, fp);
						}
					}
					fclose(fp);
				} else {
					RETURN_WITH_NOTICE(OUT, iRet, -8, "absence of data!");
				}
			} else {
				RETURN_WITH_NOTICE(OUT, iRet, -10, "wrong order type[%d]!", type);
			}
		}

		if (defOrderCnt>0 && size>0) {
			if ((printStatus = print_channel_send_data(PRINT_CHN_ID_HTTPS, (uint8_t *)cmdData, size, defOrderCnt))<0)
				LogError("send OrderSample to printer fail");
		} else
			LogDbg("unusual orderCnt[%d] or orderSize[%d]", defOrderCnt, size);

	OUT:
		if (cmdData) free(cmdData);
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogError("ZERO response!"); return CLOUD_NO_RESPONSE;
	}
}

int GetSpecificOrderInfo(const char *orderId)
{
	LogDbg("init");
	int iRet = -1;
	CURLcode res = CURLE_OK;
	time_t timeStamp;
	char sign[64] = {};
	char param[1024] = {};
	char *response = NULL;

	time(&timeStamp);
	snprintf(param, sizeof(param), "%s%s%s%s%lu", VALUE_IN_SHAREMEM(cloud_token), VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), VALUE_IN_SHAREMEM(model), timeStamp * 2);
	util_md5sum(param, strlen(param), sign);
	snprintf(param, sizeof(param), "%s%sparams={\"orderId\":\"%s\",\"timeStamp\":\"%lu\",\"sign\":\"%s\"}",
			 VALUE_IN_SHAREMEM(cloud_url), CLOUD_GET_ORDERINFO_API, orderId, timeStamp, sign);
	LogDbg("param[%s]", param);

	response = HTTP_REQ_CALC_TIME(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res));
		return CURL_PERFORM_FAIL;
	}

	iRet = SpecificOrderParser(orderId, response);
	free(response);
	return iRet;
}

static int HttpsNewPrintSample(void)
{
	LogDbg("init");
	int  iRet = -1;
	CURLcode res = CURLE_OK;
	time_t timeStamp;
	char sign[64] = {}, param[1024];
	char *response = NULL;

	time(&timeStamp);
	snprintf(param, sizeof(param), "%s%lu", VALUE_IN_SHAREMEM(sn), timeStamp*2);
	util_md5sum(param, strlen(param), sign);
	snprintf(param, sizeof(param), "%s%sparams={\"msn\":\"%s\",\"timeStamp\":\"%lu\",\"sign\":\"%s\"}",
			 VALUE_IN_SHAREMEM(cloud_url), CLOUD_GET_NEWPRINTSAMPLE, VALUE_IN_SHAREMEM(sn), timeStamp, sign);
	LogDbg("param[%s]", param);

	response = HTTP_REQ_CALC_TIME(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res));
		return CURL_PERFORM_FAIL;
	}
	iRet = NewsampleResponseParser(response);
	free(response);
	return iRet;
}

static int AbStatusReport(const char *orderId, int orderStatus)
{
	LogDbg("init");
	int iRet = -1;
	CURLcode res = CURLE_OK;
	time_t timeStamp, timeStampDouble;
	char sign[64] = {};
	char param[1024] = {};
	char *response = NULL;

	time(&timeStamp);
	timeStampDouble = timeStamp * 2;
	snprintf(param, sizeof(param), "%s%s%s%s%lu", VALUE_IN_SHAREMEM(cloud_token), VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), VALUE_IN_SHAREMEM(model), timeStampDouble);
	util_md5sum(param, strlen(param), sign);
	snprintf(param, sizeof(param), "%s%sparams={\"orderId\":\"%s\",\"status\":\"%d\",\"msn\":\"%s\",\"timeStamp\":\"%lu\",\"sign\":\"%s\"}",
			 VALUE_IN_SHAREMEM(cloud_url), CLOUD_ORDER_STAT_API, orderId, orderStatus, VALUE_IN_SHAREMEM(sn), timeStamp, sign);
	LogDbg("param[%s]", param);
	response = HTTP_REQ_CALC_TIME(param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res));
		return CURL_PERFORM_FAIL;
	}

	iRet = HttpsOrderResponseParse(response);
	free(response);
	return iRet;
}

static int HttpsDataCollectionParse(const char *response)
{
	LogDbg("response[%s]", response);
	int iRet = 0, orderCnt, printStatus;
	size_t size = 0;
	char *cmdData = NULL;
	cJSON *pRoot = NULL;

	if (strlen(response))
	{
		if (util_chk_json_format(response) < 0) RETURN_WITH_NOTICE(OUT, iRet, -1, "not json!");

		pRoot = cJSON_Parse(response);
		if (!pRoot) RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node!");

		int code = cJSON_GetValueInt(pRoot, "code", RESPONSE_ERROR_CODE);
		char *msg = cJSON_GetValueString(pRoot, "msg", NULL);
		if (code == RESPONSE_ERROR_CODE || (code == RESPONSE_ERROR_CODE && msg && strlen(msg)))
			RETURN_WITH_NOTICE(OUT, iRet, -4, "request fail!");

		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if (!pData){
			RETURN_WITH_NOTICE(OUT, iRet, -5, "absence of data!");
		} else {
			char *voice = cJSON_GetValueString(pData, "voice", NULL);
			char *voiceUrl = cJSON_GetValueString(pData, "voiceUrl", NULL);
			int voiceCnt = cJSON_GetValueInt(pData, "voiceCnt", 0);
			int delay = cJSON_GetValueInt(pData, "delay", 0);
			if (voice && strlen(voice)>0 && voiceCnt>0)
				AudioPlayText(voice, voiceCnt, delay);
			else if (voiceUrl && strlen(voiceUrl)>0 && voiceCnt>0)
				NewVoiceFile(voiceCnt, delay, strlen(voiceUrl)+1, voiceUrl);

			orderCnt = cJSON_GetValueInt(pData, "orderCnt", 0);
			if (orderCnt > 0)
			{
				int type = cJSON_GetValueInt(pData, "type", 0);
				if (type == 1) /* orginal SUNMI order type: command & data */
				{
					cJSON *pSubData = cJSON_GetObjectItem(pData, "data");
					if (pSubData)
					{
						int iCount = cJSON_GetArraySize(pSubData);
						if (iCount == 0) RETURN_WITH_NOTICE(OUT, iRet, -7, "ZERO arry!");
						FILE *fp = open_memstream(&cmdData, &size);
						if (!fp) RETURN_WITH_NOTICE(OUT, iRet, -8, "open_memstream fail!");
						for (int i=0; i<iCount; ++i)
						{
							cJSON *lineData = cJSON_GetArrayItem(pSubData, i);
							if (lineData) {
								char *c = cJSON_GetValueString(lineData, "c", NULL);
								if (c && strlen(c)) _string2Hex_(c, strlen(c), fp);
								char *d = cJSON_GetValueString(lineData, "d", NULL);
								if (d && strlen(d)) fwrite(d, strlen(d), 1, fp);
							}
						}
						fclose(fp);
					} else {
						RETURN_WITH_NOTICE(OUT, iRet, -9, "absence of data!");
					}
				}
				else if (type == 2) /* new SUNMI order type: hexadecimal */
				{
					char *orderData = cJSON_GetValueString(pData, "orderData", NULL);
					if (orderData)
					{
						FILE *fp = open_memstream(&cmdData, &size);
						if (!fp) RETURN_WITH_NOTICE(OUT, iRet, -8, "open_memstream fail!");
						_string2Hex_(orderData, strlen(orderData), fp);
						fclose(fp);
					} else {
						RETURN_WITH_NOTICE(OUT, iRet, -9, "absence of data!");
					}
				} else {
					RETURN_WITH_NOTICE(OUT, iRet, -10, "wrong order type[%d]", type);
				}
			}
		}

		if (orderCnt>0 && size>0) {
			if ((printStatus = print_channel_send_data(PRINT_CHN_ID_HTTPS, (uint8_t *)cmdData, size, orderCnt)) < 0)
				LogError("send collection response data to printer fail");
		} else
			LogDbg("unusual orderCnt[%d] or orderSize[%d]", orderCnt, size);

	OUT:
		if (cmdData) free(cmdData);
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogError("ZERO response!");
		return CLOUD_NO_RESPONSE;
	}
}

static void _hex2string_(const uint8_t *src, int length, char *des)
{
	int i, pos = 0;
	for (i = 0; i < length; i++)
		pos += sprintf(des + pos, "%02X", src[i]);
	return;
}

static void EncodeType(char *encode, uint32_t encodeLen)
{
	char ASCII[16] = {}, CJK[16] = {}, CODEPAGE[16] = {}, UTF8[16] = {};
	if (app_setting_get("printer", "ASCII_WordSet", ASCII,    sizeof(ASCII))    &&
	    app_setting_get("printer", "CJK_WordSet",   CJK,      sizeof(CJK))      &&
	    app_setting_get("printer", "CodePage",      CODEPAGE, sizeof(CODEPAGE)) &&
	    app_setting_get("printer", "Utf8_WordSet",  UTF8,     sizeof(UTF8))) {
	    snprintf(encode, encodeLen, "%02x%02x%02x%02x", atoi(ASCII), atoi(CJK), atoi(CODEPAGE), atoi(UTF8));
	}
}

static char *HttpsDoPost(const char *url, const char *param, CURLcode *errCode)
{
	char *buff = NULL;
	size_t size = 0;
	CURL *curl = curl_easy_init();
	if (!curl) {
		*errCode = CURLE_FAILED_INIT;
		return buff;
	}

	FILE *fp = open_memstream(&buff, &size);
	if (!fp) {
		*errCode = CURLE_FAILED_INIT;
		return buff;
	}

#if 0
	/* If you want a multipart/formdata HTTP POST to be made and you instruct what data to pass on to the server in the formpost argument.
	 * Pass a pointer to a linked list of curl_httppost structs as parameter. The easiest way to create such a list, is to use curl_formadd as documented.
	 * The data in this list must remain intact as long as the curl transfer is alive and is using it.
	 * Using POST with HTTP 1.1 implies the use of a "Expect: 100-continue" header. You can disable this header with CURLOPT_HTTPHEADER.
	 * When setting CURLOPT_HTTPPOST, it will automatically set CURLOPT_NOBODY to 0.
	 * This option is deprecated! Do not use it. Use CURLOPT_MIMEPOST instead after having prepared mime data.
	 */
	struct curl_httppost *formpost = NULL, *lastptr = NULL;
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "sign",       CURLFORM_COPYCONTENTS, sign,      CURLFORM_END);
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "encode",     CURLFORM_COPYCONTENTS, encode,    CURLFORM_END);
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "msn",        CURLFORM_COPYCONTENTS, msn,       CURLFORM_END);
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "model",      CURLFORM_COPYCONTENTS, model,     CURLFORM_END);
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "timeStamp",  CURLFORM_COPYCONTENTS, timeStamp, CURLFORM_END);
	curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "print_data", CURLFORM_COPYCONTENTS, printData, CURLFORM_END);
	curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
	curl_formfree(formpost);
#endif

	struct curl_slist *header = NULL;
	header = curl_slist_append(header, "Expect: 100-continue");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	if (!param) {
		char *p = strchr(url, '?');
		if (p) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, p+1);
	} else {
		/* size of the POST param */
		//curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) strlen(param));
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, param);
	}

	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, httpWriteFunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_HEADER, 0);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000*40);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 40L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	CURLcode res = curl_easy_perform(curl);

	curl_easy_cleanup(curl);
	curl_slist_free_all(header);

	fclose(fp);
	if (size <= 0)
		*errCode = res;

	return buff;
}

static int HttpsDataCollection(const char *printData, const char *sequence, uint32_t printDataLen)
{
#define HEX_TO_STR_LEN   (printDataLen*2+4)
#define TOTAL_PARAME_LEN (printDataLen*2+1024+4)

	LogDbg("len[%d] first[0x%02X] last[0x%02X]",printDataLen,printData[0],printData[printDataLen-1]);
	int iRet = -1;
	CURLcode res = CURLE_OK;
	time_t timeStamp;
	char *response = NULL;
	char sign[64]  = {};
	char *hex2str  = (char *)malloc(HEX_TO_STR_LEN);
	char *param    = (char *)malloc(TOTAL_PARAME_LEN);

	if (hex2str) memset(hex2str, 0, HEX_TO_STR_LEN);
	if (param)   memset(param,   0, TOTAL_PARAME_LEN);

	_hex2string_((const uint8_t *)printData, printDataLen, hex2str);

	time(&timeStamp);
	sprintf(param, "%s%s%s%lu", VALUE_IN_SHAREMEM(cloud_token), VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), timeStamp*2);

	util_md5sum(param, strlen(param), sign);

	char encode[16] = {};
	EncodeType(encode, sizeof(encode));
	if (!strlen(encode)) {
		LogError("wrong encode[%d]", encode); goto OUT;
	}

	snprintf(param, TOTAL_PARAME_LEN, "sign=%s&seq=%s&encode=%s&msn=%s&model=%s&timeStamp=%lu&print_data=%s",
			 sign, sequence, encode, VALUE_IN_SHAREMEM(sn), VALUE_IN_SHAREMEM(project), timeStamp, hex2str);

	char url[256] = {};
	snprintf(url, sizeof(url), "%s%s", VALUE_IN_SHAREMEM(cloud_url), CLOUD_DATA_COLLECTION);
	LogDbg("param[%s]", param);

	response = HttpsDoPost(url, param, &res);
	if (!response) {
		LogError("curl error[%d], error string[%s]", res, curl_easy_strerror(res)); iRet = CURL_PERFORM_FAIL; goto OUT;
	}

	iRet = HttpsDataCollectionParse(response);
OUT:
	if (hex2str)   free(hex2str);
	if (param)     free(param);
	if (response)  free(response);
	return iRet;
#undef HEX_TO_STR_LEN
#undef TOTAL_PARAME_LEN
}

static void AbnormalOrderStatusHandler(const char *orderId, int status)
{
	int iRet = -1;
	if (status != CURL_PERFORM_FAIL && status != CLOUD_NO_RESPONSE) {
		if (status == UNUSUAL_ORDER_CNT) iRet = AbStatusReport(orderId, REPORT_PRINT_TASK_PRINTED);
		else iRet = AbStatusReport(orderId, status);
		LogDbg("AbStatusReport return[%d] (0 on success, other on fail)", iRet);
	}
	glb_HttpsOrderListTask.orderList[glb_HttpsOrderListTask.orderListNum].status = ORDER_IDLE;
	glb_HttpsOrderTask.orderContent[glb_HttpsOrderListTask.orderListNum].status = ORDER_IDLE;
	gMqttPrintOrderDataSignal = IDLE_STATUS_ORDER;
}

static void updateFinishedOrderID(const char *orderId)
{
	static int writeIndex = 0;
	if ( writeIndex > ARRAY_SIZE(finishedOrderID)-1 ) writeIndex = 0;
	strcpy(finishedOrderID[writeIndex], orderId);
	writeIndex++;
}

#define PRINT_TASK_FULL (-1) /* if print task full print_channel_send_data return this value */
static void PrintTaskFullHandler(int orderIdIndex)
{
	static char orderNo[64] = {};
	if (strcmp(orderNo, glb_termParm.cur_prt_order_no)) {
		snprintf(orderNo, sizeof(orderNo), "%s", glb_termParm.cur_prt_order_no);
		AbStatusReport(orderNo, REPORT_PRINT_TASK_FULL);
		glb_HttpsOrderListTask.orderList[orderIdIndex].status = ORDER_IDLE;
		glb_HttpsOrderTask.orderContent[orderIdIndex].status = ORDER_IDLE;
		gMqttPrintOrderDataSignal = IDLE_STATUS_ORDER;
		g_app_vars.isCloudOrderStatChanged = 1; /* keep the next going on normally */
	}
}

static void PrintOverResetStatus(int orderIdIndex, int print_queue_taskID)
{
	if ( orderIdIndex<0 || print_queue_taskID==0) return;
	if ( print_queue_taskID==PRINT_TASK_FULL ) {
		PrintTaskFullHandler(orderIdIndex); return;
	}

	g_app_vars.print_id_status = print_task_is_complete(print_queue_taskID);
	if ( g_app_vars.print_id_status && glb_HttpsOrderTask.orderContent[orderIdIndex].status==ORDER_BUSY ) {
		glb_HttpsOrderTask.orderContent[orderIdIndex].status = ORDER_IDLE;
		gMqttPrintOrderDataSignal = READY_PRINT_ORDER_FINISH;
		glb_termParm.cur_prt_status = 1;
		updateFinishedOrderID(glb_termParm.cur_prt_order_no);
	}
}

static void CleanupAfterStatSent(void)
{
	for (int i=0; i<ORDER_LIST_NUM; i++) {
		if (!strncmp((const char *)glb_termParm.cur_prt_order_no, (const char *)glb_HttpsOrderListTask.orderList[i].OrderNo, sizeof(glb_termParm.cur_prt_order_no))) {
			glb_HttpsOrderListTask.orderList[i].status = ORDER_IDLE; break;
		}
	}
	g_app_vars.isCloudOrderStatChanged = 1;
}

int CarryoutHttpsOptions(int HttpsOptionType)
{
	LogDbg("option[%d]", HttpsOptionType);
	int iRet = -1;
	if (HttpsOptionType < GET_ORDER_LIST || HttpsOptionType > GET_PRINT_SAMPLE) {
		LogError("invalid HttpsOptionType[%d]", HttpsOptionType);
		return iRet;
	}

	switch (HttpsOptionType)
	{
		case GET_ORDER_LIST:
		case REGET_ORDER_LIST:
			iRet = HttpsGetOrderId();
			break;
		case GET_ORDER_CONT:
			iRet = HttpsGetOrderInfo();
			if (iRet < 0) AbnormalOrderStatusHandler(glb_termParm.cur_lst_order_no, iRet);
			break;
		case SEND_ORDER_STATUS:
			iRet = HttpsReportOrderStat();
			CleanupAfterStatSent();
			break;
		case GET_PRINT_SAMPLE:
			if ((iRet = HttpsNewPrintSample()) < 0)
				LogError("HttpsNewPrintSample error code[%d]", iRet);
			break;
		default:
			LogDbg("no manu for HttpsOptionType[%d]", HttpsOptionType);
			break;
	}
	return iRet;
}

static void PrintLastHistroyOrder(void)
{
#define HISTORY_ORDER_INDEX 0
	if ( g_app_vars.isReprintListUpdate && glb_HttpsHistoryOrderTask.orderContent[HISTORY_ORDER_INDEX].orderLen>0 ) {
		AppPrintOrder(HISTORY_ORDER_INDEX);
	}
#undef HISTORY_ORDER_INDEX
}

static int OrderStatTraversal(void)
{
	int i, count = 0;
	for ( i = 0; i < ORDER_NUM; i++ ) {
		if ( glb_HttpsOrderTask.orderContent[i].status==ORDER_IDLE && glb_HttpsOrderListTask.orderList[i].status == ORDER_IDLE) {
			count++;
		}
	}
	return ORDER_NUM==count?0:1;
}

static int GetRealtimeOrderInfo(int orderProcessing)
{
	if ( g_app_vars.print_id_status || (orderProcessing==0 && g_app_vars.isReprintOldOrderContent && g_app_vars.isCloudOrderStatChanged) )
		return HttpsSendReqDownOrderCont();
	return 0;
}

static void NormalSendOrderStat(int print_queue_taskID)
{
	if ( (print_queue_taskID>0 && g_app_vars.print_id_status) || gMqttPrintOrderDataSignal==READY_PRINT_ORDER_FINISH )
		HttpsSendPrtFinishResult();
}

int updateRealtimeOrderCount(int cmd)
{
	pthread_mutex_lock(&cloud_order_lock);
	if (cmd == 0) {
		gCloudC08MsgCount = 0;
		orderListFailTimes = 0; /* reset http fail time to ZERO (no order or network error) */
		LogInfo("[RESET C08 COUNT TO ZERO]");
	} else if (cmd == 1) {
		gCloudC08MsgCount += 1;
		LogInfo("[SET C08 COUNT TO %d]", gCloudC08MsgCount);
	} else
		LogError("error cmd[%d]!", cmd);
	pthread_mutex_unlock(&cloud_order_lock);

	return 0;
}

static int GetRealtimeOrderId(int orderProcessing)
{
	static uint32_t timestamp = 0; /* timestamp of getting order list failed */
	if ( !is_mqtt_alive() ) return -1;
	if ( orderListFailTimes>HTTPS_RETRY_TIMES ) updateRealtimeOrderCount(0);
	if ( orderProcessing==0 && (SysTick()-timestamp>HTTPS_RETRY_TIMEGAP) && g_app_vars.isCloudOrderStatChanged && gCloudC08MsgCount>0 )
	{
		g_app_vars.isReprintOldOrderContent = 0;
		g_app_vars.isNeedReprintOldOrder = 0;

		int iRet = HttpsSendReqDownOrderList();
		if ( iRet ) {
			timestamp = SysTick();
			orderListFailTimes++;
			return iRet;
		} else {
			timestamp = 0;
		}
	}
	return 0;
}

static void GetHistoryOrderId(int orderProcessing)
{
	if ( !is_mqtt_alive() ) return;
	if ( orderProcessing==0 && gCloudC08MsgCount<=0 && g_app_vars.isCloudOrderStatChanged && g_app_vars.isNeedReprintOldOrder && !g_app_vars.isReprintListGot )
	{
		g_app_vars.isReprintOldOrderContent = 1;
		if ( CarryoutHttpsOptions(REGET_ORDER_LIST) ) cleanHistoryOrderMsg();
	}
}

static void WnetInfoReport(void)
{
	if (g_app_vars.isMqttFirstConnected) return;
	mqtt_report_rightnow();
	g_app_vars.isMqttFirstConnected = 1;
}

static void PrinteDistanceUpdate(void)
{
    app_setting_set_int(APP_SETTING_PRINT_DISTANCE, "latest", gt_printerStatus.print_distance);
}

static void CertainTimeReport(void)
{
#define REPORT_INTERVAL_TIME (12*3600*1000)
#define UPDATE_INTERVAL_TIME (1*3600*1000)
	static uint32_t tick_report = 0, tick_update = 0;
	if ( (SysTick()-tick_report>REPORT_INTERVAL_TIME) || !tick_report ) {
		tick_report = SysTick();
		mqtt_publishTopic(UP_INTERVAL, NULL);
	} else if ((SysTick()-tick_update>UPDATE_INTERVAL_TIME)) {
        tick_update = SysTick();
        PrinteDistanceUpdate();
    }
#undef REPORT_INTERVAL_TIME
#undef UPDATE_INTERVAL_TIME
}

static void RealtimeStatReport(void)
{
	if ( ( gt_printerStatus.no_paper && g_app_vars.isNoPaperCleard ) || gCashboxOpentimes || g_app_vars.isNeedSendLBsByHttp ) {
		mqtt_publishTopic(UP_RIGHTNOW, NULL);
	}
}

static void SendBuff2PrintQueue(int *orderIdIndex, int *print_queue_taskID)
{
	int index = _WaitingOrderTask_();
	if (index == -1) ;
	else {
		*orderIdIndex = index;
		*print_queue_taskID = AppPrintOrder(index);
	}
}

static void *IOTWork_thread(void *arg)
{
	LogDbg("init");

	int iRet, orderProcessing = 0;
	static int print_queue_taskID = 0, orderIdIndex = -1;

	while(1)
	{
		/* 1.同步服务器时间 */
		if (HttpsGetServerTimestamp()<0) {
			sleep(3); continue;
		}

		/* 2.获取设备经纬度 */
		HttpsGetLBs();

		/* 3.定时 & 即时信息上报*/
		if (is_mqtt_alive()) {
			WnetInfoReport();
			CertainTimeReport();
			RealtimeStatReport();
		}

		/* 4.遍历历史订单状态 */
		orderProcessing = OrderStatTraversal();

		/* 5.获取近ORDER_NUM张历史订单号 */
		GetHistoryOrderId(orderProcessing);

		/* 6.获取实时订单列表 */
		if (GetRealtimeOrderId(orderProcessing)<0) {
			SysDelay(200); continue;
		}

		/* 7.请求订单内容 */
		iRet = GetRealtimeOrderInfo(orderProcessing);
		if (iRet==CURL_PERFORM_FAIL || iRet==CLOUD_NO_RESPONSE) {
			SysDelay(200); continue;
		}

		/* 8.查询订单是否打印完成(打印完成需要上报并恢复状态) */
		PrintOverResetStatus(orderIdIndex, print_queue_taskID);

		/* 9.查询print_queue中task是否完成 */
		if (print_queue_taskID > 0 && !g_app_vars.print_id_status) {
			SysDelay(100); continue;
		}

		/* 10.订单状态上报 */
		NormalSendOrderStat(print_queue_taskID);

		/* 11.订单添加至打印队列 */
		SendBuff2PrintQueue(&orderIdIndex, &print_queue_taskID);

		/* 12.补充打印最新历史订单数据 */
		PrintLastHistroyOrder();

		SysDelay(100);
	}

	LogDbg("IOTWork_thread exit now");
	pthread_exit(0);
}

static int _nameFilter_(const struct dirent *d)
{
	char *p = strrchr(d->d_name, '.');
	if ( p && ( !strcmp(p, ".usb") ||
	            !strcmp(p, ".ble") ||
	            !strcmp(p, ".spp") ||
	            !strcmp(p, ".tcp") ||
	            !strcmp(p, ".serial") ))
		return 1;
	return 0;
}

static void DoMemoryCollection(void)
{
	int iRet = 0;
	char sequence[64]  = {};
	char filename[128] = {}; /*filename rule: unix timestamp amend a rand number(0~9): 15906384838*/
	struct dirent **namelist;
	int n = scandir(MEM_COLLECTION_DIR, &namelist, _nameFilter_, alphasort);
	for ( int i=0; i<n; i++ )
	{
		snprintf(filename, sizeof(filename), ""MEM_COLLECTION_DIR"%s", namelist[i]->d_name);
		struct stat st = {};
		if ( stat(filename, &st) ) {
			LogWarn("get %s stst fail!", filename);
			goto OUT;
		}

		if ( st.st_size <=0 ) {
			LogWarn("%s size zero do unlink!", filename);
		} else {
			size_t size;
			size = st.st_size >= DATA_COLLECTION_LIMIT?DATA_COLLECTION_LIMIT:st.st_size;
			FILE *fp = fopen(filename, "rb");
			if (!fp) {
				LogError("fopen %s fail!", filename);
			} else {
				char *buff = (char *)malloc(size);
				if (!buff) {
					LogError("malloc fail!");
				} else {
					size_t len = fread(buff, size, 1, fp);
					if (len <= 0) {
						LogError("fread %s fail!", filename);
					} else {
						char *p = strrchr(namelist[i]->d_name, '.');
						if (p) {
							snprintf(sequence, sizeof(sequence), "%.*s", p-namelist[i]->d_name, namelist[i]->d_name);
							iRet = HttpsDataCollection(buff, sequence, size);
						}
					}
					free(buff);
				}
				fclose(fp);
			}
		}
	OUT:
		if ( iRet==0 ) {
			unlink(filename);
		} else {
			UtilRunCmd("mv %s %s", filename, FLASH_COLLECTION_DIR);
		}
		free(namelist[i]);
	}
	if (namelist) free(namelist);
}

static void DoFlashCollection(void)
{
	if ( NetGetCurRoute()== NET_NONE && !is_mqtt_alive())
		return;

	int iRet = 0;
	char sequence[64]  = {};
	char filename[128] = {}; /*filename rule: unix timestamp amend a rand number(0~9): 15906384838*/
	struct dirent **namelist;
	int n = scandir(FLASH_COLLECTION_DIR, &namelist, _nameFilter_, alphasort);
	for ( int i=0; i<n; i++ )
	{
		snprintf(filename, sizeof(filename), ""FLASH_COLLECTION_DIR"%s", namelist[i]->d_name);
		struct stat st = {};
		if ( stat(filename, &st) ) {
			LogWarn("get %s stst fail!", filename);
			goto OUT;
		}

		if ( st.st_size <=0 ) {
			LogWarn("%s size zero do unlink!", filename);
		} else {
			size_t size;
			size = st.st_size >= DATA_COLLECTION_LIMIT?DATA_COLLECTION_LIMIT:st.st_size;
			FILE *fp = fopen(filename, "rb");
			if (!fp) {
				LogError("fopen %s fail!", filename);
			} else {
				char *buff = (char *)malloc(size);
				if (!buff) {
					LogError("malloc fail!");
				} else {
					size_t len = fread(buff, size, 1, fp);
					if (len <= 0) {
						LogError("fread %s fail!", filename);
					} else {
						char *p = strrchr(namelist[i]->d_name, '.');
						if (p) {
							snprintf(sequence, sizeof(sequence), "%.*s", p-namelist[i]->d_name, namelist[i]->d_name);
							iRet = HttpsDataCollection(buff, sequence, size);
						}
					}
					free(buff);
				}
				fclose(fp);
			}
		}
	OUT:
		if ( iRet==0 ) {
			unlink(filename);
		}
		if (namelist[i]) free(namelist[i]);
	}
	if (namelist) free(namelist);
}

#define MyPopen(fmt,args...) ({ \
	char cmd[256]; \
	snprintf(cmd,sizeof(cmd),fmt,##args); \
	FILE *_fp = popen(cmd,"r"); \
	_fp; \
})

static int DataFlashRemainSize(struct statfs *stat)
{
	int iRet = -1;
	if (!stat) return iRet;
	if ( (iRet = statfs("/data/", stat)) ) {
		LogError("get data partition accur error %s", strerror(errno));
		return iRet;
	}
	return (stat->f_bfree*stat->f_bsize)>>10;//unit:KB
}

static int DeviceFlashType(void) {
/*********************************
 * mtd block : partition  : size *
 * mtdblock0 : boot       : 1MB  *
 * mtdblock1 : hwinfo     : 1MB  *
 * mtdblock2 : kernerl    : 4MB  *
 * mtdblock3 : kernel_bak : 4MB  *
 * mtdblock4 : rootfs     : 8MB  *
 * mtdblock5 : rootfs_bak : 8MB  *
 * mtdblock6 : homefs     : 4MB  *
 * mtdblock7 : homefs_bak : 4MB  *
 *********************************/
#define FIXED_FILE_SYSTEM_SIZE 34  //MB
#define SMALL_FLASH_TYPE       128 //MB
#define MIDDLE_FLASH_TYPE      256 //MB
#define LARGE_FLASH_TYPE       512 //MB
	size_t data_size = 0, total_size = 0;
	FILE *fp = MyPopen("df /dev/ubi0_0 -h | grep data");
	if (fp) {
		char buff[256];
		while ( fgets(buff, sizeof(buff), fp) ) {
			sscanf(buff, "/dev/ubi0_0 %dM", &data_size);
		}
		pclose(fp);
	}
	total_size = data_size+FIXED_FILE_SYSTEM_SIZE;
	return (total_size>450)?LARGE_FLASH_TYPE:((total_size>200)?MIDDLE_FLASH_TYPE:SMALL_FLASH_TYPE);
}

static uint16_t FlashReservationInfo(int flashType) {
#define RESULT(t,d) { \
	LogDbg("[%dMB flash type | %dMB for data collection | rest %dMB for ota & resource]", \
	        t,d,t-d-FIXED_FILE_SYSTEM_SIZE); \
	return d; }

	switch(flashType) {
		case SMALL_FLASH_TYPE : RESULT(flashType, 10);  //10MB  flash for collection
		case MIDDLE_FLASH_TYPE: RESULT(flashType, 100); //100MB flash for collection
		case LARGE_FLASH_TYPE : RESULT(flashType, 350); //350MB flash for collection
		default:                RESULT(flashType, 0);   //0MB   flash for collection
	}
#undef RESULT
#undef FIX_FILE_SYSTEM_SIZE
#undef SMALL_FLASH_TYPE
#undef MIDDLE_FLASH_TYPE
#undef LARGE_FLASH_TYPE
}

static void CollectionFlashKeeper(uint16_t size_for_collection)
{
#define RESERVE_FLASH_FOR_OTA (30*1024) //reserve 30MB for OTA at least
	int size = 0; //unit:KB
	FILE *fp = MyPopen("du -k %s", FLASH_COLLECTION_DIR); //take f_bsize:4KB into consideration
	if ( fp ) {
		char buff[256];
		while ( fgets(buff, sizeof(buff), fp) ) {
			sscanf(buff, "%d ", &size);
		}
		pclose(fp);
	}
	if ( size<=0 ) return;
	struct statfs statfs = {};
	int difference = DataFlashRemainSize(&statfs)-RESERVE_FLASH_FOR_OTA;

	while ( difference<0 ||
		(size>0 && size>=(size_for_collection<<10)) ||
		(difference>0 && size>difference) ) {
		char filename[128] = {};
		struct dirent **namelist;
		int n = scandir(FLASH_COLLECTION_DIR, &namelist, _nameFilter_, alphasort);
		for ( int i=0; i<n; i++ )
		{
			snprintf(filename, sizeof(filename), ""FLASH_COLLECTION_DIR"%s", namelist[i]->d_name);
			struct stat st = {};
			if ( stat(filename, &st) ) {
				LogWarn("get %s stst fail!", filename);
				if (namelist[i]) free(namelist[i]);
				continue;
			}
			unlink(filename);
			if ( st.st_size<=0 ) {
				LogWarn("%s size error do unlink!", filename);
			} else {
				LogDbg("BEFORE:{%s dir takes[%d KB]}", FLASH_COLLECTION_DIR, size);
				uint32_t releaseKB = ((st.st_size-1)/statfs.f_bsize+1)*statfs.f_bsize;
				size -= releaseKB;
				difference = DataFlashRemainSize(&statfs)-RESERVE_FLASH_FOR_OTA;
				LogDbg("NOW   :{%s dir takes[%d KB]}", FLASH_COLLECTION_DIR, size);
			}
			if (namelist[i]) free(namelist[i]);
			break;
		}

		if (namelist) free(namelist);
	}
#undef RESERVE_FLASH_FOR_OTA
}

static void *collection_thread(void *arg)
{
	LogDbg("init");
	mkdir(MEM_COLLECTION_DIR,   0777);
	mkdir(FLASH_COLLECTION_DIR, 0777);
	uint16_t size_for_collection = FlashReservationInfo(DeviceFlashType());
	while (1)
	{
		CollectionFlashKeeper(size_for_collection);
		DoMemoryCollection();
		DoFlashCollection();
		SysDelay(200);
	}

	LogDbg("collection_thread exit now");
	pthread_exit(0);
}

void collection_init(void)
{
	if ( VALUE_IN_SHAREMEM(data_report) )
	{
		set_data_collection_state(app_setting_get_int("switch", "data_report", 1));
		pthread_t tid;
		if (pthread_create(&tid, NULL, collection_thread, NULL)) {
			LogError("collection_thread create fail"); return;
		}
		pthread_detach(tid);
	}
}

void iot_init(void)
{
	pthread_t tid;
	if (pthread_create(&tid, NULL, IOTWork_thread, NULL)) {
		LogError("IOTWork_thread create fail"); return;
    }
	pthread_detach(tid);
}
