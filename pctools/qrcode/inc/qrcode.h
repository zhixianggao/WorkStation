#ifndef __QRCODE_H__
#define __QRCODE_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <semaphore.h>
#include <openssl/aes.h>

#include "md5.h"
#include "cJSON.h"
#include "log.h"

#define cJSON_GetValueString(it, name, def) ({ \
	char *v = NULL;                            \
	cJSON *o = cJSON_GetObjectItem(it, name);  \
	if (o && o->type == cJSON_String)          \
		v = o->valuestring;                    \
	(v ? v : def);                             \
})

#define cJSON_GetValueInt(it, name, defValue) ({ \
	int v = defValue;                            \
	cJSON *o = cJSON_GetObjectItem(it, name);    \
	if (o) {                                     \
	if (o->type == cJSON_Number)                 \
	v = o->valueint;                             \
	else if (o->type == cJSON_False)             \
	v = 0;                                       \
	else if (o->type == cJSON_True)              \
	v = 1;                                       \
	}                                            \
	v;                                           \
})

#define cJSON_ReplaceValueString(it, name, str) ({ \
    cJSON *o = cJSON_GetObjectItem(it, name);      \
    if (o && o->type == cJSON_String)              \
        o->valuestring = str;                      \
})

#endif /* __QRCODE_H__ */