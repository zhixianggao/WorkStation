#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

#include "iniparser.h"
#define SEM_TIMEDWAIT(sem__,ms) ({ \
    struct timespec abstime; \
    clock_gettime(CLOCK_REALTIME, &abstime); \
	abstime.tv_sec += ms/1000; \
    abstime.tv_nsec += (ms%1000)*1000000; \
	if( abstime.tv_nsec >= 1000000000L ) { \
		abstime.tv_sec += 1; \
		abstime.tv_nsec -= 1000000000L; \
	} \
    sem_timedwait((sem__), &abstime); \
})

#define _myDebug(fmt,args...)
#define LogWarn(fmt,args...) fprintf(stderr,"%s|%d|%s() "fmt"\n",__FILE__,__LINE__,__func__,##args)

static char *parse_section(char *in,char *section,int size)
{
	char *p0,*p1;

	if( in[0] != '[') //  [section] 必须从行首开始
		return NULL;
	p0 = in+1;
	p1 = strchr(p0,']');
	if( p1 ) {
		snprintf(section,size,"%.*s",(int)(p1-p0),p0);
		return section;
	} else {
		return NULL;
	}
}

static int parse_name_value(char *in,char **ppname,char **ppvalue)
{
	char *p = strchr(in,'=');
	if( !p ) return -1;

	char *pName=in,*pValue=p+1;
	while(*pName==' ' || *pName=='\t') pName++; //去掉行首空格和'\t'
	if( *pName == '#') return -1;  //注释行
	while(p>=pName && (*p==' ' || *p=='\t' || *p=='=')) *p--=0; //去掉等号和等号前的空格'\t'
	while(*pValue==' ' || *pValue=='\t') pValue++; //去等号后面的空格和'\t'
	p = pValue+strlen(pValue)-1;
	while(p>=pValue && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n')) *p--=0; //去掉行尾的空格'\t''\r''\n'
	*ppname = pName;
	*ppvalue = pValue;
	return 0;
}

static int get_name_value_pos(char *in,char **ppname,int *namelen,char **ppvalue,int *valuelen)
{
	char *p = strchr(in,'=');
	if( !p ) return -1;

	char *pName=in,*pValue=p+1;
	while(*pName==' ' || *pName=='\t') pName++; //去掉行首空格和'\t'
	if( *pName == '#') return -1;  //注释行
	while(p>=pName && (*p==' ' || *p=='\t' || *p=='=')) p--; //去掉等号和等号前的空格'\t'
	*namelen = p-pName+1;
	while(*pValue==' ' || *pValue=='\t') pValue++; //去等号后面的空格和'\t'
	p = pValue+strlen(pValue)-1;
	while(p>=pValue && (*p==' ' || *p=='\t' || *p=='\r' || *p=='\n')) p--; //去掉行尾的空格'\t''\r''\n'
	*ppname = pName;
	*ppvalue = pValue;
	*valuelen = p-pValue+1;
	return 0;
}

char *ini_get_value_with_section(const char *inifile,const char *section,const char *key,char *value,int size)
{
	char *pSection,*pName,*pValue;
	char buf[4*1024],secbuf[256];
	int gotSection=0,gotValue=0,line=0;
	FILE *fp = fopen(inifile,"r");
	if( !fp ) {
		LogWarn("fopen(%s) fail(%d),%s",inifile,errno,strerror(errno));
		return NULL;
	}

	if( !section || strlen(section)==0 ) 
		gotSection = 1;

	while(fgets(buf,sizeof(buf),fp)) {
		line++;
		pSection = parse_section(buf,secbuf,sizeof(secbuf));
		if( !gotSection ) {
			if( !pSection ) continue;
			if( !strcasecmp(pSection,section) ) { // section不区分大小写
				gotSection = 1;
				_myDebug("Find section[%s] at line %d",section,line);
			}
			continue;
		} else if( pSection ) { //找到下一个section,退出
			break;
		}
		if( parse_name_value(buf,&pName,&pValue)<0 )
			continue;
		if( !strcmp(key,pName) ) {
			gotValue = 1;
			snprintf(value,size,"%s",pValue);
			_myDebug("Got [%s.%s]=[%s]",section?section:"null",key,value);
			break;
		}
	}
	fclose(fp);
	if( gotValue ) return value;
	return NULL;
}

int ini_get_section(const char *inifile,const char *section,void *data,pIni_ReadSection_CB cb)
{
	char *pSection,*pName,*pValue;
	char buf[4*1024],secbuf[256];
	int gotSection=0,line=0,count=0;
	FILE *fp = fopen(inifile,"r");
	if( !fp ) {
		LogWarn("fopen(%s) fail(%d),%s",inifile,errno,strerror(errno));
		return -1;
	}

	if( !section || strlen(section)==0 ) 
		gotSection = 1;

	while(fgets(buf,sizeof(buf),fp)) {
		line++;
		pSection = parse_section(buf,secbuf,sizeof(secbuf));
		if( !gotSection ) {
			if( !pSection ) continue;
			if( !strcasecmp(pSection,section) ) { // section不区分大小写
				gotSection = 1;
				_myDebug("Find section[%s] at line %d",section,line);
			}
			continue;
		} else if( pSection ) { //找到下一个section,退出
			break;
		}
		if( parse_name_value(buf,&pName,&pValue)<0 )
			continue;
		count++;
		if( cb ) {
			if( cb(pName,pValue,data)<0 )
				break;
		}
	}
	fclose(fp);
	return count;
}

int ini_scan_section(const char *inifile,const char *section,ini_item_t **list)
{
	char *pSection,*pName,*pValue;
	char buf[4*1024],secbuf[256];
	int gotSection=0,line=0,count=0;
	FILE *fp = fopen(inifile,"r");
	if( !fp ) {
		LogWarn("fopen(%s) fail(%d),%s",inifile,errno,strerror(errno));
		return -1;
	}

	if( !section || strlen(section)==0 ) 
		gotSection = 1;

	while(fgets(buf,sizeof(buf),fp)) {
		line++;
		pSection = parse_section(buf,secbuf,sizeof(secbuf));
		if( !gotSection ) {
			if( !pSection ) continue;
			if( !strcasecmp(pSection,section) ) { // section不区分大小写
				gotSection = 1;
				_myDebug("Find section[%s] at line %d",section,line);
			}
			continue;
		} else if( pSection ) { //找到下一个section,退出
			break;
		}
		if( parse_name_value(buf,&pName,&pValue)<0 )
			continue;
		count++;
		*list = realloc(*list,count*sizeof(ini_item_t));
		(*list)[count-1].name = strdup(pName);
		(*list)[count-1].value = strdup(pValue);
	}
	fclose(fp);
	return count;
}

void ini_free_item(ini_item_t *item)
{
	free(item->name);
	free(item->value);
}

int ini_get_all(const char *inifile,void *data,pIni_ReadAll_CB cb)
{
	char *pSection = NULL,*pName,*pValue;
	char buf[4*1024],secbuf[256],secName[256]={};
	int line=0,count=0;
	FILE *fp = fopen(inifile,"r");
	if( !fp ) {
		LogWarn("fopen(%s) fail(%d),%s",inifile,errno,strerror(errno));
		return -1;
	}

	while(fgets(buf,sizeof(buf),fp)) {
		line++;
		pSection = parse_section(buf,secbuf,sizeof(secbuf));
		if( pSection ) {
			strcpy(secName,secbuf);
			continue;
		}
		if( parse_name_value(buf,&pName,&pValue)<0 )
			continue;
		count++;
		if( cb ) {
			if( cb(secName,pName,pValue,data)<0 )
				break;
		}
	}
	fclose(fp);
	return count;
}

int ini_set_value_with_section(const char *inifile,const char *section,const char *name,const char *value)
{
	char fullpath[128]={};
	char buf[4*1024],secbuf[256];
	sem_t *lock = NULL;
	char tmpfile[128]={},lockfile[128],*pos;
	int gotSection=(!section || strlen(section)==0);
	int line = 0,setValue=0;
	FILE *fin=NULL,*fout=NULL;

	if( !inifile || strlen(inifile) == 0 )
		return -1;
	if( readlink(inifile,tmpfile,sizeof(tmpfile))>0 ){ // 如果ini文件是符号链接
		if( tmpfile[0] != '/'){
			char tmp[128]={};
			pos = strrchr(inifile,'/');
			if( pos ) {
				snprintf(tmp,sizeof(tmp),"%.*s/%s",(int)(pos-inifile),inifile,tmpfile);
				snprintf(tmpfile,sizeof(tmpfile),"%s",tmp);
			}
		}
	} else {
		snprintf(tmpfile,sizeof(tmpfile),"%s",inifile);
	}
	if( realpath(tmpfile,fullpath)<0 ) 
		snprintf(fullpath,sizeof(fullpath),"%s",tmpfile);
	_myDebug("%s full path:%s",inifile,fullpath);

	snprintf(tmpfile,sizeof(tmpfile),"%s.%lx",fullpath,pthread_self());
	fout = fopen(tmpfile,"w+");
	if( !fout ) {
		LogWarn("fopen(%s) fail(%d),%s",tmpfile,errno,strerror(errno));
		return -1;
	}

	snprintf(lockfile,sizeof(lockfile),"%s",fullpath);
	pos = lockfile+1;
	while( *pos ) {
		if( *pos == '/') *pos='_';
		pos ++;
	}
	lock = sem_open(lockfile,O_RDWR|O_CREAT,0644,1);
	if( !lock ) {
		LogWarn("sem_open(%s) fail",lockfile);
		return -1;
	}
	if( SEM_TIMEDWAIT(lock,2000)<0 && errno == ETIMEDOUT ){
		LogWarn("sem_timedwait(%s) timeout",lockfile);
	}

	fin = fopen(inifile,"r");
	while(fin && fgets(buf,sizeof(buf),fin)) {
		line++;
		if( setValue ) {
			fputs(buf,fout);
			continue;
		}
		char *pSection = parse_section(buf,secbuf,sizeof(secbuf));
		if( !gotSection ) {
			fputs(buf,fout);
			if( pSection && !strcasecmp(pSection,section) ) { // section不区分大小写
				gotSection = 1;
			}
			continue;
		}
		if( pSection ) { //找到下一个section
			if( !setValue ) {
				setValue=1;
				fprintf(fout,"\t%s = %s\n",name,value);
			}
			fputs(buf,fout);
			continue;
		}
		char *pName,*pValue;
		int namelen,valuelen;
		if( get_name_value_pos(buf,&pName,&namelen,&pValue,&valuelen)<0 ) {
			fputs(buf,fout);
			continue;
		}
		if( strlen(name) == namelen && !strncmp(name,pName,namelen) ) {
			setValue=1;
			if( strlen(value) == valuelen && !strncmp(pValue,value,valuelen)) { //same
				fclose(fin);
				fclose(fout);
				unlink(tmpfile);
				goto out;
			}
			snprintf(pValue,sizeof(buf)+buf-pValue,"%s\n",value);
			fputs(buf,fout);
			continue;
		}
		fputs(buf,fout);
	}
	if( !setValue || !gotSection ) {
		if( !gotSection && section && strlen(section) )
			fprintf(fout,"[%s]\n",section);
		fprintf(fout,"\t%s = %s\n",name,value);
	}
	if( fin ) fclose(fin);
	if( fout ) {
		fclose(fout);
		rename(tmpfile,fullpath);
		sync();
	}
out:
	sem_post(lock);
	sem_close(lock);
	return 0;
}

#ifdef _LOCAL_TEST_
int getCB(const char *section,const char *name,const char *value,void *data)
{
	printf("===%s.%s=%s\n",section,name,value);
}
int main(int argc,char **argv)
{
	return ini_get_all(argv[1],NULL,getCB);
}
#endif

