#include "CameraPipeline.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <system/graphics.h>
#include <cutils/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <errno.h>
#include <linux/videodev2.h>


#ifndef LOG_TAG
#define LOG_TAG "MochaCameraHAL"
#endif

namespace mocha {

CameraPipeline::CameraPipeline()
    : mFd(-1),
      mState(PIPELINE_CLOSED),
      mRgbBuffer(nullptr),
      mRgbBufferSize(0),
      mStreaming(false),
      mBufferCount(0),
      mCurrentBuffer(0),
    mCurrentExposure(2400),
    mCurrentGain(150),
      mHasAwbInit(false),
      mLastGamma(0.0f),
      mFocusPosition(0),
      mAfState(0) {
    for (int i = 0; i < 4; i++) {
        mBuffers[i].start = nullptr;
        mBuffers[i].length = 0;
        mBuffers[i].allocated = false;
    }
    mAwbGains[0] = 1.0f;
    mAwbGains[1] = 1.0f;
    mAwbGains[2] = 1.0f;
    mAwbGains[3] = 1.0f;
}

CameraPipeline::~CameraPipeline() {
    close();
}

int CameraPipeline::open(int cameraId) {
    ALOGI("CameraPipeline::open cameraId=%d", cameraId);

    if (mState != PIPELINE_CLOSED) {
        ALOGE("Pipeline already open");
        return -EBUSY;
    }

    mCameraId = cameraId;
    const char* devPath = (cameraId == 0) ? "/dev/video0" : "/dev/video1";

    mFd = ::open(devPath, O_RDWR | O_NONBLOCK);
    if (mFd < 0) {
        ALOGE("Failed to open V4L2 device: %s (error %d: %s)", devPath, errno, strerror(errno));
        return -ENODEV;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(mFd, VIDIOC_QUERYCAP, &cap) < 0) {
        ALOGE("VIDIOC_QUERYCAP failed: %s", strerror(errno));
        ::close(mFd);
        mFd = -1;
        return -ENODEV;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_STREAMING)) {
        ALOGE("Device does not support video capture or streaming");
        ::close(mFd);
        mFd = -1;
        return -ENODEV;
    }

    if (cameraId == 0) {
        initFocuser();
    }

    mState = PIPELINE_OPENED;
    return 0;
}

int CameraPipeline::close() {
    stopStreaming();

    for (int i = 0; i < mBufferCount; i++) {
        if (mBuffers[i].start && mBuffers[i].allocated) {
            munmap(mBuffers[i].start, mBuffers[i].length);
            mBuffers[i].start = nullptr;
            mBuffers[i].allocated = false;
        }
    }
    mBufferCount = 0;

    if (mRgbBuffer) {
        delete[] mRgbBuffer;
        mRgbBuffer = nullptr;
        mRgbBufferSize = 0;
    }

    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }

    deinitFocuser();

    mState = PIPELINE_CLOSED;
    return 0;
}

int CameraPipeline::setExposure(int exposure) {
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE;
    ctrl.value = exposure;
    int ret = ioctl(mFd, VIDIOC_S_CTRL, &ctrl);
    if (ret == 0) {
        mCurrentExposure = exposure;
    } else {
        ALOGE("setExposure(%d) failed: ret=%d errno=%d (%s)",
              exposure, ret, errno, strerror(errno));
    }
    return ret;
}

int CameraPipeline::setGain(int gain) {
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_GAIN;
    ctrl.value = gain;
    int ret = ioctl(mFd, VIDIOC_S_CTRL, &ctrl);
    if (ret == 0) {
        mCurrentGain = gain;
    } else {
        ALOGE("setGain(%d) failed: ret=%d errno=%d (%s)",
              gain, ret, errno, strerror(errno));
    }
    return ret;
}

int CameraPipeline::getExposure() {
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE;
    if (ioctl(mFd, VIDIOC_G_CTRL, &ctrl) == 0) {
        mCurrentExposure = ctrl.value;
        return ctrl.value;
    }
    return mCurrentExposure;
}

