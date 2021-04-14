#ifndef __COMMON__H__
#define __COMMON_H__

/*----------------------------------------------*
 | 宏定义                                       |
 *----------------------------------------------*/
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define CreateDetachThread(fun, arg)                                                \
	do                                                                              \
	{                                                                               \
		pthread_t tid = 0;                                                          \
		if (pthread_create(&tid, NULL, (ThreadStartRoutine)fun, (void *)(arg)) < 0) \
			LogError("CreateDetachThread fail(%d),%s", errno, strerror(errno));     \
		else                                                                        \
			pthread_detach(tid);                                                    \
	} while (0)

/*----------------------------------------------*
 | 可供外部使用                                 |
 *----------------------------------------------*/
extern print_status_t gt_printerStatus;
typedef void *(*ThreadStartRoutine)(void *);

void app_set_lbs(char *lngBuff, char *latBuff);
void burnin_test_init(void);
void am_service_init(void);
void loop_sleep(void);
void server_api_load(void);
int PrintTestPage(void);


#endif /* __COMMON_H__ */
