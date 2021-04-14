#ifndef __IOTAENTRANCE_H__
#define __IOTAENTRANCE_H__


/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
/* HTTPS CMD */
#define GET_ORDER_LIST     1  //获取订单列表
#define GET_ORDER_CONT     2  //获取订单详情
#define REGET_ORDER_LIST   3  //获取历史订单号列表
#define SEND_ORDER_STATUS  4  //订单处理状态上报
#define GET_PRINT_SAMPLE   5  //获取订单样张
#define POST_PRINT_DATA    6  //采集打印机数据

/* ORDER ID/CONTENT STATUS */
#define ORDER_IDLE   0  // 订单空闲或处理完成
#define ORDER_READY  1  // 下载到本地未使用
#define ORDER_BUSY   2  // 下载到本地处理中

/* NUMBER */
#define HTTPS_RETRY_TIMES 10 // HTTP重试次数
#define ORDER_LIST_NUM    5  // 可存储订单数
#define ORDER_NUM         5  // 可存储订单数

/* TIMEGAP */
#define HTTPS_RETRY_TIMEGAP         (10*1000)        //HTTPS请求失败后隔10秒后重试

/* HTTPS API*/
#define CLOUD_ORDER_STAT_API       "/printTicket/updatePrintTicketStatus?"
#define CLOUD_GET_ORDERID_API      "/printTicket/getPrintTicketOrderId?"
#define CLOUD_REGET_ORDERID_API    "/printTicket/regetPrintTicketOrderId?"
#define CLOUD_GET_ORDERINFO_API    "/printTicket/getPrintTicketInfo?"
#define CLOUD_GET_NEWPRINTSAMPLE   "/printTicket/getPrinterTest?"
#define CLOUD_DATA_COLLECTION      "/dataUpload/postPrintData"
#define CLOUD_GET_SERVER_TIMESTAMP "/check/getSystemTime"
#define CLOUD_GET_LBS_WITHOUT_GPRS "/check/getGeoIpInfo"

/* HTTPS RESPONSE ERROR CODE */
#define CURL_OK_WITH_RESPONSE      0
#define REPORT_PRINT_TASK_PRINTED  1     /* print queue 中的任务已经打印完，此值用于上报后台 */
#define RESPONSE_ERROR_CODE       (-1)
#define REPORT_PRINT_TASK_FULL    (-66)  /* print queue 中的任务已经满了，此值用于上报后台 */
#define CURL_PERFORM_FAIL         (-88)
#define CLOUD_NO_RESPONSE         (-99)
#define UNUSUAL_ORDER_CNT         (-111) /* 订单数据中orderCnt字段的值小于等于0时，也以 REPORT_PRINT_TASK_PRINTED 上报后台 */

/*----------------------------------------------*
 | 结构体定义                                   |
 *----------------------------------------------*/
typedef struct
{
	int status;
	unsigned char OrderNo[64];
}HTTPS_ORDER_LIST;

typedef struct
{
	int orderListNum;
	HTTPS_ORDER_LIST orderList[ORDER_LIST_NUM];
}HTTPS_ORDER_LIST_TASK;

typedef struct orderInfo
{
	int orderCnt;
	int voiceCnt;
	int voiceLength;
	int voiceUrlLength;
	int voiceDelay; //delay = (voiceDelay * 1000)ms
	int hex2str;	//hex to str bytes
	char orderData[];
} T_orderInfo, *PT_orderInfo;

typedef struct PartnerOrder
{
	int orderStatus;
	char orderID[64];
	unsigned int orderSize;
	PT_orderInfo orderInfo;
	void *next;
} T_PartnerOrder, *PT_PartnerOrder;

typedef struct
{
	int status;
	int orderLen;
	int orderCnt;
	char OrderNo[64];
	unsigned char *printData;
} HTTPS_ORDER;

typedef struct
{
	int orderNum;
	HTTPS_ORDER orderContent[ORDER_NUM];
}HTTPS_ORDER_TASK;


/*----------------------------------------------*
 * 可供外部使用函数                             |
 *----------------------------------------------*/
void iot_init(void);
void collection_init(void);

int DldCertainAudio(void *arg);
int updateRealtimeOrderCount(int cmd);
int GetSpecificOrderInfo(const char *orderId);
int CarryoutHttpsOptions(int HttpsOptionType);

#endif /* __IOTAENTRANCE_H__ */