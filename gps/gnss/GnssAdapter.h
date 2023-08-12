/* Copyright (c) 2017-2020 The Linux Foundation. All rights reserved.
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

/*
Changes from Qualcomm Innovation Center are provided under the following license:

Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef GNSS_ADAPTER_H
#define GNSS_ADAPTER_H

#include <LocAdapterBase.h>
#include <LocContext.h>
#include <IOsObserver.h>
#include <EngineHubProxyBase.h>
#include <LocationAPI.h>
#include <Agps.h>
#include <SystemStatus.h>
#include <XtraSystemStatusObserver.h>
#include <map>
#include <functional>
#include <loc_misc_utils.h>
#include <queue>
#include <NativeAgpsHandler.h>

#define MAX_URL_LEN 256
#define NMEA_SENTENCE_MAX_LENGTH 200
#define GLONASS_SV_ID_OFFSET 64
#define MAX_SATELLITES_IN_USE 12
#define LOC_NI_NO_RESPONSE_TIME 20
#define LOC_GPS_NI_RESPONSE_IGNORE 4
#define ODCPI_EXPECTED_INJECTION_TIME_MS 10000
#define DELETE_AIDING_DATA_EXPECTED_TIME_MS 5000

class GnssAdapter;

typedef std::map<LocationSessionKey, LocationOptions> LocationSessionMap;
typedef std::map<LocationSessionKey, TrackingOptions> TrackingOptionsMap;

class OdcpiTimer : public LocTimer {
public:
    OdcpiTimer(GnssAdapter* adapter) :
            LocTimer(), mAdapter(adapter), mActive(false) {}

    inline void start() {
        mActive = true;
        LocTimer::start(ODCPI_EXPECTED_INJECTION_TIME_MS, false);
    }
    inline void stop() {
        mActive = false;
        LocTimer::stop();
    }
    inline void restart() {
        stop();
        start();
    }
    inline bool isActive() {
        return mActive;
    }

private:
    // Override
    virtual void timeOutCallback() override;

    GnssAdapter* mAdapter;
    bool mActive;
};

typedef struct {
    pthread_t               thread;        /* NI thread */
    uint32_t                respTimeLeft;  /* examine time for NI response */
    bool                    respRecvd;     /* NI User reponse received or not from Java layer*/
    void*                   rawRequest;
    uint32_t                reqID;         /* ID to check against response */
    GnssNiResponse          resp;
    pthread_cond_t          tCond;
    pthread_mutex_t         tLock;
    GnssAdapter*            adapter;
} NiSession;
typedef struct {
    NiSession session;    /* SUPL NI Session */
    NiSession sessionEs;  /* Emergency SUPL NI Session */
    uint32_t reqIDCounter;
} NiData;

typedef enum {
    NMEA_PROVIDER_AP = 0, // Application Processor Provider of NMEA
    NMEA_PROVIDER_MP      // Modem Processor Provider of NMEA
} NmeaProviderType;
typedef struct {
    GnssSvType svType;
    const char* talker;
    uint64_t mask;
    uint32_t svIdOffset;
} NmeaSvMeta;

typedef struct {
    double latitude;
    double longitude;
    float  accuracy;
    // the CPI will be blocked until the boot time
    // specified in blockedTillTsMs
    int64_t blockedTillTsMs;
    // CPIs whose both latitude and longitude differ
    // no more than latLonThreshold will be blocked
    // in units of degree
    double latLonDiffThreshold;
} BlockCPIInfo;

typedef struct {
    bool isValid;
    bool enable;
    float tuncThresholdMs; // need to be specified if enable is true
    uint32_t energyBudget; // need to be specified if enable is true
} TuncConfigInfo;

typedef struct {
    bool isValid;
    bool enable;
} PaceConfigInfo;

typedef struct {
    bool isValid;
    bool enable;
    bool enableFor911;
} RobustLocationConfigInfo;

