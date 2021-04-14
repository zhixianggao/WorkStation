#ifndef __HTTP_INFO_LOG_H__
#define __HTTP_INFO_LOG_H__

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <malloc.h>
#include <semaphore.h>
#include <libcommon.h>
#include <curl/curl.h>
#include <pthread.h>

#define HTTP_REQ_CALC_TIME(url,err) ({ \
	int ms = 0; \
	struct timespec t0,t1; \
	char *r = NULL; \
	clock_gettime(CLOCK_MONOTONIC,&t0); \
	r = HttpsPostReq(url,err); \
	clock_gettime(CLOCK_MONOTONIC,&t1); \
	ms = (t1.tv_sec-t0.tv_sec)*1000+(int)((t1.tv_nsec-t0.tv_nsec)/1000000); \
	if( ms>=5000 ) { \
		asyncWriteHttpLog(url,r,err,ms); \
	} \
	r; \
})

void asyncWriteHttpLog(const char *url,const char *response,const CURLcode *errCode,int ms);

#endif
