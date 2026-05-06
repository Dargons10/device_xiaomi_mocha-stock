/*
 * CameraPipeline Implementation
 */

#include "CameraPipeline.h"
#include <cstring>
#include <system/graphics.h>
#include <cutils/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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
      mCurrentBuffer(0) {
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

    ALOGI("Opened V4L2 device: %s (fd=%d)", devPath, mFd);
    
    // Verify capabilities
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(mFd, VIDIOC_QUERYCAP, &cap) < 0) {
        ALOGE("VIDIOC_QUERYCAP failed: %s", strerror(errno));
        ::close(mFd);
        mFd = -1;
        return -ENODEV;
    }
    
    ALOGI("V4L2 capabilities: driver=%s, card=%s, capabilities=0x%08X",
          cap.driver, cap.card, cap.capabilities);
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        ALOGE("Device does not support video capture");
        ::close(mFd);
        mFd = -1;
        return -ENODEV;
    }

    mState = PIPELINE_OPENED;
    ALOGI("CameraPipeline opened successfully");
    return 0;
}

int CameraPipeline::close() {
    ALOGI("CameraPipeline::close");

    stopStreaming();

    // Unmap and free buffers
    for (int i = 0; i < mBufferCount; i++) {
        if (mBuffers[i].start) {
            munmap(mBuffers[i].start, mBuffers[i].length);
            mBuffers[i].start = nullptr;
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

int CameraPipeline::configure(const PipelineConfig& config) {
    ALOGI("CameraPipeline::configure %dx%d format=%d",
          config.width, config.height, config.pixelFormat);

    if (mState == PIPELINE_STREAMING) {
        ALOGE("Cannot configure while streaming");
        return -EBUSY;
    }

    mConfig = config;

    if (mFd < 0) {
        ALOGE("V4L2 device not initialized");
        return -ENODEV;
    }

    // Set format
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = config.width;
    fmt.fmt.pix.height = config.height;
    fmt.fmt.pix.pixelformat = config.pixelFormat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    
    if (ioctl(mFd, VIDIOC_S_FMT, &fmt) < 0) {
        ALOGE("VIDIOC_S_FMT failed: %s", strerror(errno));
        return -errno;
    }
    
    ALOGI("Set format: %dx%d, pixelformat=%c%c%c%c, bytesperline=%d, sizeimage=%d",
          fmt.fmt.pix.width, fmt.fmt.pix.height,
          (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 24) & 0xFF,
          fmt.fmt.pix.bytesperline, fmt.fmt.pix.sizeimage);

    // Request buffers
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(mFd, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return -errno;
    }
    
    ALOGI("Requested %d buffers, got %d", 4, req.count);
    
    if (req.count < 2) {
        ALOGE("Not enough buffers: %d", req.count);
        return -ENOMEM;
    }
    
    mBufferCount = req.count;
    
    // Query and map buffers
    for (int i = 0; i < mBufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(mFd, VIDIOC_QUERYBUF, &buf) < 0) {
            ALOGE("VIDIOC_QUERYBUF failed for buffer %d: %s", i, strerror(errno));
            return -errno;
        }
        
        mBuffers[i].length = buf.length;
        mBuffers[i].start = mmap(nullptr, buf.length,
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, mFd, buf.m.offset);
        
        if (mBuffers[i].start == MAP_FAILED) {
            ALOGE("mmap failed for buffer %d: %s", i, strerror(errno));
            mBuffers[i].start = nullptr;
            return -errno;
        }
        
        ALOGI("Mapped buffer %d: %p, %zu bytes", i, mBuffers[i].start, mBuffers[i].length);
    }

    // Initialize ISP if enabled
    if (config.enableISP) {
        DemosaicParams demosaicParams;
        demosaicParams.width = config.width;
        demosaicParams.height = config.height;
        demosaicParams.bayerPattern = config.bayerPattern;
        demosaicParams.blackLevel = 0;

        mDemosaic = std::make_unique<DemosaicNEON>();
        int ret = mDemosaic->initialize(demosaicParams);
        if (ret != 0) {
            ALOGE("Failed to initialize demosaic");
            return ret;
        }

        mColorConv = std::make_unique<ColorConvNEON>();
        ret = mColorConv->initialize(config.width, config.height);
        if (ret != 0) {
            ALOGE("Failed to initialize color conv");
            return ret;
        }

        mRgbBufferSize = config.width * config.height * 3;
        mRgbBuffer = new uint8_t[mRgbBufferSize];
    }

    ALOGI("Pipeline configured successfully");
    return 0;
}

int CameraPipeline::startStreaming() {
    ALOGI("CameraPipeline::startStreaming");

    if (mState != PIPELINE_OPENED) {
        ALOGE("Pipeline not opened");
        return -EINVAL;
    }

    // Queue all buffers
    for (int i = 0; i < mBufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(mFd, VIDIOC_QBUF, &buf) < 0) {
            ALOGE("VIDIOC_QBUF failed for buffer %d: %s", i, strerror(errno));
            return -errno;
        }
    }
    
    ALOGI("Queued %d buffers", mBufferCount);

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(mFd, VIDIOC_STREAMON, &type) < 0) {
        ALOGE("VIDIOC_STREAMON failed: %s", strerror(errno));
        return -errno;
    }

    mStreaming = true;
    mState = PIPELINE_STREAMING;
    ALOGI("Pipeline streaming started");
    return 0;
}

