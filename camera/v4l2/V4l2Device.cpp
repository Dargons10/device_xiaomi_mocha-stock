/*
 * V4L2 Device Wrapper Implementation
 */

#define LOG_TAG "MochaCameraHAL"
#define LOG_NDEBUG 0

#include "v4l2/V4l2Device.h"

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <errno.h>

namespace mocha {

V4l2Device::V4l2Device()
    : mFd(-1),
      mStreaming(false),
      mMemory(V4L2_MEMORY_MMAP),
      mBufferCount(0),
      mBufType(V4L2_BUF_TYPE_VIDEO_CAPTURE) {
}

V4l2Device::~V4l2Device() {
    close();
}

int V4l2Device::open(const char *devicePath) {
    if (mFd >= 0) {
        ALOGW("Device already opened");
        return 0;
    }

    mFd = ::open(devicePath, O_RDWR | O_NONBLOCK);
    if (mFd < 0) {
        ALOGE("Failed to open %s: %s", devicePath, strerror(errno));
        return -errno;
    }

    ALOGI("Opened V4L2 device: %s (fd=%d)", devicePath, mFd);
    return 0;
}

int V4l2Device::close() {
    if (mFd >= 0) {
        if (mStreaming) {
            streamOff(mBufType);
        }

        // Unmap all buffers
        for (size_t i = 0; i < mBuffers.size(); i++) {
            if (mBuffers[i].start) {
                munmap(mBuffers[i].start, mBuffers[i].length);
                mBuffers.editItemAt(i).start = nullptr;
            }
        }
        mBuffers.clear();

        ::close(mFd);
        mFd = -1;
        ALOGI("Closed V4L2 device");
    }
    return 0;
}

int V4l2Device::queryCap(struct v4l2_capability *cap) {
    if (mFd < 0) return -EINVAL;

    memset(cap, 0, sizeof(struct v4l2_capability));
    if (ioctl(mFd, VIDIOC_QUERYCAP, cap) < 0) {
        ALOGE("VIDIOC_QUERYCAP failed: %s", strerror(errno));
        return -errno;
    }

    ALOGI("V4L2 capabilities: driver=%s, card=%s, bus_info=%s, version=%u",
          cap->driver, cap->card, cap->bus_info, cap->version);
    ALOGI("Capabilities: 0x%08X (V4L2_CAP_VIDEO_CAPTURE=%d)",
          cap->capabilities, (cap->capabilities & V4L2_CAP_VIDEO_CAPTURE) != 0);

    return 0;
}

int V4l2Device::enumInput(int index, struct v4l2_input *input) {
    if (mFd < 0) return -EINVAL;

    memset(input, 0, sizeof(struct v4l2_input));
    input->index = index;
    if (ioctl(mFd, VIDIOC_ENUMINPUT, input) < 0) {
        return -errno;
    }
    return 0;
}

int V4l2Device::setInput(int index) {
    if (mFd < 0) return -EINVAL;

    if (ioctl(mFd, VIDIOC_S_INPUT, &index) < 0) {
        ALOGE("VIDIOC_S_INPUT failed: %s", strerror(errno));
        return -errno;
    }
    return 0;
}

int V4l2Device::getInput() {
    if (mFd < 0) return -EINVAL;

    int index;
    if (ioctl(mFd, VIDIOC_G_INPUT, &index) < 0) {
        return -errno;
    }
    return index;
}

int V4l2Device::enumFmt(uint32_t type, int index, struct v4l2_fmtdesc *fmt) {
    if (mFd < 0) return -EINVAL;

    memset(fmt, 0, sizeof(struct v4l2_fmtdesc));
    fmt->type = type;
    fmt->index = index;
    if (ioctl(mFd, VIDIOC_ENUM_FMT, fmt) < 0) {
        return -errno;
    }
    return 0;
}

int V4l2Device::getFmt(struct v4l2_format *fmt) {
    if (mFd < 0) return -EINVAL;

    memset(fmt, 0, sizeof(struct v4l2_format));
    fmt->type = mBufType;
    if (ioctl(mFd, VIDIOC_G_FMT, fmt) < 0) {
        ALOGE("VIDIOC_G_FMT failed: %s", strerror(errno));
        return -errno;
    }

    ALOGI("Current format: %dx%d, pixelformat=%c%c%c%c",
          fmt->fmt.pix.width, fmt->fmt.pix.height,
          (fmt->fmt.pix.pixelformat >> 0) & 0xFF,
          (fmt->fmt.pix.pixelformat >> 8) & 0xFF,
          (fmt->fmt.pix.pixelformat >> 16) & 0xFF,
          (fmt->fmt.pix.pixelformat >> 24) & 0xFF);

    return 0;
}

int V4l2Device::setFmt(int width, int height, int pixelFormat) {
    if (mFd < 0) return -EINVAL;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = mBufType;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixelFormat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = 0;
    fmt.fmt.pix.sizeimage = 0;
    fmt.fmt.pix.colorspace = V4L2_COLORSPACE_REC709;

    if (ioctl(mFd, VIDIOC_S_FMT, &fmt) < 0) {
        ALOGE("VIDIOC_S_FMT failed: %s", strerror(errno));
        return -errno;
    }

    ALOGI("Set format: %dx%d, pixelformat=%c%c%c%c",
          fmt.fmt.pix.width, fmt.fmt.pix.height,
          (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
          (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    return 0;
}

int V4l2Device::reqBuffers(int count, enum v4l2_memory memory) {
    if (mFd < 0) return -EINVAL;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = count;
    req.type = mBufType;
    req.memory = memory;

    if (ioctl(mFd, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return -errno;
    }

    mMemory = memory;
    mBufferCount = req.count;
    ALOGI("Requested %d buffers (type=%d, memory=%d)", req.count, req.type, req.memory);

    // Clear existing buffers
    for (size_t i = 0; i < mBuffers.size(); i++) {
        if (mBuffers[i].start) {
            munmap(mBuffers[i].start, mBuffers[i].length);
        }
    }
    mBuffers.clear();

    // Initialize buffer array
    for (int i = 0; i < mBufferCount; i++) {
        V4l2Buffer buf = {nullptr, 0, -1};
        mBuffers.push_back(buf);
    }

    return mBufferCount;
}

int V4l2Device::queryBuffer(int index, struct v4l2_buffer *buf) {
    if (mFd < 0 || index < 0 || index >= mBufferCount) return -EINVAL;

    memset(buf, 0, sizeof(struct v4l2_buffer));
    buf->type = mBufType;
    buf->memory = mMemory;
    buf->index = index;

    if (ioctl(mFd, VIDIOC_QUERYBUF, buf) < 0) {
        ALOGE("VIDIOC_QUERYBUF failed: %s", strerror(errno));
        return -errno;
    }

    return 0;
}

int V4l2Device::exportBuffer(int index, int *dmaFd) {
    if (mFd < 0 || index < 0 || index >= mBufferCount) return -EINVAL;

    struct v4l2_exportbuffer exp;
    memset(&exp, 0, sizeof(exp));
    exp.type = mBufType;
    exp.index = index;
    exp.flags = O_RDONLY;

    if (ioctl(mFd, VIDIOC_EXPBUF, &exp) < 0) {
        ALOGE("VIDIOC_EXPBUF failed: %s", strerror(errno));
        return -errno;
    }

    *dmaFd = exp.fd;
    return 0;
}

int V4l2Device::mapBuffer(int index, void **ptr, size_t *length) {
    if (mFd < 0 || index < 0 || index >= mBufferCount) return -EINVAL;

    if (mMemory != V4L2_MEMORY_MMAP) {
        ALOGE("Not using MMAP memory");
        return -EINVAL;
    }

    struct v4l2_buffer buf;
    queryBuffer(index, &buf);

    void *addr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                      MAP_SHARED, mFd, buf.m.offset);
    if (addr == MAP_FAILED) {
        ALOGE("mmap failed: %s", strerror(errno));
        return -errno;
    }

    mBuffers.editItemAt(index).start = addr;
    mBuffers.editItemAt(index).length = buf.length;

    *ptr = addr;
    *length = buf.length;

    ALOGI("Mapped buffer %d: %p, %zu bytes", index, addr, buf.length);
    return 0;
}

int V4l2Device::unmapBuffer(int index) {
    if (index < 0 || index >= (int)mBuffers.size()) return -EINVAL;

    if (mBuffers[index].start) {
        munmap(mBuffers[index].start, mBuffers[index].length);
        mBuffers.editItemAt(index).start = nullptr;
        mBuffers.editItemAt(index).length = 0;
    }

    return 0;
}

int V4l2Device::streamOn(enum v4l2_buf_type type) {
    if (mFd < 0 || mStreaming) return -EINVAL;

    if (ioctl(mFd, VIDIOC_STREAMON, &type) < 0) {
        ALOGE("VIDIOC_STREAMON failed: %s (errno=%d)", strerror(errno), errno);
        return -errno;
    }

    mStreaming = true;
    mBufType = type;
    ALOGI("Stream ON (type=%d)", type);
    return 0;
}

int V4l2Device::streamOff(enum v4l2_buf_type type) {
    if (mFd < 0 || !mStreaming) return 0;

    if (ioctl(mFd, VIDIOC_STREAMOFF, &type) < 0) {
        ALOGE("VIDIOC_STREAMOFF failed: %s (errno=%d)", strerror(errno), errno);
        return -errno;
    }

    mStreaming = false;
    ALOGI("Stream OFF (type=%d)", type);
    return 0;
}

int V4l2Device::dequeueBuffer(struct v4l2_buffer *buf) {
    if (mFd < 0) return -EINVAL;

    // Wait for buffer using poll() with retries
    struct pollfd pfd;
    pfd.fd = mFd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int poll_timeout_ms = 500;
    int max_retries = 3;
    int retry_count = 0;

    do {
        pfd.revents = 0;
        int pollRet = poll(&pfd, 1, poll_timeout_ms);
        if (pollRet < 0) {
            ALOGE("poll failed: %s", strerror(errno));
            return -errno;
        }
        if (pollRet == 0) {
            retry_count++;
            if (retry_count >= max_retries) {
                ALOGW("poll timeout after %d retries - no frame available", retry_count);
                return -EAGAIN;
            }
            poll_timeout_ms = 250;
            ALOGW("poll timeout, retry %d/%d", retry_count, max_retries);
            continue;
        }
        if (!(pfd.revents & POLLIN)) {
            ALOGW("poll returned unexpected event: 0x%x", pfd.revents);
            return -EAGAIN;
        }
        break;
    } while (retry_count < max_retries);

    memset(buf, 0, sizeof(struct v4l2_buffer));
    buf->type = mBufType;
    buf->memory = mMemory;

    int ret = ioctl(mFd, VIDIOC_DQBUF, buf);
    if (ret < 0) {
        if (errno == EAGAIN) {
            return -EAGAIN;
        }
        ALOGE("VIDIOC_DQBUF failed: %s", strerror(errno));
        return -errno;
    }

    return 0;
}

int V4l2Device::queueBuffer(struct v4l2_buffer *buf) {
    if (mFd < 0) return -EINVAL;

    buf->type = mBufType;
    buf->memory = mMemory;

    ALOGV("queueBuffer: index=%d, memory=%d", buf->index, buf->memory);
    if (ioctl(mFd, VIDIOC_QBUF, buf) < 0) {
        ALOGE("VIDIOC_QBUF failed: %s (errno=%d)", strerror(errno), errno);
        return -errno;
    }

    return 0;
}

} // namespace mocha