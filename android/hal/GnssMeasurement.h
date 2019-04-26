#ifndef ANDROID_HARDWARE_GNSS_V1_1_GNSSMEASUREMENT_H
#define ANDROID_HARDWARE_GNSS_V1_1_GNSSMEASUREMENT_H

#include <android/hardware/gnss/1.1/IGnssMeasurement.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct GnssMeasurement : public IGnssMeasurement {
    // Methods from ::android::hardware::gnss::V1_0::IGnssMeasurement follow.
    Return<::android::hardware::gnss::V1_0::IGnssMeasurement::GnssMeasurementStatus> setCallback(
        const sp<::android::hardware::gnss::V1_0::IGnssMeasurementCallback>& callback) override;
    Return<void> close() override;

    // Methods from ::android::hardware::gnss::V1_1::IGnssMeasurement follow.
    Return<::android::hardware::gnss::V1_0::IGnssMeasurement::GnssMeasurementStatus>
    setCallback_1_1(const sp<::android::hardware::gnss::V1_1::IGnssMeasurementCallback>& callback,
                    bool enableFullTracking) override;

    // Methods from ::android::hidl::base::V1_0::IBase follow.
};

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GNSS_V1_1_GNSSMEASUREMENT_H
