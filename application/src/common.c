#include "project_includer.h"

/*----------------------------------------------*
 | 常量区                                       |
 *----------------------------------------------*/
static const char *CODE_PAGE_NAMES[] = {
	"0) CP437: USA, Standard Europe",
	"1) Katakana",
	"2) CP850: Multilingual",
	"3) CP860: Portuguese",
	"4) CP863: Canadian-French",
	"5) CP865: Nordic",
	"11) CP851: Greek",
	"12) CP853: Turkish",
	"13) CP857: Turkish",
	"14) CP737: Greek",
	"15) ISO8859-7: Greek",
	"16) CP1252",
	"17) CP866: Cyrillic #2",
	"18) CP852: Latin2",
	"19) CP858: Euro",
	"20) KU42: Thai",
	"21) TIS11: Thai",
	"26) TIS18: Thai",
	"30) TCVN-3: Vientamese",
	"31) TCVN-3: Vientamese",
	"32) CP720: Arabic",
	"33) CP775: Baltic Rim",
	"34) CP855: Cylillic",
	"35) CP861: Icelandic",
	"36) CP862: Hebrew",
	"37) CP864: Arabic",
	"38) CP869: Greek",
	"39) ISO8859-2: Latin2",
	"40) ISO8859-15: Latin9",
	"42) CP1118: Lithuanian",
	"44) CP1125: Ukranian",
	"45) CP1250: Latin 2",
	"46) CP1251: Cyrillic",
	"47) CP1253: Greek",
	"48) CP1254: Turkish",
	"49) CP1255: Hebrew",
	"50) CP1256: Arabic",
	"51) CP1257: Baltic Rim",
	"52) CP1258: Vientamese",
	"53) KZ1048: Kazakhstan"
};

static const unsigned char CODE_PAGE_NUM[] = {
	0,
	1,
	2,
	3,
	4,
	5,
	11,
	12,
	13,
	14,
	15,
	16,
	17,
	18,
	19,
	20,
	21,
	26,
	30,
	31,
	32,
	33,
	34,
	35,
	36,
	37,
	38,
	39,
	40,
	42,
	44,
	45,
	46,
	47,
	48,
	49,
	50,
	51,
	52,
	53
};

/*----------------------------------------------*
 | 函数区                                       |
 *----------------------------------------------*/
static int PrintBurnPage(void)
{
	LogDbg("init");

	char *PrtData = NULL;
	size_t PrtDataLen = 0;
	char buf[128] = {};

	int bytes_per_line = sys_global_var()->prt_dots_per_line/12; //修改字体宽度是需要同步修改这个
	FILE *fp = open_memstream(&PrtData,&PrtDataLen);
	fputs("\x1B\x40", fp); //init printer
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\x00",1,8,fp); //default ASCII 12X24
	//fputs("\x1B\x33\x01",fp);
	fputs("\n",fp);

	memset(buf,'H',bytes_per_line);
	buf[bytes_per_line] = '\n';
	fputs(buf,fp);
	fputs(buf,fp);
	//fwrite("\x1D\x56\x31", 1, 3, fp); //partial cut
	//fputs("\x1B\x33\x06",fp);
	fputs("\n",fp);
	fclose(fp);

	print_channel_send_data(PRINT_CHN_ID_INT, (unsigned char *)PrtData, PrtDataLen, 1);
	free(PrtData);

	return 0;
}