int CameraPipeline::getGain() {
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_GAIN;
    if (ioctl(mFd, VIDIOC_G_CTRL, &ctrl) == 0) {
        mCurrentGain = ctrl.value;
        return ctrl.value;
    }
    return mCurrentGain;
}

int CameraPipeline::configure(const PipelineConfig& config) {
    if (mState == PIPELINE_STREAMING) {
        return -EBUSY;
    }

    mConfig = config;

    if (mFd < 0) {
        return -ENODEV;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config.width;
    fmt.fmt.pix.height = config.height;
    fmt.fmt.pix.pixelformat = config.pixelFormat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    ALOGI("VIDIOC_S_FMT: requesting %dx%d fmt=0x%x", config.width, config.height, config.pixelFormat);

    if (ioctl(mFd, VIDIOC_S_FMT, &fmt) < 0) {
        ALOGE("VIDIOC_S_FMT failed: %s", strerror(errno));
        return -errno;
    }

    ALOGI("VIDIOC_S_FMT: got %dx%d fmt=0x%x sizeimage=%d bytesperline=%d",
          fmt.fmt.pix.width, fmt.fmt.pix.height,
          fmt.fmt.pix.pixelformat, fmt.fmt.pix.sizeimage,
          fmt.fmt.pix.bytesperline);


    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(mFd, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return -errno;
    }

    if (req.count < 2) {
        return -ENOMEM;
    }

    mBufferCount = req.count;

    uint32_t bufSize = fmt.fmt.pix.sizeimage;
    for (int i = 0; i < mBufferCount; i++) {
        struct v4l2_buffer qbuf;
        memset(&qbuf, 0, sizeof(qbuf));
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index = i;
        if (ioctl(mFd, VIDIOC_QUERYBUF, &qbuf) < 0) {
            for (int j = 0; j < i; j++) {
                if (mBuffers[j].start) munmap(mBuffers[j].start, mBuffers[j].length);
                mBuffers[j].start = nullptr;
                mBuffers[j].allocated = false;
            }
            mBufferCount = 0;
            return -errno;
        }
        mBuffers[i].length = qbuf.length;
        mBuffers[i].start = mmap(NULL, qbuf.length, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, qbuf.m.offset);
        if (mBuffers[i].start == MAP_FAILED) {
            mBuffers[i].start = nullptr;
            for (int j = 0; j < i; j++) {
                if (mBuffers[j].start) munmap(mBuffers[j].start, mBuffers[j].length);
                mBuffers[j].start = nullptr;
                mBuffers[j].allocated = false;
            }
            mBufferCount = 0;
            return -ENOMEM;
        }
        mBuffers[i].allocated = true;
    }

    if (config.enableISP) {
        DemosaicParams demosaicParams;
        demosaicParams.width = config.width;
        demosaicParams.height = config.height;
        demosaicParams.bayerPattern = config.bayerPattern;
        demosaicParams.offset_x = config.offset_x;
        demosaicParams.offset_y = config.offset_y;
        demosaicParams.blackLevel = config.blackLevel;

        mDemosaic = std::unique_ptr<DemosaicNEON>(new DemosaicNEON());
        int ret = mDemosaic->initialize(demosaicParams);
        if (ret != 0) return ret;

        mColorConv = std::unique_ptr<ColorConvNEON>(new ColorConvNEON());
        ret = mColorConv->initialize(config.width, config.height);
        if (ret != 0) return ret;

        if (mRgbBuffer) {
            delete[] mRgbBuffer;
            mRgbBuffer = nullptr;
        }
        mRgbBufferSize = config.width * config.height * 3;
        mRgbBuffer = new uint8_t[mRgbBufferSize];
    }

    mHasAwbInit = false;

    /* Rebuild gamma LUT if gamma changed */
    if (mLastGamma != config.gamma) {
        for (int i = 0; i < 256; i++)
            mGammaLut[i] = (uint8_t)(powf(i / 255.0f, config.gamma) * 255.0f + 0.5f);
        mLastGamma = config.gamma;
    }

    return 0;
}

int CameraPipeline::startStreaming() {
    if (mState != PIPELINE_OPENED) return -EINVAL;

    /* Re-request buffers (idempotent: reuses existing if count matches) */
    for (int i = 0; i < mBufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(mFd, VIDIOC_QBUF, &buf) < 0) return -errno;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(mFd, VIDIOC_STREAMON, &type) < 0) return -errno;

    /* Set initial exposure/gain AFTER streaming starts (sensor must be powered) */
    setExposure(mCurrentExposure);
    setGain(mCurrentGain);
    ALOGI("Initial exposure=%d gain=%d", mCurrentExposure, mCurrentGain);

    mStreaming = true;
    mState = PIPELINE_STREAMING;
    return 0;
}