typedef struct {
    TuncConfigInfo tuncConfigInfo;
    PaceConfigInfo paceConfigInfo;
    RobustLocationConfigInfo robustLocationConfigInfo;
    LeverArmConfigInfo  leverArmConfigInfo;
} LocIntegrationConfigInfo;

using namespace loc_core;

namespace loc_core {
    class SystemStatus;
}

typedef std::function<void(
    uint64_t gnssEnergyConsumedFromFirstBoot
)> GnssEnergyConsumedCallback;

typedef void* QDgnssListenerHDL;
typedef std::function<void(
    bool    sessionActive
)> QDgnssSessionActiveCb;

struct CdfwInterface {
    void (*startDgnssApiService)(const MsgTask& msgTask);
    QDgnssListenerHDL (*createUsableReporter)(
            QDgnssSessionActiveCb sessionActiveCb);
    void (*destroyUsableReporter)(QDgnssListenerHDL handle);
    void (*reportUsable)(QDgnssListenerHDL handle, bool usable);
};

typedef uint16_t  DGnssStateBitMask;
#define DGNSS_STATE_ENABLE_NTRIP_COMMAND      0X01
#define DGNSS_STATE_NO_NMEA_PENDING           0X02
#define DGNSS_STATE_NTRIP_SESSION_STARTED     0X04

class GnssReportLoggerUtil {
public:
    typedef void (*LogGnssLatency)(const GnssLatencyInfo& gnssLatencyMeasInfo);

    GnssReportLoggerUtil() : mLogLatency(nullptr) {
        const char* libname = "liblocdiagiface.so";
        void* libHandle = nullptr;
        mLogLatency = (LogGnssLatency)dlGetSymFromLib(libHandle, libname, "LogGnssLatency");
    }

    bool isLogEnabled();
    void log(const GnssLatencyInfo& gnssLatencyMeasInfo);

private:
    LogGnssLatency mLogLatency;
};

class GnssAdapter : public LocAdapterBase {

    /* ==== Engine Hub ===================================================================== */
    EngineHubProxyBase* mEngHubProxy;
    bool mNHzNeeded;
    bool mSPEAlreadyRunningAtHighestInterval;

    /* ==== TRACKING ======================================================================= */
    TrackingOptionsMap mTimeBasedTrackingSessions;
    LocationSessionMap mDistanceBasedTrackingSessions;
    LocPosMode mLocPositionMode;
    GnssSvUsedInPosition mGnssSvIdUsedInPosition;
    bool mGnssSvIdUsedInPosAvail;
    GnssSvMbUsedInPosition mGnssMbSvIdUsedInPosition;
    bool mGnssMbSvIdUsedInPosAvail;

    /* ==== CONTROL ======================================================================== */
    LocationControlCallbacks mControlCallbacks;
    uint32_t mAfwControlId;
    uint32_t mNmeaMask;
    uint64_t mPrevNmeaRptTimeNsec;
    GnssSvIdConfig mGnssSvIdConfig;
    GnssSvTypeConfig mGnssSeconaryBandConfig;
    GnssSvTypeConfig mGnssSvTypeConfig;
    GnssSvTypeConfigCallback mGnssSvTypeConfigCb;
    bool mSupportNfwControl;
    LocIntegrationConfigInfo mLocConfigInfo;

    /* ==== NI ============================================================================= */
    NiData mNiData;

    /* ==== AGPS =========================================================================== */
    // This must be initialized via initAgps()
    AgpsManager mAgpsManager;
    void initAgps(const AgpsCbInfo& cbInfo);

    /* ==== NFW =========================================================================== */
    NfwStatusCb mNfwCb;
    IsInEmergencySession mIsE911Session;
    inline void initNfw(const NfwCbInfo& cbInfo) {
        mNfwCb = (NfwStatusCb)cbInfo.visibilityControlCb;
        mIsE911Session = (IsInEmergencySession)cbInfo.isInEmergencySession;
    }