int PrintTestPage(void)
{
	LogDbg("init");
	int ret = -1;
	char *PrtData = NULL;
	size_t PrtDataLen = 0;
	int i = 0, cnt = 0;
	unsigned int *bad_point_p = NULL;
	wifi_detail_info_t wifi_detail_info = {};

	char buf[128]={};
	int len = badpoint_info_from_file(buf,sizeof(buf),NULL);
	
	if (len<0)
		return ret;

	char *fmt = NULL;
	FILE *fp = open_memstream(&PrtData, &PrtDataLen);
	fwrite("\x1B\x40", 1, 2, fp);						  //init printer
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\x00", 1, 8, fp); //ASCII 12x24
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x00", 1, 8, fp); //GB18030
	fwrite("\x1D\x28\x45\x03\x00\x06\x02\x00", 1, 8, fp); //CP437
	fwrite("\x1D\x28\x45\x03\x00\x06\x03\x00", 1, 8, fp); //UTF-8 Off

	fputs("\x1B\x21\x18", fp);
	fputs("\x1B\x61\x31", fp); //center align
	fputs("\n** TEST PAGE **\n\n", fp);
	fwrite("\x1B\x21\x00", 1, 3, fp); //single printer mode
	fwrite("\x1B\x61\x30", 1, 3, fp); //left align

	fmt = "%-13s:%s\n";
	fputs("\n", fp);
	fprintf(fp, fmt, "Brand", SUNMI_BRAND_NAME);
	fprintf(fp, fmt, "Model", sys_global_var()->model);
	fprintf(fp, fmt, "Serial num", sys_global_var()->sn);
	fputs("\n", fp);

	fmt = "%-20s:%s\n";
	fprintf(fp, fmt, "HW version", sys_global_var()->hw_ver);
	fprintf(fp, fmt, "BootLoader version", sys_global_var()->boot_ver);
	fprintf(fp, fmt, "FW version", sys_global_var()->fw_ver);
	fprintf(fp, fmt, "SUNMI APP version", APP0_VERSION);
	fprintf(fp, fmt, "Partner APP version", APP1_VERSION);
	fprintf(fp, fmt, "MiniAPP version", MINIAPP_VERSION);

	/* Font Version */
	char fontVersion[16] = {};
	print_get_fontlib_version(fontVersion);
	fprintf(fp, fmt, "Font version", fontVersion);

	/* TTS Version */
	audio_cfg_t *audiocfg = NULL;
	audiocfg = getTTSInfo();
	if (audiocfg) fprintf(fp, fmt, "TTS version", audiocfg->tts.version);
	else fprintf(fp, fmt, "TTS version", "");
	fprintf(fp, fmt, "Drawer", SELF_TEST_STRING_YES);
	fprintf(fp, fmt, "Cover open sensor", SUNMI_COVER_OPEN_SENSOR);
	fprintf(fp, fmt, "End paper sensor", SUNMI_END_PAPER_SENSOR);
	fprintf(fp, fmt, "Near-end sensor", SUNMI_NEAR_END_SENSOR);
	fprintf(fp, fmt, "Take paper sensor", SUNMI_TAKE_PAPER_SENSOR);
	fprintf(fp, fmt, "Block sensor", SUNMI_PAPER_BLOCK_SENSOR);
	fprintf(fp, fmt, "Auto cutter", SUNMI_AUTO_CUTTER_SENSOR);
	fprintf(fp, fmt, "Black mark mode", SUNMI_BLACK_MARK_MODE);
	fprintf(fp, fmt, "Label mode", SUNMI_LABEL_MARK_MODE);
	fprintf(fp, fmt, "Alarm light", SUNMI_ALARM_LIGHT);

	fprintf(fp, fmt, "Print head dead dots", "");
	cnt = len / 4; // cnt of 4byte
	bad_point_p = (unsigned int *)buf;
	/* 每行字符数sys_global_var()->prt_dots_per_line/12 每个int占9个字符"00000000 "*/
	int num_of_int_perline = sys_global_var()->prt_dots_per_line/12/9;
	for (i = 0; i < cnt; i++)
		fprintf(fp,"%08X%c",bad_point_p[i],((i%num_of_int_perline)==(num_of_int_perline-1))?'\n':' ');
	fputs("\n",fp);

	fmt = "%-13s:%s\n";
	char longitude[32] = {}, latitude[32] = {};
	sys_get_lbs(longitude, latitude);
	fprintf(fp, fmt, "Longitude", longitude);
	fprintf(fp, fmt, "Latitude", latitude);
	fputs("\n", fp);

	fmt = "%-20s:%s\n";
	fputs("## Receipt ##\n", fp);
	fprintf(fp, "Print density :%s\n", SUNMI_PRINTER_DENSITY);
	fprintf(fp, "Resolution    :%s\n", SUNMI_PRINTER_RESOLUTION);
	fprintf(fp, "Print speed   :%s\n", SUNMI_PRINTER_SPEED);

	print_settings_t pring_settings;
	if (print_get_settings(&pring_settings)) {
		fprintf(fp, "Encoding      :\n");
		fprintf(fp, "Code page     :\n");
	}
	else {
		fprintf(fp, "Encoding      :");
		if (pring_settings.Utf8_WordSet == 1)
			fprintf(fp, "UTF-8\n");
		else {
			switch (pring_settings.CJK_WordSet) {
			case 1:   fprintf(fp, "BIG5\n");      break;
			case 11:  fprintf(fp, "Shift_JIS\n"); break;
			case 12:  fprintf(fp, "JIS0208\n");   break;
			case 21:  fprintf(fp, "KSC5601\n");   break;
			case 128: fprintf(fp, "None\n");      break;
			default:  fprintf(fp, "GB18030\n");   break;
			}
		}
		fprintf(fp, "Code page     :");
		for (i = 0; i < 40; i++) {
			if (pring_settings.CodePage == CODE_PAGE_NUM[i])
				break;
		}
		if (i < 40) {
			char *s, name[32];
			s = strchr(CODE_PAGE_NAMES[pring_settings.CodePage], ' ');
			if (s) {
				strncpy(name, s + 1, sizeof(name) - 1);
				name[31] = 0;
				s = strchr(name, ':');
				if (s)
					(*s) = 0;
				fprintf(fp, "%s", name);
			}
		}
		fprintf(fp, "\n");
	}
	fprintf(fp, "\n");

	fputs("## Interface ##\n", fp);
	fputs("[USB]\n", fp);
	fputs(SUNMI_PRINTER_USB_TYPE "\n", fp);
	fprintf(fp,"USB VID:%s\n",SUNMI_USB_VID);
	fprintf(fp,"USB PID:%s\n",SUNMI_USB_PID);
	fputs("\n", fp);

	if( sys_global_var()->lan_exist ) {
		char lan_ip[32]={},lan_gw[32]={},*p_gw=lan_gw,lan_netmask[32]={},lan_mac[32]={};

		fmt = "%-13s:%s\n";
		util_get_str_ip("eth0",lan_ip,lan_netmask);
		util_get_str_mac("eth0",lan_mac,sizeof(lan_mac));
		util_get_str_gw("eth0",lan_gw,sizeof(lan_gw));
		fprintf(fp,"[LAN]\n");
		fprintf(fp,fmt,"IP",lan_ip);
		fprintf(fp,fmt,"Netmask",lan_netmask);
		fprintf(fp,fmt,"GateWay",p_gw);
		fprintf(fp,fmt,"MAC", lan_mac);
		fprintf(fp,fmt,"DHCP","Enable");

		char fixed_ip[32]={};
		net_LPrtSrv_ini_get("eth0","ip",fixed_ip,sizeof(fixed_ip));
		if( fixed_ip[0] ) {
			char fixed_netmask[32]={};
			net_LPrtSrv_ini_get("eth0","netmask",fixed_netmask,sizeof(fixed_netmask));
			fprintf(fp,"\n[LAN-Fixed]\n");
			fprintf(fp,fmt,"IP",fixed_ip);
			fprintf(fp,fmt,"Netmask",fixed_netmask);
		}
		fputs("\n",fp);
	}

	if( sys_global_var()->wifi_exist ) {
		char bt_name[64]={};
		fmt = "%-13s:%s\n";
		fputs("[Bluetooth]\n", fp);
		fputs(SUNMI_PRINTER_BLUETOOTH_TYPE "\n", fp);
		fprintf(fp, fmt, "BT Name", get_bt_devname(bt_name, sizeof(bt_name)));
		fprintf(fp, fmt, "BT MAC", sys_global_var()->bt_mac);
		fputs("\n", fp);

		fputs("[WiFi]\n", fp);
		fputs(SUNMI_PRINTER_WIFI_TYPE "\n", fp);
		GetWifiDetail(&wifi_detail_info);
		fprintf(fp, fmt, "IP", wifi_detail_info.strIP);
		fprintf(fp, fmt, "Netmask", wifi_detail_info.strNM);
		fprintf(fp, fmt, "Gateway", wifi_detail_info.gw);
		fprintf(fp, fmt, "MAC", sys_global_var()->wifi_mac);
		fprintf(fp, fmt, "DHCP", SELF_TEST_STRING_ENABLE);
		fputs("\n", fp);

		char fixed_ip[32]={};
		net_LPrtSrv_ini_get("wlan0","ip",fixed_ip,sizeof(fixed_ip));
		if( fixed_ip[0] ) {
			char fixed_netmask[32]={};
			net_LPrtSrv_ini_get("wlan0","netmask",fixed_netmask,sizeof(fixed_netmask));
			fprintf(fp,"\n[WiFi-Fixed]\n");
			fprintf(fp,fmt,"IP",fixed_ip);
			fprintf(fp,fmt,"Netmask",fixed_netmask);
		}
		fputs("\n",fp);
	}

	if( sys_global_var()->wnet_exist ) {
		fmt = "%-8s:%s\n";
		fputs("[Mobile]\n", fp);
		char ip[24]={},imei[32]={}, iccid[32]={},isp_name[32]={};
		fprintf(fp, fmt, "ICCID", wnet_ini_get(WNET_KEY_ICCID,iccid,sizeof(iccid)));
		fprintf(fp, fmt, "IMEI", wnet_ini_get(WNET_KEY_IMEI,imei,sizeof(imei)));
		util_get_str_ip("ppp0",ip,0);
		fprintf(fp, fmt, "IP", ip);
		fprintf(fp, fmt, "Operator", wnet_ini_get(WNET_KEY_ISP,isp_name,sizeof(isp_name)));
		fputs("\n", fp);
	}

	fmt = "%-14s:%s\n";
	fputs("[Sound]\n", fp);
	if (audiocfg) {
		char sys_volume[8] = {};
		snprintf(sys_volume, sizeof(sys_volume), "%d", audiocfg->sys_volume);
		fprintf(fp, fmt, "Volume", sys_volume);
		fprintf(fp, fmt, "TTS Language", audiocfg->tts.lang);
	} else {
		fprintf(fp, fmt, "Volume", "");
		fprintf(fp, fmt, "TTS version", "");
	}
	fputs("\n", fp);

	fputs("## Resident character ##\n", fp);
	fputs("Alphanumeric\n", fp);
	fputs("Traditional Chinese\n", fp);
	fputs("Simplified Chinese\n", fp);
	fputs("Japanese\n", fp);
	fputs("Korean\n", fp);
	fputs("\n", fp);

	fputs("## Alphanumeric Font ##\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\x00", 1, 8, fp); //back to default ASCII 12X24
	fputs("ASCII(9x17)\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\x01", 1, 8, fp); //ASCII(9x17)
	fputs("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\x00", 1, 8, fp); //back to default ASCII 12X24
	fputs("ASCII(12x24)\n", fp);
	fputs("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n", fp);
	fputs("\n", fp);

	fputs("## Chinese Font ##\n", fp);
	fputs("GB18030(24x24)\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x00", 1, 8, fp);												   //GB18030(24x24)
	fwrite("\xB0\xA1\xB0\xA2\xB0\xA3\xB0\xA4\xB0\xA5\xB0\xA6\xB0\xA7\xB0\xA8\xB0\xA9\xB0\xAA", 1, 20, fp); //10 chinese characters
	fputs("\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x01",1,8,fp);														   //BIG5(24x24)
	fwrite("\xBE\x70\xBE\x71\xBE\x72\xBE\x73\xBE\x74\xBE\x75\xBE\x76\xBE\x77\xBE\x78\xBE\x79", 1, 20, fp); //10 chinese characters
	fputs("\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x00", 1, 8, fp); //back to default GB18030 24x24
	fputs("\n\n", fp);

	fputs("## Japanese Font ##\n", fp);
	fputs("Shift_JIS(24x24)\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x0B", 1, 8, fp);												   //Shift_JIS(24x24)
	fwrite("\x82\x9F\x82\xA0\x82\xA1\x82\xA2\x82\xA3\x82\xA4\x82\xA5\x82\xA6\x82\xA7\x82\xA8", 1, 20, fp); //10 Japanese characters
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x00", 1, 8, fp); //back to default GB18030 24x24
	fputs("\n\n", fp);

	fputs("## Korean Font ##\n", fp);
	fputs("KSC5601(24x24)\n", fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x15", 1, 8, fp);												   //KSC5601(24x24)
	fwrite("\xB0\xA1\xB0\xA2\xB0\xA3\xB0\xA4\xB0\xA5\xB0\xA6\xB0\xA7\xB0\xA8\xB0\xA9\xB0\xAA", 1, 20, fp); //10 Korean characters
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x00", 1, 8, fp);												   //back to default GB18030 24x24
	fputs("\n\n", fp);

	fputs("## Code Page ##\n", fp);
	fwrite("\x1C\x2E", 1, 2, fp); //Cancel Kanji mode
	uint32_t codepage, charcode;
	for (codepage = 0; codepage < 40; codepage++)
	{
		u_int8_t buff[256]={}, bufflen = 0;
		fputs(CODE_PAGE_NAMES[codepage], fp);
		fwrite("\x0A\x1B\x74", 1, 3, fp);
		buff[bufflen++] = CODE_PAGE_NUM[codepage];
		for (charcode = 0x80; charcode <= 0xFF; charcode++)
			buff[bufflen++] = charcode;
		fwrite(buff, 1, sizeof(buff), fp);
		fputs("\n", fp);
	}

	fwrite("\x1C\x26", 1, 2, fp); //Select Kanji mode
	fwrite("\x1B\x74\x00", 1, 3, fp);
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\x00", 1, 8, fp); //back to default GB18030(24x24) m=1, n=0 to remove code page function
	fputs("\n", fp);

	fwrite("\x1D\x68\x30", 1, 3, fp); //bar code height  x30=48
	fwrite("\x1D\x77\x03", 1, 3, fp); //bar code module width
	fwrite("\x1D\x48\x32", 1, 3, fp); //below the barcode

	fputs("## Barcode Types ##", fp);
	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nUPC-A\n", fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x00"
		   "123456789012"
		   "\x00",
		   1, 16, fp); //GS k m d1...dk NUL; m=0 is UPC-A

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nUPC-E\n", fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x01"
		   "123456"
		   "\x00",
		   1, 10, fp); //GS k m d1...dk NUL; m=1 is UPC-E

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nEAN-13\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x02"
		   "123456789012"
		   "\x00",
		   1, 16, fp); //GS k m d1...dk NUL; m=2 is EAN-13

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nEAN-8\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x03"
		   "12345678"
		   "\x00",
		   1, 12, fp); //GS k m d1...dk NUL; m=3 is EAN-8

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 11\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x55\x08"
		   "12345678",
		   1, 12, fp); //GS k m n d1...dn; m=85 is CODE11

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 39\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x04"
		   "1234ABCD"
		   "\x00",
		   1, 12, fp); //GS k m d1...dk NUL; m=4 is CODE39

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 39 Extended\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x56\x08"
		   "1234abcd",
		   1, 12, fp); //GS k m n d1...dn; m=86 is CODE39 Extended

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 2 of 5 Interleaved\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x05"
		   "1234567890"
		   "\x00",
		   1, 14, fp); //GS k m d1...dk NUL; m=5 is Code 2 of 5 Interleaved

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 2 of 5 Matrix\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x51\x0A"
		   "1234567890",
		   1, 14, fp); //GS k m n d1...dn; m=81 is Code 2 of 5 Matrix

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 2 of 5 Industrial\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x52\x0A"
		   "1234567890",
		   1, 14, fp); //GS k m n d1...dn; m=82 is Code 2 of 5 Industrial

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 2 of 5 IATA\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x53\x0A"
		   "1234567890",
		   1, 14, fp); //GS k m n d1...dn; m=83 is Code 2 of 5 IATA

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 2 of 5 Datalogic\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x54\x0A"
		   "1234567890",
		   1, 14, fp); //GS k m n d1...dn; m=84 is Code 2 of 5 Datalogic

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCODABAR\n", fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x06"
		   "AB1234CD"
		   "\x00",
		   1, 12, fp); //GS k m d1...dk NUL; m=6 is CODABAR

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 93\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x48\x08"
		   "ABcd1234"
		   "\x00",
		   1, 13, fp); //GS k m d1...dk NUL; m=x48=72 is CODE93, n=x08

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nCode 128\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x49\x08"
		   "ABcd1234"
		   "\x00",
		   1, 13, fp); //GS k m d1...dk NUL; m=x49=73 is CODE128, n=x08

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nGS1 DataBar\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x57\x0D"
		   "1234567890123",
		   1, 17, fp); //GS k m n d1...dn; m=87 is GS1 DataBar

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nGS1 DataBar Expanded\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x58\x0A"
		   "[86]123456",
		   1, 14, fp); //GS k m n d1...dn; m=88 is GS1 DataBar Expanded

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nMSI Plessey\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fwrite("\x1D\x6B\x59\x06"
		   "123456",
		   1, 10, fp); //GS k m n d1...dn; m=89 is MSI Plessey

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nPDF417\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp);						  //center align
	fwrite("\x1D\x28\x6B\x03\x00\x30\x43\x03", 1, 8, fp);	 //GS ( k cn=48 fn=67
	fwrite("\x1D\x28\x6B\x03\x00\x30\x44\x08", 1, 8, fp);	 //GS ( k cn=48 fn=68
	fwrite("\x1D\x28\x6B\x04\x00\x30\x45\x30\x02", 1, 9, fp); //GS ( k cn=48 fn=69
	fwrite("\x1D\x28\x6B\x09\x00\x30\x50\x30"
		   "123456",
		   1, 14, fp);									  //GS ( k cn=48 fn=80
	fwrite("\x1D\x28\x6B\x03\x00\x30\x51\x30", 1, 8, fp); //GS ( k cn=48 fn=81

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nPDF417 Truncated\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp);					  //center align
	fwrite("\x1D\x28\x6B\x03\x00\x30\x46\x01", 1, 8, fp); //GS ( k cn=48 fn=70
	fwrite("\x1D\x28\x6B\x09\x00\x30\x50\x30"
		   "123456",
		   1, 14, fp);									  //GS ( k cn=48 fn=80
	fwrite("\x1D\x28\x6B\x03\x00\x30\x51\x30", 1, 8, fp); //GS ( k cn=48 fn=81

	fwrite("\x1B\x61\x30", 1, 3, fp); //left align
	fputs("\nQR Code\n",fp);
	fwrite("\x1B\x61\x31", 1, 3, fp);						  //center align
	fwrite("\x1D\x28\x6B\x04\x00\x31\x41\x00\x00", 1, 9, fp); //GS ( k cn=49 fn=65
	fwrite("\x1D\x28\x6B\x03\x00\x31\x43\x08", 1, 8, fp);	 //GS ( k cn=49 fn=67
	fwrite("\x1D\x28\x6B\x03\x00\x31\x45\x32", 1, 8, fp);	 //GS ( k cn=49 fn=69
	fwrite("\x1D\x28\x6B\x19\x00\x31\x50\x30"
		   "https://www.sunmi.com/",
		   1, 30, fp);									  //GS ( k cn=49 fn=80
	fwrite("\x1D\x28\x6B\x03\x00\x31\x51\x30", 1, 8, fp); //GS ( k cn=49 fn=81

	fwrite("\x1B\x21\x18", 1, 3, fp);
	fwrite("\x1B\x61\x31", 1, 3, fp); //center align
	fputs("\n\n** COMPLETE **\n\n\n\n\n\n",fp);
	fwrite("\x1D\x56\x31", 1, 3, fp); //partial cut
	fwrite("\x1B\x40", 1, 2, fp); //init printer
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\xFF", 1, 8, fp); //Restore settings
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\xFF", 1, 8, fp); //Restore settings
	fwrite("\x1D\x28\x45\x03\x00\x06\x02\xFF", 1, 8, fp); //Restore settings
	fwrite("\x1D\x28\x45\x03\x00\x06\x03\xFF", 1, 8, fp); //Restore settings
	fclose(fp);

	int task_id;
	task_id = print_channel_send_data(PRINT_CHN_ID_INT, PrtData, PrtDataLen, 1);
	if (task_id > 0)
		print_task_set_flag(task_id, TASK_FLAG_DISCARD_DATA_ON_OOP);

	free(PrtData);

	return 0;
}