int CameraPipeline::stopStreaming() {
    if (!mStreaming) return 0;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(mFd, VIDIOC_STREAMOFF, &type);

    for (int i = 0; i < mBufferCount; i++) {
        if (mBuffers[i].start) {
            munmap(mBuffers[i].start, mBuffers[i].length);
            mBuffers[i].start = nullptr;
            mBuffers[i].allocated = false;
        }
    }
    mBufferCount = 0;

    mStreaming = false;
    mState = PIPELINE_OPENED;
    return 0;
}

void CameraPipeline::doAutoExposure(const uint8_t* rgbBuffer) {
    if (!rgbBuffer || !mConfig.enableAE) return;

    int w = mConfig.width;
    int h = mConfig.height;
    int step = 8;
    uint64_t sum = 0;
    int count = 0;
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            int off = (y * w + x) * 3;
            uint8_t r = rgbBuffer[off], g = rgbBuffer[off+1], b = rgbBuffer[off+2];
            sum += (r * 77 + g * 150 + b * 29) >> 8;
            count++;
        }
    }
    if (count == 0) return;
    float avgLuma = (float)sum / count / 255.0f;
    float target = mConfig.targetLuma;

    float ratio = target / (avgLuma > 0.001f ? avgLuma : 0.001f);
    ratio = (ratio < 0.5f) ? 0.5f : (ratio > 4.0f) ? 4.0f : ratio;

    int newExp = (int)(mCurrentExposure * ratio);
    int newGain = mCurrentGain;

    /* Prefer longer exposure over higher gain to reduce noise */
    if (newExp > 2490) {
        newGain = (int)(mCurrentGain * (newExp / 2490.0f));
        newExp = 2490;
    } else if (newGain > 150 && newExp < 2490) {
        /* If gain is high, increase exposure instead */
        newExp = (int)(newExp * (newGain / 150.0f));
        newGain = 150;
        if (newExp > 2490) newExp = 2490;
    }
    if (newExp < 10) {
        newExp = 10;
    }
    if (newGain > 180) newGain = 180;
    if (newGain < 1) newGain = 1;

    if (newExp != mCurrentExposure || newGain != mCurrentGain) {
        if (newGain != mCurrentGain) setGain(newGain);
        if (newExp != mCurrentExposure) setExposure(newExp);
        ALOGI("AE: luma=%.2f target=%.2f exp=%d(%d) gain=%d(%d)",
              avgLuma, target, newExp, mCurrentExposure, newGain, mCurrentGain);
    }
}

