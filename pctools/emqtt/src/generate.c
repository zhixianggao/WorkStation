#include "generate.h"

typedef unsigned char uint8_t;
/*----------------------------------------------*
 | enum                                        |
 *----------------------------------------------*/
enum
{
	LOPT_ALL = 1,
	LOPT_SINGLE,
	LOPT_HELP,
	LOPT_VERSION
};

/*----------------------------------------------*
 | define                                      |
 *----------------------------------------------*/
#define ALL_OPTION_ITEM_NUM     (4U)
#define SINGLE_OPTION_FIVE_ITEM (5U)
#define SINGLE_OPTION_SIX_ITEM  (6U)
#define ARRAY_SIZE(array)  (sizeof(array)/sizeof(array[0]))

typedef struct _emq_request_t {
	char sn[64];
	char token[64];
	char project[64];
	char model[64];
	char org_file[256];
	char dst_file[256];
} emq_request_t;

typedef struct _emq_param_t {
	char clientID[64];
	char username[64];
	char password[64];
	char secret[64];
	char serverAddress[64];
	char port[16];
	char httpAddress[128];
	char token[64];
} emq_param_t;

/*----------------------------------------------*
 | const                                       |
 *----------------------------------------------*/
static const char *GENERATOR_VERSION = "0.0.1";
static const char *GENERATOR_USAGE =
	"[Usage]: %s [option] [parameters]"
	"\n"
	"\n[Support options]:"
	"\n  -a, --all generation       all -a org_file dst_file"
	"\n  -s, --single generation    all -s sn project model token (dst_file)"
	"\n  -h, --help                 all -h"
	"\n  -v, --version              all -v";

static void show_usage(const char *name)
{
	char *program = strrchr(name, '/');
	printf(GENERATOR_USAGE, (program)?(program+1):name);
	printf("\n\n");
	exit(1);
}

static void show_version(void)
{
	printf("Version %s\n", GENERATOR_VERSION);
	exit(1);
}

static const struct option LONGOPTS[] = {
	{"all",     optional_argument, NULL, LOPT_ALL},
	{"single",  optional_argument, NULL, LOPT_SINGLE},
	{"help",    0,                 NULL, LOPT_HELP},
	{"version", 0,                 NULL, LOPT_VERSION},
	{NULL,      0,                 NULL, 0}
};

static void write_emq_param_into_file(const emq_request_t *emq_request, const emq_param_t *emq_param)
{
	if ( emq_request && strlen(emq_request->dst_file) )
	{
		FILE *fp = fopen(emq_request->dst_file, "a");
		if (!fp) {
			LogError("fopen(%s) error(%d),%s", emq_request->dst_file, errno, strerror(errno));
			return;
		}
		if (emq_param) {
			fprintf(fp, "[%s]\n"              ,   emq_param->clientID);
			fprintf(fp, "SN            = %s\n",   emq_param->clientID);
			fprintf(fp, "clientID      = %s\n",   emq_param->clientID);
			fprintf(fp, "password      = %s\n",   emq_param->password);
			fprintf(fp, "port          = %s\n",   emq_param->port);
			fprintf(fp, "secret        = %s\n",   emq_param->secret);
			fprintf(fp, "token         = %s\n",   emq_param->token);
			fprintf(fp, "username      = %s\n",   emq_param->username);
			fprintf(fp, "httpAddress   = %s\n",   emq_param->httpAddress);
			fprintf(fp, "serverAddress = %s\n\n", emq_param->serverAddress);
		}
		fclose(fp);
	}
}

static int check_json(const char *jsonStr)
{
	int iRet = -1;
	int jFlgTotal = 0, jFlgMatch = 0;

	if (!strlen(jsonStr)) {
		return iRet;
	}

	for (int i = 0; i < strlen(jsonStr); i++)
	{
		if (jsonStr[i] == '{') {
			jFlgTotal++;
			jFlgMatch++;
		} else if (jsonStr[i] == '}') {
			jFlgTotal++;
			jFlgMatch--;
		}
	}

	if ((jFlgTotal > 0) && (jFlgMatch == 0)) {
		iRet = 0;
	} else {
		LogError("invalid JSON string: %s", jsonStr);
	}
	return iRet;
}

