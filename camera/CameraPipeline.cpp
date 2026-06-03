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
      mCurrentGain(64),
      mHasAwbInit(false),
      mLastGamma(0.0f) {
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

    mState = PIPELINE_OPENED;
    return 0;
}

int CameraPipeline::close() {
    stopStreaming();

    for (int i = 0; i < mBufferCount; i++) {
        if (mBuffers[i].start && mBuffers[i].allocated) {
            free(mBuffers[i].start);
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

    ALOGI("VIDIOC_S_FMT: got %dx%d fmt=0x%x sizeimage=%d",
          fmt.fmt.pix.width, fmt.fmt.pix.height,
          fmt.fmt.pix.pixelformat, fmt.fmt.pix.sizeimage);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;

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
        mBuffers[i].length = bufSize;
        void* ptr = nullptr;
        if (posix_memalign(&ptr, 4096, bufSize) != 0) ptr = nullptr;
        mBuffers[i].start = ptr;
        if (!mBuffers[i].start) {
            for (int j = 0; j < i; j++) {
                free(mBuffers[j].start);
                mBuffers[j].start = nullptr;
                mBuffers[j].allocated = false;
            }
            mBufferCount = 0;
            return -ENOMEM;
        }
        mBuffers[i].allocated = true;
        memset(mBuffers[i].start, 0, bufSize);
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
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = mBufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(mFd, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("startStreaming REQBUFS failed: %s", strerror(errno));
        return -errno;
    }
    if (req.count < mBufferCount) {
        ALOGE("startStreaming: got %d buffers, need %d", req.count, mBufferCount);
        return -ENOMEM;
    }

    for (int i = 0; i < mBufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.index = i;
        buf.m.userptr = (unsigned long)mBuffers[i].start;
        buf.length = mBuffers[i].length;

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

    /* Release kernel buffer allocations for clean restart */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    ioctl(mFd, VIDIOC_REQBUFS, &req);

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

    if (avgLuma < 0.01f) return;

    float ratio = target / avgLuma;
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
    if (newGain > 150) newGain = 150;
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
    buf.memory = V4L2_MEMORY_USERPTR;

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

    buf.m.userptr = (unsigned long)mBuffers[buf.index].start;
    buf.length = mBuffers[buf.index].length;
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

    float rG = mConfig.enableAWB ? mAwbGains[0] : mConfig.wbGain[0];
    float gG = mConfig.enableAWB ? mAwbGains[1] : mConfig.wbGain[1];
    float bG = mConfig.enableAWB ? mAwbGains[2] : mConfig.wbGain[2];

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
    } else if (outputFormat == HAL_PIXEL_FORMAT_RGBA_8888) {
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

} // namespace mocha