void CameraPipeline::doAutoWhiteBalance(const uint8_t* rgbBuffer) {
    if (!rgbBuffer || !mConfig.enableAWB) return;

    int w = mConfig.width;
    int h = mConfig.height;
    int step = 16;
    uint64_t sumR = 0, sumG = 0, sumB = 0;
    int pixelCount = 0;

    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            int off = (y * w + x) * 3;
            sumR += rgbBuffer[off];
            sumG += rgbBuffer[off+1];
            sumB += rgbBuffer[off+2];
            pixelCount++;
        }
    }

    if (pixelCount < 100) return;
    float avgR = (float)sumR / pixelCount;
    float avgG = (float)sumG / pixelCount;
    float avgB = (float)sumB / pixelCount;

    if (avgR < 5.0f || avgG < 5.0f || avgB < 5.0f) return;

    float rGain = avgG / avgR;
    float bGain = avgG / avgB;

    rGain = (rGain < 0.5f) ? 0.5f : (rGain > 3.0f) ? 3.0f : rGain;
    bGain = (bGain < 0.5f) ? 0.5f : (bGain > 3.0f) ? 3.0f : bGain;

    float alpha = 0.3f;
    if (!mHasAwbInit) {
        mAwbGains[0] = rGain;
        mAwbGains[2] = bGain;
        mHasAwbInit = true;
    } else {
        mAwbGains[0] = mAwbGains[0] * (1.0f - alpha) + rGain * alpha;
        mAwbGains[2] = mAwbGains[2] * (1.0f - alpha) + bGain * alpha;
    }
    mAwbGains[1] = 1.0f;
    mAwbGains[3] = 1.0f;

    ALOGI("AWB: R/G=%.2f B/G=%.2f gains R=%.2f B=%.2f", avgR/avgG, avgB/avgG, mAwbGains[0], mAwbGains[2]);
}

static void applyGamma(uint8_t* rgb, int width, int height, float gamma) {
    uint8_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = (uint8_t)(powf(i / 255.0f, gamma) * 255.0f + 0.5f);
    int total = width * height;
    for (int i = 0; i < total * 3; i++)
        rgb[i] = lut[rgb[i]];
}

static void flipVertical(uint8_t* buf, int width, int height, int bpp) {
    int rowSize = width * bpp;
    uint8_t* tmp = new uint8_t[rowSize];
    for (int y = 0; y < height / 2; y++) {
        int topOff = y * rowSize;
        int botOff = (height - 1 - y) * rowSize;
        memcpy(tmp, buf + topOff, rowSize);
        memcpy(buf + topOff, buf + botOff, rowSize);
        memcpy(buf + botOff, tmp, rowSize);
    }
    delete[] tmp;
}

int CameraPipeline::captureFrame(uint8_t* outputBuffer, uint32_t outputFormat) {
    if (mState != PIPELINE_STREAMING) return -EINVAL;
    if (!outputBuffer) return -EINVAL;

    struct pollfd pfd;
    pfd.fd = mFd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int poll_timeout_ms = 300;
    int max_retries = 6;
    int retry_count = 0;
    int pollRet = 0;

    do {
        pfd.revents = 0;
        pollRet = poll(&pfd, 1, poll_timeout_ms);
        if (pollRet < 0) return -errno;
        if (pollRet == 0) {
            retry_count++;
            if (retry_count >= max_retries) return -EAGAIN;
            continue;
        }
        if (!(pfd.revents & POLLIN)) return -EAGAIN;
        break;
    } while (retry_count < max_retries);

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    int ret = ioctl(mFd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        if (errno == EAGAIN) return -EAGAIN;
        return -errno;
    }

    if (buf.index >= mBufferCount) return -EINVAL;

    uint8_t* frameBuffer = (uint8_t*)mBuffers[buf.index].start;
    uint32_t frameSize = buf.bytesused;

    if (mConfig.enableISP && mDemosaic && mColorConv) {
        ret = processBayerToYuv(frameBuffer, outputBuffer, outputFormat);
    } else {
        memcpy(outputBuffer, frameBuffer, frameSize);
        ret = 0;
    }

    if (ioctl(mFd, VIDIOC_QBUF, &buf) < 0) return -errno;

    return ret;
}