    /* ==== Measurement Corrections========================================================= */
    bool mIsMeasCorrInterfaceOpen;
    measCorrSetCapabilitiesCb mMeasCorrSetCapabilitiesCb;
    bool initMeasCorr(bool bSendCbWhenNotSupported);
    bool mIsAntennaInfoInterfaceOpened;

    /* ==== DGNSS Data Usable Report======================================================== */
    QDgnssListenerHDL mQDgnssListenerHDL;
    const CdfwInterface* mCdfwInterface;
    bool mDGnssNeedReport;
    bool mDGnssDataUsage;
    void reportDGnssDataUsable(const GnssSvMeasurementSet &svMeasurementSet);

    /* ==== ODCPI ========================================================================== */
    OdcpiRequestCallback mOdcpiRequestCb;
    bool mOdcpiRequestActive;
    OdcpiPrioritytype mCallbackPriority;
    OdcpiTimer mOdcpiTimer;
    OdcpiRequestInfo mOdcpiRequest;
    void odcpiTimerExpire();

    /* ==== DELETEAIDINGDATA =============================================================== */
    int64_t mLastDeleteAidingDataTime;

    /* === SystemStatus ===================================================================== */
    SystemStatus* mSystemStatus;
    std::string mServerUrl;
    std::string mMoServerUrl;
    XtraSystemStatusObserver mXtraObserver;
    LocationSystemInfo mLocSystemInfo;
    std::vector<GnssSvIdSource> mBlacklistedSvIds;
    PowerStateType mSystemPowerState;

    /* === Misc ===================================================================== */
    BlockCPIInfo mBlockCPIInfo;
    bool mPowerOn;
    uint32_t mAllowFlpNetworkFixes;
    std::queue<GnssLatencyInfo> mGnssLatencyInfoQueue;
    GnssReportLoggerUtil mLogger;
    bool mDreIntEnabled;

    /* === NativeAgpsHandler ======================================================== */
    NativeAgpsHandler mNativeAgpsHandler;

    /* === Misc callback from QMI LOC API ============================================== */
    GnssEnergyConsumedCallback mGnssEnergyConsumedCb;
    std::function<void(bool)> mPowerStateCb;

    /*==== CONVERSION ===================================================================*/
    static void convertOptions(LocPosMode& out, const TrackingOptions& trackingOptions);
    static void convertLocation(Location& out, const UlpLocation& ulpLocation,
                                const GpsLocationExtended& locationExtended);
    static void convertLocationInfo(GnssLocationInfoNotification& out,
                                    const GpsLocationExtended& locationExtended,
                                    loc_sess_status status);
    static uint16_t getNumSvUsed(uint64_t svUsedIdsMask,
                                 int totalSvCntInThisConstellation);

    /* ======== UTILITIES ================================================================== */
    inline void initOdcpi(const OdcpiRequestCallback& callback, OdcpiPrioritytype priority);
    inline void injectOdcpi(const Location& location);
    static bool isFlpClient(LocationCallbacks& locationCallbacks);

    /*==== DGnss Ntrip Source ==========================================================*/
    StartDgnssNtripParams   mStartDgnssNtripParams;
    bool    mSendNmeaConsent;
    DGnssStateBitMask   mDgnssState;
    void checkUpdateDgnssNtrip(bool isLocationValid);
    void stopDgnssNtrip();
    uint64_t   mDgnssLastNmeaBootTimeMilli;

protected:

    /* ==== CLIENT ========================================================================= */
    virtual void updateClientsEventMask();
    virtual void stopClientSessions(LocationAPI* client);
    inline void setNmeaReportRateConfig();
    void logLatencyInfo();

public:
    GnssAdapter();
    virtual inline ~GnssAdapter() { }

    /* ==== SSR ============================================================================ */
    /* ======== EVENTS ====(Called from QMI Thread)========================================= */
    virtual void handleEngineUpEvent();
    /* ======== UTILITIES ================================================================== */
    void restartSessions(bool modemSSR = false);
    void checkAndRestartTimeBasedSession();
    void checkAndRestartSPESession();
    void suspendSessions();

