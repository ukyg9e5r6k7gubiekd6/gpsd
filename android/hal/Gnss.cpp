#define LOG_TAG "GPSd_HAL"

#include <android/hardware/gnss/1.0/types.h>
#include <log/log.h>
#include <cutils/properties.h>
#include <stdlib.h>
#include <time.h>

#include "Gnss.h"
#include "GnssMeasurement.h"

namespace android {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

using GnssSvFlags = IGnssCallback::GnssSvFlags;

const uint32_t MIN_INTERVAL_MILLIS = 100;
sp<::android::hardware::gnss::V1_1::IGnssCallback> Gnss::sGnssCallback = nullptr;

Gnss::Gnss() : mMinIntervalMs(1000), mGnssConfiguration{new GnssConfiguration()} {}

Gnss::~Gnss() {
    stop();
}

// Methods from ::android::hardware::gnss::V1_0::IGnss follow.
Return<bool> Gnss::setCallback(const sp<::android::hardware::gnss::V1_0::IGnssCallback>&) {
    // Mock handles only new callback (see setCallback1_1) coming from Android P+
    return false;
}

Return<bool> Gnss::start() {
    if (mIsActive) {
        ALOGW("Gnss has started. Restarting...");
        stop();
    }

    mIsActive = true;
    mThread = std::thread([this]() {
        struct gps_data_t gps_data;
        int gpsopen = -1;
        char gpsdhost[PROP_VALUE_MAX];
        char gpsdport[PROP_VALUE_MAX];
        char gpsdauto[PROP_VALUE_MAX];
        int is_automotive;
        char gpslat[PROP_VALUE_MAX];
        char gpslon[PROP_VALUE_MAX];
        long last_recorded_fix = 0;
        char dtos[100];
        GnssLocation location;

        // Normally, GPSd will be running on localhost, but we can set a system property
        // "service.gpsd.host" to some other hostname in order to open a GPSd instance
        // running on a different host.
        property_get("service.gpsd.host", gpsdhost, "localhost");
	property_get("service.gpsd.port", gpsdport, "2947");
        is_automotive = (property_get("service.gpsd.automotive", gpsdauto, "") > 0);

        // Load coordinates stored in persist properties as current location
        // This is to provide instantaneous fix to the last good location
        // in order to provide instantaneous ability to begin navigator routing.
	if (is_automotive && property_get("persist.service.gpsd.latitude", gpslat, "") > 0
                    && property_get("persist.service.gpsd.longitude", gpslon, "") > 0){
            location = {
                     .gnssLocationFlags = 0xDD,
                     .latitudeDegrees = atof(gpslat),
                     .longitudeDegrees = atof(gpslon),
                     .speedMetersPerSec = 0.0,
                     .bearingDegrees = 0.0,
                     .horizontalAccuracyMeters = 0.0,
                     .speedAccuracyMetersPerSecond = 0.0,
                     .bearingAccuracyDegrees = 0.0,
                     .timestamp = (long) time(NULL)
            };
            this->reportLocation(location);
        }

        memset(&gps_data, 0, sizeof(gps_data));

        while (mIsActive == true) {
            // If the connection to GPSd is not open, try to open it.
            // If the attempt to open it fails, sleep 5 seconds and try again.
            // Note the continue; statement that will skip the reading in the
            // event that the connection to GPSd cannot be established.
            if (gpsopen != 0){
		ALOGD("%s: gpsd_host: %s, gpsd_port: %s", __func__, gpsdhost, gpsdport);
                if ((gpsopen = gps_open(gpsdhost, gpsdport, &gps_data)) == 0){
		    ALOGD("%s: gps_open SUCCESS", __func__);
                    gps_stream(&gps_data, WATCH_ENABLE, NULL);
                } else {
		    ALOGD("%s: gps_open FAIL (%d). Trying again in 5 seconds.", __func__, gpsopen);
                    sleep(5);
                    continue;
                }
            }

            // Wait for data from gpsd, then process it.
            if (gps_waiting (&gps_data, 2000000)) {
                errno = 0;
                if (gps_read (&gps_data, NULL, 0) != -1) {

                    if (gps_data.status >= 1 && gps_data.fix.mode >= 2){

                        // Every 30 seconds, store current coordinates to persist property.
                        if (is_automotive && ((long) gps_data.fix.time) > last_recorded_fix + 30){
                            last_recorded_fix = (long) gps_data.fix.time;
                            sprintf(dtos, "%lf", gps_data.fix.latitude);
                            property_set("persist.service.gpsd.latitude", dtos);
                            sprintf(dtos, "%lf", gps_data.fix.longitude);
                            property_set("persist.service.gpsd.longitude", dtos);
                        }

                        unsigned short flags =
                                 V1_0::GnssLocationFlags::HAS_LAT_LONG |
                                 V1_0::GnssLocationFlags::HAS_SPEED |
                                 V1_0::GnssLocationFlags::HAS_BEARING |
                                 V1_0::GnssLocationFlags::HAS_HORIZONTAL_ACCURACY |
                                 V1_0::GnssLocationFlags::HAS_SPEED_ACCURACY |
                                 V1_0::GnssLocationFlags::HAS_BEARING_ACCURACY;

                        location = {
                                 .latitudeDegrees = (double) gps_data.fix.latitude,
                                 .longitudeDegrees = (double) gps_data.fix.longitude,
                                 .speedMetersPerSec = (float) gps_data.fix.speed,
                                 .bearingDegrees = (float) gps_data.fix.track,
                                 .horizontalAccuracyMeters = (float) gps_data.fix.eph,
                                 .speedAccuracyMetersPerSecond = (float) gps_data.fix.eps,
                                 .bearingAccuracyDegrees = (float) gps_data.fix.epd,
                                 .timestamp = (long) gps_data.fix.time
                        };

                        if (gps_data.fix.mode == 3){
                            flags |= V1_0::GnssLocationFlags::HAS_ALTITUDE |
                                    V1_0::GnssLocationFlags::HAS_VERTICAL_ACCURACY;

                            location.altitudeMeters = gps_data.fix.altitude;
                            location.verticalAccuracyMeters = gps_data.fix.epv;
                        }

			location.gnssLocationFlags = flags;

                        this->reportLocation(location);
                    } else if (is_automotive && last_recorded_fix == 0){
                        location.timestamp = (long) time(NULL);
                        this->reportLocation(location);
                    }

                    GnssSvStatus svStatus = {.numSvs = (uint32_t) gps_data.satellites_visible};
                    for (int i = 0; i < gps_data.satellites_visible; i++){
                        GnssConstellationType constellation_type = GnssConstellationType::UNKNOWN;
                        switch (gps_data.skyview[i].gnssid){
                            case 0:
                                constellation_type = GnssConstellationType::GPS;
                                break;
                            case 1:
                                constellation_type = GnssConstellationType::SBAS;
                                break;
                            case 2:
                                constellation_type = GnssConstellationType::GALILEO;
                                break;
                            case 3:
                                constellation_type = GnssConstellationType::BEIDOU;
                                break;
                            case 4:
                                constellation_type = GnssConstellationType::UNKNOWN;
                                break;
                            case 5:
                                constellation_type = GnssConstellationType::QZSS;
                                break;
                            case 6:
                                constellation_type = GnssConstellationType::GLONASS;
                                break;
                        }
                        svStatus.gnssSvList[i] = getSvInfo(
                                    gps_data.skyview[i].svid,
                                    constellation_type,
                                    gps_data.skyview[i].ss,
                                    gps_data.skyview[i].elevation,
                                    gps_data.skyview[i].azimuth,
                                    gps_data.skyview[i].used
                                );

			svStatus.gnssSvList[i].svFlag = 0;
                        if (gps_data.skyview[i].used == 1) svStatus.gnssSvList[i].svFlag |= GnssSvFlags::USED_IN_FIX;

                        if (gps_data.skyview[i].elevation > -91 && gps_data.skyview[i].azimuth > -1){
                            svStatus.gnssSvList[i].svFlag |= GnssSvFlags::HAS_ALMANAC_DATA;
                            if (gps_data.skyview[i].ss > 0)
                                svStatus.gnssSvList[i].svFlag |= GnssSvFlags::HAS_EPHEMERIS_DATA;
                        }
                    }
                    this->reportSvStatus(svStatus);
                }
            }
        }

        // Close the GPS
        gps_stream(&gps_data, WATCH_DISABLE, NULL);
        gps_close (&gps_data);
    });

    return true;
}

Return<bool> Gnss::stop() {
    mIsActive = false;
    if (mThread.joinable()) {
        mThread.join();
    }
    return true;
}

Return<void> Gnss::cleanup() {
    // TODO implement
    return Void();
}

Return<bool> Gnss::injectTime(int64_t, int64_t, int32_t) {
    // TODO implement
    return bool{};
}

Return<bool> Gnss::injectLocation(double, double, float) {
    // TODO implement
    return bool{};
}

Return<void> Gnss::deleteAidingData(::android::hardware::gnss::V1_0::IGnss::GnssAidingData) {
    return Void();
}

Return<bool> Gnss::setPositionMode(::android::hardware::gnss::V1_0::IGnss::GnssPositionMode,
                                   ::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence,
                                   uint32_t, uint32_t, uint32_t) {
    // TODO implement
    return bool{};
}

Return<sp<::android::hardware::gnss::V1_0::IAGnssRil>> Gnss::getExtensionAGnssRil() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IAGnssRil>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssGeofencing>> Gnss::getExtensionGnssGeofencing() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssGeofencing>{};
}