int CameraPipeline::processBayerToYuv(const uint8_t* bayerData, uint8_t* output, uint32_t outputFormat) {
    if (!bayerData || !output || !mDemosaic || !mColorConv) return -EINVAL;

    mDemosaic->process(bayerData, mRgbBuffer);

    /* AE update before applying gains */
    if (mConfig.enableAE)
        doAutoExposure(mRgbBuffer);

    /* AWB update before applying gains */
    if (mConfig.enableAWB)
        doAutoWhiteBalance(mRgbBuffer);

    float rG = (mConfig.enableAWB ? mAwbGains[0] : mConfig.wbGain[0]) * mConfig.digitalGain;
    float gG = (mConfig.enableAWB ? mAwbGains[1] : mConfig.wbGain[1]) * mConfig.digitalGain;
    float bG = (mConfig.enableAWB ? mAwbGains[2] : mConfig.wbGain[2]) * mConfig.digitalGain;

    if (outputFormat == HAL_PIXEL_FORMAT_YCBCR_420_888) {
        /* For YUV: apply WB gains in-place first, then gamma, then convert */
        {
            int total = mConfig.width * mConfig.height;
            if (rG != 1.0f || gG != 1.0f || bG != 1.0f) {
                for (int i = 0; i < total; i++) {
                    int off = i * 3;
                    int r = (int)(mRgbBuffer[off]   * rG);
                    int g = (int)(mRgbBuffer[off+1] * gG);
                    int b = (int)(mRgbBuffer[off+2] * bG);
                    mRgbBuffer[off]   = r > 255 ? 255 : (uint8_t)r;
                    mRgbBuffer[off+1] = g > 255 ? 255 : (uint8_t)g;
                    mRgbBuffer[off+2] = b > 255 ? 255 : (uint8_t)b;
                }
            }
        }
        applyGamma(mRgbBuffer, mConfig.width, mConfig.height, mConfig.gamma);
        uint8_t* yPlane = output;
        uint8_t* uvPlane = output + mConfig.width * mConfig.height;
        mColorConv->rgbToNv12(mRgbBuffer, yPlane, uvPlane);
    } else if (outputFormat == HAL_PIXEL_FORMAT_RGBA_8888 || outputFormat == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        /* Merged WB + gamma + RGB→RGBA + flip in one pass */
        mColorConv->rgbToRgbaWbGamma(mRgbBuffer, output,
                                     mConfig.width, mConfig.height,
                                     rG, gG, bG, mGammaLut, mConfig.flipV);
    } else {
        /* For other formats: apply WB and gamma, then raw copy */
        {
            int total = mConfig.width * mConfig.height;
            if (rG != 1.0f || gG != 1.0f || bG != 1.0f) {
                for (int i = 0; i < total; i++) {
                    int off = i * 3;
                    int r = (int)(mRgbBuffer[off]   * rG);
                    int g = (int)(mRgbBuffer[off+1] * gG);
                    int b = (int)(mRgbBuffer[off+2] * bG);
                    mRgbBuffer[off]   = r > 255 ? 255 : (uint8_t)r;
                    mRgbBuffer[off+1] = g > 255 ? 255 : (uint8_t)g;
                    mRgbBuffer[off+2] = b > 255 ? 255 : (uint8_t)b;
                }
            }
        }
        applyGamma(mRgbBuffer, mConfig.width, mConfig.height, mConfig.gamma);
        memcpy(output, mRgbBuffer, mRgbBufferSize);
    }

    return 0;
}

int CameraPipeline::initFocuser() {
    mFocusPosition = -1;
    ALOGI("Focuser: kernel controls AD5823 power/I2C; position 400 set at stream start");
    return 0;
}

void CameraPipeline::deinitFocuser() {
}

int CameraPipeline::setFocus(int position) {
    if (position < 0) position = 0;
    if (position > 1023) position = 1023;

    struct v4l2_control ctrl;
    ctrl.id = V4L2_CID_FOCUS_ABSOLUTE;
    ctrl.value = position;

    int ret = ioctl(mFd, VIDIOC_S_CTRL, &ctrl);
    if (ret == 0) {
        mFocusPosition = position;
        ALOGI("Focuser: set position %d via V4L2", position);
    } else {
        ALOGW("Focuser: ioctl V4L2_CID_FOCUS_ABSOLUTE=%d failed: %s",
              position, strerror(errno));
    }
    return ret;
}

