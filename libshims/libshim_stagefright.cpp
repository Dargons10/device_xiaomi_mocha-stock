#include <stdint.h>

#include <media/IMediaSource.h>

using namespace android;

// MEDIA_MIMETYPE_AUDIO_WMA
extern "C" const char *_ZN7android24MEDIA_MIMETYPE_AUDIO_WMAE = "audio/x-ms-wma";
// MEDIA_MIMETYPE_VIDEO_MJPEG
extern "C" const char *_ZN7android26MEDIA_MIMETYPE_VIDEO_MJPEGE = "video/x-jpeg";
// MEDIA_MIMETYPE_VIDEO_WMV
extern "C" const char *_ZN7android24MEDIA_MIMETYPE_VIDEO_WMVE = "video/x-ms-wmv";
// MEDIA_MIMETYPE_CONTAINER_ASF
extern "C" const char *_ZN7android28MEDIA_MIMETYPE_CONTAINER_ASFE = "video/x-ms-asf";

// android::IMediaSource::ReadOptions::getSeekTo(long long*, android::IMediaSource::ReadOptions::SeekMode*)
extern "C" void _ZNK7android12IMediaSource11ReadOptions9getSeekToEPxPNS1_8SeekModeE(int64_t *time_us, android::IMediaSource::ReadOptions::SeekMode *mode);

// android::MediaSource::ReadOptions::getSeekTo(long long*, android::MediaSource::ReadOptions::SeekMode*)
extern "C" void _ZNK7android11MediaSource11ReadOptions9getSeekToEPxPNS1_8SeekModeE(int64_t *time_us, android::IMediaSource::ReadOptions::SeekMode *mode) {
	_ZNK7android12IMediaSource11ReadOptions9getSeekToEPxPNS1_8SeekModeE(time_us, mode);
}
