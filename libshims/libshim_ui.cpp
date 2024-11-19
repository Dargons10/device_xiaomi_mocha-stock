#include <stdint.h>
#include <string>

#include <ui/GraphicBuffer.h>
#include <media/stagefright/MediaBuffer.h>

extern "C" {
    void _ZN7android13GraphicBufferC1EjjijNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
            void *(pthis), uint32_t inWidth, uint32_t inHeight, int inFormat,
            uint32_t inUsage, std::string requestorName);

    void _ZN7android13GraphicBufferC1Ejjij(void *(pthis), uint32_t inWidth, uint32_t inHeight, int inFormat, uint32_t inUsage) {
        _ZN7android13GraphicBufferC1EjjijNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE(
            pthis, inWidth, inHeight, inFormat, inUsage, "<Unknown>");
    }

    void _ZN7android13GraphicBufferC1EjjijjjP13native_handleb(
            const native_handle_t* handle,
            android::GraphicBuffer::HandleWrapMethod method,
            uint32_t width,
            uint32_t height,
            int format,
            uint32_t layerCount,
            uint64_t usage,
            uint32_t stride);

    void _ZN7android13GraphicBufferC1EjjijjP13native_handleb(
            uint32_t inWidth,
            uint32_t inHeight,
            int inFormat,
            uint32_t inUsage,
            uint32_t inStride,
            native_handle_t* inHandle,
            bool keepOwnership)
    {
        android::GraphicBuffer::HandleWrapMethod inMethod =
            (keepOwnership ? android::GraphicBuffer::TAKE_HANDLE : android::GraphicBuffer::WRAP_HANDLE);
        _ZN7android13GraphicBufferC1EjjijjjP13native_handleb(inHandle, inMethod, inWidth, inHeight,
            inFormat, static_cast<uint32_t>(1), static_cast<uint64_t>(inUsage), inStride);
    }

    #if defined(__LP64__)
    extern void _ZN7android13GraphicBufferC1EPK13native_handleNS0_16HandleWrapMethodEjjijmj(
	    void *self, const native_handle_t* handle,
            android::GraphicBuffer::HandleWrapMethod method,
            uint32_t width, uint32_t height, android::PixelFormat format,
            uint32_t layerCount, uint64_t usage, uint32_t stride);

    void _ZN7android13GraphicBufferC1EP19ANativeWindowBufferb(
	    void *self, ANativeWindowBuffer* buffer, bool keepOwnership)
    {
        _ZN7android13GraphicBufferC1EPK13native_handleNS0_16HandleWrapMethodEjjijmj(
	    self, buffer->handle,
	    keepOwnership ? android::GraphicBuffer::TAKE_HANDLE : android::GraphicBuffer::WRAP_HANDLE,
	    buffer->width, buffer->height, buffer->format, (uint32_t)buffer->layerCount,
	    buffer->usage, buffer->stride);
    }
    #else
    extern void _ZN7android13GraphicBufferC1EPK13native_handleNS0_16HandleWrapMethodEjjijyj(
	    void *self, const native_handle_t* handle,
            android::GraphicBuffer::HandleWrapMethod method,
            uint32_t width, uint32_t height, android::PixelFormat format,
            uint32_t layerCount, uint64_t usage, uint32_t stride);

    void _ZN7android13GraphicBufferC1EP19ANativeWindowBufferb(
	    void *self, ANativeWindowBuffer* buffer, bool keepOwnership)
    {
        _ZN7android13GraphicBufferC1EPK13native_handleNS0_16HandleWrapMethodEjjijyj(
	    self, buffer->handle,
	    keepOwnership ? android::GraphicBuffer::TAKE_HANDLE : android::GraphicBuffer::WRAP_HANDLE,
	    buffer->width, buffer->height, buffer->format, buffer->layerCount,
	    buffer->usage, buffer->stride);
    }
    #endif

    extern void _ZN7android11BufferQueue17createBufferQueueEPNS_2spINS_22IGraphicBufferProducerEEEPNS1_INS_22IGraphicBufferConsumerEEEb(
	    void* outProducer, void* outConsumer, bool consumerIsSurfaceFlinger);

    void _ZN7android11BufferQueue17createBufferQueueEPNS_2spINS_22IGraphicBufferProducerEEEPNS1_INS_22IGraphicBufferConsumerEEERKNS1_INS_19IGraphicBufferAllocEEE(
	    void* outProducer, void* outConsumer, void* allocator __unused, bool consumerIsSurfaceFlinger)
    {
        _ZN7android11BufferQueue17createBufferQueueEPNS_2spINS_22IGraphicBufferProducerEEEPNS1_INS_22IGraphicBufferConsumerEEEb(
	    outProducer, outConsumer, consumerIsSurfaceFlinger);
    }

    int _ZNK7android11MediaBuffer8refcountEv(android::MediaBuffer *self)
    {
        return self->refcount();
    }
}
