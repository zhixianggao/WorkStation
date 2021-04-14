#include "project_includer.h"

static void *NetworkMonitor_thread(void *arg)
{
	LogDbg("init");
	while (1)
	{
		uint8_t curnet = NetGetCurRoute();
		if ( curnet == NET_NONE ) {
			SysDelay(200);
			continue;
		}
		libam_handler()->notify(AM_NOTIFY_TYPE_NET, &curnet, sizeof(curnet));
		SysDelay(200);
		break;
	}
	LogDbg("success exit");
	pthread_exit(0);
}

void network_monitor(void)
{
	pthread_t tid;
	if (pthread_create(&tid, NULL, &NetworkMonitor_thread, NULL)) {
		LogError("NetworkMonitor_thread create fail");
		return;
	}
	pthread_detach(tid);
}