#ifndef __NETWORK_TEST__H__
#define __NETWORK_TEST__H__

typedef struct
{
    int st;                 // -1-not configure 0-disconnect 1-connected 2-got ip
    struct in_addr ip;      // ip addr u32
    struct in_addr netmask; // ip addr u32
    char ssid[64];
    char strNM[32];
    char strIP[32];
    char gw[32];
} wifi_detail_info_t;

void startNetworkTest(void);
void GetWifiDetail(wifi_detail_info_t *info);

#endif /* __NETWORK_TEST__H__ */