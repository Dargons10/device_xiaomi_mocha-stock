/*
 * Mocha Camera Device - HAL3 implementation
 * Camera3 device stub for Xiaomi Mi Pad
 * For LineageOS 15.1 (Android 8.1)
 */

#define LOG_TAG "MochaCameraHAL"
#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/native_handle.h>
#include <hardware/camera_common.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <utils/threads.h>
#include <utils/Vector.h>
#include <system/graphics.h>
#include <system/camera_metadata.h>
#include <errno.h>

#include "MochaCameraHAL.h"
#include "CameraPipeline.h"

using namespace android;


namespace mocha {

// Camera configurations
const MochaCameraInfo MochaCameraHAL::kCameras[] = {
    { 0, "IMX179", CAMERA_FACING_BACK, 90, 3280, false },
    { 1, "OV5693", CAMERA_FACING_FRONT, 270, 2592, false },
};

const int MochaCameraHAL::kNumCameras = sizeof(MochaCameraHAL::kCameras) / sizeof(MochaCameraHAL::kCameras[0]);

// Static camera characteristics cache
static camera_metadata_t* gCameraCharacteristics[2] = { nullptr, nullptr };

// Module callbacks
static camera_module_callbacks_t gModuleCallbacks;

// Forward declarations
static int camera_device_init(const hw_module_t *module, hw_device_t **device);
static int camera_device_close(hw_device_t *device);
static int camera_device_initialize(const camera3_device_t *device, const camera3_callback_ops_t *ops);
static int camera_device_configure_streams(const camera3_device_t *device, camera3_stream_configuration_t *config);
static const camera_metadata_t* camera_device_construct_default_request_settings(const camera3_device_t *device, int type);
static int camera_device_process_capture_request(const camera3_device_t *device, camera3_capture_request_t *request);
static void camera_device_dump(const camera3_device_t *device, int fd);
static int camera_device_flush(const camera3_device_t *device);

static camera3_device_ops_t camera_device_ops = {
    .initialize = camera_device_initialize,
    .configure_streams = camera_device_configure_streams,
    .register_stream_buffers = nullptr,
    .construct_default_request_settings = camera_device_construct_default_request_settings,
    .process_capture_request = camera_device_process_capture_request,
    .get_metadata_vendor_tag_ops = nullptr,
    .dump = camera_device_dump,
    .flush = camera_device_flush,
    .reserved = { 0 },
};

struct mocha_camera_device_t {
    hw_device_t common;
    camera3_device_ops_t *ops;
    int camera_id;
    const camera3_callback_ops_t *callback_ops;
    bool is_initialized;
    bool streams_configured;

    void* pipeline;
    camera3_stream_t* output_stream;
    uint32_t stream_width;
    uint32_t stream_height;
    uint32_t stream_format;
};

// Initialize static camera characteristics
static camera_metadata_t* init_static_characteristics(int cameraId) {
    if (cameraId < 0 || cameraId >= 2) return nullptr;
    if (gCameraCharacteristics[cameraId] != nullptr) return gCameraCharacteristics[cameraId];

    const MochaCameraInfo& cam = MochaCameraHAL::kCameras[cameraId];
    
    size_t entry_capacity = 100;
    size_t data_capacity = 8192;
    camera_metadata_t* metadata = allocate_camera_metadata(entry_capacity, data_capacity);
    if (!metadata) {
        ALOGE("Failed to allocate camera metadata");
        return nullptr;
    }

    // Camera facing
    uint8_t facing = cam.facing;
    add_camera_metadata_entry(metadata, ANDROID_LENS_FACING, &facing, 1);

    // Orientation
    int32_t orientation = cam.orientation;
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_ORIENTATION, &orientation, 1);

    // Available stream configurations
    int32_t configs[] = {
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480,  CAMERA3_STREAM_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, CAMERA3_STREAM_OUTPUT,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, CAMERA3_STREAM_OUTPUT,
        HAL_PIXEL_FORMAT_YCBCR_420_888, 640, 480,  CAMERA3_STREAM_OUTPUT,
        HAL_PIXEL_FORMAT_YCBCR_420_888, 1280, 720, CAMERA3_STREAM_OUTPUT,
        HAL_PIXEL_FORMAT_YCBCR_420_888, 1920, 1080, CAMERA3_STREAM_OUTPUT,
        HAL_PIXEL_FORMAT_BLOB, 3280, 2464, CAMERA3_STREAM_OUTPUT,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, configs, sizeof(configs)/sizeof(int32_t));