static void *BurnInTest_thread(void *arg)
{
	LogDbg("init");
	int total_time = 8*60; //打印老化测试时总长，单位分钟
	int step_time = 3; //打印每单的间隔时间，单位分钟
	int start_time = SysTick() / 60000; //系统时间毫秒转化成单位分钟;
	int step_cnt = 0; //每打印一单，时间后移一个step time
	int run_first = 0;

	LogDbg("start burnin test total time %d min, step time %d min", total_time, step_time);
	prctl(PR_SET_NAME, "App-BurnInTest_thread");

	while ((start_time + total_time) >= (SysTick() / 60000) && g_app_vars.isBurnInTest)
	{
		if ((start_time + (step_cnt * step_time)) == (SysTick() / 60000))
		{
			LogDbg("print now count %d time %d min", step_cnt, SysTick() / 60000);
			step_cnt ++;
			if(run_first)
				AudioPlayFixed(BURNINTESTSTART, 1, 0);
			PrintBurnPage();
			run_first = 1;
			if(step_cnt == (total_time / step_time))
				break;
		}
		SysDelay(1000);
	}
	SysDelay(5000);
	PrintInfoPage();

	while(g_app_vars.isBurnInTest)
	{
		AudioPlayFixed(BURNINTESTFINISH, 1, 0);
		SysDelay(5000);
	}

	//run_first = 0;
	pthread_exit(0);
}

