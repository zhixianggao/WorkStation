#include "project_includer.h"
#include "partner.h"


/*----------------------------------------------*
 | 静态变量                                     |
 *----------------------------------------------*/
static usb_bt_data_t g_usb_data={},g_bt_data={};
static pthread_mutex_t g_mutex_usbbt_data = PTHREAD_MUTEX_INITIALIZER;

/*----------------------------------------------*
 | 结构体定义                                    |
 *----------------------------------------------*/
typedef struct {
	int cur_value;
	int *addr;
} status_value_info_t;

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
void AMServer_Notify_USBData(void *data,int datalen)
{
	libam_handler()->notify(AM_NOTIFY_TYPE_USBDATA,data,datalen);
	pthread_mutex_lock(&g_mutex_usbbt_data);
	g_usb_data.data = realloc(g_usb_data.data,datalen);
	if( g_usb_data.data ) {
		memcpy(g_usb_data.data,data,datalen);
		g_usb_data.len = datalen;
	}
	pthread_mutex_unlock(&g_mutex_usbbt_data);
}

void AMServer_Notify_BTData(void *data,int datalen)
{
	libam_handler()->notify(AM_NOTIFY_TYPE_BTDATA,data,datalen);
	pthread_mutex_lock(&g_mutex_usbbt_data);
	g_bt_data.data = realloc(g_bt_data.data,datalen);
	if( g_bt_data.data ) {
		memcpy(g_bt_data.data,data,datalen);
		g_bt_data.len = datalen;
	}
	pthread_mutex_unlock(&g_mutex_usbbt_data);
}

static void *am_print_task_stat_check(void *args)
{
	pthread_detach(pthread_self());
	int iRet = -1;

	while(1)
	{
		if ((iRet = print_task_is_complete((int)args)) == 1) {
			libam_handler()->notify(AM_NOTIFY_TYPE_ORDER_FINISH, &iRet, sizeof(int));
			break;
		}

		SysDelay(10);
		continue;
	}
	pthread_exit(0);
}

static int am_service_msgproc(unsigned short cmd, const void *inbuf, int inlen, void *outbuf, int outlen)
{
	int len=0;
	switch(cmd) {
		case AM_CMD_PRINT_DATA:
			LogDbg("don't use this command, use AM_CMD_HANDLE_ORDER instead!");
			break;
		case AM_CMD_GET_USB_DATA:
			pthread_mutex_lock(&g_mutex_usbbt_data);
			if( g_usb_data.data && g_usb_data.len>0 ) {
				len = g_usb_data.len>outlen?outlen:g_usb_data.len;
				memcpy(outbuf,g_usb_data.data,len);
			}
			pthread_mutex_unlock(&g_mutex_usbbt_data);
			break;
		case AM_CMD_GET_BT_DATA:
			pthread_mutex_lock(&g_mutex_usbbt_data);
			if( g_bt_data.data && g_bt_data.len>0 ) {
				len = g_bt_data.len>outlen?outlen:g_bt_data.len;
				memcpy(outbuf,g_bt_data.data,len);
			}
			pthread_mutex_unlock(&g_mutex_usbbt_data);
			break;
		case AM_CMD_GET_STATUS:
			if (!outbuf || outlen!=sizeof(gt_printerStatus)) {
				//LogError("AM_CMD_GET_STATUS param error!");
			} else {
				memcpy(outbuf, &gt_printerStatus, outlen);
			}
			break;
		case AM_CMD_PLAY_VOICE:
			if(inbuf) {
				T_orderInfo *partnerOrder = (T_orderInfo *)inbuf;
				AudioPlayText((const char *)partnerOrder->orderData, partnerOrder->voiceCnt, partnerOrder->voiceDelay);
			} else {
				len = AM_ERR_PARAM;
			}
			break;
		case AM_CMD_GET_DEV_INFO:
			LogDbg("AM_CMD_GET_DEV_INFO, outlen=%d", outlen);
			if( outbuf && outlen >= sizeof(T_PartnerParam) ){
				T_PartnerParam *partner = (T_PartnerParam *)outbuf;
				GetPartnerParam(partner);
				len = sizeof(T_PartnerParam);
			} else {
				len = AM_ERR_PARAM;
			}
			break;
		case AM_CMD_SEND_SELF_INFO:
			LogDbg("AM_CMD_SEND_SELF_INFO, inlen=%d", inlen);
			break;
		case AM_CMD_HANDLE_ORDER:
			LogDbg("AM_CMD_HANDLE_ORDER, inlen=%d", inlen);
			if (inbuf && inlen >= sizeof(T_orderInfo))
			{
				T_orderInfo *partnerOrder = (T_orderInfo *)inbuf;
				if (partnerOrder->voiceLength>1 && partnerOrder->voiceCnt>0) {
					AudioPlayText((const char *)partnerOrder->orderData, partnerOrder->voiceCnt, partnerOrder->voiceDelay);
				} else if (partnerOrder->voiceUrlLength>5 && partnerOrder->voiceCnt>0) { /* dest file have *.mp3 or *.wav five characters at least */
					NewVoiceFile(partnerOrder->voiceCnt, partnerOrder->voiceDelay, partnerOrder->voiceUrlLength, partnerOrder->orderData);
				}
				if (partnerOrder->hex2str>0 && partnerOrder->orderCnt) {
					int ret = print_channel_send_data(PRINT_CHN_ID_AM, (const uint8_t *)partnerOrder->orderData + partnerOrder->voiceLength, partnerOrder->hex2str, partnerOrder->orderCnt);
					int *value = (int *)outbuf;
					if (ret>0) { /* normal print task ID */
						pthread_t tid;
						if (pthread_create(&tid, NULL, am_print_task_stat_check, (void *)ret) < 0) {
							LogError("pthread_create am_print_task_stat_check failed");
							*value = -1;
						} else *value = 0;
					} else if (ret == -1) { /* print queue task full */
						LogDbg("print queue task is full!!!");
						*value = -2;
					} else {
						LogError("unexcepted return value[%d] from print_channel_send_data", ret);
						*value = -3;
					}
				}
			} else {
				len = AM_ERR_PARAM;
			}
			break;
		default:
			len = AM_ERR_UNSUPPORT;
			break;
	}
	return len;
}

static int readFile2Buf(void **buf,const char *filename)
{
	int size = 0;

	*buf = NULL;
	FILE *fp = fopen(filename,"rb");
	if( !fp ) {
		LogDbg("fopen(%s) fail,%s",filename,strerror(errno));
		return 0;
	}

	fseek(fp,0,SEEK_END);
	size = ftell(fp);
	rewind(fp);
	if( size>0 ) {
		*buf = malloc(size);
		if( fread(*buf,size,1,fp)<=0 ) {
			LogDbg("fread(%s) fail,%s",filename,strerror(errno));
			free(*buf);
			*buf = NULL;
			size = 0;
		}
	}
	fclose(fp);
	return size;
}

void am_service_start()
{
	libam_handler()->server_init(am_service_msgproc);
}
