#include "convert.h"


typedef unsigned char uint8_t;

/*----------------------------------------------*
 | enum                                        |
 *----------------------------------------------*/
enum
{
	LOPT_ALL = 1,
	LOPT_CONVERT,
	LOPT_HELP,
	LOPT_VERSION
};

/*----------------------------------------------*
 | define                                      |
 *----------------------------------------------*/
#define ALL_OPTION_MAX_ITEM (3U)
#define OPTION_MAX_ITEM (4U)
#define ARRAY_SIZE(array)  (sizeof(array)/sizeof(array[0]))

/*----------------------------------------------*
 | const                                       |
 *----------------------------------------------*/
static const char *ini_file[] = {
	"./ini/bajisitan_female.ini",
	"./ini/deguo_female.ini",
	"./ini/dongbei_female.ini",
	"./ini/eluosi_female.ini",
	"./ini/english_male.ini",
	"./ini/faguo_female.ini",
	"./ini/hanguo_female.ini",
	"./ini/hefei_male.ini",
	"./ini/henan_male.ini",
	"./ini/hubei_male.ini",
	"./ini/hunan_male.ini",
	"./ini/putaoya_female.ini",
	"./ini/shandong_male.ini",
	"./ini/shanxi_female.ini",
	"./ini/taiwan_female.ini",
	"./ini/weiwuer_female.ini",
	"./ini/xibanya_female.ini",
	"./ini/yidali_female.ini",
	"./ini/yindi_female.ini",
	"./ini/yuenan_female.ini"
};

static const char *conver_version = "0.0.1";
static const char *CONVERT_USAGE =
	"[Usage]: %s [option] [parameters]"
	"\n"
	"\n[Support options]:"
	"\n  -a, --all merge  Merge all ini files into dest file"
	"\n  -c, --convert    Merge file_A into file_B"
	"\n  -h, --help       Show this introduction information"
	"\n  -v, --version    Show current convert version";

void *util_file2buf(const char *fname,int *size)
{
	char *buf = NULL;
	if( size ) *size = 0;
	FILE *fp = fopen(fname,"rb");
	if( !fp ) return NULL;
	fseek(fp,0,SEEK_END);
	int sz = ftell(fp);
	rewind(fp);
	if( sz>0 ) {
		buf = (char *)malloc(sz+1);
		fread(buf,sz,1,fp);
		buf[sz] = 0;
	}
	fclose(fp);
	if( size ) *size = sz;
	return buf;
}

cJSON *util_file2cJSON(const char *fname)
{
	cJSON *root = NULL;
	char *buf = (char *)util_file2buf(fname,0);
	if( buf ) {
		root = cJSON_Parse(buf);
		free(buf);
	}
	return root;
}

static void show_usage(const char *name)
{
	char *program = strrchr(name, '/');
	printf(CONVERT_USAGE, (program)?(program+1):name);
	printf("\n\n");
	exit(1);
}

static void show_version(void)
{
	printf("Version %s\n", conver_version);
	exit(1);
}

static const struct option LONGOPTS[] = {
	{"all", optional_argument, NULL, LOPT_ALL},
	{"convert", optional_argument, NULL, LOPT_CONVERT},
	{"help", 0, NULL, LOPT_HELP},
	{"version", 0, NULL, LOPT_VERSION},
	{NULL, 0, NULL, 0}
};

