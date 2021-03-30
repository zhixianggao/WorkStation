#include <stdbool.h>
#include "libcommon.h"
#include "cutter_test.h"

#define CUTTER_TEST_ENABLED 0

#if (CUTTER_TEST_ENABLED)

typedef struct {
	bool initialized;
	bool running;
	uint32_t count;
} context_t;

static context_t context;

static void *test_thread(void *arg)
{
	print_status_t status;
	uint8_t buffer[64];
	time_t t;
	struct tm *loctim;
	int len, n = 0;

	context.initialized = true;
	context.running = false;
	context.count = 0;

	while (1) {
		if (!context.running)
			goto _cont;
		if (print_get_status(&status))
			goto _cont;
		if (status.no_paper || status.paper_jam || status.platen_opened ||
			status.thermal_head_overheated || status.step_motor_overheated)
			goto _cont;
		if (n != 0)
			goto _cont;

		t = time(NULL);
		loctim = localtime(&t);

		len = 0;
		buffer[len++] = 0x1B;
		buffer[len++] = 0x61;
		buffer[len++] = 0x31;

		len += sprintf((char *)buffer + len, "%02d-%02d %02d:%02d:%02d %12u\n\n\n\n",
					loctim->tm_mon + 1,
					loctim->tm_mday,
					loctim->tm_hour,
					loctim->tm_min,
					loctim->tm_sec,
					++context.count);

		buffer[len++] = 0x1D;
		buffer[len++] = 0x56;
		buffer[len++] = 0x31;

		print_channel_send_data(PRINT_CHN_ID_INT, buffer, len, 1);

_cont:
		if (!context.running)
			n = 0;
		else if (++n > 7)
			n = 0;
		SysDelay(500);
	}

	return NULL;
}

void cutter_test_toggle_state(void)
{
	if (!context.initialized)
		return;
	set_paper_not_taken_actions(0);
	context.running = !context.running;
}

void cutter_test_init(void)
{
	pthread_t tid;

	context.initialized = false;

	if (strcmp(sys_global_var()->project, "NT310"))
		return;

	if (pthread_create(&tid, NULL, test_thread, NULL)) {
		LogError("pthread_create failed.");
		return;
	}
	pthread_detach(tid);
}

#else

void cutter_test_toggle_state(void)
{
}

void cutter_test_init(void)
{
}

#endif /* CUTTER_TEST_ENABLED */