    /* ==== CLIENT ========================================================================= */
    /* ======== COMMANDS ====(Called from Client Thread)==================================== */
    virtual void addClientCommand(LocationAPI* client, const LocationCallbacks& callbacks);

    /* ==== TRACKING ======================================================================= */
    /* ======== COMMANDS ====(Called from Client Thread)==================================== */
    uint32_t startTrackingCommand(
            LocationAPI* client, TrackingOptions& trackingOptions);
    void updateTrackingOptionsCommand(
            LocationAPI* client, uint32_t id, TrackingOptions& trackingOptions);
    void stopTrackingCommand(LocationAPI* client, uint32_t id);
    /* ======== RESPONSES ================================================================== */
    void reportResponse(LocationAPI* client, LocationError err, uint32_t sessionId);
    /* ======== UTILITIES ================================================================== */
    bool isTimeBasedTrackingSession(LocationAPI* client, uint32_t sessionId);
    bool isDistanceBasedTrackingSession(LocationAPI* client, uint32_t sessionId);
    bool hasCallbacksToStartTracking(LocationAPI* client);
    bool isTrackingSession(LocationAPI* client, uint32_t sessionId);
    void saveTrackingSession(LocationAPI* client, uint32_t sessionId,
                             const TrackingOptions& trackingOptions);
    void eraseTrackingSession(LocationAPI* client, uint32_t sessionId);

    bool setLocPositionMode(const LocPosMode& mode);
    LocPosMode& getLocPositionMode() { return mLocPositionMode; }

    bool startTimeBasedTrackingMultiplex(LocationAPI* client, uint32_t sessionId,
                                         const TrackingOptions& trackingOptions);
    void startTimeBasedTracking(LocationAPI* client, uint32_t sessionId,
            const TrackingOptions& trackingOptions);
    bool stopTimeBasedTrackingMultiplex(LocationAPI* client, uint32_t id);
    void stopTracking(LocationAPI* client, uint32_t id);
    bool updateTrackingMultiplex(LocationAPI* client, uint32_t id,
            const TrackingOptions& trackingOptions);
    void updateTracking(LocationAPI* client, uint32_t sessionId,
            const TrackingOptions& updatedOptions, const TrackingOptions& oldOptions);
    bool checkAndSetSPEToRunforNHz(TrackingOptions & out);

    void setConstrainedTunc(bool enable, float tuncConstraint,
                            uint32_t energyBudget, uint32_t sessionId);
    void setPositionAssistedClockEstimator(bool enable, uint32_t sessionId);
    void gnssUpdateSvConfig(uint32_t sessionId,
                        const GnssSvTypeConfig& constellationEnablementConfig,
                        const GnssSvIdConfig& blacklistSvConfig);

    void gnssUpdateSecondaryBandConfig(
        uint32_t sessionId, const GnssSvTypeConfig& secondaryBandConfig);
    void gnssGetSecondaryBandConfig(uint32_t sessionId);
    void resetSvConfig(uint32_t sessionId);
    void configLeverArm(uint32_t sessionId, const LeverArmConfigInfo& configInfo);
    void configRobustLocation(uint32_t sessionId, bool enable, bool enableForE911);
    void configMinGpsWeek(uint32_t sessionId, uint16_t minGpsWeek);

    /* ==== NI ============================================================================= */
    /* ======== COMMANDS ====(Called from Client Thread)==================================== */
    void gnssNiResponseCommand(LocationAPI* client, uint32_t id, GnssNiResponse response);
    /* ======================(Called from NI Thread)======================================== */
    void gnssNiResponseCommand(GnssNiResponse response, void* rawRequest);
    /* ======== UTILITIES ================================================================== */
    bool hasNiNotifyCallback(LocationAPI* client);
    NiData& getNiData() { return mNiData; }

