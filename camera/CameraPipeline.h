/*
 * CameraPipeline - Integrates V4L2 capture with NEON ISP processing
 */

#ifndef MOCHA_CAMERA_PIPELINE_H
#define MOCHA_CAMERA_PIPELINE_H

#include <cstdint>
#include <memory>

#include <system/graphics.h>
#include "isp/DemosaicNEON.h"
#include "isp/ColorConvNEON.h"

namespace mocha {

enum PipelineState {
    PIPELINE_CLOSED,
    PIPELINE_OPENED,
    PIPELINE_STREAMING
};

struct PipelineConfig {
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;  // V4L2 format
    uint8_t bayerPattern;
    bool enableISP;
};

struct V4l2Buffer {
    void* start;
    size_t length;
};

class CameraPipeline {
public:
    CameraPipeline();
    ~CameraPipeline();

    int open(int cameraId);
    int close();

    int configure(const PipelineConfig& config);
    int startStreaming();
    int stopStreaming();

    int captureFrame(uint8_t* outputBuffer, uint32_t outputFormat);

    PipelineState getState() const { return mState; }

private:
    int processBayerToYuv(const uint8_t* bayerData, uint8_t* output, uint32_t outputFormat);

    int mFd;
    int mCameraId;
    PipelineState mState;
    bool mStreaming;

    std::unique_ptr<DemosaicNEON> mDemosaic;
    std::unique_ptr<ColorConvNEON> mColorConv;

    PipelineConfig mConfig;

    V4l2Buffer mBuffers[4];
    int mBufferCount;
    int mCurrentBuffer;

    uint8_t* mRgbBuffer;
    uint32_t mRgbBufferSize;
};

} // namespace mocha

#endif // MOCHA_CAMERA_PIPELINE_H
