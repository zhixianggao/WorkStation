#ifndef __APP_MQTT_H__
#define __APP_MQTT_H__

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
//pack type
#define UP_DEV_STATUS         1  //开机连网成功后上报数据 s01
#define UP_NET_TYPE           2  //网络连接成功或者切换网络时上报数据 s02
#define UP_INTERVAL           3  //定时上报数据 s03
#define UP_RIGHTNOW           4  //即时上报数据 s04
#define UP_UPGRADE_STATUS     8  //打印机升级后上报升级状态   s08
#define UP_SUPPLEMENT_STATUS  9  //补打订单打印完成后上报状态 s09

//unpack type
#define PRINT_CERTAIN_ORDER       101  // 指定历史订单消息 c01
#define DOWNLOAD_ORDER_CONT       102  // 下载订单内容 c02
#define DOWNLOAD_TTS_STR          103  // 下载语音播报数据内容 c03
#define OTA_FILE_UPGRADE          104  // OTA文件更新 c04
#define DOWNLOAD_TIME             105  // 服务器端时间戳，用于云打印机同步时间戳(服务器时间下送) c05
#define CONFIG_CLOUD_PARAM        106  // 参数配置数据 c06
#define DEV_RESTART               107  // 设备重启 c07
#define SUNMI_HAVE_ORDER_LIST     108  // 触发终端向服务器请求订单列表 c08 (针对挂靠商米云后台的商家订单)
#define HAVE_NEW_PRINT_SAMPLE     109  // 有新打印测试页 c09
#define PARTNER_HAVE_ORDER_LIST   110  // 触发终端向服务器请求订单列表 c10(针对挂靠第三方合作伙伴后台的商家订单)
#define PARTNER_PLATFORM_PARAM    111  // 第三方合作伙伴配置参数 c11
#define PARTNER_PLATFORM_PARAM_FILE    "/data/PUB/partnerParam"    //第三方合作伙伴配置参数文件

// 打印同步通知信号
#define IDLE_STATUS_ORDER           0 // 主线程和MQTT不处理订单打印和下载
#define READY_DOWN_ORDER_LIST       1 // MQTT请求下载订单列表
#define READY_DOWN_ORDER_CONT       2 // 主线程打印过程中通知MQTT去下载订单内容
#define READY_PRINTING_ORDER        3 // MQTT下载完成订单内容，打印订单中
#define READY_PRINT_ORDER_FINISH    4 // 打印完成
#define ORDER_PER_LIST              5 // 每张订单列表中订单数

#define MQTT_CONNECT_FAIL 0
#define MQTT_CONNECT_SUCC 1

/*----------------------------------------------*
 | 供外部引用                                   |
 *----------------------------------------------*/
extern int gMqttOrderDataSignal;      // 此值用于下载控制
extern int gMqttPrintOrderDataSignal; // 此值用于打印控制

/*----------------------------------------------*
 | 结构体定义                                   |
 *----------------------------------------------*/
typedef struct
{
    char mcc[8];
    char mnc[8];
    char isp[48];
    char type[32];
    char apn[64];
    char dial[64];
    char ver[128];
	char imei[32];
    char cpsi[256];
	char imsi[WNET_IMSI_MAX_LEN+1];
	char iccid[WNET_ICCID_MAX_LEN+1];
} __attribute__((packed)) Network_Info_t;

typedef struct
{
    int cur_prt_status;        // 送入print_queue后的订单状态
    char cur_lst_order_no[64]; // 送入print_queue前的订单号
    char cur_prt_order_no[64]; // 送入print_queue后的订单号
} TRANS_TERM_PARAM;

extern TRANS_TERM_PARAM glb_termParm;

/*----------------------------------------------*
 | 外部引用                                     |
 *----------------------------------------------*/
int mqtt_getOrderId(char *OrderNo);
int mqtt_getHistoryOrderId(char *OrderNo);
int mqtt_publishTopic(int type, void *topicbuf);
int mqtt_saveOrderId(const char *OrderId, int len);
int mqtt_saveHistoryOrderId(const char *OrderId, int len);
int mqtt_saveOrderContent(const uint8_t *OrderId, const uint8_t *OrderContent, int orderLen, int orderCnt);

void mqtt_thread_init(void);
void mqtt_report_rightnow(void);
void update_sdk_in_bucks_count(uint8_t option);
void mqtt_report_trade_bindstatus(uint8_t bindStatus);


#endif /* __APP_MQTT_H__ */