void burnin_test_init(void)
{
	pthread_t tid;
	if (pthread_create(&tid, NULL, &BurnInTest_thread, NULL))
	{
		LogError("BurnInTest_thread create fail");
		return;
	}
	pthread_detach(tid);
}

enum
{
	NO_PAPER_STAT = 0,
	TOO_HOT_STAT,
	//MOTOR_TOO_HOT_STAT,
	DOOR_OPEN_STAT,
	PAPER_JAM_STAT,
	PAPER_TAKEN_STAT,
	PAPER_RUN_OUT_STAT,
	VOLTAGE_TOO_LOW_STAT,
	VOLTAGE_TOO_HIGH_STAT,
	ABNORMAL_STAT_MAX
};
static const short g_stat_audio_ids[ABNORMAL_STAT_MAX] = {
	NO_PAPER,
	TOO_HOT,
	//MOTOR_TOO_HOT,
	DOOR_OPEN,
	PAPER_JAM,
	PAPER_TAKEN,
	PAPER_RUN_OUT,
	VOLTAGE_TOO_LOW,
	VOLTAGE_TOO_HIGH,
};

static timer_t timerlist[ABNORMAL_STAT_MAX] = {};
static void timerHandler(union sigval v)
{
#define BEE_FILE_PATH "/home/audio/bee.wav"
	if( v.sival_int>=0 && v.sival_int<ABNORMAL_STAT_MAX ) {
		int last_chn_id = print_get_last_channel_id();
		if ((v.sival_int == PAPER_TAKEN_STAT/* || v.sival_int == PAPER_JAM_STAT*/) &&
			(last_chn_id >= PRINT_CHN_ID_TCP_BEGIN && last_chn_id <= PRINT_CHN_ID_TCP_END) &&
			access(BEE_FILE_PATH, F_OK) == 0) {
			AudioPlayFile(BEE_FILE_PATH, 1, 0);
		} else {
			AudioPlayFixed(g_stat_audio_ids[v.sival_int], 1, 0);
		}
	}
#undef BEE_FILE_PATH
}

