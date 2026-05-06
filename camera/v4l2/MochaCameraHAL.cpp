/*
 * Mocha Camera HAL - Stub with miscdevice communication
 * Uses NVIDIA protocol via /dev/imx179 and /dev/ov5693
 */

#define LOG_TAG "MochaCameraHAL"
#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/camera_common.h>
#include <hardware/camera3.h>
#include <camera/CameraMetadata.h>
#include <utils/Errors.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>

using namespace android;

#define BACK_DEV "/dev/imx179"
#define FRONT_DEV "/dev/ov5693"
#define MAX_CAMERAS 2
#define BACK_CAMERA_ID 0
#define FRONT_CAMERA_ID 1

#define NVC_IOCTL_CAPS_RD    0x6f6a
#define NVC_IOCTL_MODE_WR    0x6f6b
#define NVC_IOCTL_MODE_RD  0x6f6c
#define NVC_IOCTL_STATIC_RD 0x6f6d
#define NVC_IOCTL_DYNAMIC_RD 0x6f6e

struct nvc_imager_cap {
    char identifier[32];
    __u32 sensor_nvc_interface;
    __u32 pixel_types[4];
    __u32 orientation;
    __u32 direction;
    __u32 initial_clock_rate_khz;
    __u32 clock_profiles[2];
    __u32 h_sync_edge;
    __u32 v_sync_edge;
    __u32 mclk_on_vgp0;
    __u8 csi_port;
    __u8 data_lanes;
    __u8 virtual_channel_id;
    __u8 discontinuous_clk_mode;
} __packed;

struct nvc_imager_mode {
    __s32 res_x;
    __s32 res_y;
    __s32 active_start_x;
    __s32 active_start_y;
    __u32 peak_frame_rate;
    __u32 pixel_aspect_ratio;
} __packed;

struct nvc_imager_mode_list {
    struct nvc_imager_mode *p_modes;
    __u32 *p_num_mode;
} __packed;

struct nvc_imager_static_nvc {
    __u32 api_version;
    __u32 sensor_type;
    __u32 bits_per_pixel;
    __u32 sensor_id;
    __u32 sensor_id_minor;
    __u32 focal_len;
    __u32 max_aperture;
    __u32 fnumber;
    __u32 view_angle_h;
    __u32 view_angle_v;
    __u32 stereo_cap;
    __u32 res_chg_wait_time;
    __u8 support_isp;
} __packed;

struct MochaDevice {
    int fd;
    const char* devicePath;
    bool streaming;
    int width;
    int height;
};

static MochaDevice gDevices[MAX_CAMERAS] = {
    { -1, BACK_DEV, false, 0, 0 },
    { -1, FRONT_DEV, false, 0, 0 },
};

struct MochaCamInfo {
    int cameraId;
    const char* devicePath;
    int facing;
    int orientation;
    bool opened;
};

static MochaCamInfo gCameraInfo[MAX_CAMERAS] = {
    { BACK_CAMERA_ID, BACK_DEV, ANDROID_LENS_FACING_BACK, 90, false },
    { FRONT_CAMERA_ID, FRONT_DEV, ANDROID_LENS_FACING_FRONT, 270, false },
};

static camera_metadata_t* gCameraMetadata[MAX_CAMERAS] = {NULL, NULL};

