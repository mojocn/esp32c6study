#ifndef VARS_H
#define VARS_H

extern const char *TAG;
extern char sta_ip[16];               // 仅用于存储 STA IP 字符串
extern int s_retry_num;               // 连接重试计数器
extern const int WIFI_CONN_MAX_RETRY; // 最大重试次数

#endif // VARS_H