    // Available min frame durations
    int64_t durations[] = {
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 640, 480,  33333333LL,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1280, 720, 33333333LL,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, 1920, 1080, 33333333LL,
        HAL_PIXEL_FORMAT_YCBCR_420_888, 640, 480,  33333333LL,
        HAL_PIXEL_FORMAT_YCBCR_420_888, 1280, 720, 33333333LL,
        HAL_PIXEL_FORMAT_YCBCR_420_888, 1920, 1080, 33333333LL,
    };
    int ret = add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS, durations, sizeof(durations)/sizeof(int64_t));
    ALOGI("DEBUG: Added min frame durations, ret=%d", ret);

    // Available stall durations
    int64_t stall_durations[] = {
        HAL_PIXEL_FORMAT_BLOB, 3280, 2464, 500000000LL,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stall_durations, sizeof(stall_durations)/sizeof(int64_t));

    // Available processed sizes (for preview/video)
    int32_t processed_sizes[] = {
        640, 480,
        1280, 720,
        1920, 1080,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES, processed_sizes, sizeof(processed_sizes)/sizeof(int32_t));

    // Available processed min durations
    int64_t processed_durations[] = {
        33333333LL,
        33333333LL,
        33333333LL,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_PROCESSED_MIN_DURATIONS, processed_durations, sizeof(processed_durations)/sizeof(int64_t));

    // Available JPEG sizes
    int32_t jpeg_sizes[] = {
        3280, 2464,
        1920, 1080,
        1280, 720,
        640, 480,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_JPEG_SIZES, jpeg_sizes, sizeof(jpeg_sizes)/sizeof(int32_t));

    // Available JPEG min durations
    int64_t jpeg_durations[] = {
        500000000LL,
        33333333LL,
        33333333LL,
        33333333LL,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_JPEG_MIN_DURATIONS, jpeg_durations, sizeof(jpeg_durations)/sizeof(int64_t));

    // Available formats
    int32_t available_formats[] = {
        HAL_PIXEL_FORMAT_RGBA_8888,
        HAL_PIXEL_FORMAT_YCbCr_420_888,
        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
        HAL_PIXEL_FORMAT_BLOB,
    };
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_FORMATS, available_formats, sizeof(available_formats)/sizeof(int32_t));

    // Max digital zoom
    float max_digital_zoom = 4.0f;
    add_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, &max_digital_zoom, 1);

    // Request max pipeline depth (removed - not available in 8.1)
    
    // Flash available
    uint8_t flash_available = (cameraId == 0) ? 1 : 0;
    add_camera_metadata_entry(metadata, ANDROID_FLASH_INFO_AVAILABLE, &flash_available, 1);

    // Sensor info
    int32_t sensor_width = cam.maxResolution;
    int32_t sensor_height = (cam.maxResolution == 3280) ? 2464 : 1944;
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, (int32_t[]){0, 0, sensor_width, sensor_height}, 4);
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, (int32_t[]){sensor_width, sensor_height}, 2);
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE, (int32_t[]){0, 0, sensor_width, sensor_height}, 4);

    // Physical sensor size (required for FOV calculation)
    // IMX179: 3.676mm x 2.757mm (1/3.2"), OV5693: 2.8mm x 2.1mm (1/4")
    float phys_size[2];
    if (cameraId == 0) {
        phys_size[0] = 3.676f;
        phys_size[1] = 2.757f;
    } else {
        phys_size[0] = 2.8f;
        phys_size[1] = 2.1f;
    }
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_PHYSICAL_SIZE, phys_size, 2);

    // Sensor timestamp source
    int32_t sensor_timestamp_source = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &sensor_timestamp_source, 1);

    // Supported hardware level
    uint8_t hw_level = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;
    add_camera_metadata_entry(metadata, ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &hw_level, 1);

    // Request available capabilities
    uint8_t capabilities[] = {
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING,
    };
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, capabilities, sizeof(capabilities)/sizeof(uint8_t));

    // Available request keys
    int32_t request_keys[] = {
        ANDROID_CONTROL_AE_MODE,
        ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
        ANDROID_CONTROL_AWB_MODE,
        ANDROID_COLOR_CORRECTION_MODE,
        ANDROID_CONTROL_MODE,
        ANDROID_FLASH_MODE,
        ANDROID_JPEG_QUALITY,
        ANDROID_LENS_FOCUS_DISTANCE,
        ANDROID_NOISE_REDUCTION_MODE,
        ANDROID_REQUEST_ID,
        ANDROID_REQUEST_TYPE,
        ANDROID_SCALER_CROP_REGION,
        ANDROID_SENSOR_FRAME_DURATION,
        ANDROID_SENSOR_EXPOSURE_TIME,
        ANDROID_SENSOR_SENSITIVITY,
        ANDROID_STATISTICS_FACE_DETECT_MODE,
    };
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, request_keys, sizeof(request_keys)/sizeof(int32_t));

    // Available result keys
    int32_t result_keys[] = {
        ANDROID_CONTROL_AE_MODE,
        ANDROID_CONTROL_AE_STATE,
        ANDROID_CONTROL_AWB_MODE,
        ANDROID_CONTROL_AWB_STATE,
        ANDROID_CONTROL_MODE,
        ANDROID_FLASH_MODE,
        ANDROID_JPEG_QUALITY,
        ANDROID_LENS_FOCUS_DISTANCE,
        ANDROID_REQUEST_ID,
        ANDROID_SCALER_CROP_REGION,
        ANDROID_SENSOR_EXPOSURE_TIME,
        ANDROID_SENSOR_FRAME_DURATION,
        ANDROID_SENSOR_SENSITIVITY,
        ANDROID_SENSOR_TIMESTAMP,
    };
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, result_keys, sizeof(result_keys)/sizeof(int32_t));

    // Available scene modes (required by deriveCameraCharacteristicsKeys)
    uint8_t scene_modes[] = {
        ANDROID_CONTROL_SCENE_MODE_DISABLED,
    };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AVAILABLE_SCENE_MODES, scene_modes, sizeof(scene_modes)/sizeof(uint8_t));

    // Available AE modes (required by deriveCameraCharacteristicsKeys)
    uint8_t ae_modes[] = {
        ANDROID_CONTROL_AE_MODE_ON,
        ANDROID_CONTROL_AE_MODE_OFF,
    };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_AVAILABLE_MODES, ae_modes, sizeof(ae_modes)/sizeof(uint8_t));

    // Available AF modes (required by deriveCameraCharacteristicsKeys)
    uint8_t af_modes[] = {
        ANDROID_CONTROL_AF_MODE_OFF,
    };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AF_AVAILABLE_MODES, af_modes, sizeof(af_modes)/sizeof(uint8_t));

    // Available AWB modes (required by deriveCameraCharacteristicsKeys)
    uint8_t awb_modes[] = {
        ANDROID_CONTROL_AWB_MODE_AUTO,
        ANDROID_CONTROL_AWB_MODE_OFF,
    };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AWB_AVAILABLE_MODES, awb_modes, sizeof(awb_modes)/sizeof(uint8_t));

    // Available AE target FPS ranges (required by Parameters::initialize)
    // Note: Reference HAL uses simple FPS units (15, 30), not milli-fps
    int32_t ae_fps_ranges[] = {
        15, 30,  // 15-30 fps
    };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, ae_fps_ranges, sizeof(ae_fps_ranges)/sizeof(int32_t));

    // Available JPEG thumbnail sizes (required by Parameters::initialize)
    int32_t jpeg_thumbnail_sizes[] = {
        0, 0,       // No thumbnail
        160, 120,   // Small
        320, 240,   // Medium
    };
    add_camera_metadata_entry(metadata, ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, jpeg_thumbnail_sizes, sizeof(jpeg_thumbnail_sizes)/sizeof(int32_t));

    // Available hot pixel modes
    uint8_t hot_pixel_modes[] = {
        ANDROID_HOT_PIXEL_MODE_FAST,
        ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY,
    };
    add_camera_metadata_entry(metadata, ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, hot_pixel_modes, sizeof(hot_pixel_modes)/sizeof(uint8_t));

    // Available edge modes
    uint8_t edge_modes[] = {
        ANDROID_EDGE_MODE_OFF,
        ANDROID_EDGE_MODE_FAST,
        ANDROID_EDGE_MODE_HIGH_QUALITY,
    };
    add_camera_metadata_entry(metadata, ANDROID_EDGE_AVAILABLE_EDGE_MODES, edge_modes, sizeof(edge_modes)/sizeof(uint8_t));

    // Available noise reduction modes
    uint8_t nr_modes[] = {
        ANDROID_NOISE_REDUCTION_MODE_OFF,
        ANDROID_NOISE_REDUCTION_MODE_FAST,
        ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY,
    };
    add_camera_metadata_entry(metadata, ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, nr_modes, sizeof(nr_modes)/sizeof(uint8_t));

    // Available shading modes
    uint8_t shading_modes[] = {
        ANDROID_SHADING_MODE_OFF,
        ANDROID_SHADING_MODE_FAST,
        ANDROID_SHADING_MODE_HIGH_QUALITY,
    };
    add_camera_metadata_entry(metadata, ANDROID_SHADING_AVAILABLE_MODES, shading_modes, sizeof(shading_modes)/sizeof(uint8_t));

    // Available lens shading map modes
    uint8_t lsc_map_modes[] = {
        ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF,
    };
    add_camera_metadata_entry(metadata, ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES, lsc_map_modes, sizeof(lsc_map_modes)/sizeof(uint8_t));

    // Available tonemap modes
    uint8_t tonemap_modes[] = {
        ANDROID_TONEMAP_MODE_CONTRAST_CURVE,
        ANDROID_TONEMAP_MODE_FAST,
        ANDROID_TONEMAP_MODE_HIGH_QUALITY,
    };
    add_camera_metadata_entry(metadata, ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, tonemap_modes, sizeof(tonemap_modes)/sizeof(uint8_t));

    // Available cfa layout
    uint8_t cfa_layout = ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB;
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, &cfa_layout, 1);

    // AE lock available
    uint8_t ae_lock_available = ANDROID_CONTROL_AE_LOCK_AVAILABLE_TRUE;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_LOCK_AVAILABLE, &ae_lock_available, 1);

    // AWB lock available
    uint8_t awb_lock_available = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_TRUE;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awb_lock_available, 1);

    // AE compensation range (required by Parameters::initialize)
    int32_t ae_comp_range[] = { -9, 9 };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_COMPENSATION_RANGE, ae_comp_range, 2);

    // AE compensation step (required)
    camera_metadata_rational ae_comp_step = { 1, 3 };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_COMPENSATION_STEP, &ae_comp_step, 1);

    // Sensor exposure time range (required)
    int64_t exposure_time_range[] = { 10000LL, 500000000LL };  // 10us to 500ms
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, exposure_time_range, 2);

    // Sensor sensitivity range (required)
    int32_t sensitivity_range[] = { 100, 1600 };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, sensitivity_range, 2);

    // Max analog sensitivity (required)
    int32_t max_analog_sensitivity = 1600;
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY, &max_analog_sensitivity, 1);

    // JPEG max size (required)
    int32_t jpeg_max_size = 3280 * 2464 * 2;
    add_camera_metadata_entry(metadata, ANDROID_JPEG_MAX_SIZE, &jpeg_max_size, 1);

    // Pipeline max depth (required)
    uint8_t pipeline_depth = 4;
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &pipeline_depth, 1);

    // Max input streams (required)
    int32_t max_input_streams = 0;
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &max_input_streams, 1);

    // Lens hyperfocal distance (required)
    float hyperfocal = 0.0f;
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, &hyperfocal, 1);

    // Lens minimum focus distance (required)
    float min_focus_distance = (cameraId == 0) ? 10.0f : 0.0f;  // diopters
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, &min_focus_distance, 1);

    // Focus distance calibration (required)
    uint8_t focus_cal = ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED;
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION, &focus_cal, 1);

    // Available effects (required)
    uint8_t effects[] = { ANDROID_CONTROL_EFFECT_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AVAILABLE_EFFECTS, effects, 1);

    // Available antibanding modes (required)
    uint8_t antibanding[] = { ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, antibanding, 1);

    // Available video stabilization modes (required)
    uint8_t video_stab[] = { ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, video_stab, 1);

    // Lens optical stabilization (required)
    uint8_t optical_stab[] = { ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, optical_stab, 1);

    // Color correction aberration modes (required)
    uint8_t cc_aberration[] = { ANDROID_COLOR_CORRECTION_ABERRATION_MODE_OFF };
    add_camera_metadata_entry(metadata, ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, cc_aberration, 1);

    // Available modes
    uint8_t control_modes[] = {
        ANDROID_CONTROL_MODE_AUTO,
        ANDROID_CONTROL_MODE_OFF,
    };
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AVAILABLE_MODES, control_modes, sizeof(control_modes)/sizeof(uint8_t));

    // Test pattern data modes
    uint8_t test_pattern_modes[] = {
        ANDROID_SENSOR_TEST_PATTERN_MODE_OFF,
    };
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, test_pattern_modes, sizeof(test_pattern_modes)/sizeof(uint8_t));

    // Lens focal length (required by buildFastInfo)
    float focal_lengths[] = { 3.5f };  // ~3.5mm typical for tablet cameras
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, focal_lengths, sizeof(focal_lengths)/sizeof(float));

    // Lens aperture (required by some framework paths)
    float apertures[] = { 2.8f };
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_AVAILABLE_APERTURES, apertures, sizeof(apertures)/sizeof(float));

    // Filter density
    float filter_densities[] = { 0.0f };
    add_camera_metadata_entry(metadata, ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, filter_densities, sizeof(filter_densities)/sizeof(float));

    // Max 3A regions
    int32_t max_3a_regions[] = { 1, 1, 0 };  // AE, AWB, AF
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_MAX_REGIONS, max_3a_regions, sizeof(max_3a_regions)/sizeof(int32_t));

    // Request max num output streams
    int32_t max_output_streams[] = { 3, 3, 1 };  // PREVIEW, RECORD, MAX
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, max_output_streams, sizeof(max_output_streams)/sizeof(int32_t));

    // Partial result count
    uint8_t partial_result_count = 1;
    add_camera_metadata_entry(metadata, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partial_result_count, 1);

    // Sync max latency
    int64_t sync_max_latency = ANDROID_SYNC_MAX_LATENCY_PER_FRAME_CONTROL;
    add_camera_metadata_entry(metadata, ANDROID_SYNC_MAX_LATENCY, &sync_max_latency, 1);

    sort_camera_metadata(metadata);
    
    // Debug: dump stream configurations
    camera_metadata_entry_t debug_configs;
    if (find_camera_metadata_entry(metadata, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &debug_configs) == 0 && debug_configs.count > 0) {
        ALOGI("DEBUG: Stream configs count=%zu", debug_configs.count);
        for (size_t i = 0; i < debug_configs.count; i += 4) {
            ALOGI("DEBUG: Config[%zu] format=%d width=%d height=%d input=%d",
                  i/4, debug_configs.data.i32[i], debug_configs.data.i32[i+1],
                  debug_configs.data.i32[i+2], debug_configs.data.i32[i+3]);
        }
    } else {
        ALOGE("DEBUG: No stream configs found!");
    }
    
    // Debug: dump FPS ranges
    camera_metadata_entry_t debug_fps;
    if (find_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &debug_fps) == 0 && debug_fps.count > 0) {
        ALOGI("DEBUG: FPS ranges count=%zu", debug_fps.count);
        for (size_t i = 0; i < debug_fps.count; i += 2) {
            ALOGI("DEBUG: FPS[%zu] min=%d max=%d", i/2, debug_fps.data.i32[i], debug_fps.data.i32[i+1]);
        }
    }
    
    // Debug: dump focal lengths
    camera_metadata_entry_t debug_focal;
    if (find_camera_metadata_entry(metadata, ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, &debug_focal) == 0 && debug_focal.count > 0) {
        ALOGI("DEBUG: Focal lengths count=%zu", debug_focal.count);
        for (size_t i = 0; i < debug_focal.count; i++) {
            ALOGI("DEBUG: Focal[%zu] = %f", i, debug_focal.data.f[i]);
        }
    }
    
    ALOGI("DEBUG: Metadata sorted successfully for camera %d", cameraId);
    
    gCameraCharacteristics[cameraId] = metadata;
    return metadata;
}