Return<sp<::android::hardware::gnss::V1_0::IAGnss>> Gnss::getExtensionAGnss() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IAGnss>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssNi>> Gnss::getExtensionGnssNi() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssNi>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssMeasurement>> Gnss::getExtensionGnssMeasurement() {
    // TODO implement
    return new GnssMeasurement();
}

Return<sp<::android::hardware::gnss::V1_0::IGnssNavigationMessage>>
Gnss::getExtensionGnssNavigationMessage() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssNavigationMessage>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssXtra>> Gnss::getExtensionXtra() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssXtra>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssConfiguration>>
Gnss::getExtensionGnssConfiguration() {
    // TODO implement
    return new GnssConfiguration();
}

Return<sp<::android::hardware::gnss::V1_0::IGnssDebug>> Gnss::getExtensionGnssDebug() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssDebug>{};
}

Return<sp<::android::hardware::gnss::V1_0::IGnssBatching>> Gnss::getExtensionGnssBatching() {
    // TODO implement
    return ::android::sp<::android::hardware::gnss::V1_0::IGnssBatching>{};
}

// Methods from ::android::hardware::gnss::V1_1::IGnss follow.
Return<bool> Gnss::setCallback_1_1(
    const sp<::android::hardware::gnss::V1_1::IGnssCallback>& callback) {
    if (callback == nullptr) {
        ALOGE("%s: Null callback ignored", __func__);
        return false;
    }

    sGnssCallback = callback;

    uint32_t capabilities = 0x0;
    auto ret = sGnssCallback->gnssSetCapabilitesCb(capabilities);
    if (!ret.isOk()) {
        ALOGE("%s: Unable to invoke callback", __func__);
    }

    IGnssCallback::GnssSystemInfo gnssInfo = {.yearOfHw = 2018};

    ret = sGnssCallback->gnssSetSystemInfoCb(gnssInfo);
    if (!ret.isOk()) {
        ALOGE("%s: Unable to invoke callback", __func__);
    }

    auto gnssName = "GPSd GNSS Implementation v1.1";
    ret = sGnssCallback->gnssNameCb(gnssName);
    if (!ret.isOk()) {
        ALOGE("%s: Unable to invoke callback", __func__);
    }

    return true;
}

