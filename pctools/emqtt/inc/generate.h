#ifndef __GENERATE_H__
#define __GENERATE_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <semaphore.h>

#include "iniparser.h"
#include "md5.h"
#include "cJSON.h"
#include "log.h"

#define LogDbg LogError
#define CLOUD_NO_RESPONSE (-99)
#define EMQ_SRC_FILE "./org/org.json"
#define EMQ_DST_FILE "./dst/dst.ini"
#define RELEASE_CLOUD_URL "cloud-print.sunmi.com"
#define EMQ_GEN_API "http://webapi.sunmi.com/webapi/iot/web/factory/1.0/?service=Initialize.create"

#define RETURN_WITH_NOTICE(label, key, value, notice, args...) ({ \
	key = value;                                                  \
	LogError(notice, ##args);                                     \
	goto label;})

#define cJSON_GetValueInt(it, name, defValue) ({ \
	int v = defValue;                            \
	cJSON *o = cJSON_GetObjectItem(it, name);    \
	if (o)                                       \
	{                                            \
		if (o->type == cJSON_Number)             \
			v = o->valueint;                     \
		else if (o->type == cJSON_False)         \
			v = 0;                               \
		else if (o->type == cJSON_True)          \
			v = 1;                               \
	}                                            \
	v;                                           \
})

#define cJSON_GetValueString(it, name, def) ({ \
	char *v = NULL;                            \
	cJSON *o = cJSON_GetObjectItem(it, name);  \
	if (o && o->type == cJSON_String)          \
		v = o->valuestring;                    \
	(v ? v : def);                             \
})

#endif /* __GENERATE_H__ */