#define TIMER_INTERVAL_SECONDS 12 /* Timer interval, take audio play time-consuming into consideration */
#define TIMER_INTERVAL_NANOS (100 * 1000 * 1000) /* Initial expiration */
static void AppStatusTimer(int index,bool en)
{
	if( timerlist[index] && en ) return; //timer already start
	if( !timerlist[index] && !en) return; //timer already stop

	LogWarn("%s timer(%d)",en?"Start":"Stop",index);
	if( en ) {
		struct sigevent evp = {};
		evp.sigev_value.sival_int = index;
		evp.sigev_notify = SIGEV_THREAD;
		evp.sigev_notify_function = timerHandler;

		if (timer_create(CLOCK_MONOTONIC, &evp, &timerlist[index]) < 0) {
			localDbg("timer_create errorstr:%s", strerror(errno));
			return;
		}

		struct itimerspec it = {};
		int last_chn_id = print_get_last_channel_id();
		if ((index == PAPER_TAKEN_STAT || index == PAPER_JAM_STAT) &&
			(last_chn_id >= PRINT_CHN_ID_TCP_BEGIN && last_chn_id <= PRINT_CHN_ID_TCP_END)) {
			it.it_interval.tv_sec = 1;
			it.it_value.tv_nsec = TIMER_INTERVAL_NANOS;
		} else {
			it.it_interval.tv_sec = TIMER_INTERVAL_SECONDS;
			it.it_value.tv_nsec = TIMER_INTERVAL_NANOS;
		}

		if (timer_settime(timerlist[index], 0, &it, NULL) < 0) {
			localDbg("timer_settime errorstr:%s", strerror(errno));
		}
	} else {
		AudioDelListNode(g_stat_audio_ids[index]);
		timer_delete(timerlist[index]);
		timerlist[index] = 0;
	}
}

