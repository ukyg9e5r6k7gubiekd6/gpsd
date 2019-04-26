#include "GnssMeasurement.h"

namespace android {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

// Methods from ::android::hardware::gnss::V1_0::IGnssMeasurement follow.
Return<::android::hardware::gnss::V1_0::IGnssMeasurement::GnssMeasurementStatus>
GnssMeasurement::setCallback(const sp<::android::hardware::gnss::V1_0::IGnssMeasurementCallback>&) {
    // TODO implement
    return ::android::hardware::gnss::V1_0::IGnssMeasurement::GnssMeasurementStatus{};
}

Return<void> GnssMeasurement::close() {
    // TODO implement
    return Void();
}

// Methods from ::android::hardware::gnss::V1_1::IGnssMeasurement follow.
Return<::android::hardware::gnss::V1_0::IGnssMeasurement::GnssMeasurementStatus>
GnssMeasurement::setCallback_1_1(
    const sp<::android::hardware::gnss::V1_1::IGnssMeasurementCallback>&, bool) {
    // TODO implement
    return ::android::hardware::gnss::V1_0::IGnssMeasurement::GnssMeasurementStatus{};
}

// Methods from ::android::hidl::base::V1_0::IBase follow.

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android