    /* ==== CONTROL CLIENT ================================================================= */
    /* ======== COMMANDS ====(Called from Client Thread)==================================== */
    uint32_t enableCommand(LocationTechnologyType techType);
    void disableCommand(uint32_t id);
    void setControlCallbacksCommand(LocationControlCallbacks& controlCallbacks);
    void readConfigCommand();
    void requestUlpCommand();
    void initEngHubProxyCommand();
    uint32_t* gnssUpdateConfigCommand(const GnssConfig& config);
    uint32_t* gnssGetConfigCommand(GnssConfigFlagsMask mask);
    uint32_t gnssDeleteAidingDataCommand(GnssAidingData& data);
    void deleteAidingData(const GnssAidingData &data, uint32_t sessionId);
    void gnssUpdateXtraThrottleCommand(const bool enabled);
    std::vector<LocationError> gnssUpdateConfig(const std::string& oldMoServerUrl,
            const std::string& moServerUrl,
            const std::string& serverUrl,
            GnssConfig& gnssConfigRequested,
            GnssConfig& gnssConfigNeedEngineUpdate, size_t count = 0);

    /* ==== GNSS SV TYPE CONFIG ============================================================ */
    /* ==== COMMANDS ====(Called from Client Thread)======================================== */
    /* ==== These commands are received directly from client bypassing Location API ======== */
    void gnssUpdateSvTypeConfigCommand(GnssSvTypeConfig config);
    void gnssGetSvTypeConfigCommand(GnssSvTypeConfigCallback callback);
    void gnssResetSvTypeConfigCommand();

    /* ==== UTILITIES ====================================================================== */
    LocationError gnssSvIdConfigUpdateSync(const std::vector<GnssSvIdSource>& blacklistedSvIds);
    LocationError gnssSvIdConfigUpdateSync();
    void gnssSvIdConfigUpdate(const std::vector<GnssSvIdSource>& blacklistedSvIds);
    void gnssSvIdConfigUpdate();
    void gnssSvTypeConfigUpdate(const GnssSvTypeConfig& config);
    void gnssSvTypeConfigUpdate(bool sendReset = false);
    inline void gnssSetSvTypeConfig(const GnssSvTypeConfig& config)
    { mGnssSvTypeConfig = config; }
    inline void gnssSetSvTypeConfigCallback(GnssSvTypeConfigCallback callback)
    { mGnssSvTypeConfigCb = callback; }
    inline GnssSvTypeConfigCallback gnssGetSvTypeConfigCallback()
    { return mGnssSvTypeConfigCb; }
    void setConfig();
    void gnssSecondaryBandConfigUpdate(LocApiResponse* locApiResponse= nullptr);

    /* ========= AGPS ====================================================================== */
    /* ======== COMMANDS ====(Called from Client Thread)==================================== */
    void initDefaultAgpsCommand();
    void initAgpsCommand(const AgpsCbInfo& cbInfo);
    void initNfwCommand(const NfwCbInfo& cbInfo);
    void dataConnOpenCommand(AGpsExtType agpsType,
            const char* apnName, int apnLen, AGpsBearerType bearerType);
    void dataConnClosedCommand(AGpsExtType agpsType);
    void dataConnFailedCommand(AGpsExtType agpsType);
    void getGnssEnergyConsumedCommand(GnssEnergyConsumedCallback energyConsumedCb);
    void nfwControlCommand(bool enable);
    uint32_t setConstrainedTuncCommand (bool enable, float tuncConstraint,
                                        uint32_t energyBudget);
    uint32_t setPositionAssistedClockEstimatorCommand (bool enable);
    uint32_t gnssUpdateSvConfigCommand(const GnssSvTypeConfig& constellationEnablementConfig,
                                       const GnssSvIdConfig& blacklistSvConfig);
    uint32_t gnssUpdateSecondaryBandConfigCommand(
                                       const GnssSvTypeConfig& secondaryBandConfig);
    uint32_t gnssGetSecondaryBandConfigCommand();
    uint32_t configLeverArmCommand(const LeverArmConfigInfo& configInfo);
    uint32_t configRobustLocationCommand(bool enable, bool enableForE911);
    bool openMeasCorrCommand(const measCorrSetCapabilitiesCb setCapabilitiesCb);
    bool measCorrSetCorrectionsCommand(const GnssMeasurementCorrections gnssMeasCorr);
    inline void closeMeasCorrCommand() { mIsMeasCorrInterfaceOpen = false; }
    uint32_t antennaInfoInitCommand(const antennaInfoCb antennaInfoCallback);
    inline void antennaInfoCloseCommand() { mIsAntennaInfoInterfaceOpened = false; }
    uint32_t configMinGpsWeekCommand(uint16_t minGpsWeek);
    uint32_t configDeadReckoningEngineParamsCommand(const DeadReckoningEngineConfig& dreConfig);
    uint32_t configEngineRunStateCommand(PositioningEngineMask engType,
                                         LocEngineRunState engState);