Return<bool> Gnss::setPositionMode_1_1(
    ::android::hardware::gnss::V1_0::IGnss::GnssPositionMode,
    ::android::hardware::gnss::V1_0::IGnss::GnssPositionRecurrence, uint32_t minIntervalMs,
    uint32_t, uint32_t, bool) {
    mMinIntervalMs = (minIntervalMs < MIN_INTERVAL_MILLIS) ? MIN_INTERVAL_MILLIS : minIntervalMs;
    return true;
}

Return<sp<::android::hardware::gnss::V1_1::IGnssConfiguration>>
Gnss::getExtensionGnssConfiguration_1_1() {
    return mGnssConfiguration;
}

Return<sp<::android::hardware::gnss::V1_1::IGnssMeasurement>>
Gnss::getExtensionGnssMeasurement_1_1() {
    // TODO implement
    return new GnssMeasurement();
}

Return<bool> Gnss::injectBestLocation(const GnssLocation&) {
    return true;
}

Return<GnssSvInfo> Gnss::getSvInfo(int16_t svid, GnssConstellationType type, float cN0DbHz,
                                   float elevationDegrees, float azimuthDegrees, int16_t used) const {
    GnssSvInfo svInfo = {.svid = svid,
                         .constellation = type,
                         .cN0Dbhz = cN0DbHz,
                         .elevationDegrees = elevationDegrees,
                         .azimuthDegrees = azimuthDegrees,
                         .svFlag = 0};
    if (used)
        svInfo.svFlag |= GnssSvFlags::USED_IN_FIX;
    if (elevationDegrees > 0 && azimuthDegrees > 0)
        svInfo.svFlag |= GnssSvFlags::HAS_EPHEMERIS_DATA | GnssSvFlags::HAS_ALMANAC_DATA;

    return svInfo;
}

Return<void> Gnss::reportLocation(const GnssLocation& location) const {
    std::unique_lock<std::mutex> lock(mMutex);
    if (sGnssCallback == nullptr) {
        ALOGE("%s: sGnssCallback is null.", __func__);
        return Void();
    }
    sGnssCallback->gnssLocationCb(location);
    return Void();
}

Return<void> Gnss::reportSvStatus(const GnssSvStatus& svStatus) const {
    std::unique_lock<std::mutex> lock(mMutex);
    if (sGnssCallback == nullptr) {
        ALOGE("%s: sGnssCallback is null.", __func__);
        return Void();
    }
    sGnssCallback->gnssSvStatusCb(svStatus);
    return Void();
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android
