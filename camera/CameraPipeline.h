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
    uint32_t pixelFormat;
    uint8_t bayerPattern;
    uint8_t offset_x;
    uint8_t offset_y;
    bool flipV;
    bool enableISP;
    uint16_t blackLevel;
    float wbGain[4];
    float ccm[9];
    float gamma;
    bool enableAE;
    bool enableAWB;
    float targetLuma;
    float digitalGain;  // software brightness boost (multiplicative on RGB)
};

struct V4l2Buffer {
    void* start;
    size_t length;
    bool allocated;
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

    int setExposure(int exposure);
    int setGain(int gain);
    int getExposure();
    int getGain();

    int setFocus(int position);
    int getFocusPosition() const { return mFocusPosition; }
    int getAfState() const { return mAfState; }
    void startAfScan();
    void cancelAf();

private:
    int initFocuser();
    void deinitFocuser();
    int captureForAf();       // Capture a frame during AF sweep, returns sobel energy
    int sobelEnergy(const uint8_t* rgb, int w, int h) const;
    int sobelEnergyNw(const uint8_t* rgb, int w, int h) const; // 8px-wide NEON-style (C reference)

private:
    int processBayerToYuv(const uint8_t* bayerData, uint8_t* output, uint32_t outputFormat);
    void doAutoExposure(const uint8_t* rgbBuffer);
    void doAutoWhiteBalance(const uint8_t* rgbBuffer);

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

    int mCurrentExposure;
    int mCurrentGain;
    float mAwbGains[4];
    bool mHasAwbInit;
    uint8_t mGammaLut[256];
    float mLastGamma;

    int mFocusPosition;
    int mAfState;
};

} // namespace mocha

#endif // MOCHA_CAMERA_PIPELINE_H