    /* ========= ODCPI ===================================================================== */
    /* ======== COMMANDS ====(Called from Client Thread)==================================== */
    void initOdcpiCommand(const OdcpiRequestCallback& callback, OdcpiPrioritytype priority);
    void injectOdcpiCommand(const Location& location);
    /* ======== RESPONSES ================================================================== */
    void reportResponse(LocationError err, uint32_t sessionId);
    void reportResponse(size_t count, LocationError* errs, uint32_t* ids);
    /* ======== UTILITIES ================================================================== */
    LocationControlCallbacks& getControlCallbacks() { return mControlCallbacks; }
    void setControlCallbacks(const LocationControlCallbacks& controlCallbacks)
    { mControlCallbacks = controlCallbacks; }
    void setAfwControlId(uint32_t id) { mAfwControlId = id; }
    uint32_t getAfwControlId() { return mAfwControlId; }
    virtual bool isInSession() { return !mTimeBasedTrackingSessions.empty(); }
    void initDefaultAgps();
    bool initEngHubProxy();
    void initCDFWService();
    void odcpiTimerExpireEvent();

    /* ==== REPORTS ======================================================================== */
    virtual void handleEngineLockStatusEvent(EngineLockState engineLockState);
    void handleEngineLockStatus(EngineLockState engineLockState);
    /* ======== EVENTS ====(Called from QMI/EngineHub Thread)===================================== */
    virtual void reportPositionEvent(const UlpLocation& ulpLocation,
                                     const GpsLocationExtended& locationExtended,
                                     enum loc_sess_status status,
                                     LocPosTechMask techMask,
                                     GnssDataNotification* pDataNotify = nullptr,
                                     int msInWeek = -1);
    virtual void reportEnginePositionsEvent(unsigned int count,
                                            EngineLocationInfo* locationArr);

    virtual void reportSvEvent(const GnssSvNotification& svNotify,
                               bool fromEngineHub=false);
    virtual void reportNmeaEvent(const char* nmea, size_t length);
    virtual void reportDataEvent(const GnssDataNotification& dataNotify, int msInWeek);
    virtual bool requestNiNotifyEvent(const GnssNiNotification& notify, const void* data,
                                      const LocInEmergency emergencyState);
    virtual void reportGnssMeasurementsEvent(const GnssMeasurements& gnssMeasurements,
                                                int msInWeek);
    virtual void reportSvPolynomialEvent(GnssSvPolynomial &svPolynomial);
    virtual void reportSvEphemerisEvent(GnssSvEphemerisReport & svEphemeris);
    virtual void reportGnssSvIdConfigEvent(const GnssSvIdConfig& config);
    virtual void reportGnssSvTypeConfigEvent(const GnssSvTypeConfig& config);
    virtual void reportGnssConfigEvent(uint32_t sessionId, const GnssConfig& gnssConfig);
    virtual bool reportGnssEngEnergyConsumedEvent(uint64_t energyConsumedSinceFirstBoot);
    virtual void reportLocationSystemInfoEvent(const LocationSystemInfo& locationSystemInfo);