static int camera_device_init(const hw_module_t *module, hw_device_t **device) {
    ALOGI("camera_device_init");
    
    mocha_camera_device_t *dev = new mocha_camera_device_t();
    if (!dev) {
        ALOGE("Failed to allocate camera device");
        return -ENOMEM;
    }

    memset(dev, 0, sizeof(mocha_camera_device_t));
    
    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = CAMERA_DEVICE_API_VERSION_3_2;
    dev->common.module = const_cast<hw_module_t *>(module);
    dev->common.close = camera_device_close;
    dev->ops = &camera_device_ops;
    
    dev->camera_id = 0;
    dev->callback_ops = nullptr;
    dev->is_initialized = false;
    dev->streams_configured = false;
    dev->pipeline = nullptr;
    dev->output_stream = nullptr;

    *device = &dev->common;
    
    ALOGI("Camera device initialized");
    return 0;
}

static int camera_device_close(hw_device_t *device) {
    ALOGI("camera_device_close");
    
    if (!device) {
        return -EINVAL;
    }

    mocha_camera_device_t *dev = (mocha_camera_device_t *)device;

    if (dev->pipeline) {
        mocha::CameraPipeline* pipeline = static_cast<mocha::CameraPipeline*>(dev->pipeline);
        pipeline->close();
        delete pipeline;
        dev->pipeline = nullptr;
    }

    delete dev;
    
    ALOGI("Camera device closed");
    return 0;
}

