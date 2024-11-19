#include <media/AudioTrack.h>
#include <media/AudioResampler.h>

using namespace android;

extern "C" status_t _ZN7android10AudioTrack11getPositionEPj(uint32_t *position);

extern "C" status_t _ZNK7android10AudioTrack11getPositionEPj(uint32_t *position) {
    return _ZN7android10AudioTrack11getPositionEPj(position);
}

extern "C" ssize_t _ZN7android10AudioTrack5writeEPKvjb(const void* buffer, size_t userSize, bool blocking);

extern "C" ssize_t _ZN7android10AudioTrack5writeEPKvj(const void* buffer, size_t userSize) {
    return _ZN7android10AudioTrack5writeEPKvjb(buffer, userSize, false);
}

extern "C" void _ZN7android10AudioTrackC2E19audio_stream_type_tj14audio_format_tjj20audio_output_flags_tPFviPvS4_ES4_i15audio_session_tNS0_13transfer_typeEPK20audio_offload_info_tjiPK18audio_attributes_tbf(void *thisPtr, audio_stream_type_t streamType, uint32_t sampleRate, audio_format_t format, audio_channel_mask_t channelMask, size_t frameCount, audio_output_flags_t flags, AudioTrack::callback_t cbf, void* user, int32_t notificationFrames, audio_session_t sessionId, AudioTrack::transfer_type transferType, const audio_offload_info_t *offloadInfo, uid_t uid, pid_t pid, const audio_attributes_t* pAttributes, bool doNotReconnect, float maxRequiredSpeed);

extern "C" void _ZN7android10AudioTrackC1E19audio_stream_type_tj14audio_format_tji20audio_output_flags_tPFviPvS4_ES4_iiNS0_13transfer_typeEPK20audio_offload_info_ti(void *thisPtr, audio_stream_type_t streamType, uint32_t sampleRate, audio_format_t format, audio_channel_mask_t channelMask, int frameCount, audio_output_flags_t flags, AudioTrack::callback_t cbf, void* user, int notificationFrames, int sessionId, AudioTrack::transfer_type transferType, const audio_offload_info_t *offloadInfo, int uid) {
    _ZN7android10AudioTrackC2E19audio_stream_type_tj14audio_format_tjj20audio_output_flags_tPFviPvS4_ES4_i15audio_session_tNS0_13transfer_typeEPK20audio_offload_info_tjiPK18audio_attributes_tbf(thisPtr, streamType, sampleRate, format, channelMask, frameCount, flags, cbf, user, notificationFrames, static_cast<audio_session_t>(sessionId), transferType, offloadInfo, uid, -1, nullptr, false, -1);
}

extern "C" void _ZN7android14AudioResampler6createE14audio_format_tiiNS0_11src_qualityE(audio_format_t format, int inChannelCount, int32_t sampleRate, AudioResampler::src_quality quality);

extern "C" void _ZN7android14AudioResampler6createEiiiNS0_11src_qualityE(int bitDepth, int inChannelCount, int32_t sampleRate, AudioResampler::src_quality quality) {
    _ZN7android14AudioResampler6createE14audio_format_tiiNS0_11src_qualityE((bitDepth == 16) ? AUDIO_FORMAT_PCM_16_BIT : AUDIO_FORMAT_PCM_FLOAT, inChannelCount, sampleRate, quality);
}