    virtual bool requestATL(int connHandle, LocAGpsType agps_type, LocApnTypeMask apn_type_mask);
    virtual bool releaseATL(int connHandle);
    virtual bool requestOdcpiEvent(OdcpiRequestInfo& request);
    virtual bool reportDeleteAidingDataEvent(GnssAidingData& aidingData);
    virtual bool reportKlobucharIonoModelEvent(GnssKlobucharIonoModel& ionoModel);
    virtual bool reportGnssAdditionalSystemInfoEvent(
            GnssAdditionalSystemInfo& additionalSystemInfo);
    virtual void reportNfwNotificationEvent(GnssNfwNotification& notification);
    virtual void reportLatencyInfoEvent(const GnssLatencyInfo& gnssLatencyInfo);
    virtual bool reportQwesCapabilities
    (
        const std::unordered_map<LocationQwesFeatureType, bool> &featureMap
    );
    void reportPdnTypeFromWds(int pdnType, AGpsExtType agpsType, std::string apnName,
            AGpsBearerType bearerType);

    /* ======== UTILITIES ================================================================= */
    bool needReportForGnssClient(const UlpLocation& ulpLocation,
            enum loc_sess_status status, LocPosTechMask techMask);
    bool needReportForFlpClient(enum loc_sess_status status, LocPosTechMask techMask);
    bool needToGenerateNmeaReport(const uint32_t &gpsTimeOfWeekMs,
        const struct timespec32_t &apTimeStamp);
    void reportPosition(const UlpLocation &ulpLocation,
                        const GpsLocationExtended &locationExtended,
                        enum loc_sess_status status,
                        LocPosTechMask techMask);
    void reportEnginePositions(unsigned int count,
                               const EngineLocationInfo* locationArr);
    void reportSv(GnssSvNotification& svNotify);
    void reportNmea(const char* nmea, size_t length);
    void reportData(GnssDataNotification& dataNotify);
    bool requestNiNotify(const GnssNiNotification& notify, const void* data,
                         const bool bInformNiAccept);
    void reportGnssMeasurementData(const GnssMeasurementsNotification& measurements);
    void reportGnssSvIdConfig(const GnssSvIdConfig& config);
    void reportGnssSvTypeConfig(const GnssSvTypeConfig& config);
    void reportGnssConfig(uint32_t sessionId, const GnssConfig& gnssConfig);
    void requestOdcpi(const OdcpiRequestInfo& request);
    void invokeGnssEnergyConsumedCallback(uint64_t energyConsumedSinceFirstBoot);
    void saveGnssEnergyConsumedCallback(GnssEnergyConsumedCallback energyConsumedCb);
    void reportLocationSystemInfo(const LocationSystemInfo & locationSystemInfo);
    inline void reportNfwNotification(const GnssNfwNotification& notification) {
        if (NULL != mNfwCb) {
            mNfwCb(notification);
        }
    }
    inline bool getE911State(void) {
        if (NULL != mIsE911Session) {
            return mIsE911Session();
        }
        return false;
    }

    void updateSystemPowerState(PowerStateType systemPowerState);
    void reportSvPolynomial(const GnssSvPolynomial &svPolynomial);


    std::vector<double> parseDoublesString(char* dString);
    void reportGnssAntennaInformation(const antennaInfoCb antennaInfoCallback);

    /*======== GNSSDEBUG ================================================================*/
    bool getDebugReport(GnssDebugReport& report);
    /* get AGC information from system status and fill it */
    void getAgcInformation(GnssMeasurementsNotification& measurements, int msInWeek);
    /* get Data information from system status and fill it */
    void getDataInformation(GnssDataNotification& data, int msInWeek);

    /*==== SYSTEM STATUS ================================================================*/
    inline SystemStatus* getSystemStatus(void) { return mSystemStatus; }
    std::string& getServerUrl(void) { return mServerUrl; }
    std::string& getMoServerUrl(void) { return mMoServerUrl; }