print_status_t gt_printerStatus = {};
static void status_change_handler(const print_status_t *status)
{
	static uint32_t cbots = 0; /* cashbox open times since power on*/

	memcpy(&gt_printerStatus, status, sizeof(print_status_t));
	libam_handler()->notify(AM_NOTIFY_TYPE_STATUS, &gt_printerStatus, sizeof(gt_printerStatus));

	/* 缺纸 */
	AppStatusTimer(NO_PAPER_STAT,status->no_paper && !status->platen_opened);

	/* 堵纸 */
	AppStatusTimer(PAPER_JAM_STAT,status->paper_jam);

	/* 取纸 */
	if ( (get_paper_not_taken_actions() & PAPER_NOT_TAKEN_ACTION_PLAY_AUDIO) && !status->platen_opened )
		AppStatusTimer(PAPER_TAKEN_STAT, status->paper_not_taken);
	else
		AppStatusTimer(PAPER_TAKEN_STAT, 0);

	/* 开盖 */
	AppStatusTimer(DOOR_OPEN_STAT,status->platen_opened);

#if 0
	/* 纸耗尽 */
	AppStatusTimer(PAPER_RUN_OUT_STAT,status->paper_running_out);
#endif

	/* 打印头过热 */
	/* 马达过热 */
	AppStatusTimer(TOO_HOT_STAT,status->thermal_head_overheated||status->step_motor_overheated);

	/* 电压高 */
	AppStatusTimer(VOLTAGE_TOO_HIGH_STAT,status->vpp_too_high);

	/* 电压低 */
	AppStatusTimer(VOLTAGE_TOO_LOW_STAT,status->vpp_too_low);

	/* 钱箱 */
	gCashboxOpentimes = status->cashbox_open_count - cbots;
	cbots = status->cashbox_open_count;

	/* 灯控制 */
	if( status->thermal_head_overheated || status->step_motor_overheated || status->vpp_too_low || status->vpp_too_high ) {
		led_ctrl(LED_ID_ERROR,LED_OP_BLINK); // 红灯闪烁
	} else if( status->platen_opened || status->no_paper ) {
		led_ctrl(LED_ID_ERROR,LED_OP_ON);	// 红灯常亮
	} else {
		led_ctrl(LED_ID_ERROR,LED_OP_OFF);	// 红灯常灭
	}

	if ( !status->platen_opened && (status->paper_not_taken || status->paper_jam) )
		led_ctrl(LED_ID_TAPE,LED_OP_BLINK);	// 白灯闪
	else
		led_ctrl(LED_ID_TAPE,LED_OP_OFF);	// 白灯灭
}

void server_api_load(void)
{
	while (print_api_init())
		SysDelay(1000);
	LogInfo("init print server success!!!");

	print_status_change_subscribe(status_change_handler);
	print_status_t status = {};
	if (print_get_status(&status) == 0)
		status_change_handler(&status);
}

void am_service_init(void)
{
#ifdef __AM_SERVICE_SUPPORT__
	am_service_start();
#endif
}

void loop_sleep(void)
{
	while (1)
		sleep(100);
}

void app_set_lbs(char *lngBuff, char *latBuff)
{
	if (is_lbs_got)
		return;
	char cmd[256] = {};
	snprintf(cmd, sizeof(cmd), "%s,%s", lngBuff, latBuff);
	sys_save_lbs(cmd);
}
