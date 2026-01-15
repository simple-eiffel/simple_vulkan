/*
 * simple_vulkan.c - Vulkan GPU compute implementation
 *
 * A simplified Vulkan compute wrapper optimized for SDF ray marching.
 * Handles device selection, memory management, shader loading, and dispatch.
 */

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "simple_vulkan.h"

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

struct svk_context_t {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue compute_queue;
    uint32_t compute_queue_family;
    VkCommandPool command_pool;
    VkDescriptorPool descriptor_pool;

    /* Device info */
    char device_name[256];
    uint32_t vendor_id;
    int is_discrete;
    uint32_t max_workgroup_size;

    /* Staging buffer for transfers */
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    uint64_t staging_size;
};

struct svk_buffer_t {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint64_t size;
    uint32_t usage;
};

struct svk_image_t {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint32_t width;
    uint32_t height;
    uint32_t format;
};

struct svk_shader_t {
    VkShaderModule module;
};

struct svk_pipeline_t {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout desc_layout;
    VkDescriptorSet desc_set;

    /* Bound resources */
    svk_buffer buffers[SVK_MAX_BINDINGS];
    svk_image images[SVK_MAX_BINDINGS];

    /* Push constants */
    uint8_t push_data[128];
    uint32_t push_size;

    /* For SDF helper */
    svk_image output_image;
    uint32_t workgroup_x, workgroup_y;
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static uint32_t find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static int find_compute_queue_family(VkPhysicalDevice device, uint32_t* family) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);

    VkQueueFamilyProperties* props = (VkQueueFamilyProperties*)malloc(count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props);

    for (uint32_t i = 0; i < count; i++) {
        if (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            *family = i;
            free(props);
            return 1;
        }
    }
    free(props);
    return 0;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

svk_context svk_init(void) {
    svk_context ctx = (svk_context)calloc(1, sizeof(struct svk_context_t));
    if (!ctx) return NULL;

    /* Create Vulkan instance */
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "simple_vulkan",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "simple_vulkan",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info
    };

    if (vkCreateInstance(&create_info, NULL, &ctx->instance) != VK_SUCCESS) {
        free(ctx);
        return NULL;
    }

    /* Enumerate physical devices */
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL);
    if (device_count == 0) {
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(device_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices);

    /* Select best device (prefer discrete GPU) */
    int best_score = -1;
    for (uint32_t i = 0; i < device_count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;

        uint32_t family;
        if (!find_compute_queue_family(devices[i], &family)) continue;

        if (score > best_score) {
            best_score = score;
            ctx->physical_device = devices[i];
            ctx->compute_queue_family = family;
            strncpy(ctx->device_name, props.deviceName, sizeof(ctx->device_name) - 1);
            ctx->vendor_id = props.vendorID;
            ctx->is_discrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
            ctx->max_workgroup_size = props.limits.maxComputeWorkGroupInvocations;
        }
    }
    free(devices);

    if (best_score < 0) {
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    /* Create logical device */
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->compute_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info
    };

    if (vkCreateDevice(ctx->physical_device, &device_info, NULL, &ctx->device) != VK_SUCCESS) {
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    vkGetDeviceQueue(ctx->device, ctx->compute_queue_family, 0, &ctx->compute_queue);

    /* Create command pool */
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->compute_queue_family
    };

    if (vkCreateCommandPool(ctx->device, &pool_info, NULL, &ctx->command_pool) != VK_SUCCESS) {
        vkDestroyDevice(ctx->device, NULL);
        vkDestroyInstance(ctx->instance, NULL);
        free(ctx);
        return NULL;
    }

    /* Create descriptor pool */
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 }
    };

    VkDescriptorPoolCreateInfo desc_pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 32,
        .poolSizeCount = 3,
        .pPoolSizes = pool_sizes
    };

    vkCreateDescriptorPool(ctx->device, &desc_pool_info, NULL, &ctx->descriptor_pool);

    return ctx;
}

const char* svk_get_device_name(svk_context ctx) {
    return ctx ? ctx->device_name : "Unknown";
}

uint32_t svk_get_vendor_id(svk_context ctx) {
    return ctx ? ctx->vendor_id : 0;
}