static int open_sensor(MochaDevice* dev) {
    if (dev->fd >= 0) return 0;
    
    dev->fd = open(dev->devicePath, O_RDWR);
    if (dev->fd < 0) {
        ALOGE("Failed to open %s: %s", dev->devicePath, strerror(errno));
        return -1;
    }
    
    struct nvc_imager_cap cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(dev->fd, NVC_IOCTL_CAPS_RD, &cap) < 0) {
        ALOGE("NVC_IOCTL_CAPS_RD failed: %s", strerror(errno));
    } else {
        ALOGI("Sensor %s: %dx%d, rate=%u", cap.identifier, 
             cap.pixel_types[0], cap.pixel_types[1], cap.initial_clock_rate_khz);
    }
    
    struct nvc_imager_static_nvc s_nvc;
    memset(&s_nvc, 0, sizeof(s_nvc));
    if (ioctl(dev->fd, NVC_IOCTL_STATIC_RD, &s_nvc) < 0) {
        ALOGE("NVC_IOCTL_STATIC_RD failed: %s", strerror(errno));
    } else {
        ALOGI("Static: sensor_type=%u, focal=%u", s_nvc.sensor_type, s_nvc.focal_len);
    }
    
    return 0;
}

static int set_resolution(MochaDevice* dev, int width, int height) {
    struct nvc_imager_mode_list mode_list;
    struct nvc_imager_mode modes[8];
    __u32 num_modes = 8;
    
    mode_list.p_modes = modes;
    mode_list.p_num_mode = &num_modes;
    
    if (ioctl(dev->fd, NVC_IOCTL_MODE_RD, &mode_list) < 0) {
        ALOGE("NVC_IOCTL_MODE_RD failed: %s", strerror(errno));
        return -1;
    }
    
    for (__u32 i = 0; i < num_modes; i++) {
        if (modes[i].res_x == width && modes[i].res_y == height) {
            struct nvc_imager_mode mode = modes[i];
            if (ioctl(dev->fd, NVC_IOCTL_MODE_WR, &mode) < 0) {
                ALOGE("NVC_IOCTL_MODE_WR failed: %s", strerror(errno));
                return -1;
            }
            dev->width = width;
            dev->height = height;
            ALOGI("Set resolution %dx%d", width, height);
            return 0;
        }
    }
    
    ALOGW("Resolution %dx%d not found, using default", width, height);
    if (num_modes > 0) {
        struct nvc_imager_mode mode = modes[0];
        if (ioctl(dev->fd, NVC_IOCTL_MODE_WR, &mode) < 0) {
            return -1;
        }
        dev->width = modes[0].res_x;
        dev->height = modes[0].res_y;
    }
    return 0;
}

static camera_metadata_t* create_camera_metadata(int camera_id, int facing, int orientation) {
    camera_metadata_t* metadata = allocate_camera_metadata(64, 8192);
    if (!metadata) return NULL;

    int32_t caps[] = { ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE };
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, caps, 1);

    uint8_t hw_level[] = { ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL };
    add_camera_metadata_entry(metadata, ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, hw_level, 1);

    int32_t lens_facing[] = { facing };
    add_camera_metadata_entry(metadata, ANDROID_LENS_FACING, lens_facing, 1);

    int32_t sensor_orient[] = { orientation };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_ORIENTATION, sensor_orient, 1);

    int32_t max_regions[] = { 3, 3, 3 };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_MAX_REGIONS, max_regions, 3);

    int32_t jpeg_sizes[] = { 3264, 2448, 1920, 1080, 1280, 720, 640, 480 };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_JPEG_SIZES, jpeg_sizes, 8);

    int32_t proc_sizes[] = { 3264, 2448, 1920, 1080, 1280, 720, 640, 480 };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES, proc_sizes, 8);

    int32_t stream_configs[] = {
        HAL_PIXEL_FORMAT_BLOB, 3264, 2448, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 1280, 720, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 640, 480, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 3264, 2448, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1280, 720, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 640, 480, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, stream_configs, 16);

    int64_t min_durs[] = {
        HAL_PIXEL_FORMAT_BLOB, 3264, 2448, 33333334LL,
        HAL_PIXEL_FORMAT_BLOB, 1920, 1080, 33333334LL,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 3264, 2448, 33333334LL,
        HAL_PIXEL_FORMAT_YCbCr_420_888, 1920, 1080, 33333334LL,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, min_durs, 16);

    uint8_t af_modes[] = { ANDROID_CONTROL_AF_MODE_AUTO, ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE, ANDROID_CONTROL_AF_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AF_AVAILABLE_MODES, af_modes, 3);

    uint8_t ae_modes[] = { ANDROID_CONTROL_AE_MODE_ON, ANDROID_CONTROL_AE_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_AVAILABLE_MODES, ae_modes, 2);

    uint8_t awb_modes[] = { ANDROID_CONTROL_AWB_MODE_AUTO, ANDROID_CONTROL_AWB_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AWB_AVAILABLE_MODES, awb_modes, 2);

    uint8_t flash_available[] = { ANDROID_FLASH_INFO_AVAILABLE_FALSE };
    add_camera_metadata_entry(metadata, ANDROID_FLASH_INFO_AVAILABLE, flash_available, 1);

    int32_t active_array[] = { 0, 0, 3264, 2448 };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array, 4);

    int32_t pixel_array[] = { 3264, 2448 };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixel_array, 2);

    float physical_size[] = { 3.67f, 2.74f };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_PHYSICAL_SIZE, physical_size, 2);

    float focal[] = { 2.86f };
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, focal, 1);

    int32_t sensitivity[] = { 100, 3200 };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, sensitivity, 2);

    int32_t exposure_time[] = { 1000, 1000000 };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, exposure_time, 2);

    ALOGI("Created metadata for camera %d", camera_id);
    return metadata;
}