static void do_convertion(uint8_t argc, const char *argv[])
{
	if (argc != OPTION_MAX_ITEM || !argv[0]) {
		LogError("absence of critical parameters!");
		return;
	}

	if (access(argv[argc-1], F_OK) || access(argv[argc-2], F_OK)) {
		LogError("no %s or %s check please!", argv[argc-1], argv[argc-2]);
		return;
	}

	int i;
	//for (i=0; i<argc; i++)
		//LogError("param[%d]:%s", i, argv[i]);

	cJSON *src_root = util_file2cJSON(argv[argc-1]);
	if( !src_root ) {
		LogError("util_file2cJSON(%s) fail!",argv[argc-1]);
		goto OUT;
	}

	char speaker[64] = {}, params[256] = {};
	ini_get_value_with_section(argv[argc-2], "info", "speaker", speaker, sizeof(speaker));
	ini_get_value_with_section(argv[argc-2], "info", "params", params, sizeof(params));
	//LogError("speaker[%s] params[%s]", speaker, params);
	cJSON *params_json = cJSON_Parse(params);
	cJSON *info_json = cJSON_GetObjectItem(src_root, "info");
	if (info_json) {
		cJSON *speaker_params_json = cJSON_GetObjectItem(info_json, speaker);
		if (params_json && info_json && strlen(speaker)) {
			if (speaker_params_json)
				cJSON_ReplaceItemInObject(info_json, speaker, params_json);
			else
				cJSON_AddItemToObject(info_json, speaker, params_json);
		}
	}

	ini_item_t *list = NULL;
	int ret = ini_scan_section(argv[argc-2], "audio_list", &list);
	//LogError("ret[%d]", ret);
	for (i=0; i<ret; i++)
	{
		cJSON *audio_item = cJSON_GetObjectItem(src_root, list[i].name);
		if (audio_item) {
			//LogError("audio name[%s]", list[i].name);
			cJSON *text = cJSON_GetObjectItem(audio_item, "text");
			if (text) {
				char *context = cJSON_GetValueString(text, speaker, NULL);
				if (!context) {
					//LogError("name[%s] value[%s]", speaker, list[i].value);
					cJSON_AddStringToObject(text, speaker, list[i].value);
				} else {
					//LogError("origin[%s] new[%s]", context, list[i].value);
					cJSON_ReplaceValueString(text, speaker, list[i].value);
				}
			}
		}
	}

	FILE *fp = fopen(argv[argc-1], "w");
	if (fp) {
		char *buff = cJSON_Print(src_root);
		if (buff)
			fprintf(fp, "%s", buff);
		fclose(fp);
	}
OUT:
	if (src_root)
		cJSON_Delete(src_root);
	LogError("[COVERTION OVER]");
}

static void do_all_convertion(int argc, char *argv[])
{
	if (!argv[0] || argc != ALL_OPTION_MAX_ITEM) {
		LogError("absence of critical parameters!");
		return;
	}
	const char *argv_new[OPTION_MAX_ITEM] = {};
	argv_new[0] = argv[0];
	argv_new[1] = argv[1];
	argv_new[3] = argv[2];
	int i;
	int loop = ARRAY_SIZE(ini_file);
	for (i=0; i<loop; i++)
	{
		argv_new[2] = ini_file[i];
		//LogError("argv_new:%s %s %s %s", argv_new[0], argv_new[1], argv_new[2], argv_new[3]);
		do_convertion(OPTION_MAX_ITEM, argv_new);
	}
}

static void arg_parse(uint8_t argc, char *argv[])
{
#define OPTION ":achv"
	int c;

	while ((c = getopt_long(argc, argv, OPTION, LONGOPTS, NULL)) != -1)
	{
		switch (c)
		{
		case 'a':
		case LOPT_ALL:
			do_all_convertion(argc, argv);
			break;

		case 'c':
		case LOPT_CONVERT:
			do_convertion(argc, (const char **)argv);
			break;

		case 'h':
		case LOPT_HELP:
			show_usage(argv[0]);
			break;

		case 'v':
		case LOPT_VERSION:
			show_version();
			break;

		default:
			LogWarn("Unexpected Option!");
		}
	}
#undef OPTION
}

static void loop_sleep(void)
{
	while (1)
		sleep(100);
}

int main(int argc, char *argv[])
{
	arg_parse(argc, argv);
	//loop_sleep();
	LogError("[GAME OVER]");
	return 0;
}