int svk_is_discrete_gpu(svk_context ctx) {
    return ctx ? ctx->is_discrete : 0;
}

uint32_t svk_get_max_workgroup_size(svk_context ctx) {
    return ctx ? ctx->max_workgroup_size : 0;
}

void svk_cleanup(svk_context ctx) {
    if (!ctx) return;

    vkDeviceWaitIdle(ctx->device);

    if (ctx->staging_buffer) {
        vkDestroyBuffer(ctx->device, ctx->staging_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->staging_memory, NULL);
    }

    if (ctx->descriptor_pool) vkDestroyDescriptorPool(ctx->device, ctx->descriptor_pool, NULL);
    if (ctx->command_pool) vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
    if (ctx->device) vkDestroyDevice(ctx->device, NULL);
    if (ctx->instance) vkDestroyInstance(ctx->instance, NULL);

    free(ctx);
}

/* ============================================================================
 * Buffer Management
 * ============================================================================ */

svk_buffer svk_create_buffer(svk_context ctx, uint64_t size, uint32_t usage) {
    if (!ctx || size == 0) return NULL;

    svk_buffer buf = (svk_buffer)calloc(1, sizeof(struct svk_buffer_t));
    if (!buf) return NULL;

    buf->size = size;
    buf->usage = usage;

    VkBufferUsageFlags vk_usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (usage & SVK_BUFFER_STORAGE) vk_usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (usage & SVK_BUFFER_UNIFORM) vk_usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = vk_usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(ctx->device, &buffer_info, NULL, &buf->buffer) != VK_SUCCESS) {
        free(buf);
        return NULL;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->device, buf->buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };

    if (vkAllocateMemory(ctx->device, &alloc_info, NULL, &buf->memory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, buf->buffer, NULL);
        free(buf);
        return NULL;
    }

    vkBindBufferMemory(ctx->device, buf->buffer, buf->memory, 0);

    return buf;
}

int svk_upload_buffer(svk_context ctx, svk_buffer buf, const void* data, uint64_t size, uint64_t offset) {
    if (!ctx || !buf || !data) return 0;
    if (offset + size > buf->size) return 0;

    void* mapped;
    if (vkMapMemory(ctx->device, buf->memory, offset, size, 0, &mapped) != VK_SUCCESS) return 0;
    memcpy(mapped, data, size);
    vkUnmapMemory(ctx->device, buf->memory);

    return 1;
}

int svk_download_buffer(svk_context ctx, svk_buffer buf, void* data, uint64_t size, uint64_t offset) {
    if (!ctx || !buf || !data) return 0;
    if (offset + size > buf->size) return 0;

    void* mapped;
    if (vkMapMemory(ctx->device, buf->memory, offset, size, 0, &mapped) != VK_SUCCESS) return 0;
    memcpy(data, mapped, size);
    vkUnmapMemory(ctx->device, buf->memory);

    return 1;
}

uint64_t svk_buffer_size(svk_buffer buf) {
    return buf ? buf->size : 0;
}

void svk_free_buffer(svk_context ctx, svk_buffer buf) {
    if (!ctx || !buf) return;
    vkDestroyBuffer(ctx->device, buf->buffer, NULL);
    vkFreeMemory(ctx->device, buf->memory, NULL);
    free(buf);
}

/* ============================================================================
 * Image Management
 * ============================================================================ */

svk_image svk_create_image(svk_context ctx, uint32_t width, uint32_t height, uint32_t format) {
    if (!ctx || width == 0 || height == 0) return NULL;

    svk_image img = (svk_image)calloc(1, sizeof(struct svk_image_t));
    if (!img) return NULL;

    img->width = width;
    img->height = height;
    img->format = format;

    VkFormat vk_format = (format == SVK_FORMAT_RGBA32F) ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk_format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(ctx->device, &image_info, NULL, &img->image) != VK_SUCCESS) {
        free(img);
        return NULL;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, img->image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    if (vkAllocateMemory(ctx->device, &alloc_info, NULL, &img->memory) != VK_SUCCESS) {
        vkDestroyImage(ctx->device, img->image, NULL);
        free(img);
        return NULL;
    }

    vkBindImageMemory(ctx->device, img->image, img->memory, 0);

    /* Create image view */
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = vk_format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    if (vkCreateImageView(ctx->device, &view_info, NULL, &img->view) != VK_SUCCESS) {
        vkFreeMemory(ctx->device, img->memory, NULL);
        vkDestroyImage(ctx->device, img->image, NULL);
        free(img);
        return NULL;
    }

    return img;
}

