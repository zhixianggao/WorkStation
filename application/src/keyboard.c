#include "project_includer.h"
#include "cutter_test.h"

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define RELEASE_STATE  1
#define PRESS_STATE    2
#define PHY_KEY_NUM    4

#define NOT_PLAY   0  /* notice audio server no need to play this audio */
#define NEED_PLAY  1  /* notice audio server need to play this audio    */
#define MIN_VOLUME 0  /* notice audio server set the minimum volume     */
#define MAX_VOLUME 9  /* notice audio server set the maximum volume     */

#define KD(s,K) (key_detail[s].K)

enum
{
	VOLUME_DOWN = 0,
	VOLUME_UP,
	FUNCTION,
	SETTING,
	PHY_KEY_MAX
};

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
static void feed_paper_start(void)
{
	if (!g_app_vars.isPaperFeeding) {
		g_app_vars.isPaperFeeding = 1;
		LogInfo("");
		print_set_feeding_state(1);
	} 
}

static void feed_paper_end(void)
{
	if (g_app_vars.isPaperFeeding) {
		g_app_vars.isPaperFeeding = 0;
		LogInfo("");
		print_set_feeding_state(0);
	}
}

static void reprint_newest_order(void)
{
	if (g_app_vars.isNeedReprintOldOrder) LogInfo("reorder going on, please wait");
	else g_app_vars.isNeedReprintOldOrder = 1;
}

static void long_press_update(key_detail_t *k)
{
	for (int i=0; i<PHY_KEY_NUM; i++) {
		if( k[i].isPress ) {
			k[i].lpt = SysTick()-k[i].pt;
		}
	}
}