static int single_generation_response(const char *response, const emq_request_t *emq_request)
{
	LogDbg("response[%s]", response);
	int iRet = 0;
	cJSON *pRoot = NULL;
	if (strlen(response))
	{
		if ( check_json(response)<0 ) {
			RETURN_WITH_NOTICE(OUT, iRet, -1, "not json");
		}
		if (!(pRoot = cJSON_Parse(response))) {
			RETURN_WITH_NOTICE(OUT, iRet, -2, "no root node");
		}
		int  code = cJSON_GetValueInt(pRoot, "code", -1);
		char *msg = cJSON_GetValueString(pRoot, "msg", "");
		if (code!=1 && strlen(msg)) {
			RETURN_WITH_NOTICE(OUT, iRet, -3, "request fail");
		}
		cJSON *pData = cJSON_GetObjectItem(pRoot, "data");
		if (!pData) {
			RETURN_WITH_NOTICE(OUT, iRet, -4, "absence of data item");
		} else {
			emq_param_t emq_param      = {};
			char saddr[128]            = {};
			char        *clientID      = cJSON_GetValueString(pData, "clientID",      NULL);
			char        *password      = cJSON_GetValueString(pData, "password",      NULL);
			char        *port          = cJSON_GetValueString(pData, "port",          NULL);
			char        *token         = cJSON_GetValueString(pData, "token",         NULL);
			char        *serverAddress = cJSON_GetValueString(pData, "serverAddress", NULL);
			char        *username      = cJSON_GetValueString(pData, "username",      NULL);
			char        *httpAddress   = cJSON_GetValueString(pData, "httpAddress",   NULL);
			int         secret         = cJSON_GetValueInt   (pData, "secret",        -1);
			if (username) {
				int i,j;
				for (i=0,j=0; i<strlen(username); i++) {
					if (username[i]!='\\') {
						username[j++] = username[i];
					}
				}
				username[j] = '\0';
			}
			if (httpAddress) {
				if (!strcmp(httpAddress, RELEASE_CLOUD_URL)) {
					snprintf(saddr, sizeof(saddr), "https://%s", httpAddress);
				} else if (strstr(httpAddress, ".test.") ||
				           strstr(httpAddress, ".uat.")  ||
				           strstr(httpAddress, ".dev.")) {
					snprintf(saddr, sizeof(saddr), "http://%s", httpAddress);
				} else {
					LogError("unknown http address %s", httpAddress);
				}
			}
			if (clientID && password && port && token && serverAddress && username && httpAddress && secret!=-1) {
				snprintf(emq_param.clientID,      sizeof(emq_param.clientID),      "%s", clientID);
				snprintf(emq_param.password,      sizeof(emq_param.password),      "%s", password);
				snprintf(emq_param.port,          sizeof(emq_param.port),          "%s", port);
				snprintf(emq_param.secret,        sizeof(emq_param.secret),        "%d", secret);
				snprintf(emq_param.token,         sizeof(emq_param.token),         "%s", token);
				snprintf(emq_param.serverAddress, sizeof(emq_param.serverAddress), "%s", serverAddress);
				snprintf(emq_param.username,      sizeof(emq_param.username),      "%s", username);
				snprintf(emq_param.httpAddress,   sizeof(emq_param.httpAddress),   "%s", saddr);
				write_emq_param_into_file(emq_request, &emq_param);
			}
		}
	OUT:
		if (pRoot) cJSON_Delete(pRoot);
		return iRet;
	} else {
		LogDbg("ZERO RESPONSE");
		return CLOUD_NO_RESPONSE;
	}
}

static void md5sum(const void *buf,int len,char md5[33])
{
	unsigned char md5buf[16] ={};
	MD5_CTX context;
	MD5_Init(&context);
	MD5_Update(&context, buf, len);
	MD5_Final(md5buf, &context);
	for(len=0;len<16;len++)
		snprintf(md5+len*2,3,"%02x",md5buf[len]);
}

static size_t cb(char *buff, size_t size, size_t nmemb, FILE *fp) {
	return fwrite(buff, size, nmemb, fp);
}

char *HttpsPostReq(const char *url, CURLcode *errCode)
{
	char *buff = NULL;
	size_t size = 0;
	CURL *curl = curl_easy_init();
	if (!curl) {
		if( errCode ) *errCode = CURLE_FAILED_INIT;
		return buff;
	}
	FILE *fp = open_memstream(&buff, &size);
	if (!fp) {
		if( errCode ) *errCode = CURLE_FAILED_INIT;
		return buff;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000 * 40);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 40L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	fclose(fp);
	if (size <= 0) {
		if( errCode ) *errCode = res;
		if( buff ) free(buff);
		return NULL;
	}

	return buff;
}