static int camera_device_initialize(const camera3_device_t *device, const camera3_callback_ops_t *ops) {
    ALOGI("camera_device_initialize");
    
    if (!device || !ops) {
        ALOGE("Invalid parameters");
        return -EINVAL;
    }

    mocha_camera_device_t *dev = (mocha_camera_device_t *)device;
    dev->callback_ops = ops;
    dev->is_initialized = true;

    ALOGI("Camera initialized with callbacks");
    return 0;
}

static int camera_device_configure_streams(const camera3_device_t *device, camera3_stream_configuration_t *config) {
    ALOGI("camera_device_configure_streams: num_streams=%d", config->num_streams);
    
    if (!device || !config) {
        ALOGE("Invalid parameters");
        return -EINVAL;
    }

    mocha_camera_device_t *dev = (mocha_camera_device_t *)device;

    if (dev->streams_configured) {
        ALOGI("Streams already configured, closing previous pipeline");
        if (dev->pipeline) {
            mocha::CameraPipeline* pipeline = static_cast<mocha::CameraPipeline*>(dev->pipeline);
            pipeline->close();
            delete pipeline;
            dev->pipeline = nullptr;
        }
    }

    camera3_stream_t* outputStream = nullptr;
    for (uint32_t i = 0; i < config->num_streams; i++) {
        camera3_stream_t *stream = config->streams[i];
        ALOGI("Stream %d: type=%d, width=%d, height=%d, format=%d",
              i, stream->stream_type, stream->width, stream->height, stream->format);
        
        if (stream->width == 0 || stream->height == 0) {
            ALOGE("Invalid stream dimensions");
            return -EINVAL;
        }

        if (stream->stream_type == CAMERA3_STREAM_OUTPUT) {
            outputStream = stream;
        }
    }

    if (!outputStream) {
        ALOGE("No output stream configured");
        return -EINVAL;
    }

    mocha::CameraPipeline* pipeline = new mocha::CameraPipeline();
    if (!pipeline) {
        ALOGE("Failed to create pipeline");
        return -ENOMEM;
    }

    int ret = pipeline->open(dev->camera_id);
    if (ret != 0) {
        ALOGE("Failed to open pipeline: %d (V4L2 device may not be available)", ret);
        delete pipeline;
        pipeline = nullptr;
    }

    if (pipeline) {
        mocha::PipelineConfig pipelineConfig;
        pipelineConfig.width = outputStream->width;
        pipelineConfig.height = outputStream->height;
        pipelineConfig.bayerPattern = (dev->camera_id == 0) ? 2 : 0;
        pipelineConfig.enableISP = true;

        uint32_t v4l2Format = V4L2_PIX_FMT_SBGGR10;
        if (outputStream->format == HAL_PIXEL_FORMAT_YCBCR_420_888 ||
            outputStream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
            v4l2Format = V4L2_PIX_FMT_SBGGR10;
        }

        pipelineConfig.pixelFormat = v4l2Format;

        ret = pipeline->configure(pipelineConfig);
        if (ret != 0) {
            ALOGE("Failed to configure pipeline: %d", ret);
            pipeline->close();
            delete pipeline;
            pipeline = nullptr;
        }

        ret = pipeline->startStreaming();
        if (ret != 0) {
            ALOGE("Failed to start streaming: %d", ret);
            pipeline->close();
            delete pipeline;
            pipeline = nullptr;
        }
    }

    dev->pipeline = pipeline;
    dev->output_stream = outputStream;
    dev->stream_width = outputStream->width;
    dev->stream_height = outputStream->height;
    dev->stream_format = outputStream->format;

    dev->streams_configured = true;
    ALOGI("Streams configured successfully: %dx%d format=%d", 
          dev->stream_width, dev->stream_height, dev->stream_format);
    return 0;
}

