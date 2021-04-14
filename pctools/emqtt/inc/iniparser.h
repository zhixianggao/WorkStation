#ifndef __INIPARSER_H__
#define __INIPARSER_H__
typedef struct {
	char *name;
	char *value;
} ini_item_t;

typedef int (*pIni_ReadSection_CB)(const char *name,const char *value,void *data);
typedef int (*pIni_ReadAll_CB)(const char *section,const char *name,const char *value,void *data);
char *ini_get_value_with_section(const char *inifile,const char *section,const char *key,char *value,int size);
int ini_set_value_with_section(const char *inifile,const char *section,const char *name,const char *value);
int ini_get_section(const char *inifile,const char *section,void *data,pIni_ReadSection_CB cb);
int ini_get_all(const char *inifile,void *data,pIni_ReadAll_CB cb);
int ini_scan_section(const char *inifile,const char *section,ini_item_t **list);
void ini_free_item(ini_item_t *item);

#define ini_get_value(inifile,key,value,size) ini_get_value_with_section(inifile,0,key,value,size)
#define ini_set_value(inifile,key,value) ini_set_value_with_section(inifile,0,key,value)
#endif