    /*==== CONVERSION ===================================================================*/
    static uint32_t convertSuplVersion(const GnssConfigSuplVersion suplVersion);
    static uint32_t convertEP4ES(const GnssConfigEmergencyPdnForEmergencySupl);
    static uint32_t convertSuplEs(const GnssConfigSuplEmergencyServices suplEmergencyServices);
    static uint32_t convertLppeCp(const GnssConfigLppeControlPlaneMask lppeControlPlaneMask);
    static uint32_t convertLppeUp(const GnssConfigLppeUserPlaneMask lppeUserPlaneMask);
    static uint32_t convertAGloProt(const GnssConfigAGlonassPositionProtocolMask);
    static uint32_t convertSuplMode(const GnssConfigSuplModeMask suplModeMask);
    static void convertSatelliteInfo(std::vector<GnssDebugSatelliteInfo>& out,
                                     const GnssSvType& in_constellation,
                                     const SystemStatusReports& in);
    static bool convertToGnssSvIdConfig(
            const std::vector<GnssSvIdSource>& blacklistedSvIds, GnssSvIdConfig& config);
    static void convertFromGnssSvIdConfig(
            const GnssSvIdConfig& svConfig, std::vector<GnssSvIdSource>& blacklistedSvIds);
    static void convertGnssSvIdMaskToList(
            uint64_t svIdMask, std::vector<GnssSvIdSource>& svIds,
            GnssSvId initialSvId, GnssSvType svType);
    static void computeVRPBasedLla(const UlpLocation& loc, GpsLocationExtended& locExt,
                                   const LeverArmConfigInfo& leverArmConfigInfo);

    void injectLocationCommand(double latitude, double longitude, float accuracy);
    void injectLocationExtCommand(const GnssLocationInfoNotification &locationInfo);

    void injectTimeCommand(int64_t time, int64_t timeReference, int32_t uncertainty);
    void blockCPICommand(double latitude, double longitude, float accuracy,
                         int blockDurationMsec, double latLonDiffThreshold);

    /* ==== MISCELLANEOUS ================================================================== */
    /* ======== COMMANDS ====(Called from Client Thread)==================================== */
    void getPowerStateChangesCommand(std::function<void(bool)> powerStateCb);
    /* ======== UTILITIES ================================================================== */
    void reportPowerStateIfChanged();
    void savePowerStateCallback(std::function<void(bool)> powerStateCb){
            mPowerStateCb = powerStateCb; }
    bool getPowerState() { return mPowerOn; }
    inline PowerStateType getSystemPowerState() { return mSystemPowerState; }

    void setAllowFlpNetworkFixes(uint32_t allow) { mAllowFlpNetworkFixes = allow; }
    uint32_t getAllowFlpNetworkFixes() { return mAllowFlpNetworkFixes; }
    void setSuplHostServer(const char* server, int port, LocServerType type);
    void notifyClientOfCachedLocationSystemInfo(LocationAPI* client,
                                                const LocationCallbacks& callbacks);
    LocationCapabilitiesMask getCapabilities();
    void updateSystemPowerStateCommand(PowerStateType systemPowerState);

    /*==== DGnss Usable Report Flag ====================================================*/
    inline void setDGnssUsableFLag(bool dGnssNeedReport) { mDGnssNeedReport = dGnssNeedReport;}
    inline bool isNMEAPrintEnabled() {
       return ((mContext != NULL) && (0 != mContext->mGps_conf.ENABLE_NMEA_PRINT));
    }

    /*==== DGnss Ntrip Source ==========================================================*/
    void updateNTRIPGGAConsentCommand(bool consentAccepted) { mSendNmeaConsent = consentAccepted; }
    void enablePPENtripStreamCommand(const GnssNtripConnectionParams& params, bool enableRTKEngine);
    void disablePPENtripStreamCommand();
    void handleEnablePPENtrip(const GnssNtripConnectionParams& params);
    void handleDisablePPENtrip();
    void reportGGAToNtrip(const char* nmea);
    inline bool isDgnssNmeaRequired() { return mSendNmeaConsent &&
            mStartDgnssNtripParams.ntripParams.requiresNmeaLocation;}
};

#endif //GNSS_ADAPTER_H
