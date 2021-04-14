#include "qrcode.h"


/*----------------------------------------------*
 | define                                      |
 *----------------------------------------------*/
#define BASE64_BASIC_CHARSETS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
#define AES_ENCRYPT_TEST_KEY  "69fd1caee4a34186a8b1d096bbdac707"
#define AES_ENCRYPT_ONL_KEY   "d38f5dba03944f5fbd83edbb15a85806"
#define QRCODE_SRC_FILE "./org/org.json"
#define QRCODE_DST_FILE "./dst/dst.ini"

#define BLIND_DLD_LINK (0U)
#define SHOW_DLD_LINK  (1U)

#define ALL_OPTION_ITEM          (4U)
#define MERGE_OPTION_ITEM        (6U)
#define SINGLE_OPTION_SEVEN_ITEM (7U)
#define SINGLE_OPTION_EIGHT_ITEM (8U)
#define LogDbg LogError

typedef unsigned char uint8_t;
typedef unsigned int  uint32_t;
typedef struct _qrcode_request_t {
	uint8_t need_download_link; /* 0:not 1:need */
	char sn[64];
	char project[64];
	char model[64];
	char token[64];
	char org_file[256];
	char dst_file[256];
	char qrcode_url[1024];
} __attribute__((packed)) qrcode_request_t;

typedef struct _aes_encrypt_t {
	uint8_t encrypt_bit;
	uint8_t encrypt_type;
	uint8_t padding;
} __attribute__((packed)) aes_encrypt_t;

enum QRCODE_OPTION {
	LOPT_ALL = 1, LOPT_MERGE, LOPT_SINGLE, LOPT_HELP, LOPT_VERSION
};

enum AES_TYPE {
	AES_CBC = 0, AES_CFB, AES_ECB, AES_OFB, AES_PCBC, AES_MAX
};

/*----------------------------------------------*
 | const                                       |
 *----------------------------------------------*/
static const char *QRCODE_VERSION = "0.0.1";
static const char *QRCODE_USAGE =
	"[Usage]: %s [option] [parameters]"
	"\n"
	"\n[Support options]:"
	"\n  -a, --all encryption                    all -a org_file dst_file"
	"\n  -m, --merge src sn file into org.json   all -m model dld_link sn_file org_file"
	"\n  -s, --single encryption                 all -s dld_link sn project model token (dst_file)"
	"\n  -h, --help                              all -h"
	"\n  -v, --version                           all -v";

static void show_usage(const char *name)
{
	char *program = strrchr(name, '/');
	printf(QRCODE_USAGE, (program)?(program+1):name);
	printf("\n\n");
	exit(1);
}

static void show_version(void)
{
	LogDbg("Version %s\n", QRCODE_VERSION);
	exit(1);
}

static const struct option LONGOPTS[] = {
	{"all",     optional_argument, NULL, LOPT_ALL},
	{"single",  optional_argument, NULL, LOPT_SINGLE},
	{"help",    0,                 NULL, LOPT_HELP},
	{"version", 0,                 NULL, LOPT_VERSION},
	{NULL,      0,                 NULL, 0}
};

static void write_result_into_file(const qrcode_request_t *qrcode_request, const char *qrcode_url)
{
	if ( qrcode_request && strlen(qrcode_request->dst_file) && qrcode_url )
	{
		FILE *fp = fopen(qrcode_request->dst_file, "a");
		if (!fp) {
			LogError("fopen(%s) error(%d),%s", qrcode_request->dst_file, errno, strerror(errno));
			return;
		}

		/* ini file format start */
		#if 0
		fprintf(fp, "[%s]\n"              ,   qrcode_request->sn);
		fprintf(fp, "SN            = %s\n",   qrcode_request->sn);
		fprintf(fp, "project       = %s\n",   qrcode_request->project);
		fprintf(fp, "model         = %s\n",   qrcode_request->model);
		fprintf(fp, "token         = %s\n",   qrcode_request->token);
		fprintf(fp, "qrcode_url    = %s\n",   qrcode_url);
		#endif
		/* ini file format end */
		fprintf(fp, "%s   %s\n",qrcode_request->sn,qrcode_url);
		fclose(fp);
	}
}

