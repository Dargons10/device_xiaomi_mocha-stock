/*
 * Copyright (C) 2015, The CyanogenMod Project
 *           (C) 2017, The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_PARAMETERS

#define LOG_TAG "CameraWrapper"
#include <cutils/log.h>
#include "CameraWrapper.h"
#include "Camera3Wrapper.h"


//------------DEBUG-----------------
static int pfd[2];
static pthread_t thr;

static void *thread_func(void*)
{
    ssize_t rdsz;
    char buf[128];
    while((rdsz = read(pfd[0], buf, sizeof buf - 1)) > 0) {
        if(buf[rdsz - 1] == '\n') --rdsz;
        buf[rdsz] = 0;  /* add null-terminator */
        __android_log_write(ANDROID_LOG_DEBUG, LOG_TAG, buf);
    }
    return 0;
}


int start_logger()
{

    /* make stdout line-buffered and stderr unbuffered */
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IONBF, 0);

    /* create the pipe and redirect stdout and stderr */
    pipe(pfd);
    dup2(pfd[1], 1);
    dup2(pfd[1], 2);

    /* spawn the logging thread */
    if(pthread_create(&thr, 0, thread_func, 0) == -1)
        return -1;
    pthread_detach(thr);
    return 0;
}


//-----------DEBUG-------------------------


typedef struct camera_metadata_buffer_entry {
    uint32_t tag;
    uint32_t count;
    union {
        uint32_t offset;
        uint8_t  value[4];
    } data;
    uint8_t  type;
    uint8_t  reserved[3];
} camera_metadata_buffer_entry_t;

typedef uint32_t metadata_uptrdiff_t;
typedef uint32_t metadata_size_t;

struct camera_metadata {
    metadata_size_t          size;
    uint32_t                 version;
    uint32_t                 flags;
    metadata_size_t          entry_count;
    metadata_size_t          entry_capacity;
    metadata_uptrdiff_t      entries_start; // Offset from camera_metadata
    metadata_size_t          data_count;
    metadata_size_t          data_capacity;
    metadata_uptrdiff_t      data_start; // Offset from camera_metadata
    uint8_t                  reserved[];
};

static camera_metadata_buffer_entry_t *get_entries(
        const camera_metadata_t *metadata) {
    return (camera_metadata_buffer_entry_t*)
            ((uint8_t*)metadata + metadata->entries_start);
}


static camera_module_t *gVendorModule = 0;
static char prop[PROPERTY_VALUE_MAX];
static camera_metadata_t* vendorInfo[2] = {0,0};
static camera_info vendor_camera_info;

static inline int normalize_camera_device_version(int version)
{
    if (version == CAMERA_DEVICE_API_VERSION_3_0) {
        return CAMERA_DEVICE_API_VERSION_3_2;
    }

    return version;
}

static bool has_valid_stream_configurations(camera_metadata_t* metadata, int camera_id, const char* stage)
{
    if (metadata == NULL) {
        ALOGE("%s[%s]: null metadata for camera %d", __FUNCTION__, stage, camera_id);
        return false;
    }

    camera_metadata_entry_t stream_configs;
    int rc = find_camera_metadata_entry(metadata,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            &stream_configs);
    if (rc != 0) {
        ALOGE("%s[%s]: camera %d missing stream configurations", __FUNCTION__, stage, camera_id);
        return false;
    }

    if (stream_configs.count < 4 || (stream_configs.count % 4) != 0) {
        ALOGE("%s[%s]: camera %d invalid stream count=%u", __FUNCTION__, stage, camera_id, stream_configs.count);
        return false;
    }

    for (size_t i = 0; i < stream_configs.count; i += 4) {
        int32_t width = stream_configs.data.i32[i + 1];
        int32_t height = stream_configs.data.i32[i + 2];
        if (width <= 0 || height <= 0) {
            ALOGE("%s[%s]: camera %d bad stream size %dx%d", __FUNCTION__, stage, camera_id, width, height);
            return false;
        }
    }

    ALOGI("%s[%s]: camera %d stream configurations count=%u", __FUNCTION__, stage, camera_id, stream_configs.count);
    return true;
}

