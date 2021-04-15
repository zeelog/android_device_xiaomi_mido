#include <media/AudioTrack.h>

// Old constructor without opPackageName
extern "C" void
_ZN7android10AudioTrackC1E19audio_stream_type_tj14audio_format_tjj20audio_output_flags_tPFviPvS4_ES4_i15audio_session_tNS0_13transfer_typeEPK20audio_offload_info_tjiPK18audio_attributes_tbfi(
        audio_stream_type_t streamType,
        uint32_t sampleRate,
        audio_format_t format,
        audio_channel_mask_t channelMask,
        size_t frameCount,
        audio_output_flags_t flags,
        android::AudioTrack::callback_t cbf,
        void* user,
        int32_t notificationFrames,
        audio_session_t sessionId,
        android::AudioTrack::transfer_type transferType,
        const audio_offload_info_t *offloadInfo,
        uid_t uid,
        pid_t pid,
        const audio_attributes_t* pAttributes,
        bool doNotReconnect,
        float maxRequiredSpeed,
        audio_port_handle_t selectedDeviceId) {
    const std::string& opPackageName = "com.qualcomm.wfd.service";
    new android::AudioTrack(streamType, sampleRate, format, channelMask, frameCount, flags, cbf,
                            user, notificationFrames, sessionId, transferType, offloadInfo, uid,
                            pid, pAttributes, doNotReconnect, maxRequiredSpeed, selectedDeviceId,
                            opPackageName);
}