static int normalize_version(int version) {
    if (version == CAMERA_DEVICE_API_VERSION_3_0)
        return CAMERA_DEVICE_API_VERSION_3_2;
    return version;
}

static int camera3_close(hw_device_t* device) {
    if (device) {
        camera3_device_t* cam_dev = reinterpret_cast<camera3_device_t*>(device);
        if (cam_dev->priv) {
            int* id = reinterpret_cast<int*>(cam_dev->priv);
            MochaDevice* dev = &gDevices[*id];
            if (dev->fd >= 0) {
                close(dev->fd);
                dev->fd = -1;
            }
            dev->streaming = false;
            gCameraInfo[*id].opened = false;
            delete id;
        }
        delete cam_dev;
    }
    return 0;
}

static int camera3_initialize(const camera3_device_t* device,
        const camera3_callback_ops_t* callback_ops) {
    return 0;
}

static int camera3_configure_streams(const camera3_device_t* device,
        camera3_stream_configuration_t* stream_config) {
    if (!stream_config) return -EINVAL;

    int* id = reinterpret_cast<int*>(reinterpret_cast<const camera3_device_t*>(device)->priv);
    MochaDevice* dev = &gDevices[*id];

    int width = 0, height = 0;
    for (uint32_t i = 0; i < stream_config->num_streams; i++) {
        camera3_stream_t* stream = stream_config->streams[i];
        if (stream->stream_type == CAMERA3_STREAM_OUTPUT) {
            if (stream->width * stream->height > width * height) {
                width = stream->width;
                height = stream->height;
            }
        }
    }

    if (width > 0 && height > 0) {
        if (open_sensor(dev) == 0) {
            set_resolution(dev, width, height);
            dev->streaming = true;
        }
    }

    return 0;
}

static const camera_metadata_t* camera3_construct_default_request_settings(
        const camera3_device_t* device, int type) {
    CameraMetadata cm;

    static const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    cm.update(ANDROID_CONTROL_MODE, &controlMode, 1);

    static const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    cm.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);

    static const uint8_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    cm.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

    static const uint8_t afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
    cm.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);

    int* id = reinterpret_cast<int*>(reinterpret_cast<const camera3_device_t*>(device)->priv);
    MochaDevice* dev = &gDevices[*id];

    int32_t cropRegion[] = { 0, 0, dev->width, dev->height };
    cm.update(ANDROID_SCALER_CROP_REGION, cropRegion, 4);

    return cm.release();
}

