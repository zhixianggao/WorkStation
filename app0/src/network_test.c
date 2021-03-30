#include "project_includer.h"

enum {
	WIFI_ST_NOT_CFG = -1,
	WIFI_ST_DISCONN,
	WIFI_ST_CONN,
	WIFI_ST_GOTIP,
};

typedef struct {
	struct ethernet_hdr eth_hdr;
	struct iphdr ip_hdr;
	struct udphdr udp_hdr;
	struct dhcpv4_hdr dhcp_hdr;
}__attribute__ ((__packed__)) dhcp_raw_data_t;

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define IPADDR(x) inet_ntoa(*((struct in_addr *)&x))
static int get_if_mac(const char *if_name, uint8_t *mac)
{
	struct ifreq ifr={};
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name)-1);
	ifr.ifr_name[sizeof(ifr.ifr_name)-1] = '\0';
	if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) != 0)
	{
		LogError("Error getting interface's MAC address:");
		close(sockfd);
		return -1;
	}

	memcpy(mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	close(sockfd);
	//LogDbg("%s mac[%02x:%02x:%02x:%02x:%02x:%02x]",if_name,mac[0],
		//mac[1],mac[2],mac[3],mac[4],mac[5]);
	return 0;
}
static int create_raw_socket(int iface,struct sockaddr_ll *ll)
{
	int sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if(sock < 0) {
		LogError("create raw socket error,%s",strerror(errno));
		return -1;
	} 

	ll->sll_family = AF_PACKET;
	ll->sll_protocol = htons(ETH_P_ALL);
	ll->sll_ifindex = iface; 
	ll->sll_hatype = ARPHRD_ETHER;
	ll->sll_pkttype = PACKET_OTHERHOST;
	ll->sll_halen = 6;

	if( bind(sock, (struct sockaddr *)ll, sizeof(struct sockaddr_ll))<0 )
		LogError("bind raw socket error,%s",strerror(errno));
	return sock;
}

int set_promisc(const char *ifname,int promisc) 
{
	int ret = 0;
	struct ifreq ifr={};
	int sock = socket(AF_INET,SOCK_DGRAM,0);
	if( sock<0 ) {
		LogError("socket error,%s",strerror(errno));
		return -1;
	}

	strcpy(ifr.ifr_name, ifname);
	ifr.ifr_flags = IFF_UP;
	if( promisc )
		ifr.ifr_flags |= (IFF_PROMISC);
	if( ioctl(sock, SIOCSIFFLAGS, &ifr)<0 ){
		LogError("ioctl SIOCSIFFLAGS error,%s",strerror(errno));
		ret = -1;
	}
	close(sock);
	return ret;
}

static unsigned int rand_dhcp_xid()
{
	unsigned int xid;
	srand(time(NULL) ^ (getpid() << 16));
	xid = rand() % 0xffffffff;
	return xid;
}

typedef struct {
	unsigned char options[400];
	int optlen;
} options_info_t;

static int build_option53(int msg_type,options_info_t *opt_info)
{
	unsigned char opt[] = {DHCP_MESSAGETYPE,1,msg_type};
	memcpy(&opt_info->options[opt_info->optlen],opt,sizeof(opt));
	opt_info->optlen += sizeof(opt);
	return 0;
}

static int build_option55(options_info_t *opt_info) 
{
	unsigned char opt[] = {
		DHCP_PARAMREQUEST,5,
		DHCP_SUBNETMASK,
		DHCP_BROADCASTADDR,
		DHCP_ROUTER,
		DHCP_DOMAINNAME,
		DHCP_DNS
	};
	
	memcpy(&opt_info->options[opt_info->optlen],opt,sizeof(opt));
	opt_info->optlen += sizeof(opt);
	return 0;
}

static int build_optioneof(options_info_t *opt_info)
{
	unsigned char opt[] = {DHCP_END};
	memcpy(&opt_info->options[opt_info->optlen],opt,sizeof(opt));
	opt_info->optlen += sizeof(opt);
	return 0;
}

static u_int16_t checksum(u_int16_t *buff, int words) 
{
	unsigned int sum, i;
	sum = 0;
	for(i = 0;i < words; i++){
		sum = sum + *(buff + i);
	}
	sum = (sum >> 16) + sum;
	return (u_int16_t)~sum;
}

static u_int16_t l4_sum(u_int16_t *buff, int words, u_int16_t *srcaddr, u_int16_t *dstaddr, u_int16_t proto, u_int16_t len) 
{
	unsigned int sum, i, last_word = 0;

	/* Checksum enhancement - Support for odd byte packets */
	if((htons(len) % 2) == 1) {
		last_word = *((u_int8_t *)buff + ntohs(len) - 1);
		last_word = (htons(last_word) << 8);
		sum = 0;
		for(i = 0;i < words; i++){
			sum = sum + *(buff + i);
		}
		sum = sum + last_word;
		sum = sum + *(srcaddr) + *(srcaddr + 1) + *(dstaddr) + *(dstaddr + 1) + proto + len;
		sum = (sum >> 16) + sum;
		return ~sum;
	} else {
		/* Original checksum function */
		sum = 0;
		for(i = 0;i < words; i++){
			sum = sum + *(buff + i);
		}

		sum = sum + *(srcaddr) + *(srcaddr + 1) + *(dstaddr) + *(dstaddr + 1) + proto + len;
		sum = (sum >> 16) + sum;
		return ~sum;
	}
}

