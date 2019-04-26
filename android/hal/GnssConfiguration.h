#ifndef ANDROID_HARDWARE_GNSS_V1_1_GNSSCONFIGURATION_H
#define ANDROID_HARDWARE_GNSS_V1_1_GNSSCONFIGURATION_H

#include <android/hardware/gnss/1.1/IGnssCallback.h>
#include <android/hardware/gnss/1.1/IGnssConfiguration.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>
#include <mutex>
#include <unordered_set>

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

using BlacklistedSource = ::android::hardware::gnss::V1_1::IGnssConfiguration::BlacklistedSource;
using GnssConstellationType = V1_0::GnssConstellationType;
using GnssSvInfo = V1_0::IGnssCallback::GnssSvInfo;

struct BlacklistedSourceHash {
    inline int operator()(const BlacklistedSource& source) const {
        return int(source.constellation) * 1000 + int(source.svid);
    }
};

struct BlacklistedSourceEqual {
    inline bool operator()(const BlacklistedSource& s1, const BlacklistedSource& s2) const {
        return (s1.constellation == s2.constellation) && (s1.svid == s2.svid);
    }
};

using BlacklistedSourceSet =
    std::unordered_set<BlacklistedSource, BlacklistedSourceHash, BlacklistedSourceEqual>;
using BlacklistedConstellationSet = std::unordered_set<GnssConstellationType>;

struct GnssConfiguration : public IGnssConfiguration {
    // Methods from ::android::hardware::gnss::V1_0::IGnssConfiguration follow.
    Return<bool> setSuplEs(bool enabled) override;
    Return<bool> setSuplVersion(uint32_t version) override;
    Return<bool> setSuplMode(hidl_bitfield<SuplMode> mode) override;
    Return<bool> setGpsLock(hidl_bitfield<GpsLock> lock) override;
    Return<bool> setLppProfile(hidl_bitfield<LppProfile> lppProfile) override;
    Return<bool> setGlonassPositioningProtocol(hidl_bitfield<GlonassPosProtocol> protocol) override;
    Return<bool> setEmergencySuplPdn(bool enable) override;

    // Methods from ::android::hardware::gnss::V1_1::IGnssConfiguration follow.
    Return<bool> setBlacklist(const hidl_vec<BlacklistedSource>& blacklist) override;

    Return<bool> isBlacklisted(const GnssSvInfo& gnssSvInfo) const;
    std::recursive_mutex& getMutex() const;

   private:
    BlacklistedSourceSet mBlacklistedSourceSet;
    BlacklistedConstellationSet mBlacklistedConstellationSet;
    mutable std::recursive_mutex mMutex;
};

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GNSS_V1_1_GNSSCONFIGURATION_H