void svk_image_size(svk_image img, uint32_t* width, uint32_t* height) {
    if (img) {
        if (width) *width = img->width;
        if (height) *height = img->height;
    }
}

void svk_free_image(svk_context ctx, svk_image img) {
    if (!ctx || !img) return;
    if (img->view) vkDestroyImageView(ctx->device, img->view, NULL);
    vkFreeMemory(ctx->device, img->memory, NULL);
    vkDestroyImage(ctx->device, img->image, NULL);
    free(img);
}

/* ============================================================================
 * Shader Management
 * ============================================================================ */

svk_shader svk_load_shader(svk_context ctx, const char* spv_path) {
    if (!ctx || !spv_path) return NULL;

    FILE* file = fopen(spv_path, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint32_t* code = (uint32_t*)malloc(size);
    if (!code) {
        fclose(file);
        return NULL;
    }

    fread(code, 1, size, file);
    fclose(file);

    svk_shader shader = svk_load_shader_memory(ctx, code, size);
    free(code);

    return shader;
}

svk_shader svk_load_shader_memory(svk_context ctx, const uint32_t* spirv, uint64_t size) {
    if (!ctx || !spirv || size == 0) return NULL;

    svk_shader shader = (svk_shader)calloc(1, sizeof(struct svk_shader_t));
    if (!shader) return NULL;

    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = spirv
    };

    if (vkCreateShaderModule(ctx->device, &create_info, NULL, &shader->module) != VK_SUCCESS) {
        free(shader);
        return NULL;
    }

    return shader;
}

void svk_free_shader(svk_context ctx, svk_shader shader) {
    if (!ctx || !shader) return;
    vkDestroyShaderModule(ctx->device, shader->module, NULL);
    free(shader);
}

/* ============================================================================
 * Compute Pipeline
 * ============================================================================ */

svk_pipeline svk_create_pipeline(svk_context ctx, svk_shader shader) {
    if (!ctx || !shader) return NULL;

    svk_pipeline pipe = (svk_pipeline)calloc(1, sizeof(struct svk_pipeline_t));
    if (!pipe) return NULL;

    /* Create descriptor set layout */
    VkDescriptorSetLayoutBinding bindings[SVK_MAX_BINDINGS];
    for (int i = 0; i < SVK_MAX_BINDINGS; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        };
    }

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = SVK_MAX_BINDINGS,
        .pBindings = bindings
    };

    vkCreateDescriptorSetLayout(ctx->device, &layout_info, NULL, &pipe->desc_layout);

    /* Allocate descriptor set */
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ctx->descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &pipe->desc_layout
    };

    vkAllocateDescriptorSets(ctx->device, &alloc_info, &pipe->desc_set);

    /* Create pipeline layout with push constants */
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = 128
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &pipe->desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range
    };

    vkCreatePipelineLayout(ctx->device, &pipeline_layout_info, NULL, &pipe->layout);

    /* Create compute pipeline */
    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shader->module,
            .pName = "main"
        },
        .layout = pipe->layout
    };

    if (vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipe->pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(ctx->device, pipe->layout, NULL);
        vkDestroyDescriptorSetLayout(ctx->device, pipe->desc_layout, NULL);
        free(pipe);
        return NULL;
    }

    return pipe;
}

int svk_bind_buffer(svk_pipeline pipe, uint32_t binding, svk_buffer buf) {
    if (!pipe || !buf || binding >= SVK_MAX_BINDINGS) return 0;
    pipe->buffers[binding] = buf;
    return 1;
}

int svk_bind_image(svk_pipeline pipe, uint32_t binding, svk_image img) {
    if (!pipe || !img || binding >= SVK_MAX_BINDINGS) return 0;
    pipe->images[binding] = img;
    return 1;
}