static const camera_metadata_t* camera_device_construct_default_request_settings(const camera3_device_t *device, int type) {
    ALOGI("camera_device_construct_default_request_settings: type=%d", type);
    
    if (!device) {
        ALOGE("Null device pointer");
        return nullptr;
    }

    mocha_camera_device_t *dev = (mocha_camera_device_t *)device;
    int cameraId = dev->camera_id;
    
    size_t entry_capacity = 20;
    size_t data_capacity = 80;
    camera_metadata_t* metadata = allocate_camera_metadata(entry_capacity, data_capacity);
    if (!metadata) {
        ALOGE("Failed to allocate metadata");
        return nullptr;
    }

    // Common settings for all templates
    int32_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_MODE, &aeMode, 1);
    int32_t awbMode = ANDROID_CONTROL_AWB_MODE_AUTO;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
    int32_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_MODE, &controlMode, 1);
    int32_t sceneMode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);
    int32_t aeTargetFpsRange[] = {30, 30};
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_TARGET_FPS_RANGE, aeTargetFpsRange, 2);
    int32_t aePrecaptureTrigger = ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &aePrecaptureTrigger, 1);
    int32_t afMode = ANDROID_CONTROL_AF_MODE_OFF;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AF_MODE, &afMode, 1);
    int32_t afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AF_TRIGGER, &afTrigger, 1);
    int32_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_LOCK, &aeLock, 1);
    int32_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);
    int32_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);
    int32_t mode = ANDROID_CONTROL_MODE_AUTO;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_MODE, &mode, 1);
    int32_t videoStabilizationMode = ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    add_camera_metadata_entry(metadata, ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &videoStabilizationMode, 1);
    int32_t edgeMode = ANDROID_EDGE_MODE_OFF;
    add_camera_metadata_entry(metadata, ANDROID_EDGE_MODE, &edgeMode, 1);
    int32_t nrMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
    add_camera_metadata_entry(metadata, ANDROID_NOISE_REDUCTION_MODE, &nrMode, 1);
    int32_t colorCorrectMode = ANDROID_COLOR_CORRECTION_MODE_FAST;
    add_camera_metadata_entry(metadata, ANDROID_COLOR_CORRECTION_MODE, &colorCorrectMode, 1);
    int32_t transformMatrix[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    add_camera_metadata_entry(metadata, ANDROID_COLOR_CORRECTION_TRANSFORM, transformMatrix, 9);
    int32_t gains[] = {1, 0, 1, 0};
    add_camera_metadata_entry(metadata, ANDROID_COLOR_CORRECTION_GAINS, gains, 4);
    int32_t tonemapMode = ANDROID_TONEMAP_MODE_FAST;
    add_camera_metadata_entry(metadata, ANDROID_TONEMAP_MODE, &tonemapMode, 1);
    int32_t shadingMode = ANDROID_SHADING_MODE_FAST;
    add_camera_metadata_entry(metadata, ANDROID_SHADING_MODE, &shadingMode, 1);
    int32_t lensShadingMapMode = ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
    add_camera_metadata_entry(metadata, ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lensShadingMapMode, 1);
    int32_t hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
    add_camera_metadata_entry(metadata, ANDROID_HOT_PIXEL_MODE, &hotPixelMode, 1);
    int32_t faceDetectMode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    add_camera_metadata_entry(metadata, ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);
    int32_t testPatternMode = ANDROID_SENSOR_TEST_PATTERN_MODE_OFF;
    add_camera_metadata_entry(metadata, ANDROID_SENSOR_TEST_PATTERN_MODE, &testPatternMode, 1);

    // Template-specific settings
    switch (type) {
        case CAMERA3_TEMPLATE_PREVIEW:
        case CAMERA3_TEMPLATE_VIDEO_RECORD:
            ALOGI("Using preview/video template");
            break;
        case CAMERA3_TEMPLATE_STILL_CAPTURE: {
            ALOGI("Using still capture template");
            uint8_t jpegQuality = 95;
            add_camera_metadata_entry(metadata, ANDROID_JPEG_QUALITY, &jpegQuality, 1);
            uint8_t thumbnailQuality = 95;
            add_camera_metadata_entry(metadata, ANDROID_JPEG_THUMBNAIL_QUALITY, &thumbnailQuality, 1);
            int32_t thumbnailSize[] = {320, 240};
            add_camera_metadata_entry(metadata, ANDROID_JPEG_THUMBNAIL_SIZE, thumbnailSize, 2);
            break;
        }
        case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
            ALOGI("Using ZSL template");
            break;
        case CAMERA3_TEMPLATE_MANUAL:
            ALOGI("Using manual template");
            aeMode = ANDROID_CONTROL_AE_MODE_OFF;
            add_camera_metadata_entry(metadata, ANDROID_CONTROL_AE_MODE, &aeMode, 1);
            awbMode = ANDROID_CONTROL_AWB_MODE_OFF;
            add_camera_metadata_entry(metadata, ANDROID_CONTROL_AWB_MODE, &awbMode, 1);
            break;
        default:
            ALOGW("Unknown request template %d, using preview defaults", type);
            break;
    }

    sort_camera_metadata(metadata);
    ALOGI("Request template %d created for camera %d (entries=%zu)", type, cameraId, get_camera_metadata_entry_count(metadata));
    return metadata;
}