/*
 * Horizontal Sobel energy on the green channel of an RGB buffer.
 * Uses 3×3 Sobel kernel for horizontal edges:
 *   Gx = | -1 0 +1 |
 *        | -2 0 +2 |
 *        | -1 0 +1 |
 * Only green channel (byte-offset 1) is used — fast approximation.
 */
int CameraPipeline::sobelEnergy(const uint8_t* rgb, int w, int h) const {
    if (!rgb || w < 3 || h < 3) return 0;
    int total = 0;
    int stride = w * 3;
    for (int y = 1; y < h - 1; y++) {
        const uint8_t* row = rgb + y * stride;
        for (int x = 1; x < w - 1; x++) {
            int g = abs((int)row[(x+1)*3+1] - (int)row[(x-1)*3+1]); // horizontal gradient
            total += g;
        }
    }
    return total;
}

/*
 * 8-pixel-wide horizontal sobel using green channel.
 * Processes 8 adjacent output pixels at a time, summing partial gradients.
 * Same algorithm as sobelEnergy() but loop-unrolled for 8x regions.
 */
int CameraPipeline::sobelEnergyNw(const uint8_t* rgb, int w, int h) const {
    if (!rgb || w < 3 || h < 3) return 0;
    int total = 0;
    int stride = w * 3;
    int x = 1;
    /* Process 8-wide chunks */
    for (int y = 1; y < h - 1; y++) {
        const uint8_t* row = rgb + y * stride;
        x = 1;
        while (x + 8 < w - 1) {
            int g0 = abs((int)row[(x+1)*3+1] - (int)row[(x-1)*3+1]);
            int g1 = abs((int)row[(x+2)*3+1] - (int)row[(x+0)*3+1]);
            int g2 = abs((int)row[(x+3)*3+1] - (int)row[(x+1)*3+1]);
            int g3 = abs((int)row[(x+4)*3+1] - (int)row[(x+2)*3+1]);
            int g4 = abs((int)row[(x+5)*3+1] - (int)row[(x+3)*3+1]);
            int g5 = abs((int)row[(x+6)*3+1] - (int)row[(x+4)*3+1]);
            int g6 = abs((int)row[(x+7)*3+1] - (int)row[(x+5)*3+1]);
            int g7 = abs((int)row[(x+8)*3+1] - (int)row[(x+6)*3+1]);
            total += g0 + g1 + g2 + g3 + g4 + g5 + g6 + g7;
            x += 8;
        }
        /* Remainder */
        for (; x < w - 1; x++) {
            total += abs((int)row[(x+1)*3+1] - (int)row[(x-1)*3+1]);
        }
    }
    return total;
}

/*
 * Capture one V4L2 frame and return Sobel energy from the green channel.
 * Used during AF sweep — captures a frame, demosaics to RGB, measures contrast.
 */