static int base64_encode(const void *source, int len, char *out)
{
	const uint8_t *src = (uint8_t *)source;
	const char *basic  = BASE64_BASIC_CHARSETS;
	char  *dst         = out;
	for( int i=0;i<len;i+=3 )
	{
		uint8_t idx = src[i]>>2;
		*dst++ = basic[idx];
		idx = (src[i]<<4)&0x30;
		if ( i+1>=len ) {
			*dst++ = basic[idx];
			*dst++ = '=';
			*dst++ = '=';
			break;
		}
		idx |= (src[i+1]>>4);
		*dst++ = basic[idx];
		idx = (src[i+1]<<2)&0x3c;
		if ( i+2>=len ) {
			*dst++ = basic[idx];
			*dst++ = '=';
			break;
		}
		idx |= ((src[i+2]>>6)&0x03);
		*dst++ = basic[idx];
		*dst++ = basic[src[i + 2] & 0x3f];
	}
	*dst = 0;
	return dst-out;
}

static int base64_decode(const char *src,void *destination)
{
	uint8_t *dst        = (uint8_t *)destination;
	const   char *p, *s = src, *basic = BASE64_BASIC_CHARSETS;
	int i,n,len=strlen(src);
    for ( i=0;i<len;i+=4 )
    {
		uint8_t t[4] = {0xff,0xff,0xff,0xff};
		for(n=0;n<4;n++) {
			if( i+n>=len || s[i+n]=='=' ) break;
			p = strchr(basic, s[i + n]);
			if( p )	t[n] = p-basic;
		}
        *dst++ = (((t[0]<<2))&0xFC) | ((t[1]>>4)&0x03);
        if ( s[i+2] == '=' ) break;
        *dst++ = (((t[1]<<4))&0xF0) | ((t[2]>>2)&0x0F);
        if ( s[i+3] == '=' ) break;
        *dst++ = (((t[2]<<6))&0xF0) | (t[3]&0x3F);
    }
    return dst-(uint8_t *)destination;
}

static void transfer_special_characters(const char *in, int inlen, void *destination, int outlen)
{
	const char *s = in;
	uint8_t *d    = (uint8_t *)destination;
	int pos = 0;
	for (int i=0; i<inlen; i++) {
		if ( (s[i]>='0' && s[i]<='9') ||
		     (s[i]>='a' && s[i]<='z') ||
		     (s[i] >= 'A' && s[i] <= 'Z') ) {
			d[pos] = s[i];
			pos++;
		} else {
			pos += snprintf(d+pos, outlen, "%%%02x", s[i]);
		}
	}
}

static void create_single_qrcode(uint8_t argc, const qrcode_request_t *qrcode_request)
{
	if ((argc != SINGLE_OPTION_SEVEN_ITEM && argc != SINGLE_OPTION_EIGHT_ITEM) || !qrcode_request) {
		LogError("absence of critical parameters!"); return;
	}
	LogDbg("\nsn:%s\nproject:%s\nmodel:%s\n\n",
	       qrcode_request->sn,qrcode_request->project,qrcode_request->model);
	AES_KEY aes_key;
	uint8_t aes_in[256] = {}, aes_out[256] = {}, user_key[128] = {};
	snprintf(aes_in, sizeof(aes_in), "sn=%s", qrcode_request->sn);
	if ( !strcmp(qrcode_request->token,"onl_token") ||
	     !strcmp(qrcode_request->token,"uat_token") ) {
		 snprintf(user_key, sizeof(user_key), "%s", AES_ENCRYPT_ONL_KEY);
	} else if ( !strcmp(qrcode_request->token, "test_token") ||
	            !strcmp(qrcode_request->token, "dev_token") ) {
		 snprintf(user_key, sizeof(user_key), "%s", AES_ENCRYPT_TEST_KEY);
	} else {
		LogDbg("unknown token %s!", qrcode_request->token);
		return;
	}
	if ( AES_set_encrypt_key(user_key, 256, &aes_key) < 0 ) {
		LogError("AES_set_encrypt_key fail!");
		return;
	}

	/* AES ecb 256 bit nopadding */
	uint32_t aes_in_len = strlen(aes_in);
	for ( int i=0; i<aes_in_len; i+=AES_BLOCK_SIZE ) {
		AES_ecb_encrypt(aes_in+i, aes_out+i, &aes_key, AES_ENCRYPT);
	}
	uint8_t aes_base64[256] = {};
	base64_encode(aes_out, aes_in_len, aes_base64);
	LogDbg("aes_in[%s] aes_base64[%s]", aes_in, aes_base64);

	uint8_t scheme_in[256] = {}, scheme_out[512] = {};
	snprintf(scheme_in, sizeof(scheme_in), "sunmi://printer/bind?sme=%s", aes_base64);
	LogDbg("scheme_in[%s]", scheme_in);
	if ( qrcode_request->need_download_link ) {
		transfer_special_characters(scheme_in, strlen(scheme_in), scheme_out, sizeof(scheme_out));
		LogDbg("scheme_out[%s]", scheme_out);
		uint8_t final_url[1024] = {};
		snprintf(final_url, sizeof(final_url), "https://store.sunmi.com/api/qc/cn/printer?sms=%s", scheme_out);
		LogDbg("RESULT URL[%s]", final_url);
		write_result_into_file(qrcode_request, final_url);
	} else {
		write_result_into_file(qrcode_request, scheme_in);
	}

	return;
}

