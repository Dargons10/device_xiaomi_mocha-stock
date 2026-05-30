#ifndef MOCHA_JPEG_ENCODER_H
#define MOCHA_JPEG_ENCODER_H

#include <cstdint>
#include <cstddef>

namespace mocha {

class JpegEncoder {
public:
    JpegEncoder();
    ~JpegEncoder();

    int encodeRGBA(const uint8_t* rgba, int width, int height, int quality,
                   uint8_t* output, size_t outputSize, size_t* jpegSize);

private:
    bool mInitialized;
};

} // namespace mocha

#endif // MOCHA_JPEG_ENCODER_H