static int build_discover_packet(unsigned char *iface_mac,options_info_t *opt_info,void *buf,unsigned int xid)
{
	dhcp_raw_data_t *raw_data =(dhcp_raw_data_t *)buf;
	unsigned char dmac[ETHER_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	memcpy(raw_data->eth_hdr.ether_dhost, dmac, ETHER_ADDR_LEN);
	memcpy(raw_data->eth_hdr.ether_shost, iface_mac, ETHER_ADDR_LEN);
	raw_data->eth_hdr.ether_type = htons(ETHERTYPE_IP);

	raw_data->ip_hdr.version = 4;
	raw_data->ip_hdr.ihl = 5;
	raw_data->ip_hdr.tos = 0;
	raw_data->ip_hdr.tot_len = htons(sizeof(*raw_data)-sizeof(raw_data->eth_hdr)+opt_info->optlen);
	raw_data->ip_hdr.id = 0;
	raw_data->ip_hdr.frag_off = 0;
	raw_data->ip_hdr.ttl = 64;
	raw_data->ip_hdr.protocol = IPPROTO_UDP;
	raw_data->ip_hdr.check = 0; // Filled later;
	raw_data->ip_hdr.saddr = INADDR_ANY;
	raw_data->ip_hdr.daddr = INADDR_BROADCAST;
	raw_data->ip_hdr.check = checksum((u_int16_t *)(&raw_data->ip_hdr), raw_data->ip_hdr.ihl << 1);

	raw_data->udp_hdr.source = htons(DHCP_CLIENT_PORT);
	raw_data->udp_hdr.dest = htons(DHCP_SERVER_PORT);
	raw_data->udp_hdr.len = htons(sizeof(struct udphdr) + sizeof(struct dhcpv4_hdr) + opt_info->optlen);
	raw_data->udp_hdr.check = 0; /* UDP checksum will be done after dhcp header*/

	raw_data->dhcp_hdr.dhcp_opcode = DHCP_REQUEST;
	raw_data->dhcp_hdr.dhcp_htype = ARPHRD_ETHER;
	raw_data->dhcp_hdr.dhcp_hlen = ETHER_ADDR_LEN;
	raw_data->dhcp_hdr.dhcp_hopcount = 0;
	raw_data->dhcp_hdr.dhcp_xid = htonl(xid);

	raw_data->dhcp_hdr.dhcp_secs = 0;
	raw_data->dhcp_hdr.dhcp_flags = 0;

	raw_data->dhcp_hdr.dhcp_cip = 0;

	raw_data->dhcp_hdr.dhcp_yip = 0;

	raw_data->dhcp_hdr.dhcp_sip = 0;

	raw_data->dhcp_hdr.dhcp_gip = INADDR_ANY;
	memcpy(raw_data->dhcp_hdr.dhcp_chaddr, iface_mac, ETHER_ADDR_LEN);
	raw_data->dhcp_hdr.dhcp_magic = htonl(DHCP_MAGIC);

	memcpy(raw_data->dhcp_hdr.options, opt_info->options,  opt_info->optlen);
	u_int16_t l4_proto = htons((u_int16_t)IPPROTO_UDP);
	raw_data->udp_hdr.check = l4_sum(
		(u_int16_t *) (&raw_data->udp_hdr), 
		((sizeof(struct dhcpv4_hdr) + opt_info->optlen + sizeof(struct udphdr)) / 2), 
		(u_int16_t *)&raw_data->ip_hdr.saddr, 
		(u_int16_t *)&raw_data->ip_hdr.daddr,
		l4_proto, 
		raw_data->udp_hdr.len); 

    return 0;
}

typedef struct {
	const char *ifname;
	short count;
	struct _dhcp_server_info {
		unsigned char mac[6];
		unsigned int  ip;
	}server[2];
} dhcp_offer_info_t;

// return count of offer
static int recv_offer_packet(int fd,struct sockaddr_ll *ll,unsigned int xid,dhcp_offer_info_t *info)
{
	socklen_t sklen = sizeof(struct sockaddr_ll);
	char buffer[2048];
	dhcp_raw_data_t *raw_data = (dhcp_raw_data_t *)buffer;
	struct timeval tv = {2,0} ;
	fd_set fds;
	int ret=-1;
	
	while( 1 ) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		ret = select(fd + 1, &fds, NULL, NULL, &tv);
		if(ret == 0)
			break;
		if( ret<0 && errno != EINTR ) {
			LogError("select error=%d,%s",errno,strerror(errno));
			break;
		}
		if( ret<0 || !FD_ISSET(fd, &fds) )
			continue;

		bzero(buffer, sizeof(buffer));
		ret = recvfrom(fd,buffer,sizeof(buffer),0,(struct sockaddr *)ll,&sklen);
		if( ret>=sizeof(dhcp_raw_data_t) )
		{
			if( raw_data->eth_hdr.ether_type == htons(ETHERTYPE_IP) && \
				raw_data->ip_hdr.protocol == IPPROTO_UDP && \
				raw_data->dhcp_hdr.dhcp_xid == htonl(xid) && \
				(raw_data->udp_hdr.source == htons(DHCP_SERVER_PORT) || \
					raw_data->udp_hdr.dest == htons(DHCP_CLIENT_PORT)) ) {
				unsigned char *opt = raw_data->dhcp_hdr.options;
				while( *opt != DHCP_END ) {
					if( *opt == DHCP_PAD ) {
						opt ++;
						continue;
					}
					if( *opt == DHCP_MESSAGETYPE && opt[2] == DHCP_MSGOFFER ) {
						char server[32],client[32];
						snprintf(server,sizeof(server),"%s",IPADDR(raw_data->ip_hdr.saddr));
						snprintf(client,sizeof(client),"%s",IPADDR(raw_data->dhcp_hdr.dhcp_yip));
						LogInfo("offer from %02X:%02X:%02X:%02X:%02X:%02X,server:%s,ip:%s",
							raw_data->eth_hdr.ether_shost[0],raw_data->eth_hdr.ether_shost[1],
							raw_data->eth_hdr.ether_shost[2],raw_data->eth_hdr.ether_shost[3],
							raw_data->eth_hdr.ether_shost[4],raw_data->eth_hdr.ether_shost[5],
							server,client);
						if( info->count == 0 ) {
							info->count = 1;
							memcpy(info->server[0].mac,raw_data->eth_hdr.ether_shost,6);
							info->server[0].ip = raw_data->ip_hdr.saddr;
						} else if( memcmp(info->server[0].mac,raw_data->eth_hdr.ether_shost,6) ) {
							memcpy(info->server[1].mac,raw_data->eth_hdr.ether_shost,6);
							info->server[1].ip = raw_data->ip_hdr.saddr;
							info->count++;
							return info->count;
						}
						break;
					} else {
						opt += opt[1]+2;
					}
				}
			}
		} 
	}
	return info->count;
}

static void *dhcp_test(void *arg)
{
	dhcp_offer_info_t *dhcp_offer_info = (dhcp_offer_info_t *)arg;
	struct sockaddr_ll ll = { 0 };	/* Socket address structure */
	char buf[2048];
	options_info_t optinfo={};
	unsigned char iface_mac[ETHER_ADDR_LEN] = { 0 };
	int fd,iface = if_nametoindex(dhcp_offer_info->ifname);

	if( iface==0 ) {
		LogError("if_nametoindex(%s) fail!",dhcp_offer_info->ifname);
		return (void *)-1;
	}
	if(get_if_mac(dhcp_offer_info->ifname, iface_mac) != 0)
		return (void *)-1;

	fd = create_raw_socket(iface,&ll);
	if( fd<0 )
		return (void *)-1;

	unsigned int xid = rand_dhcp_xid();
	build_option53(DHCP_MSGDISCOVER,&optinfo);
	build_option55(&optinfo);
	build_optioneof(&optinfo);
	build_discover_packet(iface_mac,&optinfo,buf,xid);

	int size = sizeof(dhcp_raw_data_t) + optinfo.optlen;
	int i=0,ret;
	for(i=0;i<3;i++)
	{
		ret = sendto(fd,buf,size,0,(struct sockaddr *) &ll,sizeof(ll));
		if( ret<0 ) {
			LogError("sendto error,%s",strerror(errno));
		}
		ret = recv_offer_packet(fd,&ll,xid,dhcp_offer_info);
		if( ret>1)
			break;
	}

	close(fd);
	return (void *)ret;
}

#define NW_TEST_JDATA_LEN (4*1024)
static size_t write_json_data(void *buffer, size_t size, size_t nmemb, void *fp)
{
	char *json = (char *)fp;
	int len = strlen(json);
	int left = NW_TEST_JDATA_LEN-len;
	json += len;
	snprintf(json,left,"%s",(char *)buffer);
	return nmemb;
}

struct checkconfig_rsp_msg {
	long http_code;
	int size;
	char *buf;
};
static void *checkMQTTConfig(void *arg)
{
	struct checkconfig_rsp_msg *msg = (struct checkconfig_rsp_msg *)arg;
	char sign[33],url[1024]={},buf[256];
	char *pos = url;
	char jdata[NW_TEST_JDATA_LEN]={};
	time_t t = time(0);

	snprintf(buf, sizeof(buf), "%s%s%s%s%ld", VALUE_IN_SHAREMEM(cloud_token), sys_global_var()->cloud_url, sys_global_var()->model, sys_global_var()->model, t * 2);
	util_md5sum(buf,strlen(buf),sign);
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "%s/check/checkConfig?msn=%s", sys_global_var()->cloud_url, sys_global_var()->sn);
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "&model=%s", sys_global_var()->model);
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "&serverAddress=%s", sys_global_var()->broker_addr);
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "&port=%s", VALUE_IN_SHAREMEM(broker_port));
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "&username=%s", VALUE_IN_SHAREMEM(broker_uname));
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "&password=%s", VALUE_IN_SHAREMEM(broker_pwd));
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "&httpAddress=%s", sys_global_var()->cloud_url);
	pos += snprintf(pos, sizeof(buf) - (pos - buf), "&token=%s", VALUE_IN_SHAREMEM(cloud_token));
	pos += snprintf(pos,sizeof(buf)-(pos-buf),"&timeStamp=%ld",t);
	pos += snprintf(pos,sizeof(buf)-(pos-buf),"&sign=%s",sign);

	LogDbg("url=%s",url);
	CURL *curl = curl_easy_init();

	if(curl == NULL){
		LogError("curl = NULL");
		return (void *)-3;
	}
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 20*1000);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_json_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)jdata);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	int res = curl_easy_perform(curl);
	if( msg )
		curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&msg->http_code);
	if(res != CURLE_OK)
	{
		LogError("curl_easy_perform fail(%d),%s", res,curl_easy_strerror(res));
		if( msg && msg->buf && msg->size>0 ) {
			snprintf(msg->buf,msg->size,"%s",curl_easy_strerror(res));
		}
		curl_easy_cleanup(curl);
		return (void *)-1;
	}
	curl_easy_cleanup(curl);
	LogDbg("Ret=%s",jdata);
	if( strstr(jdata,"\"success\"") )
		return (void *)0;
	if( msg && msg->buf && msg->size>0 ) {
		char *p0 = strstr(jdata,"\"msg\":");
		if( p0 ) {
			p0 += strlen("\"msg\":");
			char *p = strchr(p0,'\"');
			if( p ) p0 = p+1;
			p = strchr(p0,'\"');
			if( !p ) p = strchr(p0,'}');
			if( p ) *p = 0;
			snprintf(msg->buf,msg->size,"%s",p0);
		}
	}
	return (void *)-1;
}