static int camera3_process_capture_request(const camera3_device_t* device,
        camera3_capture_request_t* request) {
    if (!request || !request->output_buffers) return -EINVAL;

    int* id = reinterpret_cast<int*>(reinterpret_cast<const camera3_device_t*>(device)->priv);
    MochaDevice* dev = &gDevices[*id];

    camera3_stream_buffer* output_buffers = new camera3_stream_buffer[request->num_output_buffers];
    
    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
        output_buffers[i] = request->output_buffers[i];
        output_buffers[i].acquire_fence = -1;
        output_buffers[i].release_fence = -1;
        output_buffers[i].status = CAMERA3_BUFFER_STATUS_OK;
    }

    ALOGV("Processed frame %u on camera %d", request->frame_number, *id);
    delete[] output_buffers;
    return 0;
}

static int camera3_flush(const camera3_device_t* device) {
    int* id = reinterpret_cast<int*>(reinterpret_cast<const camera3_device_t*>(device)->priv);
    MochaDevice* dev = &gDevices[*id];
    dev->streaming = false;
    return 0;
}

static camera3_device_ops_t camera3_ops = {
    .initialize = camera3_initialize,
    .configure_streams = camera3_configure_streams,
    .construct_default_request_settings = camera3_construct_default_request_settings,
    .process_capture_request = camera3_process_capture_request,
    .flush = camera3_flush,
};

static int camera_get_number_of_cameras(void) {
    return MAX_CAMERAS;
}

static int camera_get_camera_info(int camera_id, struct camera_info* info) {
    if (!info || camera_id < 0 || camera_id >= MAX_CAMERAS)
        return -EINVAL;

    if (!gCameraMetadata[camera_id]) {
        gCameraMetadata[camera_id] = create_camera_metadata(
            camera_id,
            gCameraInfo[camera_id].facing,
            gCameraInfo[camera_id].orientation);
    }

    info->facing = gCameraInfo[camera_id].facing;
    info->orientation = gCameraInfo[camera_id].orientation;
    info->device_version = normalize_version(CAMERA_DEVICE_API_VERSION_3_2);
    info->static_camera_characteristics = gCameraMetadata[camera_id];

    return 0;
}

static int camera_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device) {
    int camera_id = -1;

    if (strcmp(name, "0") == 0) camera_id = 0;
    else if (strcmp(name, "1") == 0) camera_id = 1;
    else {
        ALOGE("Invalid camera ID: %s", name);
        return -EINVAL;
    }

    if (gCameraInfo[camera_id].opened) {
        ALOGE("Camera %d already opened", camera_id);
        return -EBUSY;
    }

    camera3_device_t* cam_dev = new camera3_device_t();
    if (!cam_dev) return -ENOMEM;

    cam_dev->common.tag = HARDWARE_DEVICE_TAG;
    cam_dev->common.version = CAMERA_DEVICE_API_VERSION_3_2;
    cam_dev->common.module = (hw_module_t*)module;
    cam_dev->common.close = camera3_close;
    cam_dev->ops = &camera3_ops;
    cam_dev->priv = new int(camera_id);

    gCameraInfo[camera_id].opened = true;
    *device = &cam_dev->common;

    ALOGI("Opened camera %d (%s)", camera_id, gCameraInfo[camera_id].devicePath);
    return 0;
}

static hw_module_methods_t camera_module_methods = {
    .open = camera_device_open
};

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = CAMERA_MODULE_API_VERSION_2_0,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = CAMERA_HARDWARE_MODULE_ID,
        .name = "Mocha Camera HAL",
        .author = "Mocha",
        .methods = &camera_module_methods,
        .dso = NULL,
        .reserved = {0},
    },
    .get_number_of_cameras = camera_get_number_of_cameras,
    .get_camera_info = camera_get_camera_info,
    .open_legacy = NULL,
    .set_callbacks = NULL,
    .get_vendor_tag_ops = NULL,
    .init = NULL,
    .reserved = {0},
};