int svk_set_push_constants(svk_pipeline pipe, const void* data, uint32_t size) {
    if (!pipe || !data || size > 128) return 0;
    memcpy(pipe->push_data, data, size);
    pipe->push_size = size;
    return 1;
}

int svk_dispatch(svk_context ctx, svk_pipeline pipe, uint32_t x, uint32_t y, uint32_t z) {
    if (!ctx || !pipe) return 0;

    /* Allocate command buffer */
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    vkBeginCommandBuffer(cmd, &begin_info);

    /* Bind pipeline */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pipeline);

    /* Update and bind descriptor set */
    VkWriteDescriptorSet writes[SVK_MAX_BINDINGS];
    VkDescriptorBufferInfo buffer_infos[SVK_MAX_BINDINGS];
    int write_count = 0;

    for (int i = 0; i < SVK_MAX_BINDINGS; i++) {
        if (pipe->buffers[i]) {
            buffer_infos[write_count] = (VkDescriptorBufferInfo){
                .buffer = pipe->buffers[i]->buffer,
                .offset = 0,
                .range = pipe->buffers[i]->size
            };

            writes[write_count] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = pipe->desc_set,
                .dstBinding = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_infos[write_count]
            };
            write_count++;
        }
    }

    if (write_count > 0) {
        vkUpdateDescriptorSets(ctx->device, write_count, writes, 0, NULL);
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->layout, 0, 1, &pipe->desc_set, 0, NULL);

    /* Push constants */
    if (pipe->push_size > 0) {
        vkCmdPushConstants(cmd, pipe->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pipe->push_size, pipe->push_data);
    }

    /* Dispatch */
    vkCmdDispatch(cmd, x, y, z);

    vkEndCommandBuffer(cmd);

    /* Submit */
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };

    vkQueueSubmit(ctx->compute_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->compute_queue);

    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);

    return 1;
}

void svk_wait_idle(svk_context ctx) {
    if (ctx) vkDeviceWaitIdle(ctx->device);
}

void svk_free_pipeline(svk_context ctx, svk_pipeline pipe) {
    if (!ctx || !pipe) return;
    vkDestroyPipeline(ctx->device, pipe->pipeline, NULL);
    vkDestroyPipelineLayout(ctx->device, pipe->layout, NULL);
    vkDestroyDescriptorSetLayout(ctx->device, pipe->desc_layout, NULL);
    free(pipe);
}

/* ============================================================================
 * Image Download (for getting compute results)
 * ============================================================================ */

int svk_download_image(svk_context ctx, svk_image img, void* data) {
    if (!ctx || !img || !data) return 0;

    uint64_t pixel_size = (img->format == SVK_FORMAT_RGBA32F) ? 16 : 4;
    uint64_t image_size = img->width * img->height * pixel_size;

    /* Create staging buffer if needed */
    if (ctx->staging_size < image_size) {
        if (ctx->staging_buffer) {
            vkDestroyBuffer(ctx->device, ctx->staging_buffer, NULL);
            vkFreeMemory(ctx->device, ctx->staging_memory, NULL);
        }

        VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = image_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        vkCreateBuffer(ctx->device, &buffer_info, NULL, &ctx->staging_buffer);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(ctx->device, ctx->staging_buffer, &mem_reqs);

        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        };

        vkAllocateMemory(ctx->device, &alloc_info, NULL, &ctx->staging_memory);
        vkBindBufferMemory(ctx->device, ctx->staging_buffer, ctx->staging_memory, 0);
        ctx->staging_size = image_size;
    }

    /* Copy image to staging buffer */
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ctx->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    vkBeginCommandBuffer(cmd, &begin_info);

    /* Transition image layout for transfer */
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT
    };

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { img->width, img->height, 1 }
    };

    vkCmdCopyImageToBuffer(cmd, img->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ctx->staging_buffer, 1, &region);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };

    vkQueueSubmit(ctx->compute_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->compute_queue);

    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);

    /* Read from staging buffer */
    void* mapped;
    vkMapMemory(ctx->device, ctx->staging_memory, 0, image_size, 0, &mapped);
    memcpy(data, mapped, image_size);
    vkUnmapMemory(ctx->device, ctx->staging_memory);

    return 1;
}
