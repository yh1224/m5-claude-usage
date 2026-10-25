#if !defined(LIB_NETWORK_H)
#define LIB_NETWORK_H

bool connectNetwork(const char *ssid, const char *passphrase);

void setMDnsHostname(const char *hostname);

void syncTime(const char *tz, const char *ntpServer);

bool clockSynced();

#endif // !defined(LIB_NETWORK_H)
