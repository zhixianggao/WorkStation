#ifndef __APP_GLBVAR_H__
#define __APP_GLBVAR_H__

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define PRINTER_BAD_POINT_DET_PARA_LEN    8

/*----------------------------------------------*
 | 结构体定义                                   |
 *----------------------------------------------*/
typedef struct {
	unsigned char isNeedReprintOldOrder   : 1;  //用于判断是否需要进行历史订单打印
	unsigned char isReprintOldOrderContent: 1;  //用于判断当前是否是处于组合键进行重复打印历史订单中
	unsigned char isReprintListGot        : 1;  //用于判断历史订单列表是否已经获取到
	unsigned char isReprintListUpdate     : 1;  //用于判断历史订单列表是否更新了一遍
	unsigned char isCloudOrderStatChanged : 1;  //打印完一张订单后，状态上送云端后，云端状态有没有改变
	unsigned char isGotNetOrder           : 1;  //用于判断工厂测试是否获取到云端C09 hasTest
	unsigned char isBurnInTest            : 1;  //老化测试标志位
	unsigned char isLBsGotByHttp          : 1;  //通过HTTP方式获取到经纬度：0 未获取，1 已获取
	unsigned char isNeedSendLBsByHttp     : 1;  //通过HTTP方式获取到经纬度是否需要上报
	unsigned char isNoPaperCleard         : 1;  //打印机缺纸状态恢复
	unsigned char print_id_status         : 1;  //当前print_queue中处理的任务状态,0-未完成 1-完成
	unsigned char isPaperFeeding          : 1;
	unsigned char isMqttRwOK              : 1;
	unsigned char isSystimeSynced         : 1;
	unsigned char isMqttFirstConnected    : 1;
}app_global_var_t;
extern app_global_var_t g_app_vars;

/*----------------------------------------------*
 | 可供外部使用全局变量                          |
 *----------------------------------------------*/
extern uint32_t gSdkInBucks;
extern uint32_t gCashboxOpentimes;     //此次需要上报的钱箱打开次数由云端做累加
extern uint32_t gCloudC08MsgCount;     //云端下发的C08消息计数
extern uint8_t gNetStat;


/*----------------------------------------------*
 | 可供外部使用函数                              |
 *----------------------------------------------*/
void init_glbvar(void);

#endif /* __GLBVAR_H__ */
