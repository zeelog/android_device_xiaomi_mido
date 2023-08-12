/* Copyright (c) 2011-2014, 2016-2020 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation, nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef LOC_API_BASE_H
#define LOC_API_BASE_H

#include <stddef.h>
#include <ctype.h>
#include <gps_extended.h>
#include <LocationAPI.h>
#include <MsgTask.h>
#include <LocSharedLock.h>
#include <log_util.h>
#ifdef NO_UNORDERED_SET_OR_MAP
    #include <map>
#else
    #include <unordered_map>
#endif
#include <inttypes.h>
#include <functional>

using namespace loc_util;

namespace loc_core {

class ContextBase;
struct LocApiResponse;
template <typename> struct LocApiResponseData;

int hexcode(char *hexstring, int string_size,
            const char *data, int data_size);
int decodeAddress(char *addr_string, int string_size,
                  const char *data, int data_size);

#define MAX_ADAPTERS          10
#define MAX_FEATURE_LENGTH    100

#define TO_ALL_ADAPTERS(adapters, call)                                \
    for (int i = 0; i < MAX_ADAPTERS && NULL != (adapters)[i]; i++) {  \
        call;                                                          \
    }

#define TO_1ST_HANDLING_ADAPTER(adapters, call)                              \
    for (int i = 0; i <MAX_ADAPTERS && NULL != (adapters)[i] && !(call); i++);

class LocAdapterBase;
struct LocSsrMsg;
struct LocOpenMsg;

typedef struct
{
    uint32_t accumulatedDistance;
    uint32_t numOfBatchedPositions;
} LocApiBatchData;

typedef struct
{
    uint32_t hwId;
} LocApiGeofenceData;

struct LocApiMsg: LocMsg {
    private:
        std::function<void ()> mProcImpl;
        inline virtual void proc() const {
            mProcImpl();
        }
    public:
        inline LocApiMsg(std::function<void ()> procImpl ) :
                         mProcImpl(procImpl) {}
};

class LocApiProxyBase {
public:
    inline LocApiProxyBase() {}
    inline virtual ~LocApiProxyBase() {}
    inline virtual void* getSibling2() { return NULL; }
    inline virtual double getGloRfLoss(uint32_t left,
            uint32_t center, uint32_t right, uint8_t gloFrequency) { return 0.0; }
    inline virtual float getGeoidalSeparation(double latitude, double longitude) { return 0.0; }
};

class LocApiBase {
    friend struct LocSsrMsg;
    //LocOpenMsg calls open() which makes it necessary to declare
    //it as a friend
    friend struct LocOpenMsg;
    friend struct LocCloseMsg;
    friend struct LocKillMsg;
    friend class ContextBase;
    static MsgTask* mMsgTask;
    static volatile int32_t mMsgTaskRefCount;
    LocAdapterBase* mLocAdapters[MAX_ADAPTERS];

protected:
    ContextBase *mContext;
    virtual enum loc_api_adapter_err
        open(LOC_API_ADAPTER_EVENT_MASK_T mask);
    virtual enum loc_api_adapter_err
        close();
    LOC_API_ADAPTER_EVENT_MASK_T getEvtMask();
    LOC_API_ADAPTER_EVENT_MASK_T mMask;
    uint32_t mNmeaMask;
    LocApiBase(LOC_API_ADAPTER_EVENT_MASK_T excludedMask,
               ContextBase* context = NULL);
    inline virtual ~LocApiBase() {
        android_atomic_dec(&mMsgTaskRefCount);
        if (nullptr != mMsgTask && 0 == mMsgTaskRefCount) {
            delete mMsgTask;
            mMsgTask = nullptr;
        }
    }
    bool isInSession();
    const LOC_API_ADAPTER_EVENT_MASK_T mExcludedMask;
    bool isMaster();
    EngineLockState mEngineLockState;

public:
    inline void sendMsg(const LocMsg* msg) const {
        if (nullptr != mMsgTask) {
            mMsgTask->sendMsg(msg);
        }
    }
    inline void destroy() {
        close();
        struct LocKillMsg : public LocMsg {
            LocApiBase* mLocApi;
            inline LocKillMsg(LocApiBase* locApi) : LocMsg(), mLocApi(locApi) {}
            inline virtual void proc() const {
                delete mLocApi;
            }
        };
        sendMsg(new LocKillMsg(this));
    }

    static bool needReport(const UlpLocation& ulpLocation,
                           enum loc_sess_status status,
                           LocPosTechMask techMask);

    void addAdapter(LocAdapterBase* adapter);
    void removeAdapter(LocAdapterBase* adapter);

    // upward calls
    void handleEngineUpEvent();
    void handleEngineDownEvent();
    void reportPosition(UlpLocation& location,
                        GpsLocationExtended& locationExtended,
                        enum loc_sess_status status,
                        LocPosTechMask loc_technology_mask =
                                  LOC_POS_TECH_MASK_DEFAULT,
                        GnssDataNotification* pDataNotify = nullptr,
                        int msInWeek = -1);
    void reportSv(GnssSvNotification& svNotify);
    void reportSvPolynomial(GnssSvPolynomial &svPolynomial);
    void reportSvEphemeris(GnssSvEphemerisReport &svEphemeris);
    void reportStatus(LocGpsStatusValue status);
    void reportNmea(const char* nmea, int length);
    void reportData(GnssDataNotification& dataNotify, int msInWeek);
    void reportXtraServer(const char* url1, const char* url2,
                          const char* url3, const int maxlength);
    void reportLocationSystemInfo(const LocationSystemInfo& locationSystemInfo);
    void requestXtraData();
    void requestTime();
    void requestLocation();
    void requestATL(int connHandle, LocAGpsType agps_type, LocApnTypeMask apn_type_mask);
    void releaseATL(int connHandle);
    void requestNiNotify(GnssNiNotification &notify, const void* data,
                         const LocInEmergency emergencyState);
    void reportGnssMeasurements(GnssMeasurements& gnssMeasurements, int msInWeek);
    void reportWwanZppFix(LocGpsLocation &zppLoc);
    void reportZppBestAvailableFix(LocGpsLocation &zppLoc, GpsLocationExtended &location_extended,
            LocPosTechMask tech_mask);
    void reportGnssSvIdConfig(const GnssSvIdConfig& config);
    void reportGnssSvTypeConfig(const GnssSvTypeConfig& config);
    void requestOdcpi(OdcpiRequestInfo& request);
    void reportGnssEngEnergyConsumedEvent(uint64_t energyConsumedSinceFirstBoot);
    void reportDeleteAidingDataEvent(GnssAidingData& aidingData);
    void reportKlobucharIonoModel(GnssKlobucharIonoModel& ionoModel);
    void reportGnssAdditionalSystemInfo(GnssAdditionalSystemInfo& additionalSystemInfo);
    void sendNfwNotification(GnssNfwNotification& notification);
    void reportGnssConfig(uint32_t sessionId, const GnssConfig& gnssConfig);
    void reportLatencyInfo(GnssLatencyInfo& gnssLatencyInfo);
    void reportEngineLockStatus(EngineLockState engineLockState);
    void reportQwesCapabilities
    (
        const std::unordered_map<LocationQwesFeatureType, bool> &featureMap
    );

    void geofenceBreach(size_t count, uint32_t* hwIds, Location& location,
            GeofenceBreachType breachType, uint64_t timestamp);
    void geofenceStatus(GeofenceStatusAvailable available);
    void reportDBTPosition(UlpLocation &location,
                           GpsLocationExtended &locationExtended,
                           enum loc_sess_status status,
                           LocPosTechMask loc_technology_mask);
    void reportLocations(Location* locations, size_t count, BatchingMode batchingMode);
    void reportCompletedTrips(uint32_t accumulated_distance);
    void handleBatchStatusEvent(BatchingStatus batchStatus);

    // downward calls
    virtual void* getSibling();
    virtual LocApiProxyBase* getLocApiProxy();
    virtual void startFix(const LocPosMode& fixCriteria, LocApiResponse* adapterResponse);
    virtual void stopFix(LocApiResponse* adapterResponse);
    virtual void deleteAidingData(const GnssAidingData& data, LocApiResponse* adapterResponse);
    virtual void injectPosition(double latitude, double longitude, float accuracy,
            bool onDemandCpi);
    virtual void injectPosition(const GnssLocationInfoNotification &locationInfo,
            bool onDemandCpi=false);
    virtual void injectPosition(const Location& location, bool onDemandCpi);
    virtual void setTime(LocGpsUtcTime time, int64_t timeReference, int uncertainty);
    virtual void atlOpenStatus(int handle, int is_succ, char* apn, uint32_t apnLen,
            AGpsBearerType bear, LocAGpsType agpsType, LocApnTypeMask mask);
    virtual void atlCloseStatus(int handle, int is_succ);
    virtual LocationError setServerSync(const char* url, int len, LocServerType type);
    virtual LocationError setServerSync(unsigned int ip, int port, LocServerType type);
    virtual void informNiResponse(GnssNiResponse userResponse, const void* passThroughData);
    virtual LocationError setSUPLVersionSync(GnssConfigSuplVersion version);
    virtual enum loc_api_adapter_err setNMEATypesSync(uint32_t typesMask);
    virtual LocationError setLPPConfigSync(GnssConfigLppProfileMask profileMask);
    virtual enum loc_api_adapter_err setSensorPropertiesSync(
            bool gyroBiasVarianceRandomWalk_valid, float gyroBiasVarianceRandomWalk,
            bool accelBiasVarianceRandomWalk_valid, float accelBiasVarianceRandomWalk,
            bool angleBiasVarianceRandomWalk_valid, float angleBiasVarianceRandomWalk,
            bool rateBiasVarianceRandomWalk_valid, float rateBiasVarianceRandomWalk,
            bool velocityBiasVarianceRandomWalk_valid, float velocityBiasVarianceRandomWalk);
    virtual enum loc_api_adapter_err setSensorPerfControlConfigSync(int controlMode,
            int accelSamplesPerBatch, int accelBatchesPerSec, int gyroSamplesPerBatch,
            int gyroBatchesPerSec, int accelSamplesPerBatchHigh, int accelBatchesPerSecHigh,
            int gyroSamplesPerBatchHigh, int gyroBatchesPerSecHigh, int algorithmConfig);
    virtual LocationError
            setAGLONASSProtocolSync(GnssConfigAGlonassPositionProtocolMask aGlonassProtocol);
    virtual LocationError setLPPeProtocolCpSync(GnssConfigLppeControlPlaneMask lppeCP);
    virtual LocationError setLPPeProtocolUpSync(GnssConfigLppeUserPlaneMask lppeUP);
    virtual GnssConfigSuplVersion convertSuplVersion(const uint32_t suplVersion);
    virtual GnssConfigLppeControlPlaneMask convertLppeCp(const uint32_t lppeControlPlaneMask);
    virtual GnssConfigLppeUserPlaneMask convertLppeUp(const uint32_t lppeUserPlaneMask);
    virtual LocationError setEmergencyExtensionWindowSync(const uint32_t emergencyExtensionSeconds);
    virtual void setMeasurementCorrections(
            const GnssMeasurementCorrections& gnssMeasurementCorrections);

    virtual void getWwanZppFix();
    virtual void getBestAvailableZppFix();
    virtual LocationError setGpsLockSync(GnssConfigGpsLock lock);
    virtual void requestForAidingData(GnssAidingDataSvMask svDataMask);
    virtual LocationError setXtraVersionCheckSync(uint32_t check);
    /* Requests for SV/Constellation Control */
    virtual LocationError setBlacklistSvSync(const GnssSvIdConfig& config);
    virtual void setBlacklistSv(const GnssSvIdConfig& config,
                                LocApiResponse *adapterResponse=nullptr);
    virtual void getBlacklistSv();
    virtual void setConstellationControl(const GnssSvTypeConfig& config,
                                         LocApiResponse *adapterResponse=nullptr);
    virtual void getConstellationControl();
    virtual void resetConstellationControl(LocApiResponse *adapterResponse=nullptr);

    virtual void setConstrainedTuncMode(bool enabled,
                                        float tuncConstraint,
                                        uint32_t energyBudget,
                                        LocApiResponse* adapterResponse=nullptr);
    virtual void setPositionAssistedClockEstimatorMode(bool enabled,
                                                       LocApiResponse* adapterResponse=nullptr);
    virtual void getGnssEnergyConsumed();

    virtual void addGeofence(uint32_t clientId, const GeofenceOption& options,
            const GeofenceInfo& info, LocApiResponseData<LocApiGeofenceData>* adapterResponseData);
    virtual void removeGeofence(uint32_t hwId, uint32_t clientId, LocApiResponse* adapterResponse);
    virtual void pauseGeofence(uint32_t hwId, uint32_t clientId, LocApiResponse* adapterResponse);
    virtual void resumeGeofence(uint32_t hwId, uint32_t clientId, LocApiResponse* adapterResponse);
    virtual void modifyGeofence(uint32_t hwId, uint32_t clientId, const GeofenceOption& options,
             LocApiResponse* adapterResponse);

    virtual void startTimeBasedTracking(const TrackingOptions& options,
             LocApiResponse* adapterResponse);
    virtual void stopTimeBasedTracking(LocApiResponse* adapterResponse);
    virtual void startDistanceBasedTracking(uint32_t sessionId, const LocationOptions& options,
             LocApiResponse* adapterResponse);
    virtual void stopDistanceBasedTracking(uint32_t sessionId,
             LocApiResponse* adapterResponse = nullptr);
    virtual void startBatching(uint32_t sessionId, const LocationOptions& options,
            uint32_t accuracy, uint32_t timeout, LocApiResponse* adapterResponse);
    virtual void stopBatching(uint32_t sessionId, LocApiResponse* adapterResponse);
    virtual LocationError startOutdoorTripBatchingSync(uint32_t tripDistance,
            uint32_t tripTbf, uint32_t timeout);
    virtual void startOutdoorTripBatching(uint32_t tripDistance,
            uint32_t tripTbf, uint32_t timeout, LocApiResponse* adapterResponse);
    virtual void reStartOutdoorTripBatching(uint32_t ongoingTripDistance,
            uint32_t ongoingTripInterval, uint32_t batchingTimeout,
            LocApiResponse* adapterResponse);
    virtual LocationError stopOutdoorTripBatchingSync(bool deallocBatchBuffer = true);
    virtual void stopOutdoorTripBatching(bool deallocBatchBuffer = true,
            LocApiResponse* adapterResponse = nullptr);
    virtual LocationError getBatchedLocationsSync(size_t count);
    virtual void getBatchedLocations(size_t count, LocApiResponse* adapterResponse);
    virtual LocationError getBatchedTripLocationsSync(size_t count, uint32_t accumulatedDistance);
    virtual void getBatchedTripLocations(size_t count, uint32_t accumulatedDistance,
            LocApiResponse* adapterResponse);
    virtual LocationError queryAccumulatedTripDistanceSync(uint32_t &accumulated_trip_distance,
            uint32_t &numOfBatchedPositions);
    virtual void queryAccumulatedTripDistance(
            LocApiResponseData<LocApiBatchData>* adapterResponseData);
    virtual void setBatchSize(size_t size);
    virtual void setTripBatchSize(size_t size);
    virtual void addToCallQueue(LocApiResponse* adapterResponse);

    void updateEvtMask();
    void updateNmeaMask(uint32_t mask);

    virtual void updateSystemPowerState(PowerStateType systemPowerState);

    virtual void configRobustLocation(bool enable, bool enableForE911,
                                      LocApiResponse* adapterResponse=nullptr);
    virtual void getRobustLocationConfig(uint32_t sessionId, LocApiResponse* adapterResponse);
    virtual void configMinGpsWeek(uint16_t minGpsWeek,
                                  LocApiResponse* adapterResponse=nullptr);
    virtual void getMinGpsWeek(uint32_t sessionId, LocApiResponse* adapterResponse);

    virtual LocationError setParameterSync(const GnssConfig & gnssConfig);
    virtual void getParameter(uint32_t sessionId, GnssConfigFlagsMask flags,
                              LocApiResponse* adapterResponse=nullptr);

    virtual void configConstellationMultiBand(const GnssSvTypeConfig& secondaryBandConfig,
                                              LocApiResponse* adapterResponse=nullptr);
    virtual void getConstellationMultiBandConfig(uint32_t sessionId,
                                        LocApiResponse* adapterResponse=nullptr);

    inline EngineLockState getEngineLockState() {
        return mEngineLockState;
    }

    inline void setEngineLockState(EngineLockState engineLockState) {
        mEngineLockState = engineLockState;
    }
};

class ElapsedRealtimeEstimator {
private:
    int64_t mCurrentClockDiff;
    int64_t mPrevUtcTimeNanos;
    int64_t mPrevBootTimeNanos;
    int64_t mFixTimeStablizationThreshold;
    int64_t mInitialTravelTime;
    int64_t mPrevDataTimeNanos;
public:

    ElapsedRealtimeEstimator(int64_t travelTimeNanosEstimate):
            mInitialTravelTime(travelTimeNanosEstimate) {reset();}
    int64_t getElapsedRealtimeEstimateNanos(int64_t curDataTimeNanos,
            bool isCurDataTimeTrustable, int64_t tbf);
    inline int64_t getElapsedRealtimeUncNanos() { return 5000000;}
    void reset();

    static int64_t getElapsedRealtimeQtimer(int64_t qtimerTicksAtOrigin);
    static bool getCurrentTime(struct timespec& currentTime, int64_t& sinceBootTimeNanos);
};

typedef LocApiBase* (getLocApi_t)(LOC_API_ADAPTER_EVENT_MASK_T exMask,
                                  ContextBase *context);

} // namespace loc_core

#endif //LOC_API_BASE_H
