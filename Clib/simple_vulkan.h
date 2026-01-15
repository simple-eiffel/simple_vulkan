/*
 * simple_vulkan.h - Vulkan GPU compute wrapper for Eiffel
 *
 * Provides a simplified C API for GPU compute operations,
 * hiding Vulkan complexity while enabling high-performance
 * parallel computation on NVIDIA, AMD, and Intel GPUs.
 *
 * Usage:
 *   1. svk_init() - Initialize Vulkan
 *   2. svk_create_buffer() - Create GPU buffers
 *   3. svk_load_shader() - Load compute shader
 *   4. svk_create_pipeline() - Create compute pipeline
 *   5. svk_dispatch() - Execute compute shader
 *   6. svk_cleanup() - Release resources
 */

#ifndef SIMPLE_VULKAN_H
#define SIMPLE_VULKAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ============================================================================
 * Opaque Handles
 * ============================================================================ */

typedef struct svk_context_t* svk_context;
typedef struct svk_buffer_t* svk_buffer;
typedef struct svk_shader_t* svk_shader;
typedef struct svk_pipeline_t* svk_pipeline;
typedef struct svk_image_t* svk_image;

/* ============================================================================
 * Initialization
 * ============================================================================ */

/* Initialize Vulkan context. Returns NULL on failure. */
svk_context svk_init(void);

/* Get device name (e.g., "NVIDIA GeForce RTX 5070 Ti") */
const char* svk_get_device_name(svk_context ctx);

/* Get device vendor ID: 0x10DE=NVIDIA, 0x1002=AMD, 0x8086=Intel */
uint32_t svk_get_vendor_id(svk_context ctx);

/* Check if device is discrete GPU (vs integrated) */
int svk_is_discrete_gpu(svk_context ctx);

/* Get maximum workgroup size (typically 256-1024) */
uint32_t svk_get_max_workgroup_size(svk_context ctx);

/* Cleanup and release all resources */
void svk_cleanup(svk_context ctx);

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

/* Buffer usage flags */
#define SVK_BUFFER_STORAGE   0x01  /* Shader storage buffer (SSBO) */
#define SVK_BUFFER_UNIFORM   0x02  /* Uniform buffer */
#define SVK_BUFFER_TRANSFER  0x04  /* Transfer source/destination */

/* Create GPU buffer. Returns NULL on failure. */
svk_buffer svk_create_buffer(svk_context ctx, uint64_t size, uint32_t usage);

/* Upload data from CPU to GPU buffer */
int svk_upload_buffer(svk_context ctx, svk_buffer buf, const void* data, uint64_t size, uint64_t offset);

/* Download data from GPU to CPU buffer */
int svk_download_buffer(svk_context ctx, svk_buffer buf, void* data, uint64_t size, uint64_t offset);

/* Get buffer size */
uint64_t svk_buffer_size(svk_buffer buf);

/* Free buffer */
void svk_free_buffer(svk_context ctx, svk_buffer buf);

/* ============================================================================
 * Image/Texture Management (for compute shader output)
 * ============================================================================ */

/* Image format */
#define SVK_FORMAT_RGBA8     0x01  /* 8-bit RGBA */
#define SVK_FORMAT_RGBA32F   0x02  /* 32-bit float RGBA */

/* Create GPU image for compute shader output */
svk_image svk_create_image(svk_context ctx, uint32_t width, uint32_t height, uint32_t format);

/* Download image data to CPU */
int svk_download_image(svk_context ctx, svk_image img, void* data);

/* Get image dimensions */
void svk_image_size(svk_image img, uint32_t* width, uint32_t* height);

/* Free image */
void svk_free_image(svk_context ctx, svk_image img);

/* ============================================================================
 * Shader Management
 * ============================================================================ */

/* Load compute shader from SPIR-V file */
svk_shader svk_load_shader(svk_context ctx, const char* spv_path);

/* Load compute shader from SPIR-V memory */
svk_shader svk_load_shader_memory(svk_context ctx, const uint32_t* spirv, uint64_t size);

/* Free shader */
void svk_free_shader(svk_context ctx, svk_shader shader);

/* ============================================================================
 * Compute Pipeline
 * ============================================================================ */

/* Maximum bindings per pipeline */
#define SVK_MAX_BINDINGS 8

/* Binding type */
#define SVK_BINDING_BUFFER  0x01
#define SVK_BINDING_IMAGE   0x02

/* Create compute pipeline from shader */
svk_pipeline svk_create_pipeline(svk_context ctx, svk_shader shader);

/* Bind buffer to pipeline at binding index */
int svk_bind_buffer(svk_pipeline pipe, uint32_t binding, svk_buffer buf);

/* Bind image to pipeline at binding index */
int svk_bind_image(svk_pipeline pipe, uint32_t binding, svk_image img);

/* Set push constant data (small, fast-changing uniforms) */
int svk_set_push_constants(svk_pipeline pipe, const void* data, uint32_t size);

/* Dispatch compute shader (workgroup counts) */
int svk_dispatch(svk_context ctx, svk_pipeline pipe, uint32_t x, uint32_t y, uint32_t z);

/* Wait for GPU to finish */
void svk_wait_idle(svk_context ctx);

/* Free pipeline */
void svk_free_pipeline(svk_context ctx, svk_pipeline pipe);

/* ============================================================================
 * SDF-Specific Helpers (convenience functions for simple_sdf)
 * ============================================================================ */

/* Push constants structure for SDF ray marching */
typedef struct {
    float camera_pos[3];
    float camera_yaw;
    float camera_pitch;
    float time;
    float _padding[2];  /* Alignment */
} svk_sdf_push_constants;

/* Create a ready-to-use SDF ray marching pipeline */
svk_pipeline svk_create_sdf_pipeline(svk_context ctx, const char* shader_path,
                                      uint32_t width, uint32_t height);

/* Render SDF scene to image */
int svk_render_sdf(svk_context ctx, svk_pipeline pipe,
                   float cam_x, float cam_y, float cam_z,
                   float cam_yaw, float cam_pitch, float time);

#ifdef __cplusplus
}
#endif

#endif /* SIMPLE_VULKAN_H */
