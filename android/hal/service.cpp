#define LOG_TAG "android.hardware.gnss@1.1-service.gpsd"

#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include "Gnss.h"

using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::gnss::V1_1::implementation::Gnss;
using ::android::hardware::gnss::V1_1::IGnss;
using ::android::hardware::joinRpcThreadpool;
using ::android::OK;
using ::android::sp;

int main(int /* argc */, char* /* argv */ []) {
    sp<IGnss> gnss = new Gnss();
    configureRpcThreadpool(1, true /* will join */);
    if (gnss->registerAsService() != OK) {
        ALOGE("Could not register gnss 1.1 service.");
        return 1;
    }
    joinRpcThreadpool();

    ALOGE("Service exited!");
    return 1;
}