static void *file_into_buff(const char *fname,int *size)
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

static cJSON *file_into_json(const char *fname)
{
	cJSON *root = NULL;
	char *buf = (char *)file_into_buff(fname, 0);
	if( buf ) {
		root = cJSON_Parse(buf);
		free(buf);
	}
	return root;
}

static void create_all_qrcode(uint8_t argc, const char *argv[])
{
	/*********************
	* PARAM INTRODUCTION *
	* argv[0]: qrcode    *
	* argv[1]: -a        *
	* argv[2]: org file  *
	* argv[3]: dst file  *
	**********************/
	if (argc!=ALL_OPTION_ITEM || !argv) {
		LogError("absence of critical parameters!"); return;
	}

	if ((access(argv[2], F_OK)!=0) || (access(argv[3], F_OK)!=0)) {
		LogError("absence of orgfile or dstfile!"); return;
	}

	cJSON *root = file_into_json(argv[2]);
	if (root) {
		cJSON *array = cJSON_GetObjectItem(root, "array");
		if (array) {
			int count = cJSON_GetArraySize(array);
			for (int i=0; i<count; i++) {
				cJSON *single = cJSON_GetArrayItem(array, i);
				if (single) {
					uint8_t need_download_link = cJSON_GetValueInt   (single, "dld_link", -1);
					char    *sn                = cJSON_GetValueString(single, "sn",       NULL);
					char    *token             = cJSON_GetValueString(single, "token",    NULL);
					char    *project           = cJSON_GetValueString(single, "project",  NULL);
					char    *model             = cJSON_GetValueString(single, "model",    NULL);
					if ((need_download_link == SHOW_DLD_LINK ||
					     need_download_link == BLIND_DLD_LINK) &&
					     sn && token && project && model) {
						qrcode_request_t qrcode_request = {};
						qrcode_request.need_download_link = need_download_link;
						snprintf(qrcode_request.sn,       sizeof(qrcode_request.sn),       "%s", sn);
						snprintf(qrcode_request.token,    sizeof(qrcode_request.token),    "%s", token);
						snprintf(qrcode_request.project,  sizeof(qrcode_request.project),  "%s", project);
						snprintf(qrcode_request.model,    sizeof(qrcode_request.model),    "%s", model);
						snprintf(qrcode_request.org_file, sizeof(qrcode_request.org_file), "%s", argv[2]);
						snprintf(qrcode_request.dst_file, sizeof(qrcode_request.org_file), "%s", argv[3]);
						create_single_qrcode(SINGLE_OPTION_SEVEN_ITEM, &qrcode_request);
					}
				}
			}
		}
	}
	if (root)
		cJSON_Delete(root);
}

