/*
 * CI smoke test: pull in the whole OpenH264 decoder, create it, initialise it,
 * tear it down. If this links and runs, the ESP-IDF component is sound for the
 * target. (No bitstream is decoded here - see the README for a real example.)
 */
#include <cstring>
#include <cstdlib>
#include "codec_api.h"
#include "codec_app_def.h"
#include "esp_log.h"

static const char *TAG = "smoke";

extern "C" void app_main(void)
{
    ISVCDecoder *dec = nullptr;
    if (WelsCreateDecoder(&dec) != 0 || dec == nullptr) {
        ESP_LOGE(TAG, "WelsCreateDecoder failed");
        abort();
    }

    SDecodingParam p;
    memset(&p, 0, sizeof(p));
    p.uiTargetDqLayer = static_cast<unsigned char>(-1);
    p.eEcActiveIdc    = ERROR_CON_SLICE_MV_COPY_CROSS_IDR_FREEZE_RES_CHANGE;
    p.sVideoProperty.size         = sizeof(p.sVideoProperty);
    p.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

    long rc = dec->Initialize(&p);
    ESP_LOGI(TAG, "OpenH264 decoder Initialize() rc=%ld", rc);

    dec->Uninitialize();
    WelsDestroyDecoder(dec);

    ESP_LOGI(TAG, "smoke OK");
}
