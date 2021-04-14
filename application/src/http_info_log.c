#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <malloc.h>
#include <semaphore.h>
#include <libcommon.h>
#include <curl/curl.h>
#include <pthread.h>

static inline void PrintTimestamp(FILE *fp)
{
	struct tm tm={};
	time_t t = time(0);
	localtime_r(&t,&tm);
	fprintf(fp,"%02d-%02d %02d:%02d:%02d ",
		tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
}

struct http_resp_args {
	char *url;
	char *response;
	CURLcode errCode;
	int ms;
};

#define HTTP_Info_Write(fp,fmt,args...) do { \
	PrintTimestamp(fp); \
	fprintf(fp,fmt"\n",##args); \
} while(0)
#define MyPopen(fmt,args...) ({ \
	char cmd[256]; \
	snprintf(cmd,sizeof(cmd),fmt,##args); \
	FILE *_fp = popen(cmd,"r"); \
	_fp; \
})
static void *WriteHttpInfo(void *arg)
{
	static unsigned g_http_f_idx = 0;
	static pthread_mutex_t g_http_mux = PTHREAD_MUTEX_INITIALIZER;

	char fname[128],tmp_fname[128];
	unsigned f_index = 0;
	struct http_resp_args *args = (struct http_resp_args *)arg;

	pthread_detach(pthread_self());
	if( !args ) return NULL;

	pthread_mutex_lock(&g_http_mux);
	f_index = ++g_http_f_idx;
	pthread_mutex_unlock(&g_http_mux);

	snprintf(fname,sizeof(fname),"%s/%08x_%d.log",LOG_HTTP_INFO_FULLDIR,f_index,getpid());
	snprintf(tmp_fname,sizeof(tmp_fname),"%s.tmp",fname);
	FILE *fp = fopen(tmp_fname,"w");
	if( !fp ) goto out;

	HTTP_Info_Write(fp,"%s cost %d ms",args->url?args->url:"NULL",args->ms);
	HTTP_Info_Write(fp,"errCode=%d,%s", args->errCode,curl_easy_strerror(args->errCode));
	HTTP_Info_Write(fp,"response:%s",args->response?args->response:"null");
	if( strlen(sys_global_var()->broker_addr) ) {
		HTTP_Info_Write(fp,"ping %s",sys_global_var()->broker_addr);
		FILE *f_ping = MyPopen("ping -c 3 %s",sys_global_var()->broker_addr);
		if(f_ping) {
			char buf[256];
			while(fgets(buf,sizeof(buf),f_ping)) {
				char *p = strrchr(buf,'\n');
				if( p ) *p = 0;
				HTTP_Info_Write(fp,"%s",buf);
			}
			pclose(f_ping);
		}
	} else {
		HTTP_Info_Write(fp,"No broker_addr");
	}
	if( strlen(sys_global_var()->cloud_url) ) {
		HTTP_Info_Write(fp,"ping %s",sys_global_var()->cloud_url);
		char *p = strstr(sys_global_var()->cloud_url,"://");
		p = p?p+3:sys_global_var()->cloud_url;
		FILE *f_ping = MyPopen("ping -c 3 %s",p);
		if(f_ping) {
			char buf[256];
			while(fgets(buf,sizeof(buf),f_ping)) {
				p = strrchr(buf,'\n');
				if( p ) *p = 0;
				HTTP_Info_Write(fp,"%s",buf);
			}
			pclose(f_ping);
		}
	} else {
		HTTP_Info_Write(fp,"No http_url");
	}
	fclose(fp);
	rename(tmp_fname,fname);
	sem_t *s = sem_open(SEM_LOG_MNG,O_WRONLY);
	if( s ) {
		sem_post(s);
		sem_close(s);
	} else {
		unlink(fname);
	}

out:
	if( args->url ) free(args->url);
	if( args->response ) free(args->response);
	free(args);
	return NULL;
}

void asyncWriteHttpLog(const char *url,const char *response,const CURLcode *errCode,int ms)
{
	pthread_t tid;
	struct http_resp_args *args = (struct http_resp_args *)malloc(sizeof(struct http_resp_args));
	if( !args ) {
		LogError("malloc fail!");
		return;
	}
	args->url = url?strdup(url):0;
	args->response = response?strdup(response):0;
	args->errCode = errCode?*errCode:0;
	args->ms = ms;
	pthread_create(&tid,NULL,WriteHttpInfo,args);
}