static int camera_device_process_capture_request(const camera3_device_t *device, camera3_capture_request_t *request) {
    ALOGI("camera_device_process_capture_request: frame_number=%llu", (unsigned long long)request->frame_number);
    
    if (!device || !request) {
        ALOGE("Invalid parameters");
        return -EINVAL;
    }

    mocha_camera_device_t *dev = (mocha_camera_device_t *)device;
    
    if (!dev->is_initialized || !dev->callback_ops) {
        ALOGE("Camera not initialized");
        return -ENOSYS;
    }

    if (request->num_output_buffers < 1 || !request->output_buffers) {
        ALOGE("No output buffers");
        return -EINVAL;
    }

    const camera3_stream_buffer_t& buf = request->output_buffers[0];
    
    if (!buf.buffer) {
        ALOGE("Invalid buffer");
        return -EINVAL;
    }

    int fence = buf.acquire_fence;
    if (fence >= 0) {
        close(fence);
    }

    ALOGI("Processing capture: frame=%llu stream=%dx%d format=%d",
          (unsigned long long)request->frame_number,
          dev->stream_width, dev->stream_height, dev->stream_format);

    // Capture frame from pipeline
    if (dev->pipeline && dev->streams_configured) {
        mocha::CameraPipeline* pipeline = static_cast<mocha::CameraPipeline*>(dev->pipeline);
        
        // Lock gralloc buffer to get CPU access
        buffer_handle_t handle = buf.buffer;
        void* vaddr = nullptr;
        
        // Try to lock the buffer for CPU write access
        const gralloc_module_t* grallocModule = nullptr;
        hw_module_t* module = nullptr;
        
        if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t**)&module) == 0) {
            grallocModule = reinterpret_cast<const gralloc_module_t*>(module);
            
            int usage = GRALLOC_USAGE_SW_WRITE_OFTEN;
            int ret = grallocModule->lock(grallocModule, handle, usage,
                                          0, 0, dev->stream_width, dev->stream_height, &vaddr);
            if (ret == 0 && vaddr) {
                // Capture frame into the locked buffer
                int captureRet = pipeline->captureFrame(static_cast<uint8_t*>(vaddr), dev->stream_format);
                if (captureRet == 0) {
                    ALOGV("Frame captured successfully");
                } else if (captureRet == -EAGAIN) {
                    ALOGV("No buffer available, skipping frame");
                } else {
                    ALOGE("Failed to capture frame: %d", captureRet);
                }
                
                grallocModule->unlock(grallocModule, handle);
            } else {
                ALOGW("Failed to lock gralloc buffer, using fallback");
                // Fallback: just return the buffer without data
            }
        } else {
            ALOGW("Failed to get gralloc module");
        }
    }

    camera3_capture_result_t result;
    memset(&result, 0, sizeof(result));
    result.frame_number = request->frame_number;
    result.result = nullptr;
    result.num_output_buffers = 1;
    result.output_buffers = request->output_buffers;
    result.partial_result = 0;

    dev->callback_ops->process_capture_result(dev->callback_ops, &result);

    ALOGI("Capture request completed: frame=%llu", (unsigned long long)request->frame_number);
    return 0;
}

