/* SPDX-License-Identifier: Apache-2.0
 * 中文：升级元数据边界与版本兼容性。/ English: Release bounds and version compatibility. */
#include "ota_online.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *valid = "{\"schema\":1,\"version\":\"0.3.3-rc79\",\"project\":\"Read_Pico\",\"board\":\"METALIO-EINK4P\",\"layout\":\"mogo-e4p-dual-5m-v1\",\"minimum_base_version\":\"0.3.3-rc72\",\"url\":\"https://wegooo-cell.github.io/read-pico-reader/Pico-update-0.3.3-rc79.bin\",\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"notes\":\"Test release\",\"size\":4096}";
int main(void) {
 pico_release_t release;
 assert(pico_release_parse(valid,strlen(valid),&release)==ESP_OK);
 assert(release.size==4096 && !strcmp(release.version,"0.3.3-rc79"));
 assert(pico_version_compare("0.3.3-rc79","0.3.3-rc9")>0);
 assert(pico_version_compare("0.3.3","0.3.3-rc79")>0);
 assert(pico_version_compare("0.4.0-rc1","0.3.3")>0);
 assert(pico_release_url_valid("https://mogo.mantake.shop/files/firmware/ota/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.bin"));
 assert(!pico_release_url_valid("https://kiikoread.com/Pico-update.bin"));
 assert(!pico_release_url_valid("https://mogo.mantake.shop/files/firmware/ota/sub/x.bin"));
 assert(!pico_release_url_valid("https://mogo.mantake.shop.evil/files/firmware/ota/a.bin"));
 assert(!pico_release_url_valid("https://mogo.mantake.shop@evil/files/firmware/ota/a.bin"));
 assert(!pico_release_url_valid("https://mogo.mantake.shop:8443/files/firmware/ota/a.bin"));
 assert(!pico_release_url_valid("https://mogo.mantake.shop/files/firmware/ota/../a.bin"));
 assert(!pico_release_url_valid("https://mogo.mantake.shop/files/firmware/ota/a.bin?url=evil"));
 assert(!pico_release_url_valid("http://wegooo-cell.github.io/read-pico-reader/a.bin"));
 assert(!pico_release_url_valid("https://wegooo-cell.github.io.evil/read-pico-reader/a.bin"));
 assert(!pico_release_url_valid("https://wegooo-cell.github.io/read-pico-reader/../a.bin"));
 // 本仓库 Pages 站点由打包脚本写入清单，设备必须能取用。
 // The packaging script writes this repository's Pages origin, so the device must accept it.
 assert(pico_release_url_valid("https://chopper1026.github.io/mogo-e4p/Pico-update-0.3.3-rc98.bin"));
 assert(!pico_release_url_valid("https://chopper1026.github.io/mogo-e4p/../a.bin"));
 assert(!pico_release_url_valid("https://chopper1026.github.io/mogo-e4p.evil/a.bin"));
 assert(pico_release_parse(valid,strlen(valid)+1,&release)!=ESP_OK);
 assert(pico_release_parse("{}",2,&release)!=ESP_OK);
 char altered[2048]; snprintf(altered,sizeof(altered),"%s junk",valid);
 assert(pico_release_parse(altered,strlen(altered),&release)!=ESP_OK);
 snprintf(altered,sizeof(altered),"{\"schema\":1,%s",valid+1);
 assert(pico_release_parse(altered,strlen(altered),&release)!=ESP_OK);
 strcpy(altered,valid); char *p=strstr(altered,"4096"); memcpy(p,"1e10",4);
 assert(pico_release_parse(altered,strlen(altered),&release)!=ESP_OK);
 strcpy(altered,valid); p=strstr(altered,"METALIO-EINK4P"); p[0]='X';
 assert(pico_release_parse(altered,strlen(altered),&release)!=ESP_OK);
 strcpy(altered,valid); p=strstr(altered,"0123456789abcdef"); p[0]='z';
 assert(pico_release_parse(altered,strlen(altered),&release)!=ESP_OK);
 // 清单镜像上限跟随单槽容量：等于槽大小可以，多一字节即拒绝。
 // The manifest image cap follows the slot size: equal is accepted, one byte over is rejected.
 strcpy(altered,valid); p=strstr(altered,"4096");
 snprintf(p,sizeof(altered)-(size_t)(p-altered),"%u}",(unsigned)PICO_OTA_SLOT_SIZE);
 assert(pico_release_parse(altered,strlen(altered),&release)==ESP_OK&&release.size==PICO_OTA_SLOT_SIZE);
 snprintf(p,sizeof(altered)-(size_t)(p-altered),"%u}",(unsigned)PICO_OTA_SLOT_SIZE+1u);
 assert(pico_release_parse(altered,strlen(altered),&release)!=ESP_OK);
 // 未配置自建源时只认官方前缀；配置后只放开该前缀下的单文件名。
 // Without a configured extra origin only the official prefixes pass; with one, only a single
 // filename below that prefix passes.
#ifdef OTA_TEST_EXTRA_ORIGIN
 assert(pico_release_url_valid("https://ota.example.com/files/x.bin"));
 assert(!pico_release_url_valid("https://ota.example.com/files/../x.bin"));
 assert(!pico_release_url_valid("https://ota.example.com/x.bin"));
 assert(!pico_release_url_valid("https://ota.example.com.evil/files/x.bin"));
#else
 assert(!pico_release_url_valid("https://ota.example.com/files/x.bin"));
#endif
 puts("PASS: release schema, slot-sized cap, hashes, version ordering, board, duplicate/trailing payload and HTTPS origin");
}