int CameraPipeline::captureForAf() {
    if (mState != PIPELINE_STREAMING) return -EINVAL;

    struct pollfd pfd;
    pfd.fd = mFd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pollRet = poll(&pfd, 1, 500);
    if (pollRet <= 0) return -EAGAIN;
    if (!(pfd.revents & POLLIN)) return -EAGAIN;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_USERPTR;

    int ret = ioctl(mFd, VIDIOC_DQBUF, &buf);
    if (ret < 0) return -errno;
    if (buf.index >= mBufferCount) return -EINVAL;

    uint8_t* frameBuffer = (uint8_t*)mBuffers[buf.index].start;
    int energy = 0;

    if (mConfig.enableISP && mDemosaic && mColorConv) {
        /* Demosaic to mRgbBuffer */
        mDemosaic->process(frameBuffer, mRgbBuffer);
        /* Apply AE — doAutoExposure reads green from mRgbBuffer */
        if (mConfig.enableAE)
            doAutoExposure(mRgbBuffer);
        /* Apply AWB gains in-place */
        if (mConfig.enableAWB) {
            doAutoWhiteBalance(mRgbBuffer);
        }
        float rG = mConfig.enableAWB ? mAwbGains[0] : mConfig.wbGain[0];
        float gG = mConfig.enableAWB ? mAwbGains[1] : mConfig.wbGain[1];
        float bG = mConfig.enableAWB ? mAwbGains[2] : mConfig.wbGain[2];
        int total = mConfig.width * mConfig.height;
        for (int i = 0; i < total; i++) {
            int off = i * 3;
            int r = (int)(mRgbBuffer[off]   * rG);
            int g = (int)(mRgbBuffer[off+1] * gG);
            int b = (int)(mRgbBuffer[off+2] * bG);
            mRgbBuffer[off]   = r > 255 ? 255 : (uint8_t)r;
            mRgbBuffer[off+1] = g > 255 ? 255 : (uint8_t)g;
            mRgbBuffer[off+2] = b > 255 ? 255 : (uint8_t)b;
        }
        applyGamma(mRgbBuffer, mConfig.width, mConfig.height, mConfig.gamma);

        /* Measure Sobel energy on the green channel */
        energy = sobelEnergyNw(mRgbBuffer, mConfig.width, mConfig.height);
    }

    /* Return buffer to queue */
    buf.m.userptr = (unsigned long)mBuffers[buf.index].start;
    buf.length = mBuffers[buf.index].length;
    if (ioctl(mFd, VIDIOC_QBUF, &buf) < 0) return -errno;

    return energy;
}

void CameraPipeline::startAfScan() {
    if (mState != PIPELINE_STREAMING) {
        ALOGW("AF: cannot scan, not streaming");
        mAfState = 0;
        return;
    }

    mAfState = 1; // MOVING
    ALOGI("AF: starting contrast-detection scan");

    int bestEnergy = 0;
    int bestPos = 400;
    const int coarseStep = 50;
    const int fineStep = 10;
    const int settleMs = 60;

    /* ---- Coarse sweep: 140 → 640 ---- */
    for (int pos = 140; pos <= 640; pos += coarseStep) {
        setFocus(pos);
        usleep(settleMs * 1000);
        int energy = captureForAf();
        if (energy < 0) {
            ALOGW("AF: capture error at pos %d: %d", pos, energy);
            continue;
        }
        ALOGI("AF: coarse pos=%d energy=%d", pos, energy);
        if (energy > bestEnergy) {
            bestEnergy = energy;
            bestPos = pos;
        }
    }

    /* ---- Fine sweep around best ---- */
    int fineStart = bestPos - coarseStep;
    if (fineStart < 140) fineStart = 140;
    int fineEnd = bestPos + coarseStep;
    if (fineEnd > 640) fineEnd = 640;

    for (int pos = fineStart; pos <= fineEnd; pos += fineStep) {
        if (pos == bestPos) continue; // already measured
        setFocus(pos);
        usleep(settleMs * 1000);
        int energy = captureForAf();
        if (energy < 0) continue;
        ALOGI("AF: fine pos=%d energy=%d", pos, energy);
        if (energy > bestEnergy) {
            bestEnergy = energy;
            bestPos = pos;
        }
    }

    /* ---- Lock to best position ---- */
    setFocus(bestPos);
    usleep(settleMs * 1000);
    mAfState = 4; // FOCUSED_LOCKED
    ALOGI("AF: scan complete, lock at position=%d energy=%d", bestPos, bestEnergy);
}

void CameraPipeline::cancelAf() {
    setFocus(0);
    mAfState = 0; // INACTIVE
    ALOGI("AF: cancelled, focus at infinity");
}

} // namespace mocha