static void do_single_generation(uint8_t argc, const emq_request_t *emq_request)
{
	if ((argc != SINGLE_OPTION_SIX_ITEM && argc != SINGLE_OPTION_FIVE_ITEM) || !emq_request) {
		LogError("absence of critical parameters!");
		return;
	}
	LogDbg("\nsn:%s\nproject:%s\nmodel:%s\ntoken:%s\n", emq_request->sn, emq_request->project, emq_request->model, emq_request->token);
	CURLcode res = CURLE_OK;
	time_t timeStamp;
	char sign[64] = {};
	char param[1024] = {};
	char *response = NULL;

	time(&timeStamp);
	snprintf(param, sizeof(param), "%s%s%s%s%lu", emq_request->token, emq_request->sn, emq_request->project, emq_request->model, timeStamp*2);
	md5sum(param, strlen(param), sign);
	snprintf(param, sizeof(param), "%s&sn=%s&model=%s&hardwareModel=%s&timestamp=%lu&sign=%s", EMQ_GEN_API, emq_request->sn, emq_request->project, emq_request->model, timeStamp, sign);
	LogDbg("param[%s]", param);
	response = HttpsPostReq(param, &res);
	if (!response) {
		LogError("curl error[%d] error string[%s]", res, curl_easy_strerror(res));
		return;
	}
	single_generation_response(response, emq_request);
	free(response);
	return;
}

static void *file2buf(const char *fname,int *size)
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

static cJSON *file2cJSON(const char *fname)
{
	cJSON *root = NULL;
	char *buf = (char *)file2buf(fname, 0);
	if( buf ) {
		root = cJSON_Parse(buf);
		free(buf);
	}
	return root;
}

static void do_all_generation(uint8_t argc, const char *argv[])
{
	/********************* 
	* PARAM INTRODUCTION *
	* argv[0]: emqtt     *
	* argv[1]: -a        *
	* argv[2]: org file  *
	* argv[3]: dst file  *
	**********************/
	if (argc!=ALL_OPTION_ITEM_NUM || !argv) {
		LogError("absence of critical parameters!");
		return;
	}

	if ((access(argv[2], F_OK)!=0) || (access(argv[3], F_OK)!=0)) {
		LogError("absence of orgfile or dstfile!");
		return;
	}

	cJSON *root = file2cJSON(argv[2]);
	if (root) {
		cJSON *array = cJSON_GetObjectItem(root, "array");
		if (array) {
			int count = cJSON_GetArraySize(array);
			for (int i=0; i<count; i++) {
				cJSON *single = cJSON_GetArrayItem(array, i);
				if (single) {
					char *sn      = cJSON_GetValueString(single, "sn",      NULL);
					char *token   = cJSON_GetValueString(single, "token",   NULL);
					char *project = cJSON_GetValueString(single, "project", NULL);
					char *model   = cJSON_GetValueString(single, "model",   NULL);
					if (sn && token && project && model) {
						emq_request_t emq_request = {};
						snprintf(emq_request.sn,       sizeof(emq_request.sn),       "%s", sn);
						snprintf(emq_request.token,    sizeof(emq_request.token),    "%s", token);
						snprintf(emq_request.project,  sizeof(emq_request.project),  "%s", project);
						snprintf(emq_request.model,    sizeof(emq_request.model),    "%s", model);
						snprintf(emq_request.org_file, sizeof(emq_request.org_file), "%s", argv[2]);
						snprintf(emq_request.dst_file, sizeof(emq_request.org_file), "%s", argv[3]);
						do_single_generation(SINGLE_OPTION_SIX_ITEM, &emq_request);
					}
				}
			}
		}
	}
	if (root)
		cJSON_Delete(root);
}

static void arg_parse(uint8_t argc, char *argv[])
{
#define OPTION ":ashv"
#define MAKE_SURE(p, func) ({if(p) func;})
	int c;
	emq_request_t emq_request = {};

	while ((c = getopt_long(argc, argv, OPTION, LONGOPTS, NULL)) != -1)
	{
		switch (c)
		{
		case 'a':
		case LOPT_ALL:
			/********************* 
			* PARAM INTRODUCTION *
			* argv[0]: emqtt     *
			* argv[1]: -a        *
			* argv[2]: org file  *
			* argv[3]: dst file  *
			**********************/
			do_all_generation(argc, (const char **)argv);
			break;

		case 's':
		case LOPT_SINGLE:
			/********************* 
			* PARAM INTRODUCTION *
			* argv[0]: emqtt     *
			* argv[1]: -s        *
			* argv[2]: sn        *
			* argv[3]: project   *
			* argv[4]: model     *
			* argv[5]: token     *
			* argv[6]: dst file  *
			**********************/
			MAKE_SURE(argv[2], snprintf(emq_request.sn,       sizeof(emq_request.sn),       "%s", argv[2]));
			MAKE_SURE(argv[3], snprintf(emq_request.project,  sizeof(emq_request.project),  "%s", argv[3]));
			MAKE_SURE(argv[4], snprintf(emq_request.model,    sizeof(emq_request.model),    "%s", argv[4]));
			MAKE_SURE(argv[5], snprintf(emq_request.token,    sizeof(emq_request.token),    "%s", argv[5]));
			MAKE_SURE(argv[6], snprintf(emq_request.dst_file, sizeof(emq_request.dst_file), "%s", argv[6]));
			do_single_generation(argc, &emq_request);
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
#undef MAKE_SURE
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