static void *ping_test(void *arg)
{
	char *dst = (char *)arg;
	int ret=0,count=3;
	char cmd[256],rsp[256];

	if( !dst || strlen(dst) == 0 )
		return (void *)-1;

	char *host = strstr(dst,"://"),*end;
	host = host?host+3:dst;
	end = strchr(host,'/');
	if( !end ) end = host+strlen(host);
	snprintf(cmd,sizeof(cmd),"ping %.*s -c %d -s 4 -q|grep round-trip",end-host,host,count);
	ret = SystemWithResult(cmd, rsp, sizeof(rsp));
	if( ret>0 ) {
		char *p = strchr(rsp,'=');
		p = p?p+1:rsp;
		p = strchr(p,'/');
		if( p ) {
			ret = atoi(p+1);
		} else {
			ret = -1;
		}
	} else {
		ret = -1;
	}
	return (void *)ret;
}

typedef struct {
	const char *ip;
	const char *ifname;
} arping_args_t;
static void *arping_test(void *arg)
{
	arping_args_t *arp_args = (arping_args_t *)arg;
	char cmd[256],rsp[256];
	int i,reply_cnt =0;
	uint8_t eth_mac[6]={},wifi_mac[6]={};

	if( strlen(arp_args->ip)==0 )
		return (void *)-1;

	get_if_mac("wlan0",wifi_mac);
	get_if_mac("eth0",eth_mac);
	snprintf(cmd,sizeof(cmd),"arping %s -I %s -D -f -b -c 3 -w 2|grep \"reply from\"",
		arp_args->ip,arp_args->ifname);
	LogInfo("%s",cmd);
	FILE *fp = popen(cmd,"r");
	if( !fp ) return (void *)0;
	while(fgets(rsp,sizeof(rsp),fp)) {
		char *p0 = strchr(rsp,'[');
		char *p1 = strchr(rsp,']');
		if( !p0 || !p1 ) continue;
		p0 += 1;
		*p1 = 0;
		unsigned int bmac[6] = {};
		uint8_t mac[6];
		int ret = sscanf(p0,"%x:%x:%x:%x:%x:%x",bmac,bmac+1,bmac+2,bmac+3,bmac+4,bmac+5);
		if( ret<6 ) continue;
		for(i=0;i<6;i++) mac[i] = bmac[i];
		if( !memcmp(mac,wifi_mac,6) || !memcmp(mac,eth_mac,6) ) {
			LogWarn("self reply %02x:%02x:%02x:%02x:%02x:%02x",bmac[0],bmac[1],bmac[2],bmac[3],bmac[4],bmac[5]);
			continue;
		}
		LogWarn("arp reply %02x:%02x:%02x:%02x:%02x:%02x",bmac[0],bmac[1],bmac[2],bmac[3],bmac[4],bmac[5]);
		reply_cnt ++;
	}
	pclose(fp);
	return (void *)reply_cnt;
}

static int dbl_height_fprintf(int double_height,FILE *fp,const char *fmt,const char *s1,const char *s2)
{
	fprintf(fp,fmt,s1);
	if( double_height )
		fwrite("\x1b\x21\x10",3,1,fp);
	fprintf(fp,"%s\n",s2);
	if( double_height )
		fwrite("\x1b\x21\x00",3,1,fp);
	return 0;
}

static int print_dead_dots(FILE *fp)
{
	
	char buf[128]={};
	int len = badpoint_info_from_file(buf,sizeof(buf),NULL);
	
	if (len<0)
		len = sys_global_var()->prt_dots_per_line/8;
	int i,cnt = len/4;
	unsigned int *point = (unsigned int*)  buf;
	int num_of_int_perline = sys_global_var()->prt_dots_per_line/12/9;
	for(i=0; i<cnt; i++)
	{
		fprintf(fp,"%s%08X%s",
			(i&&0==(i%num_of_int_perline))?"\n":"",
			point[i],
			(num_of_int_perline-1 == (i%num_of_int_perline))?"":" ");
	}
	fprintf(fp,"\n");
	return 0;
}

#define DelayMs(ms) do { \
	struct timeval tv = {(ms)/1000,((ms)%1000)*1000}; \
	while(select(0,0,0,0,&tv)<0 && errno==EINTR); \
}while(0)
static int gStopTestAlertAudio = 0;
static void *playNwTestingAudio(void *arg)
{
	int i=0,count=36;

	gStopTestAlertAudio = 0;
	while( !gStopTestAlertAudio ) {
		if( (i++%count)==0 ) {
			AudioPlayFixed(NET_CHECKING, 1, 0);
			LogVerbose("Play audio %s",NET_CHECKING);
		}
		DelayMs(100);
	}

	return NULL;
}

#define REPORT_INI_PATH "/home/SYS/net_report.ini"
typedef enum {
	RESULT_S_ERR=-1,
	RESULT_FAIL=0,
	RESULT_PASS=1,
	RESULT_WARN=2,
	RESULT_TIMEOUT=3,
}result_t;

typedef struct {
	int cur_net;
	result_t lan_stat;
	result_t wifi_stat;
	result_t gprs_stat;
	result_t wifi_dhcp_stat;
	result_t wifi_ip_stat;
	result_t ping_wifi_gw;
	result_t lan_dhcp_stat;
	result_t lan_ip_stat;
	result_t ping_lan_gw;
	result_t ping_dns;
	result_t ping_mqtt;
	result_t ping_http;
	result_t mqtt_config;
	result_t mqtt_conn;
	result_t mqtt_rw;
} test_result_info_t;

typedef struct {
	char ip[32];
	char netmask[32];
	char gw[32];
} lan_info_t;

