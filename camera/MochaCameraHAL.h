/*
 * Mocha Camera HAL - Own implementation for Xiaomi Mi Pad
 * HAL3 API with V4L2 capture and NEON ISP
 */

#ifndef MOCHA_CAMERA_HAL_H
#define MOCHA_CAMERA_HAL_H

#include <hardware/camera_common.h>
#include <hardware/camera3.h>
#include <memory>

namespace mocha {

class InFlightTracker;

struct MochaCameraInfo {
    int cameraId;
    const char* sensorName;
    int facing;  // CAMERA_FACING_BACK or CAMERA_FACING_FRONT
    int orientation;
    int maxResolution;
    bool supportsZSL;
};

// Forward declaration
struct mocha_camera_device_t;

class MochaCameraHAL {
public:
    static int getNumberOfCameras();
    static int getCameraInfo(int cameraId, struct camera_info *info);
    static int openCamera(int cameraId, hw_device_t **device);

    static const MochaCameraInfo kCameras[];
    static const int kNumCameras;

private:
};

} // namespace mocha

#endif // MOCHA_CAMERA_HAL_H