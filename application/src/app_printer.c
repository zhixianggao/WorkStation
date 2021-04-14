#include "project_includer.h"

/*----------------------------------------------*
 | 引用外部变量                                 |
 *----------------------------------------------*/
extern HTTPS_ORDER_LIST_TASK glb_HttpsHistoryOrderListTask; //历史订单列表任务
extern HTTPS_ORDER_TASK glb_HttpsOrderTask;                 //实时打印任务
extern HTTPS_ORDER_TASK glb_HttpsHistoryOrderTask;          //历史打印任务

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
void cleanHistoryOrderMsg(void)
{
    for (int i = 0; i < ORDER_NUM; i++) {
        glb_HttpsHistoryOrderListTask.orderList[i].status = ORDER_IDLE;
    }
    g_app_vars.isReprintOldOrderContent = 0;
    g_app_vars.isNeedReprintOldOrder    = 0;
    g_app_vars.isReprintListUpdate      = 0;
    g_app_vars.isReprintListGot         = 0;
}

int AppPrintOrder(int orderIndex)
{
    if (orderIndex < 0) return -1;

    int print_queue_taskID = 0;
    if ( !g_app_vars.isReprintOldOrderContent ) /* normal mode */
    {
        if ( gMqttPrintOrderDataSignal==READY_PRINTING_ORDER )
        {
            gMqttOrderDataSignal = READY_DOWN_ORDER_CONT;
            gMqttPrintOrderDataSignal = IDLE_STATUS_ORDER;
            glb_HttpsOrderTask.orderContent[orderIndex].status = ORDER_BUSY;
            snprintf(glb_termParm.cur_prt_order_no, sizeof(glb_termParm.cur_prt_order_no), "%s", glb_HttpsOrderTask.orderContent[glb_HttpsOrderTask.orderNum].OrderNo);
            LogDbg("is going to print order[%s]", glb_termParm.cur_prt_order_no);
            print_queue_taskID = print_channel_send_data(PRINT_CHN_ID_HTTPS,
                                                         glb_HttpsOrderTask.orderContent[orderIndex].printData,
                                                         glb_HttpsOrderTask.orderContent[orderIndex].orderLen,
                                                         glb_HttpsOrderTask.orderContent[glb_HttpsOrderTask.orderNum].orderCnt);
            LogDbg("print_channel_send_data return[%d]", print_queue_taskID);
            if (glb_HttpsOrderTask.orderContent[orderIndex].printData) {
                free(glb_HttpsOrderTask.orderContent[orderIndex].printData);
                glb_HttpsOrderTask.orderContent[orderIndex].printData = NULL;
            }
        }
    } else { /* reprint mode */
        print_queue_taskID = print_channel_send_data(PRINT_CHN_ID_HTTPS,
                                                     glb_HttpsHistoryOrderTask.orderContent[orderIndex].printData,
                                                     glb_HttpsHistoryOrderTask.orderContent[orderIndex].orderLen,
                                                     glb_HttpsHistoryOrderTask.orderContent[orderIndex].orderCnt);
        LogDbg("print_channel_send_data return[%d]", print_queue_taskID);
        if ( glb_HttpsHistoryOrderTask.orderContent[orderIndex].printData ) {
            free(glb_HttpsHistoryOrderTask.orderContent[orderIndex].printData);
            glb_HttpsHistoryOrderTask.orderContent[orderIndex].printData = NULL;
        }
        cleanHistoryOrderMsg();
    }

    return print_queue_taskID;
}


