/*
 * V4L2 Device Wrapper for Mocha Camera HAL
 * Handles V4L2 capture operations
 */

#ifndef MOCHA_V4L2_DEVICE_H
#define MOCHA_V4L2_DEVICE_H

#include <linux/videodev2.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cutils/log.h>

#include <utils/Vector.h>

namespace mocha {

#define V4L2_DEVICE_PATH "/dev/video0"

struct V4l2Buffer {
    void *start;
    size_t length;
    int fd;
};

struct V4l2Format {
    int width;
    int height;
    int pixelFormat;
    uint32_t field;
    uint32_t bytesperline;
    uint32_t sizeimage;
};

class V4l2Device {
public:
    V4l2Device();
    ~V4l2Device();

    int open(const char *devicePath = V4L2_DEVICE_PATH);
    int close();

    // Query capabilities
    int queryCap(struct v4l2_capability *cap);

    // Enumerate and set formats
    int enumInput(int index, struct v4l2_input *input);
    int setInput(int index);
    int getInput();

    int enumFmt(uint32_t type, int index, struct v4l2_fmtdesc *fmt);
    int getFmt(struct v4l2_format *fmt);
    int setFmt(int width, int height, int pixelFormat);

    // Buffer management
    int reqBuffers(int count, enum v4l2_memory memory);
    int queryBuffer(int index, struct v4l2_buffer *buf);
    int exportBuffer(int index, int *dmaFd);
    int mapBuffer(int index, void **ptr, size_t *length);
    int unmapBuffer(int index);

    // Streaming
    int streamOn(enum v4l2_buf_type type);
    int streamOff(enum v4l2_buf_type type);

    // Convenience methods
    int startStreaming() { return streamOn(V4L2_BUF_TYPE_VIDEO_CAPTURE); }
    int stopStreaming() { return streamOff(V4L2_BUF_TYPE_VIDEO_CAPTURE); }

    // Capture
    int dequeueBuffer(struct v4l2_buffer *buf);
    int queueBuffer(struct v4l2_buffer *buf);

    // Buffer access
    V4l2Buffer* getBuffer(int index) {
        if (index >= 0 && static_cast<size_t>(index) < mBuffers.size()) {
            return &mBuffers.editItemAt(index);
        }
        return nullptr;
    }

    bool isOpened() const { return mFd >= 0; }
    bool isStreaming() const { return mStreaming; }

private:
    int mFd;
    bool mStreaming;
    enum v4l2_memory mMemory;
    int mBufferCount;
    android::Vector<V4l2Buffer> mBuffers;
    enum v4l2_buf_type mBufType;
};

} // namespace mocha

#endif // MOCHA_V4L2_DEVICE_H