#include "JpegEncoder.h"
#include <cutils/log.h>
#include <jpeglib.h>
#include <cstring>
#include <cstdlib>

#ifndef LOG_TAG
#define LOG_TAG "MochaCameraHAL"
#endif

namespace mocha {

JpegEncoder::JpegEncoder() : mInitialized(false) {}

JpegEncoder::~JpegEncoder() {}

/* libjpeg destination manager that writes to a fixed-size buffer */
struct jpeg_dest_mgr {
    struct jpeg_destination_mgr pub;
    uint8_t* buffer;
    size_t bufferSize;
};

static void dest_init_destination(j_compress_ptr cinfo) {
    jpeg_dest_mgr* dest = (jpeg_dest_mgr*)cinfo->dest;
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = dest->bufferSize;
}

static boolean dest_empty_output_buffer(j_compress_ptr cinfo) {
    return TRUE;
}

static void dest_term_destination(j_compress_ptr cinfo) {}

int JpegEncoder::encodeRGBA(const uint8_t* rgba, int width, int height, int quality,
                            uint8_t* output, size_t outputSize, size_t* jpegSize) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    int row_stride;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    struct jpeg_dest_mgr dest;
    dest.buffer = output;
    dest.bufferSize = outputSize;
    dest.pub.init_destination = dest_init_destination;
    dest.pub.empty_output_buffer = dest_empty_output_buffer;
    dest.pub.term_destination = dest_term_destination;
    cinfo.dest = &dest.pub;

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    row_stride = width * 4; /* RGBA stride */

    uint8_t* rgbRow = (uint8_t*)malloc(width * 3);
    if (!rgbRow) {
        jpeg_destroy_compress(&cinfo);
        return -ENOMEM;
    }

    while (cinfo.next_scanline < (JDIMENSION)height) {
        const uint8_t* src = rgba + cinfo.next_scanline * row_stride;
        for (int x = 0; x < width; x++) {
            rgbRow[x * 3 + 0] = src[x * 4 + 0]; /* R */
            rgbRow[x * 3 + 1] = src[x * 4 + 1]; /* G */
            rgbRow[x * 3 + 2] = src[x * 4 + 2]; /* B */
        }
        row_pointer[0] = rgbRow;
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    free(rgbRow);
    jpeg_finish_compress(&cinfo);

    *jpegSize = outputSize - dest.pub.free_in_buffer;

    ALOGI("JPEG encoded %dx%d quality=%d -> %zu bytes", width, height, quality, *jpegSize);

    jpeg_destroy_compress(&cinfo);
    return 0;
}

} // namespace mocha