char *ini_get_value_with_section(const char *inifile,const char *section,const char *key,char *value,int size);
#define report_ini_get(_sec,_key,_v,_vlen) ini_get_value_with_section(REPORT_INI_PATH,_sec,_key,_v,_vlen)
static int showTestReport(FILE *fp,const test_result_info_t *result,
	const wifi_detail_info_t *wpa_stat,lan_info_t *lan_info,
	const struct checkconfig_rsp_msg *mqtt_ckcfg_rsp,
	dhcp_offer_info_t *wifi_dhcp_offer,dhcp_offer_info_t *lan_dhcp_offer
)
{
	int index = 0;
	char tmp[1024]={},*p;
	char sep[64]={},lang[33] = {};

	p = app_setting_get("global","language",lang,sizeof(lang));
	if( !p || strlen(lang)==0 ) {
		app_setting_set("global","language","zh-CN");
		strcpy(lang,"zh-CN");
	}
	memset(sep,'-',sys_global_var()->prt_dots_per_line/12);
	p = report_ini_get(lang,"prompt",tmp,sizeof(tmp));
	if( !p ) goto out;
	fprintf(fp,"%s\n%s\n",sep,p);
	if( result->cur_net == NET_NONE ) {
		p = report_ini_get(lang,"no_valid_net",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	}

	if( sys_global_var()->lan_exist && result->lan_stat != RESULT_PASS ) {
		p = report_ini_get(lang,"lan_link_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	}

	if( sys_global_var()->wifi_exist ) {
		if( wpa_stat->st == WIFI_ST_NOT_CFG ) {
			p = report_ini_get(lang,"wifi_need_cfg",tmp,sizeof(tmp));
			if( !p ) goto out;
			fprintf(fp,"%d.%s\n",++index,p);
		} else if( result->wifi_stat != RESULT_PASS ) {
			p = report_ini_get(lang,"wifi_link_fail",tmp,sizeof(tmp));
			if( !p ) goto out;
			fprintf(fp,"%d.%s\n",++index,p);
		}
		}

	if( sys_global_var()->wnet_exist && result->gprs_stat != RESULT_PASS ) {
		p = report_ini_get(lang,"gprs_link_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	}

	if(result->cur_net == NET_NONE)
		goto complete;

	if( sys_global_var()->lan_exist ) {
		if( result->lan_stat == RESULT_PASS ) {
			if( result->lan_dhcp_stat == RESULT_WARN ){
				int i=0;
				p = report_ini_get(lang,"lan_dhcp_conflict",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
				for(;i<lan_dhcp_offer->count;i++) {
					fprintf(fp,"%-14s",IPADDR(lan_dhcp_offer->server[i].ip));
					fprintf(fp," %02x:%02x:%02x:%02x:%02x:%02x\n",
							lan_dhcp_offer->server[i].mac[0],lan_dhcp_offer->server[i].mac[1],
							lan_dhcp_offer->server[i].mac[2],lan_dhcp_offer->server[i].mac[3],
							lan_dhcp_offer->server[i].mac[4],lan_dhcp_offer->server[i].mac[5]);
				}
			}else if( result->lan_dhcp_stat == RESULT_FAIL ){
				p = report_ini_get(lang,"lan_dhcp_fail",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
			}
		}

		if( result->lan_dhcp_stat == RESULT_PASS && result->lan_ip_stat == RESULT_WARN ){
			p = report_ini_get(lang,"lan_ip_conflict",tmp,sizeof(tmp));
			if( !p ) goto out;
			fprintf(fp,"%d.%s\n",++index,p);
			fprintf(fp,"%s\n",lan_info->ip);
			goto complete;
		}

		if( result->lan_dhcp_stat == RESULT_PASS ) {
			if( result->ping_lan_gw == RESULT_FAIL ) {
				p = report_ini_get(lang,"ping_lan_gw_fail",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
			} else if( result->ping_lan_gw == RESULT_WARN ){
				p = report_ini_get(lang,"ping_lan_gw_warn",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
			}
		}
	}

	if( sys_global_var()->wifi_exist ) {
		if( result->wifi_stat == RESULT_PASS ) {
			if( result->wifi_dhcp_stat == RESULT_WARN ){
				int i=0;
				p = report_ini_get(lang,"dhcp_conflict",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
				for(;i<wifi_dhcp_offer->count;i++) {
					fprintf(fp,"%-14s",IPADDR(wifi_dhcp_offer->server[i].ip));
					fprintf(fp," %02x:%02x:%02x:%02x:%02x:%02x\n",
							wifi_dhcp_offer->server[i].mac[0],wifi_dhcp_offer->server[i].mac[1],
							wifi_dhcp_offer->server[i].mac[2],wifi_dhcp_offer->server[i].mac[3],
							wifi_dhcp_offer->server[i].mac[4],wifi_dhcp_offer->server[i].mac[5]);
				}
			}else if( result->wifi_dhcp_stat == RESULT_FAIL ){
				p = report_ini_get(lang,"dhcp_fail",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
			}
		}

		if( result->wifi_dhcp_stat == RESULT_PASS && result->wifi_ip_stat == RESULT_WARN ){
			p = report_ini_get(lang,"ip_conflict",tmp,sizeof(tmp));
			if( !p ) goto out;
			fprintf(fp,"%d.%s\n",++index,p);
			fprintf(fp,"%s\n",IPADDR(wpa_stat->ip));
			goto complete;
		}

		if( result->wifi_dhcp_stat == RESULT_PASS ) {
			if( result->ping_wifi_gw == RESULT_FAIL ) {
				p = report_ini_get(lang,"ping_gw_fail",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
			} else if( result->ping_wifi_gw == RESULT_WARN ){
				p = report_ini_get(lang,"ping_gw_warn",tmp,sizeof(tmp));
				if( !p ) goto out;
				fprintf(fp,"%d.%s\n",++index,p);
			}
		}
	}

	if( result->ping_dns == RESULT_FAIL ) {
		p = report_ini_get(lang,"ping_dns_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	} else if( result->ping_dns == RESULT_WARN && result->wifi_dhcp_stat == RESULT_PASS ) {
		p = report_ini_get(lang,"ping_dns_warn",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	}

	if( result->ping_mqtt == RESULT_FAIL ) {
		p = report_ini_get(lang,"ping_mqtt_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	} else if( result->ping_mqtt == RESULT_WARN && result->wifi_dhcp_stat == RESULT_PASS ) {
		p = report_ini_get(lang,"ping_mqtt_warn",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	}

	if( result->ping_http == RESULT_FAIL ) {
		p = report_ini_get(lang,"ping_http_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	} else if( result->ping_http == RESULT_WARN && result->wifi_dhcp_stat == RESULT_PASS ) {
		p = report_ini_get(lang,"ping_http_warn",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	}

	if( result->ping_http != RESULT_FAIL && result->mqtt_config == RESULT_FAIL ) {
		p = report_ini_get(lang,"mqtt_cfg_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		long http_code = mqtt_ckcfg_rsp?mqtt_ckcfg_rsp->http_code:0;
		fprintf(fp,"%d.%s% ld\n",++index,p,http_code);
		if( mqtt_ckcfg_rsp && mqtt_ckcfg_rsp->buf && strlen(mqtt_ckcfg_rsp->buf) )
			fprintf(fp,"%s\n",mqtt_ckcfg_rsp->buf);
		goto complete;
	}

	if( result->ping_mqtt != RESULT_FAIL && result->mqtt_conn == RESULT_FAIL ) {
		p = report_ini_get(lang,"mqtt_conn_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
		goto complete;
	}

	if( result->ping_mqtt != RESULT_FAIL && result->mqtt_rw == RESULT_FAIL ) {
		p = report_ini_get(lang,"mqtt_rw_fail",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%d.%s\n",++index,p);
	}
complete:
	if( index == 0 ) {
		p = report_ini_get(lang,"success",tmp,sizeof(tmp));
		if( !p ) goto out;
		fprintf(fp,"%s\n",p);
	}
out:
	return 0;
}


/*
	获取字符s二进制方式中高位连续为1的个数.如
	0xxxxxxx 返回0
	10xxxxxx 返回1
	110xxxxx 返回2
	......
*/
int getLeading1(char s)
{
	int w = 0;
	while( s & 0x80 ) {
		w += 1;
		s <<= 1;
	}
	return w;
}
//获取字符串实际占用的打印宽度(单位字节)
//如 strlen("中国")=6 实际打印占4字节宽度
static int getStringWidth(const char *str)
{
	int n = 0,len=strlen(str);
	const char *p = (const char *)str;
	while( *p && p-str<len ) {
		int w = getLeading1(*p);
		if( w ) { //高位连续1格式>=0 则w表示UTF8编码的字节数
			n += 2; // 该UTF8字符占两个字节宽度
			p += w;
		} else { // ascii码，占1字节
			n += 1;
			p += 1;
		}
	}
	return n;
}

static char *fixedWidthString(char *str,int size,int width)
{
	char padding_char=' ';
	int origWidth = getStringWidth(str);

	if( origWidth == width ) return str;
	if( origWidth>width ) {
		padding_char = '*';
		while( origWidth>=width ) {
			int len = strlen(str),w;
			char *p = &str[len-1];
			/* w=getLeading1(*p)  
				w=1 说明该字节时UTF8多字节中的后续字节
				w>1 说明该字节时UTF8多字节编码中的首字节
				w=0 说明该字节是单字节编码(ASCII)
			*/
			while( p>=str && (w=getLeading1(*p))==1 ) p--;
			if( p<=str ) break;
			*p = 0;
			origWidth -= (w?2:1);
		}
	}
	char *p = str+strlen(str);
	int pad_size = width-origWidth,left_size=size+p-str;
	if( pad_size>left_size-1 ) pad_size = left_size-1;
	if( pad_size>0 ) {
		memset(p,padding_char,pad_size);
		p[pad_size] = 0;
	}
	return str;
}

#define StrResultPass "Pass"
#define StrResultFail "FAIL"
#define StrResultTimeOut "TmOut"
#define CMD_GPRS_STATUS		"ifconfig ppp0|grep addr"
#define FIXED_DNS_IP	   	"198.41.0.4"

void GetWifiDetail(wifi_detail_info_t *info)
{
	char buf[1024] = {};
	FILE *fp = fopen("/data/SYS/config","r");

	memset(info,0,sizeof(*info));
	info->st = WIFI_ST_NOT_CFG;
	if( !fp ) {
		LogInfo("open /data/SYS/config fail");
		return;
	}
	fgets(buf,sizeof(buf)-1,fp);
	fclose(fp);
	cJSON *root = cJSON_Parse(buf);
	if( !root ) {
		LogError("cJSON_Parse(%s) fail",buf);
		return;
	}
	int i,count = cJSON_GetArraySize(root);
	for(i=0;i<count;i++) {
		cJSON *item = cJSON_GetArrayItem(root,i);
		cJSON *SETTING = cJSON_GetObjectItem(item,"SETTING");
		if( SETTING && !strcmp(SETTING->valuestring,"WIFI") ) {
			cJSON *D = cJSON_GetObjectItem(item,"D");
			if( D ) {
				cJSON *S = cJSON_GetObjectItem(D,"S");
				if( S ) {
					info->st ++;
					snprintf(info->ssid,sizeof(info->ssid),"%s",S->valuestring);
				}
			}
			break;
		}
	}
	cJSON_Delete(root);
	if( info->st == WIFI_ST_NOT_CFG ) {
		LogWarn("Get SSID from config fail!");
		return;
	}

	if (SystemWithResult("/usr/sbin/wpa_cli -i wlan0 status | grep COMPLETED", buf, sizeof(buf)) <= 0)
		return;
	info->st ++;
	GetInterfaceAddresses("wlan0", &info->ip, &info->netmask);
	if( info->ip.s_addr == 0 || info->ip.s_addr == (uint32_t)-1 ) return;
	info->st ++;
	snprintf(info->strIP,sizeof(info->strIP),"%s",inet_ntoa(info->ip));
	snprintf(info->strNM,sizeof(info->strNM),"%s",inet_ntoa(info->netmask));
	SystemWithResult("/sbin/ip route|grep \"default via \"|grep wlan0", buf, sizeof(buf));
	if( strlen(buf)>strlen("default via ") ){
		char *p0 = buf + strlen("default via ");
		char *p = p0;
		while(*p) { if( *p == ' ') *p = 0; p++; }
		snprintf(info->gw,sizeof(info->gw),"%s",p0);
	}
}

static char *get_net_name(int net_type)
{
	if( net_type==NET_WIFI ) return "WIFI";
	if( net_type==NET_WNET ) return "Mobile";
	if( net_type==NET_LAN  ) return "LAN";
	return "";
}

static void *mqtt_rw_test(void *arg)
{
	bool *en = (bool *)arg;
	pthread_detach(pthread_self());
	while(!is_mqtt_alive() && *en) {
		util_msleep(100);
	}
	sunmi_mqtt_network_test();
	return NULL;
}

static int NetworkTest_WiFIChannelInfo(FILE *fp,const char *wifi_ssid)
{
#define FREQ_TO_CHANNEL(f)  ((f-2407)/5)

#define PRINT_CHANNEL_INFO(fp,cnt,scan_ap,chan,ind)  \
do{\
	int i,prefix=1;\
	char *format;\
	const char* channel_array[]={  \
		"Current channel:",   \
		"Pre channel:",    \
		"Next channel:"   \
	}; \
	if( sys_global_var()->prt_dots_per_line>384 ) \
		format = "%-34s"; \
	else  \
		format = "%-27s"; \
	for( i=0;i<cnt;i++){ \
		if(FREQ_TO_CHANNEL(scan_ap[i].freq) == chan){\
			if(prefix){	\
				char str_chan[8]={};\
				prefix =0 ;\
				snprintf(str_chan,sizeof(str_chan),"%d",chan);\
				dbl_height_fprintf(0,fp,format,channel_array[ind], str_chan);\
			}\
			char str_rssi[8]={};\
			snprintf(str_rssi,sizeof(str_rssi),"%d", scan_ap[i].rssi);\
			if(scan_ap[i].essid[0]!=0x0)\
				dbl_height_fprintf(0,fp,format,scan_ap[i].essid, str_rssi);\
			else\
				dbl_height_fprintf(0,fp,format,scan_ap[i].bssid,str_rssi);\
			}\
	}\
}while(0)

		
	fprintf(fp,"\n[WiFi Channel]\n");
	if(wifi_ssid && wifi_ssid[0]!=0x0) {
		int tryCnt=0;
		
		for(tryCnt=0;tryCnt<3;tryCnt++){
			int i, current_chan=0;
			ST_WIFI_SCAN_RESULT *result = net_wifi_scan();

			int cnt=result->count;
			if(cnt){
				ST_WIFI_SCAN_AP *scan_ap = result->list;
				for( i=0;i < cnt;i++){
					if(!strncmp(wifi_ssid, scan_ap[i].essid,strlen(wifi_ssid))){
						current_chan=FREQ_TO_CHANNEL(scan_ap[i].freq);
						break;
					}
				}

				if(current_chan){
					PRINT_CHANNEL_INFO(fp,cnt,scan_ap,current_chan,0);

					int neighbour_chan_l=current_chan-1;
					int neighbour_chan_h=current_chan +1;
					if(neighbour_chan_l){
						PRINT_CHANNEL_INFO(fp,cnt,scan_ap,neighbour_chan_l,1);
					}
					if(neighbour_chan_h<14){
						PRINT_CHANNEL_INFO(fp,cnt,scan_ap,neighbour_chan_h,2);
					}
				}
				free(result);
				break;
			}else{
				if(result)  
					free(result);
			}
		}
	}
	return 0;
}

static int NetworkTest_BaseInfo(FILE *fp,wifi_detail_info_t *wifi_info,lan_info_t *lan_info)
{
	char tmp[256],*format = "%-20s: ";

	fwrite("\x1B\x40",2,1,fp); //init printer
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\x00",8,1,fp); //default ASCII 12X24

	fwrite("\x1B\x21\x18",3,1,fp);
	fwrite("\x1B\x61\x31",3,1,fp); //center align
	fprintf(fp,"\n** NETWORK PAGE **\n\n");
	fwrite("\x1B\x21\x00",3,1,fp); //single printer mode
	fwrite("\x1B\x61\x30",3,1,fp); //left align
	fwrite("\x1d(E\x03\0\x06\x03\x01",8,1,fp); //utf8

	if( sys_global_var()->prt_dots_per_line<=384 )
		format = "%-13s:";
	dbl_height_fprintf(0,fp,format,"Model", sys_global_var()->model);
	dbl_height_fprintf(0,fp,format,"Serial num", sys_global_var()->sn);
	fprintf(fp,"\n");

	if( sys_global_var()->prt_dots_per_line<=384 )
		format = "%-20s:";
	//dbl_height_fprintf(0,fp,format,"HW version", sys_global_var()->hw_ver);
	//dbl_height_fprintf(0,fp,format,"BootLoader Version", sys_global_var()->boot_ver);
	dbl_height_fprintf(0,fp,format,"FW version", sys_global_var()->fw_ver);
	dbl_height_fprintf(0,fp,format,"SUNMI APP version",APP0_VERSION);
	dbl_height_fprintf(0,fp,format,"Partner APP version",APP1_VERSION);
	dbl_height_fprintf(0,fp,format,"MiniAPP version",MINIAPP_VERSION);
#if  0
	/* Font Version */
	char fontVersion[16] = {};
	print_get_fontlib_version(fontVersion);
	dbl_height_fprintf(0,fp,format,"Font version", fontVersion);

	/* TTS Version */
	dbl_height_fprintf(0,fp,format,"TTS version", getTTSInfo()?getTTSInfo()->tts.version:"");
	dbl_height_fprintf(0,fp,format,"Print head dead dots","");
	print_dead_dots(fp);
#endif
	if( sys_global_var()->prt_dots_per_line<=384 )
		format = "%-12s:";
#if  0
	char longitude[32] = {}, latitude[32] = {};
	sys_get_lbs(longitude, latitude);
	dbl_height_fprintf(0,fp,format,"Longitude", longitude);
	dbl_height_fprintf(0,fp,format,"Latitude", latitude);
#endif
	if( sys_global_var()->lan_exist ) {
		char lan_mac[32]={};

		util_get_str_ip("eth0",lan_info->ip,lan_info->netmask);
		util_get_str_mac("eth0",lan_mac,sizeof(lan_mac));
		util_get_str_gw("eth0",lan_info->gw,sizeof(lan_info->gw));
		fprintf(fp,"\n[LAN]\n");
		dbl_height_fprintf(0,fp,format,"IP",lan_info->ip);
		dbl_height_fprintf(0,fp,format,"SubnetMask",lan_info->netmask);
		dbl_height_fprintf(0,fp,format,"GateWay",lan_info->gw);
		dbl_height_fprintf(0,fp,format,"MAC", lan_mac);
		dbl_height_fprintf(0,fp,format,"DHCP","Enable");

		char fixed_ip[32]={};
		net_LPrtSrv_ini_get("eth0","ip",fixed_ip,sizeof(fixed_ip));
		if( fixed_ip[0] ) {
			char fixed_netmask[32]={};
			net_LPrtSrv_ini_get("eth0","netmask",fixed_netmask,sizeof(fixed_netmask));
			fprintf(fp,"\n[LAN-Fixed]\n");
			dbl_height_fprintf(0,fp,format,"IP",fixed_ip);
			dbl_height_fprintf(0,fp,format,"SubnetMask",fixed_netmask);
		}
	}
	if( sys_global_var()->wifi_exist ) {
		GetWifiDetail(wifi_info);
		fprintf(fp,"\n[WiFi]\n");
		dbl_height_fprintf(0,fp,format,"IP",wifi_info->strIP);
		dbl_height_fprintf(0,fp,format,"SubnetMask",wifi_info->strNM);
		dbl_height_fprintf(0,fp,format,"GateWay",wifi_info->gw);
		dbl_height_fprintf(0,fp,format,"MAC", sys_global_var()->wifi_mac);
		dbl_height_fprintf(0,fp,format,"DHCP","Enable");

		char fixed_ip[32]={};
		net_LPrtSrv_ini_get("wlan0","ip",fixed_ip,sizeof(fixed_ip));
		if( fixed_ip[0] ) {
			char fixed_netmask[32]={};
			net_LPrtSrv_ini_get("wlan0","netmask",fixed_netmask,sizeof(fixed_netmask));
			fprintf(fp,"\n[WiFi-Fixed]\n");
			dbl_height_fprintf(0,fp,format,"IP",fixed_ip);
			dbl_height_fprintf(0,fp,format,"SubnetMask",fixed_netmask);
		}
		fprintf(fp,"\n[Bluetooth]\n");
		dbl_height_fprintf(0,fp,format,"BT Name",get_bt_devname(tmp,sizeof(tmp)));
		dbl_height_fprintf(0,fp,format,"BT Address", sys_global_var()->bt_mac);
	}

	if( sys_global_var()->wnet_exist ) {
		char ip[64]={},imei[32] = {}, iccid[32] = {},isp[64]={};
		if( sys_global_var()->prt_dots_per_line<=384 )
			format = "%-8s:";
		util_get_str_ip("ppp0",ip,NULL);

		fprintf(fp,"\n[Mobile]\n");
		dbl_height_fprintf(0,fp,format,"ICCID", wnet_ini_get(WNET_KEY_ICCID,iccid, sizeof(iccid)));
		dbl_height_fprintf(0,fp,format,"IMEI", wnet_ini_get(WNET_KEY_IMEI,imei, sizeof(imei)));
		dbl_height_fprintf(0,fp,format,"IP",ip);
		dbl_height_fprintf(0,fp,format,"Operator",wnet_ini_get(WNET_KEY_ISP,isp,sizeof(isp)));
	}
	NetworkTest_WiFIChannelInfo(fp,wifi_info->ssid);
	fprintf(fp,"\n[Network Test]\n");
	return 0;
}

void *doNetworkTest(void *arg)
{
	LogDbg("init");
	dhcp_offer_info_t wifi_dhcp_offer = {"wlan0",0,{}};
	dhcp_offer_info_t lan_dhcp_offer = {"eth0",0,{}};
	arping_args_t wifi_arping_args = {};
	arping_args_t lan_arping_args = {};
	wifi_detail_info_t wifi_detail_info = {};
	lan_info_t lan_info = {};
	test_result_info_t result = {};
	void *pThreadRet;
	int *pRet = (int *)&pThreadRet;
	char *format;
	static bool s_isNetworkTesting = false;
	char *buf=NULL,*pos,tmp[512]={};
	size_t sz = 0;
	char ckcfg_errmsg[256]={};
	struct checkconfig_rsp_msg mqtt_ckcfg_rsp = {0,sizeof(ckcfg_errmsg),ckcfg_errmsg};
#define LEFTSIZE (sizeof(buf)-(pos-buf))

	pthread_detach(pthread_self());

	if( s_isNetworkTesting ) {
		LogDbg("NetworkTest is running!");
		return NULL;
	}
	FILE *fp = open_memstream(&buf,&sz);
	if( !fp ) return 0;

	s_isNetworkTesting = true;

	unsigned int t0,t1;

	t0 = SysTick();

	g_app_vars.isMqttRwOK = 0;
	pthread_t tid_playing;
	pthread_create(&tid_playing,0,playNwTestingAudio,0);

	NetworkTest_BaseInfo(fp,&wifi_detail_info,&lan_info);
	if( sys_global_var()->prt_dots_per_line>384 )
		format = "%-32s=> ";
	else
		format = "%-25s=>"; // 27 chars
	result.cur_net = NetGetCurRoute();
	pos = "Current Network";
	if (result.cur_net != NET_NONE ) {
		char netname[16];
		if( sys_global_var()->prt_dots_per_line>384 )
			snprintf(netname,sizeof(netname),"%s",get_net_name(result.cur_net));
		else
			snprintf(netname,sizeof(netname),"%.*s",5,get_net_name(result.cur_net));
			dbl_height_fprintf(0,fp,format,pos,netname );
	} else {
		dbl_height_fprintf(1,fp,format,pos,StrResultFail);
	}

	if( sys_global_var()->lan_exist ) {
		pos = "Link=>LAN";
		if( !net_lan_is_linkup() ) {
			result.lan_stat = RESULT_S_ERR;
			dbl_height_fprintf(1,fp,format,pos,"Down");
		} else if( lan_info.ip[0] ) {
			result.lan_stat = RESULT_PASS;
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		} else {
			result.lan_stat = RESULT_FAIL;
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
		}
	}

	if( sys_global_var()->wifi_exist ) {
		if( wifi_detail_info.st>=WIFI_ST_CONN )
			result.wifi_stat = RESULT_PASS;
		else if( wifi_detail_info.st == WIFI_ST_DISCONN )
			result.wifi_stat = RESULT_FAIL;
		else
			result.wifi_stat = RESULT_S_ERR;
		snprintf(tmp,sizeof(tmp),"Link=>SSID:%s",wifi_detail_info.ssid);
		fixedWidthString(tmp,sizeof(tmp),25);

		if( wifi_detail_info.st < WIFI_ST_CONN ) {
			dbl_height_fprintf(1,fp,format,tmp,StrResultFail);
		} else {
			dbl_height_fprintf(0,fp,format,tmp,StrResultPass);
		}
	}

	if( sys_global_var()->wnet_exist ) {
		pos = "Link=>Mobile";
		result.gprs_stat = access("/sys/class/net/ppp0",F_OK)?RESULT_FAIL:RESULT_PASS;
		if( result.gprs_stat == RESULT_PASS )
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		else
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
	}

	pthread_t tid_wifi_dhcp=0,tid_wifi_check_ip=0,tid_ping_wifi_gw=0,
		tid_lan_dhcp=0,tid_lan_check_ip=0,tid_ping_lan_gw=0,
		tid_ping_dns=0,tid_ping_mqtt=0,tid_ping_http=0,
		tid_check_mqtt=0,tid_mqtt_rw=0;
	
	if( result.wifi_stat == RESULT_PASS ) {
		pthread_create(&tid_wifi_dhcp,NULL,dhcp_test,&wifi_dhcp_offer);
		if( wifi_detail_info.st == WIFI_ST_GOTIP ) {
			wifi_arping_args.ip = wifi_detail_info.strIP;
			wifi_arping_args.ifname = "wlan0";
			pthread_create(&tid_wifi_check_ip,NULL,arping_test,&wifi_arping_args);
			pthread_create(&tid_ping_wifi_gw,NULL,ping_test,wifi_detail_info.gw);
		}
	}
	if( result.lan_stat == RESULT_PASS ) {
		pthread_create(&tid_lan_dhcp,NULL,dhcp_test,&lan_dhcp_offer);
		if( lan_info.ip[0] ) {
			lan_arping_args.ip = lan_info.ip;
			lan_arping_args.ifname = "eth0";
			pthread_create(&tid_lan_check_ip,NULL,arping_test,&lan_arping_args);
			pthread_create(&tid_ping_lan_gw,NULL,ping_test,lan_info.gw);
		}
	}
	if( result.cur_net != NET_NONE ) {
		pthread_create(&tid_ping_dns,NULL,ping_test,FIXED_DNS_IP);
		pthread_create(&tid_ping_mqtt, NULL, ping_test, sys_global_var()->broker_addr);
		pthread_create(&tid_ping_http, NULL, ping_test, sys_global_var()->cloud_url);
		pthread_create(&tid_check_mqtt,NULL,checkMQTTConfig,&mqtt_ckcfg_rsp);
		pthread_create(&tid_mqtt_rw,NULL,mqtt_rw_test,&s_isNetworkTesting);
	}

	if( sys_global_var()->lan_exist ) {
		result.lan_dhcp_stat = lan_info.ip[0]?RESULT_PASS:RESULT_FAIL;
		if( tid_lan_dhcp ) {
			pthread_join(tid_lan_dhcp,&pThreadRet);
			if( *pRet>1 )
				result.lan_dhcp_stat = RESULT_WARN;
		}
		pos = "Check=>LAN DHCP Conflict";
		if( result.lan_dhcp_stat == RESULT_FAIL ) {
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
		} else if( result.lan_dhcp_stat == RESULT_WARN ) {
			dbl_height_fprintf(1,fp,format,pos,"WARN");
		} else {
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		}

		result.lan_ip_stat = RESULT_S_ERR;
		if( tid_lan_check_ip ) {
			pthread_join(tid_lan_check_ip,&pThreadRet);
			if( *pRet == 0 ) result.lan_ip_stat = RESULT_PASS;
			else if( *pRet>0 ) result.lan_ip_stat = RESULT_WARN;
		}
		pos = "Check=>LAN IP Conflict";
		if( result.lan_ip_stat == RESULT_PASS ) {
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		} else if( result.lan_ip_stat == RESULT_S_ERR ) {
			dbl_height_fprintf(1,fp,format,pos,"No IP");
		} else {
			dbl_height_fprintf(1,fp,format,pos,"WARN");
		}

		result.ping_lan_gw = RESULT_FAIL;
		if( tid_ping_lan_gw ) {
			pthread_join(tid_ping_lan_gw,&pThreadRet);
			if( *pRet>=1000 ) result.ping_lan_gw = RESULT_WARN;
			else if( *pRet>=0 ) result.ping_lan_gw = RESULT_PASS;
		}
		if( lan_info.gw[0] ) {
			if( sys_global_var()->prt_dots_per_line>384 )
				snprintf(tmp,sizeof(tmp),"Ping=>LAN GW %s",lan_info.gw);
			else
				snprintf(tmp,sizeof(tmp),"Ping=>GW %s",lan_info.gw);
			if( result.ping_lan_gw == RESULT_WARN ) {
				char tm_str[32];
				snprintf(tm_str,sizeof(tm_str),"%d.%02dS",(*pRet)/1000,((*pRet)%1000)/10);
				dbl_height_fprintf(1,fp,format,tmp,tm_str);
			} else if( result.ping_lan_gw == RESULT_PASS ) {
				char tm_str[32];
				snprintf(tm_str,sizeof(tm_str),"%dms",*pRet);
				dbl_height_fprintf(0,fp,format,tmp,tm_str);
			} else {
				dbl_height_fprintf(1,fp,format,tmp,StrResultFail);
			}
		} else {
			result.ping_lan_gw = RESULT_PASS;
		}
	}

	if( sys_global_var()->wifi_exist ) {
		result.wifi_dhcp_stat = RESULT_FAIL;
		if( tid_wifi_dhcp ) {
			pthread_join(tid_wifi_dhcp,&pThreadRet);
			if( *pRet>1 )
				result.wifi_dhcp_stat = RESULT_WARN;
			else if( wifi_detail_info.st == WIFI_ST_GOTIP )
				result.wifi_dhcp_stat = RESULT_PASS;
		}

		pos = "Check=>WiFi DHCP Conflict";
		if( result.wifi_dhcp_stat == RESULT_FAIL ) {
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
		} else if( result.wifi_dhcp_stat == RESULT_WARN ) {
			dbl_height_fprintf(1,fp,format,pos,"WARN");
		} else {
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		}

		result.wifi_ip_stat = RESULT_S_ERR;
		if( tid_wifi_check_ip ) {
			pthread_join(tid_wifi_check_ip,&pThreadRet);
			if( *pRet == 0 ) result.wifi_ip_stat = RESULT_PASS;
			else if( *pRet>0 ) result.wifi_ip_stat = RESULT_WARN;
		}
		pos = "Check=>WiFi IP Conflict";
		if( result.wifi_ip_stat == RESULT_PASS ) {
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		} else if( result.wifi_ip_stat == RESULT_S_ERR ) {
			dbl_height_fprintf(1,fp,format,pos,"No IP");
		} else {
			dbl_height_fprintf(1,fp,format,pos,"WARN");
		}

		result.ping_wifi_gw = RESULT_FAIL;
		if( wifi_detail_info.st == WIFI_ST_GOTIP ) {
			if( tid_ping_wifi_gw ) {
				pthread_join(tid_ping_wifi_gw,&pThreadRet);
				if( *pRet>=1000 ) result.ping_wifi_gw = RESULT_WARN;
				else if( *pRet>=0 ) result.ping_wifi_gw = RESULT_PASS;
			}
			if( sys_global_var()->prt_dots_per_line>384 )
				snprintf(tmp,sizeof(tmp),"Ping=>WiFi GW %s",wifi_detail_info.gw);
			else
				snprintf(tmp,sizeof(tmp),"Ping=>GW %s",wifi_detail_info.gw);
			if( result.ping_wifi_gw == RESULT_WARN ) {
				char tm_str[32];
				snprintf(tm_str,sizeof(tm_str),"%d.%02dS",(*pRet)/1000,((*pRet)%1000)/10);
				dbl_height_fprintf(1,fp,format,tmp,tm_str);
			} else if( result.ping_wifi_gw == RESULT_PASS ) {
				char tm_str[32];
				snprintf(tm_str,sizeof(tm_str),"%dms",*pRet);
				dbl_height_fprintf(0,fp,format,tmp,tm_str);
			} else {
				dbl_height_fprintf(1,fp,format,tmp,StrResultFail);
			}
		}
	}

	result.ping_dns = RESULT_FAIL;
	if( result.cur_net != NET_NONE ) {
		if( tid_ping_dns ) {
			pthread_join(tid_ping_dns,&pThreadRet);
			if( *pRet>=1000 ) result.ping_dns = RESULT_WARN;
			else if( *pRet>=0 ) result.ping_dns = RESULT_PASS;
		}
		snprintf(tmp,sizeof(tmp),"Ping=>DNS %s",FIXED_DNS_IP);
		if( result.ping_dns == RESULT_WARN ) {
			char tm_str[32];
			snprintf(tm_str,sizeof(tm_str),"%d.%02dS",(*pRet)/1000,((*pRet)%1000)/10);
			dbl_height_fprintf(1,fp,format,tmp,tm_str);
		} else if( result.ping_dns == RESULT_PASS ) {
			char tm_str[32];
			snprintf(tm_str,sizeof(tm_str),"%dms",*pRet);
			dbl_height_fprintf(0,fp,format,tmp,tm_str);
		} else {
			dbl_height_fprintf(1,fp,format,tmp,StrResultFail);
		}

		result.ping_mqtt = RESULT_FAIL;
		if( tid_ping_mqtt ) {
			pthread_join(tid_ping_mqtt,&pThreadRet);
			if( *pRet>=1000 ) result.ping_mqtt = RESULT_WARN;
			else if( *pRet>=0 ) result.ping_mqtt = RESULT_PASS;
		}
		pos = "Ping=>MQTT Server";
		if( result.ping_mqtt == RESULT_WARN ) {
			snprintf(tmp,sizeof(tmp),"%d.%02dS",(*pRet)/1000,((*pRet)%1000)/10);
			dbl_height_fprintf(1,fp,format,pos,tmp);
		} else if( result.ping_mqtt == RESULT_PASS ) {
			snprintf(tmp,sizeof(tmp),"%dms",*pRet);
			dbl_height_fprintf(0,fp,format,pos,tmp);
		} else {
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
		}

		result.ping_http = RESULT_FAIL;
		if( tid_ping_http ) {
			pthread_join(tid_ping_http,&pThreadRet);
			if( *pRet>=1000 ) result.ping_http = RESULT_WARN;
			else if( *pRet>=0 ) result.ping_http = RESULT_PASS;
		}
		pos = "Ping=>HTTP Server";
		if( result.ping_http == RESULT_WARN ) {
			snprintf(tmp,sizeof(tmp),"%d.%02dS",(*pRet)/1000,((*pRet)%1000)/10);
			dbl_height_fprintf(1,fp,format,pos,tmp);
		} else if( result.ping_http == RESULT_PASS ) {
			snprintf(tmp,sizeof(tmp),"%dms",*pRet);
			dbl_height_fprintf(0,fp,format,pos,tmp);
		} else {
			dbl_height_fprintf(1,fp,format,tmp,StrResultFail);
		}

		result.mqtt_config = RESULT_FAIL;
		if( tid_check_mqtt ) {
			pthread_join(tid_check_mqtt,&pThreadRet);
			if( *pRet<-1 ) result.mqtt_config = RESULT_TIMEOUT;
			else if( *pRet>=0 ) result.mqtt_config = RESULT_PASS;
		}
		pos = "MQTT=>Check Config";
		if( result.mqtt_config == RESULT_FAIL ) {
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
		}else if( result.mqtt_config == RESULT_TIMEOUT ) {
			dbl_height_fprintf(1,fp,format,pos,StrResultTimeOut);
		} else {
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		}
		pos = "MQTT=>Connect";
		if (is_mqtt_alive()) {
			result.mqtt_conn = RESULT_PASS;
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		} else {
			result.mqtt_conn = RESULT_FAIL;
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
		}

		result.mqtt_rw = g_app_vars.isMqttRwOK?RESULT_PASS:RESULT_FAIL;
		pos = "MQTT=>R&W";
		if( result.mqtt_rw == RESULT_FAIL ) {
			dbl_height_fprintf(1,fp,format,pos,StrResultFail);
		} else {
			dbl_height_fprintf(0,fp,format,pos,StrResultPass);
		}
	}

	time_t t = time(0);
	struct tm tm_now = {};

	gmtime_r(&t,&tm_now);
	fprintf(fp,"\n%04d-%02d-%02d %02d:%02d:%02d UTC\n",
				tm_now.tm_year+1900,tm_now.tm_mon+1,tm_now.tm_mday,
				tm_now.tm_hour,tm_now.tm_min,tm_now.tm_sec);
	if( sys_global_var()->prt_dots_per_line<=384 )
		format = "%-13s:";
	else
		format = "%-20s: ";
	dbl_height_fprintf(0,fp,format, "Serial num", sys_global_var()->sn);
	bool is_onl_env = (!strcmp(sys_global_var()->broker_addr,"nt211.sunmi.com"));
	dbl_height_fprintf(0,fp,format,"MQTT env",is_onl_env?"Online":"Test");
	dbl_height_fprintf(0,fp,format,"DATA",sys_global_var()->data_report?"On":"Off");
	T_PartnerParam param = {};
	GetPartnerParam(&param);
	if ( strlen(param.parterURL) ) {
		char tmp[sizeof(param.parterURL)+1] = {};
		snprintf(tmp,sizeof(tmp),"%s%s","\n",param.parterURL);
		dbl_height_fprintf(0,fp,format,"CALLBACK URL",tmp);
	}

	showTestReport(fp,&result,&wifi_detail_info,&lan_info,&mqtt_ckcfg_rsp,&wifi_dhcp_offer,&lan_dhcp_offer);
	fwrite("\x1B\x21\x18",3,1,fp);
	fwrite("\x1B\x61\x31",3,1,fp); //center align
	fprintf(fp,"\n\n**COMPLETE**\n\n\n\n");
	if( sys_global_var()->prt_dots_per_line>384 )
		fprintf(fp,"\n\n\n\n\n\n");
	fwrite("\x1D\x56\x31", 3,1,fp); //cut paper
	fwrite("\x1B\x40",2,1,fp); //init printer
	fwrite("\x1D\x28\x45\x03\x00\x06\x00\xFF", 1, 8, fp); //Restore settings
	fwrite("\x1D\x28\x45\x03\x00\x06\x01\xFF", 1, 8, fp); //Restore settings
	fwrite("\x1D\x28\x45\x03\x00\x06\x02\xFF", 1, 8, fp); //Restore settings
	fwrite("\x1D\x28\x45\x03\x00\x06\x03\xFF", 1, 8, fp); //Restore settings
	fclose(fp);
	t1 = SysTick();
	LogDbg("cost time %ums",t1-t0);
	print_channel_send_data(PRINT_CHN_ID_INT, buf, sz, 1);

	free(buf);
	gStopTestAlertAudio = 1;
	pthread_join(tid_playing,&pThreadRet);
	s_isNetworkTesting = false;
	return NULL;
}

void startNetworkTest(void)
{
	pthread_t tid;
	if (pthread_create(&tid, NULL,doNetworkTest, 0)<0)
		LogError("pthread_create doNetworkTest failed");
}