static bool add_stream_configurations_entry(camera_metadata_t** metadata_ptr,
        const int32_t* entries, size_t entry_count, int camera_id)
{
    camera_metadata_t* metadata = *metadata_ptr;
    int add_rc = add_camera_metadata_entry(metadata,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            entries,
            entry_count);
    if (add_rc == 0) {
        return true;
    }

    size_t extra_data = entry_count * sizeof(int32_t) + 512;
    camera_metadata_t* expanded = allocate_camera_metadata(
            metadata->entry_count + 4,
            metadata->data_count + extra_data);
    if (expanded == NULL) {
        ALOGE("%s: failed to allocate expanded metadata for camera %d", __FUNCTION__, camera_id);
        return false;
    }

    int append_rc = append_camera_metadata(expanded, metadata);
    if (append_rc != 0) {
        ALOGE("%s: failed appending metadata for camera %d", __FUNCTION__, camera_id);
        free_camera_metadata(expanded);
        return false;
    }

    add_rc = add_camera_metadata_entry(expanded,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            entries,
            entry_count);
    if (add_rc != 0) {
        ALOGE("%s: failed adding synthesized stream configurations for camera %d", __FUNCTION__, camera_id);
        free_camera_metadata(expanded);
        return false;
    }

    free_camera_metadata(metadata);
    *metadata_ptr = expanded;
    return true;
}

static bool ensure_stream_configurations(camera_metadata_t** metadata_ptr, int camera_id)
{
    camera_metadata_t* metadata = *metadata_ptr;
    if (metadata == NULL) {
        return false;
    }

    camera_metadata_entry_t stream_configs;
    int rc = find_camera_metadata_entry(metadata,
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
            &stream_configs);
    if (rc == 0 && stream_configs.count >= 4 && (stream_configs.count % 4 == 0)) {
        return has_valid_stream_configurations(metadata, camera_id, "existing");
    }

    camera_metadata_entry_t processed_sizes;
    camera_metadata_entry_t jpeg_sizes;
    int processed_rc = find_camera_metadata_entry(metadata,
            ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
            &processed_sizes);
    int jpeg_rc = find_camera_metadata_entry(metadata,
            ANDROID_SCALER_AVAILABLE_JPEG_SIZES,
            &jpeg_sizes);

    if (processed_rc != 0 || jpeg_rc != 0 ||
            processed_sizes.count < 2 || jpeg_sizes.count < 2 ||
            (processed_sizes.count % 2) != 0 || (jpeg_sizes.count % 2) != 0) {
        ALOGE("%s: cannot synthesize stream configurations for camera %d", __FUNCTION__, camera_id);
        return false;
    }

    if (rc == 0) {
        delete_camera_metadata_entry(metadata, stream_configs.index);
    }

    const int32_t stream_field_count = 4;
    const size_t processed_pair_count = processed_sizes.count / 2;
    const size_t jpeg_pair_count = jpeg_sizes.count / 2;
    const size_t total_stream_count = (processed_pair_count + jpeg_pair_count) * stream_field_count;

    int32_t* synthesized = (int32_t*)calloc(total_stream_count, sizeof(int32_t));
    if (synthesized == NULL) {
        ALOGE("%s: failed to allocate synthesized stream configurations", __FUNCTION__);
        return false;
    }

    size_t out = 0;
    for (size_t i = 0; i < processed_sizes.count; i += 2) {
        if (processed_sizes.data.i32[i] <= 0 || processed_sizes.data.i32[i + 1] <= 0) {
            continue;
        }
        synthesized[out++] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
        synthesized[out++] = processed_sizes.data.i32[i];
        synthesized[out++] = processed_sizes.data.i32[i + 1];
        synthesized[out++] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
    }

    for (size_t i = 0; i < jpeg_sizes.count; i += 2) {
        if (jpeg_sizes.data.i32[i] <= 0 || jpeg_sizes.data.i32[i + 1] <= 0) {
            continue;
        }
        synthesized[out++] = HAL_PIXEL_FORMAT_BLOB;
        synthesized[out++] = jpeg_sizes.data.i32[i];
        synthesized[out++] = jpeg_sizes.data.i32[i + 1];
        synthesized[out++] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;
    }

    if (out < 4 || (out % 4) != 0) {
        ALOGE("%s: synthesized stream configurations invalid for camera %d", __FUNCTION__, camera_id);
        free(synthesized);
        return false;
    }

    bool added = add_stream_configurations_entry(metadata_ptr, synthesized, out, camera_id);
    free(synthesized);

    if (!added) {
        return false;
    }

    metadata = *metadata_ptr;
    return has_valid_stream_configurations(metadata, camera_id, "synthesized");
}

