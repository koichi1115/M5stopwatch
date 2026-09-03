// Wi-Fi の認証情報。このファイルを src/secrets.h にコピーして書き換える (secrets.h は git に入らない)
// チケット (QR) の受信時だけ Wi-Fi を使う。自宅 / 職場 / iPhone のテザリング などを順に試す
#pragma once

struct WifiCred { const char* ssid; const char* pass; };

static const WifiCred WIFI_NETWORKS[] = {
  {"your-home-ssid", "your-password"},
  {"KS-iPhone", "hotspot-password"},
};
