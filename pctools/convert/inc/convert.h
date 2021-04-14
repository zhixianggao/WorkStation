#ifndef __CONVERT_H__
#define __CONVERT_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>

#include "cJSON.h"
#include "iniparser.h"
#include "log.h"


#define cJSON_GetValueString(it,name,def) ({ \
	char *v = NULL; \
	cJSON *o = cJSON_GetObjectItem(it,name); \
	if( o && o->type==cJSON_String ) v=o->valuestring; \
	(v?v:def); \
})
#define cJSON_ReplaceValueString(it,name,str) ({ \
	cJSON *o = cJSON_GetObjectItem(it,name); \
	if( o && o->type==cJSON_String ) o->valuestring=str; \
})


#endif /* __CONVERT_H__ */