static int check_vendor_module()
{
    int rv = 0;
    ALOGV("%s", __FUNCTION__);

    if(gVendorModule)
        return 0;

    rv = hw_get_module_by_class("camera", "vendor", (const hw_module_t **)&gVendorModule);
    if (rv)
        ALOGE("failed to open vendor camera module");
    return rv;
}

static struct hw_module_methods_t camera_module_methods = {
        open: camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
         .tag = HARDWARE_MODULE_TAG,
         .module_api_version = CAMERA_MODULE_API_VERSION_2_3,
         .hal_api_version = HARDWARE_HAL_API_VERSION,
         .id = CAMERA_HARDWARE_MODULE_ID,
         .name = "MI PAD Camera Wrapper",
         .author = "The LineageOS Project",
         .methods = &camera_module_methods,
         .dso = NULL,
         .reserved = {0},
    },
    .get_number_of_cameras = camera_get_number_of_cameras,
    .get_camera_info = camera_get_camera_info,
    .set_callbacks = camera_set_callbacks,
    .get_vendor_tag_ops = camera_get_vendor_tag_ops,
    .open_legacy = NULL,
    .set_torch_mode = NULL,
    .init = NULL,
    .reserved = {0},
};

static int camera_device_open(const hw_module_t* module, const char* name,
                hw_device_t** device)
{
    int rv = -EINVAL;
    start_logger(); //DEBUG

    if (name != NULL) {
        if (check_vendor_module())
            return -EINVAL;

        rv = camera3_device_open(module, name, device);
    }

    return rv;
}

static int camera_get_number_of_cameras(void)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;
    return gVendorModule->get_number_of_cameras();
}

static int camera_get_camera_info(int camera_id, struct camera_info *info)
{
    ALOGV("%s camera_id: = %d", __FUNCTION__, camera_id);
    if (check_vendor_module())
        return 0;
//    int ret = gVendorModule->get_camera_info(camera_id, info);
    int ret = gVendorModule->get_camera_info(camera_id, &vendor_camera_info);
//    fillStaticInfo();

    info->facing = vendor_camera_info.facing;
    info->orientation = vendor_camera_info.orientation;
    info->device_version = normalize_camera_device_version(vendor_camera_info.device_version);

    if (vendorInfo[camera_id] == 0 ) {
        vendorInfo[camera_id] = clone_camera_metadata(vendor_camera_info.static_camera_characteristics);
        if (vendorInfo[camera_id] == NULL) {
            ALOGE("%s: clone_camera_metadata failed for camera %d", __FUNCTION__, camera_id);
            info->static_camera_characteristics = vendor_camera_info.static_camera_characteristics;
            return ret;
        }

        camera_metadata_entry_t found_entry;
        int rc = find_camera_metadata_entry(
                vendorInfo[camera_id],
                ANDROID_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS,
                &found_entry);
        if (rc == 0) {
            delete_camera_metadata_entry(vendorInfo[camera_id], found_entry.index);
        }

        if (!ensure_stream_configurations(&vendorInfo[camera_id], camera_id)) {
            ALOGE("%s: camera %d stream synthesis failed, keeping high-speed cleanup", __FUNCTION__, camera_id);
        }

        has_valid_stream_configurations(vendorInfo[camera_id], camera_id, "final");
    }

    info->static_camera_characteristics = vendorInfo[camera_id];
    dump_camera_metadata(info->static_camera_characteristics, 1, 2);

    return ret;
}

static int camera_set_callbacks(const camera_module_callbacks_t *callbacks)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;
    return gVendorModule->set_callbacks(callbacks);
}

static void camera_get_vendor_tag_ops(vendor_tag_ops_t* ops)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return;
    return gVendorModule->get_vendor_tag_ops(ops);
}

static int camera_open_legacy(const struct hw_module_t* module, const char* id, uint32_t halVersion, struct hw_device_t** device)
{
    ALOGV("%s", __FUNCTION__);
    if (check_vendor_module())
        return 0;

    return gVendorModule->open_legacy(module, id, halVersion, device);
}