static void combined_test_page(const key_detail_t *k, const uint8_t p, const uint8_t r)
{
	static bool isTestpage = false;
	switch (p) {
		case PHY_KEY_SETTING:    break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	switch (r) {
		case PHY_KEY_SETTING:
			if (isTestpage && !g_app_vars.isBurnInTest) {
				PrintTestPage();
				isTestpage = false;
			}
			break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	if (isTestpage == false && k[FUNCTION].isPress && k[SETTING].isPress) {
		isTestpage = true;
	}
}

static void combined_burning_test(const key_detail_t *k, const uint8_t p, const uint8_t r)
{
	static bool isBurning = false;
	switch (p) {
		case PHY_KEY_SETTING:    break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	switch (r) {
		case PHY_KEY_SETTING:
			if (isBurning) {
				g_app_vars.isBurnInTest = 1;
				AudioPlayFixed(BURNINTESTSTART, 1, 0);
				burnin_test_init();
				isBurning = false;
			}
			break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	if (isBurning == false && k[VOLUME_UP].isPress && k[FUNCTION].isPress && k[SETTING].isPress) {
		isBurning = true;
	}
}

static bool isResetting = false;
static uint32_t reset_tick = 0;
static void combined_long_press(const key_detail_t *k, const uint8_t p, const uint8_t r)
{
	static bool isParing = false;
	switch (p) {
		case PHY_KEY_SETTING:    break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	switch (r) {
		case PHY_KEY_SETTING: isParing = false; break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	if ( isParing == false && k[SETTING].isPress && k[SETTING].lpt>=3000 ) {
		isParing = true;
		BtStartParing();
	}
    if ( isResetting == false && k[SETTING].isPress && k[SETTING].lpt>=10000 ) {
        LogDbg("please press function key to make sure factory setting reset");
        isResetting = true;
        reset_tick = SysTick();
        AudioPlayFixed(FUNCTION_KEY_PRESS, 1, 0);
    }
}

static void combined_double_click(const key_detail_t *k, const uint8_t p, const uint8_t r)
{
	switch (p) {
		case PHY_KEY_SETTING:
			if ( k[SETTING].pst<1000 ) {
				startNetworkTest();
			}
			break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	switch (r) {
		case PHY_KEY_SETTING:    break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
}

static void *test_page_proc(void *args)
{
	pthread_detach(pthread_self());
	int iRet = PrintTestPage();
	if (iRet) LogError("error code[%d]", iRet);
	return NULL;
}

static void reset_or_not(void)
{
    if (isResetting) {
        if (SysTick()-reset_tick <= 30*1000) {
            AudioPlayFixed(RESETTING, 1, 0);
            system("rm -rf /data/SYS/config");
            system("rm -rf /data/SYS/lsrv_cfg.ini");
            SysDelay(5000);
            sync();
            SysReboot();
        } else {
            isResetting = false;
        }
    }
}

static void one_click(const key_detail_t *k, const uint8_t p, const uint8_t r)
{
	switch (p) {
		case PHY_KEY_FN:
			if (g_app_vars.isBurnInTest) {
				LogInfo("burning test ending");
				g_app_vars.isBurnInTest = 0;
				AudioPlayFixed(BURNINTESTEND, 1, 0);
			}
			cutter_test_toggle_state();
			break;
		case PHY_KEY_VOLUMEDOWN:
			AudioVolumeSub(NEED_PLAY);
			break;
		case PHY_KEY_VOLUMEUP:
			AudioVolumeAdd(NEED_PLAY);
			break;
		default:
			break;
	}
    switch (r) {
        case PHY_KEY_SETTING:
            break;
        case PHY_KEY_FN:
            reset_or_not();
            break;
        case PHY_KEY_VOLUMEDOWN:
            break;
        case PHY_KEY_VOLUMEUP:
            break;
        default:
            break;
	}
}

static void double_click(const key_detail_t *k, const uint8_t p, const uint8_t r)
{
	switch (p)
	{
		case PHY_KEY_FN:
			if( k[FUNCTION].pst<1000 ) {
				reprint_newest_order();
			}
			break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
	switch (r) {
		case PHY_KEY_SETTING:    break;
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		default:                 break;
	}
}

static void long_press(const key_detail_t *k, const uint8_t p, const uint8_t r)
{
	static bool feeding = false, v_up_long = false, v_down_long = false;
	switch (p) {
		case PHY_KEY_FN:         break;
		case PHY_KEY_VOLUMEDOWN: break;
		case PHY_KEY_VOLUMEUP:   break;
		case PHY_KEY_SETTING:    break;
		default:                 break;
	}
	switch (r) {
		case PHY_KEY_FN:
			if (feeding == true) {
				feeding = false;
				feed_paper_end();
			}
			break;
		case PHY_KEY_VOLUMEDOWN:
			if (v_down_long) {
				v_down_long = false;
				AudioSetVolume(MIN_VOLUME, NEED_PLAY);
			}
			break;
		case PHY_KEY_VOLUMEUP:
			if (v_up_long) {
				v_up_long = false;
				AudioSetVolume(MAX_VOLUME, NEED_PLAY);
			}
			break;
		default:
			break;
	}
	if ( k[VOLUME_DOWN].isPress ) {
		if (k[VOLUME_DOWN].lpt>=2000) {
			v_down_long = true;
		}
	}
	if ( k[VOLUME_UP].isPress ) {
		if (k[VOLUME_UP].lpt>=2000) {
			v_up_long = true;
		}
	}
	if ( k[FUNCTION].isPress ) {
		if (k[FUNCTION].lpt>=500) {
			feeding = true;
			feed_paper_start();
		}
	}
}

static uint8_t index_to_sequence(uint8_t index)
{
	uint8_t seq = 0;
	switch (index) {
		case PHY_KEY_VOLUMEDOWN: seq=VOLUME_DOWN; break;
		case PHY_KEY_VOLUMEUP:   seq=VOLUME_UP;   break;
		case PHY_KEY_FN:         seq=FUNCTION;    break;
		case PHY_KEY_SETTING:    seq=SETTING;     break;
		default:                                  break;
	}
	return seq;
}

static void show_key_state(const key_detail_t *k)
{
	static uint8_t flag=0;
	if ( k[VOLUME_UP].isPress ) {
		if (flag == 0) {
			flag = 1;
			//LogInfo("press time[%d] long press[%d] press space[%d]",
			//		k[VOLUME_UP].pt,
			//		k[VOLUME_UP].lpt,
			//		k[VOLUME_UP].pst);
		}
	} else if (!k[VOLUME_UP].isPress) {
		if(flag == 1) {
			flag = 0;
			//LogInfo("press time[%d] long press[%d] press space[%d]",
			//		k[VOLUME_UP].pt,
			//		k[VOLUME_UP].lpt,
			//		k[VOLUME_UP].pst);
		}
	}
}

static int virtual_key_proc(key_info_t *k)
{
	pthread_t tid;
	switch(k->index) {
		case VIRTUAL_KEY_TESTPAGE:
			pthread_create(&tid, NULL, test_page_proc, NULL);
			break;
		case VIRTUAL_KEY_WNET_INFO:
			mqtt_report_rightnow();
			break;
		case VIRTUAL_KEY_ALIPAY_BIND:
			mqtt_report_trade_bindstatus(1);
			break;
		case VIRTUAL_KEY_ALIPAY_UNBIND:
			mqtt_report_trade_bindstatus(0);
			break;
		case VIRTUAL_KEY_ALIPAY_BUCKS:
			update_sdk_in_bucks_count(1);
			break;
		default:
			return -1;
	}
	return 0;
}

static char *show_current_keyname(uint8_t index)
{
	switch(index) {
		case PHY_KEY_FN:
			return "Physical Function";
		case PHY_KEY_VOLUMEUP:
			return "Physical Volume+";
		case PHY_KEY_VOLUMEDOWN:
			return "Physical Volume-";
		case PHY_KEY_SETTING:
			return "Physical Setting";
		case VIRTUAL_KEY_TESTPAGE:
			return "Virtual Testpage";
		case VIRTUAL_KEY_WNET_INFO:
			return "Virtual Report Wnet";
		case VIRTUAL_KEY_ALIPAY_BIND:
			return "Virtual Alipay Bind";
		case VIRTUAL_KEY_ALIPAY_UNBIND:
			return "Virtual Alipay Unbind";
		case VIRTUAL_KEY_ALIPAY_BUCKS:
			return "Virtual Alipay Bucks";
		default:
			return "Unknown";
	}
}

static void *KeyLoop(void *arg)
{
	LogInfo("enterance");

	bool is_combination = false;
	key_info_t key_info;
	key_detail_t key_detail[PHY_KEY_NUM] = {{0,0,0,0}};
	fd_set fds;
	struct timeval tv = {0, 500*1000};
	struct mq_attr mq_attr;
	mqd_t mqID = mq_open(KEYBOARD_MQ, O_RDONLY | O_NONBLOCK);
	if (mqID < 0) {
		LogInfo("mqID[%d] errorno[%d] errorstr[%s]", mqID, errno, strerror(errno));
		return NULL;
	}
	mq_getattr(mqID, &mq_attr);
	flush_mq(mqID);
	while(1)
	{
		FD_ZERO(&fds);
		FD_SET(mqID,&fds);
		uint8_t press = 0, release = 0;
		tv.tv_sec = 0;
		tv.tv_usec = 500*1000;
		bzero(&key_info,sizeof(key_info));

		int ret = select(mqID+1,&fds,NULL,NULL,&tv);
		if ( ret<0 && errno==EINTR ) {
			LogError("select interrupted system call");
			continue;
		}
		if ( ret<0 ) {
			LogError("select error");
			continue;
		}
		if ( ret==0 ) {
			long_press_update(key_detail);
		}
		if ( ret>0 ) {
			if ( mq_receive(mqID, (char *)&key_info, mq_attr.mq_msgsize, (uint32_t *)0)<0 ) {
				LogError("mq_receive errorno[%d]", errno);
			}
			if ( key_info.state == PRESS_STATE ) {
				press = key_info.index;
			} else if (key_info.state == RELEASE_STATE) {
				release = key_info.index;
			}

			LogInfo("%s key %s", show_current_keyname(key_info.index), key_info.state==RELEASE_STATE?"Released":"Pressed");
			if ( virtual_key_proc(&key_info)==0 ) {
				continue;
			}
			long_press_update(key_detail);
		}

		uint8_t seq = index_to_sequence(key_info.index);
		if ( key_info.state == PRESS_STATE ) {
			uint32_t timenow = SysTick();
			KD(seq,isPress)  = true;
			KD(seq,pst)      = timenow-KD(seq,pt);
			KD(seq,pt)       = timenow;
			KD(seq,lpt)      = 0;
		} else if ( key_info.state == RELEASE_STATE ) {
			KD(seq,isPress) = false;
		}

		if (KD(SETTING,isPress) || release == PHY_KEY_SETTING) {
			is_combination = true;
			combined_double_click(key_detail, press, release);
			combined_long_press(  key_detail, press, release);
			combined_burning_test(key_detail, press, release);
			combined_test_page(   key_detail, press, release);
		} else if (is_combination && (KD(VOLUME_DOWN,isPress)||KD(VOLUME_UP,isPress)||KD(FUNCTION,isPress))) {
			LogDbg("exit combination release %s key", show_current_keyname(release));
		} else {
			is_combination = false;
			//show_key_state(key_detail);
			one_click(     key_detail, press, release);
			double_click(  key_detail, press, release);
			long_press(    key_detail, press, release);
		}
	}
	pthread_exit(0);
}

void key_init(void)
{
	LogDbg("");
	pthread_t tid;
	if (pthread_create(&tid, NULL, &KeyLoop, NULL)) {
		LogError("KeyLoop create fail");
		return;
	}
	pthread_detach(tid);
	cutter_test_init();
}