int CameraPipeline::stopStreaming() {
    ALOGI("CameraPipeline::stopStreaming");

    if (!mStreaming) {
        return 0;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(mFd, VIDIOC_STREAMOFF, &type) < 0) {
        ALOGE("VIDIOC_STREAMOFF failed: %s", strerror(errno));
    }

    mStreaming = false;
    mState = PIPELINE_OPENED;
    return 0;
}

int CameraPipeline::captureFrame(uint8_t* outputBuffer, uint32_t outputFormat) {
    if (mState != PIPELINE_STREAMING) {
        ALOGE("Pipeline not streaming");
        return -EINVAL;
    }

    if (!outputBuffer) {
        return -EINVAL;
    }

    // Dequeue a buffer
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    int ret = ioctl(mFd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        if (errno == EAGAIN) {
            return -EAGAIN;  // No buffer available
        }
        ALOGE("VIDIOC_DQBUF failed: %s", strerror(errno));
        return -errno;
    }
    
    if (buf.index >= mBufferCount) {
        ALOGE("Invalid buffer index: %d", buf.index);
        return -EINVAL;
    }

    uint8_t* frameBuffer = (uint8_t*)mBuffers[buf.index].start;
    uint32_t frameSize = buf.bytesused;
    
    ALOGV("Dequeued buffer %d, bytesused=%d", buf.index, frameSize);

    if (mConfig.enableISP && mDemosaic && mColorConv) {
        ret = processBayerToYuv(frameBuffer, outputBuffer, outputFormat);
    } else {
        // Copy raw data
        memcpy(outputBuffer, frameBuffer, frameSize);
        ret = 0;
    }

    // Re-queue the buffer
    if (ioctl(mFd, VIDIOC_QBUF, &buf) < 0) {
        ALOGE("VIDIOC_QBUF failed: %s", strerror(errno));
        return -errno;
    }

    return ret;
}

int CameraPipeline::processBayerToYuv(const uint8_t* bayerData, uint8_t* output, uint32_t outputFormat) {
    if (!bayerData || !output || !mDemosaic || !mColorConv) {
        return -EINVAL;
    }

    mDemosaic->process(bayerData, mRgbBuffer);

    if (outputFormat == HAL_PIXEL_FORMAT_YCBCR_420_888) {
        uint8_t* yPlane = output;
        uint8_t* uvPlane = output + mConfig.width * mConfig.height;
        mColorConv->rgbToNv12(mRgbBuffer, yPlane, uvPlane);
    } else if (outputFormat == HAL_PIXEL_FORMAT_RGBA_8888) {
        mColorConv->rgbToRgba(mRgbBuffer, output, mConfig.width, mConfig.height);
    } else {
        memcpy(output, mRgbBuffer, mRgbBufferSize);
    }

    return 0;
}

} // namespace mocha
