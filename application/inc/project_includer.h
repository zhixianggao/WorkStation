#ifndef _PROJECT_INCLUDER_H_
#define _PROJECT_INCLUDER_H_

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stddef.h>

#include <sys/unistd.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <sys/socket.h>

#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/videodev2.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <arpa/inet.h>

#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <pthread.h>
#include <signal.h>
#include <mqueue.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <dlfcn.h>
#include <ctype.h>
#include <asm/types.h>
#include <semaphore.h>
#include <malloc.h>
#include <openssl/err.h>
#include <getopt.h>
#include <syslog.h>
#include <libgen.h>
#include <dirent.h>

#include "libcommon.h"
#include "app_mqtt.h"
#include "app_ssl.h"
#include "download_manager.h"
#include "am_service.h"
#include "app_printer.h"
#include "app_audio.h"
#include "basic_setting.h"
#include "network_test.h"
#include "libam.h"
#include "mqtt.h"
#include "glbvar.h"
#include "common.h"
#include "network.h"
#include "keyboard.h"
#include "IOTEntrance.h"
#include "headers.h"

#endif /* _PROJECT_INCLUDER_H_ */