static void camera_device_dump(const camera3_device_t *device, int fd) {
    ALOGI("camera_device_dump");
    
    if (!device) {
        return;
    }

    dprintf(fd, "Mocha Camera HAL - Device Dump\n");
    dprintf(fd, "================================\n");
    
    mocha_camera_device_t *dev = (mocha_camera_device_t *)device;
    dprintf(fd, "Camera ID: %d\n", dev->camera_id);
    dprintf(fd, "Initialized: %s\n", dev->is_initialized ? "Yes" : "No");
    dprintf(fd, "Streams configured: %s\n", dev->streams_configured ? "Yes" : "No");
}

static int camera_device_flush(const camera3_device_t *device) {
    ALOGI("camera_device_flush");
    return 0;
}

// Camera device open implementation
int MochaCameraHAL::openCamera(int cameraId, hw_device_t **device) {
    ALOGI("openCamera: cameraId=%d", cameraId);

    if (cameraId < 0 || cameraId >= MochaCameraHAL::kNumCameras) {
        ALOGE("Invalid camera ID: %d", cameraId);
        return -EINVAL;
    }

    int ret = camera_device_init(nullptr, device);
    if (ret != 0) {
        ALOGE("Failed to initialize camera device: %d", ret);
        return ret;
    }

    mocha_camera_device_t *dev = (mocha_camera_device_t *)*device;
    dev->camera_id = cameraId;

    ALOGI("Camera %d opened successfully", cameraId);
    return 0;
}

} // namespace mocha

