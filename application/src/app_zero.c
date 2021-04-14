#include "project_includer.h"


int main(int argc, char *argv[])
{
	ssl_mutilthread_init();
	init_glbvar();
	util_set_default_time();
	audio_file_handler();
	key_init();
	get_basic_info();
	check_hw_info();
	mqtt_thread_init();
	iot_init();
	download_thread_init();
	am_service_init();
	network_monitor();
	server_api_load();
	collection_init();
	loop_sleep();

	return 0;
}