static void snfile_into_jsonfile(uint8_t argc, const char *argv[])
{
#define REMOVE_TYPICAL_CHARACTER(s) do { \
		int len = strlen(s); \
		for (int i=0,j=0; i<len; i++,j++) { \
			if ( !isspace(s[i]) ) {s[j]=s[i];} else {s[j]='\0';}}} while(0)

	/*********************
	* PARAM INTRODUCTION *
	* argv[0]: qrcode    *
	* argv[1]: -m        *
	* argv[2]: model     *
	* argv[3]: dld link  *
	* argv[4]: sn file   *
	* argv[5]: org file  *
	**********************/
	if ( argc != MERGE_OPTION_ITEM || !argv ) {
		LogError("absence of critical parameters!"); return;
	}
	if ( access(argv[4],F_OK) || access(argv[5],F_OK) ) {
		LogError("absence of %s or %s!", argv[4], argv[5]); return;
	}
	char *model = (char *)argv[2];
	FILE *sfp = fopen(argv[4], "r");
	FILE *jfp = fopen(argv[5], "w");
	cJSON *root = cJSON_CreateObject();
	cJSON *array = cJSON_CreateArray();
	if ( !sfp || !jfp || !root || !array ) goto EXIT;
	int dld_link = atoi(argv[3]);
	char project[16] = {}, buff[128] = {};
	char *sn = buff;
	if ( strstr(model, "NT2") ) {
		snprintf(project, sizeof(project), "%s", "NT211");
	} else if ( strstr(model, "NT3") ) {
		snprintf(project, sizeof(project), "%s", "NT310");
	} else {
		LogDbg("known model %s!", model); return;
	}
	while ( fgets(buff, sizeof(buff), sfp) ) {
		REMOVE_TYPICAL_CHARACTER(buff);
		cJSON *item = cJSON_CreateObject();
		if ( !item ) goto EXIT;
		cJSON_AddNumberToObject(item, "dld_link", dld_link);
		cJSON_AddStringToObject(item, "sn",       sn);
		cJSON_AddStringToObject(item, "token",    "onl_token");
		cJSON_AddStringToObject(item, "project",  project);
		cJSON_AddStringToObject(item, "model",    model);
		cJSON_AddItemToArray(array, item);
	}
	cJSON_AddItemToObject(root, "array", array);
	char *json_string = cJSON_PrintUnformatted(root);
	fwrite(json_string, 1, strlen(json_string), jfp);

EXIT:
	if (sfp)   fclose(sfp);
	if (jfp)   fclose(jfp);
	if (root)  cJSON_Delete(root);

#undef REMOVE_TYPICAL_CHARACTER
}

static void arg_parse(uint8_t argc, char *argv[])
{
#define OPTION ":amshv"
#define MAKE_SURE(p, func) ({if(p) func;})
	int c;
	qrcode_request_t qrcode_request = {};
	while ((c = getopt_long(argc, argv, OPTION, LONGOPTS, NULL)) != -1)
	{
		switch (c)
		{
		case 'a':
		case LOPT_ALL:
			/*********************
			* PARAM INTRODUCTION *
			* argv[0]: qrcode    *
			* argv[1]: -a        *
			* argv[2]: org file  *
			* argv[3]: dst file  *
			**********************/
			create_all_qrcode(argc, (const char **)argv);
			break;
		case 'm':
		case LOPT_MERGE:
			/*********************
			* PARAM INTRODUCTION *
			* argv[0]: qrcode    *
			* argv[1]: -m        *
			* argv[2]: model     *
			* argv[3]: dld link  *
			* argv[4]: sn file   *
			* argv[5]: org file  *
			**********************/
			snfile_into_jsonfile(argc, (const char **)argv);
			break;
		case 's':
		case LOPT_SINGLE:
			/*********************
			* PARAM INTRODUCTION *
			* argv[0]: qrcode    *
			* argv[1]: -s        *
			* argv[2]: dld link  *
			* argv[3]: sn        *
			* argv[4]: project   *
			* argv[5]: model     *
			* argv[6]: token     *
			* argv[7]: dst file  *
			**********************/
			MAKE_SURE(argv[2], (qrcode_request.need_download_link = atoi(argv[2])                              ));
			MAKE_SURE(argv[3], snprintf(qrcode_request.sn,       sizeof(qrcode_request.sn),       "%s", argv[3]));
			MAKE_SURE(argv[4], snprintf(qrcode_request.project,  sizeof(qrcode_request.project),  "%s", argv[4]));
			MAKE_SURE(argv[5], snprintf(qrcode_request.model,    sizeof(qrcode_request.model),    "%s", argv[5]));
			MAKE_SURE(argv[6], snprintf(qrcode_request.token,    sizeof(qrcode_request.token),    "%s", argv[6]));
			MAKE_SURE(argv[7], snprintf(qrcode_request.dst_file, sizeof(qrcode_request.dst_file), "%s", argv[7]));
			create_single_qrcode(argc, &qrcode_request);
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
	while (1) sleep(6);
}

int main(int argc, char *argv[])
{
	arg_parse(argc, argv);
	//loop_sleep();
	LogError("[GAME OVER]");
	return 0;
}