namespace mocha {

// Implementation of MochaCameraHAL
/*static*/ int MochaCameraHAL::getNumberOfCameras() {
    ALOGI("getNumberOfCameras: %d", MochaCameraHAL::kNumCameras);
    return MochaCameraHAL::kNumCameras;
}

/*static*/ int MochaCameraHAL::getCameraInfo(int cameraId, struct camera_info *info) {
    if (cameraId < 0 || cameraId >= MochaCameraHAL::kNumCameras) {
        ALOGE("Invalid camera ID: %d", cameraId);
        return -EINVAL;
    }

    const MochaCameraInfo& cam = MochaCameraHAL::kCameras[cameraId];
    info->facing = cam.facing;
    info->orientation = cam.orientation;
    info->device_version = CAMERA_DEVICE_API_VERSION_3_2;
    info->static_camera_characteristics = init_static_characteristics(cameraId);
    info->resource_cost = 100;
    info->conflicting_devices = nullptr;
    info->conflicting_devices_length = 0;

    ALOGI("Camera info: id=%d, facing=%d, orientation=%d", cameraId, info->facing, info->orientation);

    return 0;
}

} // namespace mocha

// Module entry point
static int get_number_of_cameras() {
    return mocha::MochaCameraHAL::getNumberOfCameras();
}

static int get_camera_info(int camera_id, struct camera_info *info) {
    return mocha::MochaCameraHAL::getCameraInfo(camera_id, info);
}

static int open(const hw_module_t* module, const char* name, hw_device_t** device) {
    ALOGI("Camera HAL open: name=%s module=%p", name, module);

    if (!name) {
        ALOGE("Camera HAL open: null name");
        return -EINVAL;
    }

    int cameraId = atoi(name);
    int ret = mocha::MochaCameraHAL::openCamera(cameraId, device);
    ALOGI("Camera HAL open: returning %d", ret);
    return ret;
}

static hw_module_methods_t methods = {
    .open = open
};

// set_callbacks implementation
static int set_callbacks(const camera_module_callbacks_t *callbacks) {
    ALOGI("set_callbacks called");
    if (callbacks) {
        memcpy(&mocha::gModuleCallbacks, callbacks, sizeof(camera_module_callbacks_t));
    }
    return 0;
}

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = CAMERA_HARDWARE_MODULE_ID,
        .name = "Mocha Camera HAL",
        .author = "Mocha Team",
        .methods = &methods,
    },
    .get_number_of_cameras = get_number_of_cameras,
    .get_camera_info = get_camera_info,
    .set_callbacks = set_callbacks,
    .get_vendor_tag_ops = nullptr,
    .open_legacy = nullptr,
    .set_torch_mode = nullptr,
    .init = nullptr,
};
