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
#include <time.h>
#include "simple_vulkan.h"

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

/* Asynchronous submission (0.7.0): recorded commands go into a ring of slots, each with
   its own command buffer, fence, upload arena, deferred frees and timestamp queries. A
   slot is submitted without waiting every SVK_SUBMIT_EVERY commands, so the GPU runs
   while the host records; a flush submits the open slot and waits for all of them. */
#define SVK_SLOTS 3u
#define SVK_SUBMIT_EVERY 128u
#define SVK_ARENA_CHUNKS 16u

struct svk_slot_t {
    VkCommandBuffer cmd;
    VkFence fence;
    int in_flight;
    struct svk_buffer_t** frees;        /* destroyed once this slot's work has completed */
    uint32_t free_count;
    uint32_t free_capacity;
    VkBuffer arena_buffer[SVK_ARENA_CHUNKS];   /* recorded uploads, reset at completion */
    VkDeviceMemory arena_memory[SVK_ARENA_CHUNKS];
    void* arena_mapped[SVK_ARENA_CHUNKS];
    uint64_t arena_size[SVK_ARENA_CHUNKS];
    uint64_t arena_used[SVK_ARENA_CHUNKS];
    uint32_t arena_count;
    VkQueryPool query_pool;             /* two timestamps per profiled dispatch */
    struct svk_pipeline_t** prof_pipes;
    uint32_t prof_count;
};

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

    /* Compute batches (0.2.0): device-local buffers addressed from shaders,
       dispatches recorded into one command buffer, submitted at a flush. */
    int has_device_address;
    int has_coop_bf16;             /* 0.6.0: bf16 cooperative matrices (tensor cores) enabled */
    VkCommandBuffer batch_cmd;     /* the open slot's command buffer */
    int batch_open;
    uint32_t batch_count;          /* commands in the open slot */
    struct svk_slot_t slots[SVK_SLOTS];
    uint32_t slot_cur;             /* the slot being recorded, or the next to open */
    uint32_t since_flush;          /* commands recorded since the last flush */
    uint64_t flush_count;          /* flushes (full waits) so far: each one drains the GPU */
    uint64_t wait_ns;              /* host time spent waiting on fences */
    VkBuffer xfer_buffer;          /* host-visible staging for device-local transfers */
    VkDeviceMemory xfer_memory;
    void* xfer_mapped;
    uint64_t xfer_size;
    struct svk_buffer_t** pool;         /* recycled device buffers, matched by exact size */
    uint32_t pool_count;
    uint32_t pool_capacity;
    uint64_t pool_bytes;
    uint64_t pool_limit;
    uint64_t pool_hits;
    /* Profiling (0.4.0): a timestamp before and after each recorded dispatch, in the
       open slot's query pool (query_pool and prof_pipes alias the open slot's) */
    int profiling;
    VkQueryPool query_pool;
    struct svk_pipeline_t** prof_pipes;
    uint32_t prof_count;
    double timestamp_period;   /* ns per tick */
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
    uint64_t prof_ns;          /* GPU time across profiled dispatches */
    uint64_t prof_calls;
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

/* Does `device' offer the extension `name'? */
static int svk_device_has_extension(VkPhysicalDevice device, const char* name) {
    uint32_t count = 0;
    int found = 0;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL) != VK_SUCCESS || count == 0) return 0;
    VkExtensionProperties* props = (VkExtensionProperties*) malloc(count * sizeof(VkExtensionProperties));
    if (!props) return 0;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, props) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count && !found; i++) found = strcmp(props[i].extensionName, name) == 0;
    }
    free(props);
    return found;
}

/* Does `device' run 16x16x16 subgroup cooperative matrices with bf16 A and B and f32 C and
   result (the shape the tensor kernels use)? An extension function: found through the instance. */
static int svk_has_bf16_16x16x16(VkInstance instance, VkPhysicalDevice device) {
    PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR get_props = (PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR)
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR");
    uint32_t count = 0;
    int found = 0;
    if (!get_props || get_props(device, &count, NULL) != VK_SUCCESS || count == 0) return 0;
    VkCooperativeMatrixPropertiesKHR* props = (VkCooperativeMatrixPropertiesKHR*) calloc(count, sizeof(VkCooperativeMatrixPropertiesKHR));
    if (!props) return 0;
    for (uint32_t i = 0; i < count; i++) props[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
    if (get_props(device, &count, props) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count && !found; i++) {
            found = props[i].MSize == 16 && props[i].NSize == 16 && props[i].KSize == 16
                && props[i].AType == VK_COMPONENT_TYPE_BFLOAT16_KHR && props[i].BType == VK_COMPONENT_TYPE_BFLOAT16_KHR
                && props[i].CType == VK_COMPONENT_TYPE_FLOAT32_KHR && props[i].ResultType == VK_COMPONENT_TYPE_FLOAT32_KHR
                && props[i].scope == VK_SCOPE_SUBGROUP_KHR;
        }
    }
    free(props);
    return found;
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
        .apiVersion = VK_API_VERSION_1_3   /* 0.6.0: SPIR-V 1.6 kernels (cooperative matrices) */
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
            ctx->timestamp_period = props.limits.timestampPeriod;
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

    /* Enable buffer device addresses when the device offers them (Vulkan 1.2 core), and
       (0.6.0) bf16 cooperative matrices (tensor cores) when it offers the whole set:
       Vulkan 1.3, VK_KHR_cooperative_matrix + VK_KHR_shader_bfloat16, the Vulkan memory
       model, and a 16x16x16 bf16 x bf16 -> f32 subgroup shape. Otherwise nothing extra. */
    VkPhysicalDeviceProperties chosen_props;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &chosen_props);
    VkPhysicalDeviceVulkan12Features supported12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan12Features enable12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR supported_coop = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR };
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR enable_coop = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR };
    VkPhysicalDeviceShaderBfloat16FeaturesKHR supported_bf16 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_BFLOAT16_FEATURES_KHR };
    VkPhysicalDeviceShaderBfloat16FeaturesKHR enable_bf16 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_BFLOAT16_FEATURES_KHR };
    const char* extensions[2];
    uint32_t extension_count = 0;
    int coop = chosen_props.apiVersion >= VK_API_VERSION_1_3
        && svk_device_has_extension(ctx->physical_device, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME)
        && svk_device_has_extension(ctx->physical_device, VK_KHR_SHADER_BFLOAT16_EXTENSION_NAME);
    if (coop) {
        supported12.pNext = &supported_coop;
        supported_coop.pNext = &supported_bf16;
    }
    if (chosen_props.apiVersion >= VK_API_VERSION_1_2) {
        VkPhysicalDeviceFeatures2 features2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supported12 };
        vkGetPhysicalDeviceFeatures2(ctx->physical_device, &features2);
        enable12.bufferDeviceAddress = supported12.bufferDeviceAddress;
        ctx->has_device_address = supported12.bufferDeviceAddress == VK_TRUE;
    }
    coop = coop && supported_coop.cooperativeMatrix && supported_bf16.shaderBFloat16Type
        && supported_bf16.shaderBFloat16CooperativeMatrix && supported12.vulkanMemoryModel
        && svk_has_bf16_16x16x16(ctx->instance, ctx->physical_device);
    if (coop) {
        enable12.vulkanMemoryModel = VK_TRUE;
        enable12.vulkanMemoryModelDeviceScope = supported12.vulkanMemoryModelDeviceScope;
        enable12.pNext = &enable_coop;
        enable_coop.cooperativeMatrix = VK_TRUE;
        enable_coop.pNext = &enable_bf16;
        enable_bf16.shaderBFloat16Type = VK_TRUE;
        enable_bf16.shaderBFloat16CooperativeMatrix = VK_TRUE;
        extensions[extension_count++] = VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_SHADER_BFLOAT16_EXTENSION_NAME;
        ctx->has_coop_bf16 = 1;
    }

    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = (chosen_props.apiVersion >= VK_API_VERSION_1_2) ? &enable12 : NULL,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extension_count ? extensions : NULL
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
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, /* sets return on svk_free_pipeline */
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

    svk_flush(ctx);
    vkDeviceWaitIdle(ctx->device);

    if (ctx->xfer_buffer) {
        vkUnmapMemory(ctx->device, ctx->xfer_memory);
        vkDestroyBuffer(ctx->device, ctx->xfer_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->xfer_memory, NULL);
    }
    for (uint32_t i = 0; i < ctx->pool_count; i++) {
        vkDestroyBuffer(ctx->device, ctx->pool[i]->buffer, NULL);
        vkFreeMemory(ctx->device, ctx->pool[i]->memory, NULL);
        free(ctx->pool[i]);
    }
    free(ctx->pool);
    for (uint32_t k = 0; k < SVK_SLOTS; k++) {     /* the flush above emptied every free list */
        struct svk_slot_t* s = &ctx->slots[k];
        if (s->fence) vkDestroyFence(ctx->device, s->fence, NULL);
        free(s->frees);
        if (s->query_pool) vkDestroyQueryPool(ctx->device, s->query_pool, NULL);
        free(s->prof_pipes);
        for (uint32_t i = 0; i < s->arena_count; i++) {
            vkUnmapMemory(ctx->device, s->arena_memory[i]);
            vkDestroyBuffer(ctx->device, s->arena_buffer[i], NULL);
            vkFreeMemory(ctx->device, s->arena_memory[i], NULL);
        }
    }

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
    svk_flush(ctx);
    if (offset + size > buf->size) return 0;

    void* mapped;
    if (vkMapMemory(ctx->device, buf->memory, offset, size, 0, &mapped) != VK_SUCCESS) return 0;
    memcpy(mapped, data, size);
    vkUnmapMemory(ctx->device, buf->memory);

    return 1;
}

int svk_download_buffer(svk_context ctx, svk_buffer buf, void* data, uint64_t size, uint64_t offset) {
    if (!ctx || !buf || !data) return 0;
    svk_flush(ctx);
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
    svk_flush(ctx);  /* a recorded dispatch may still use it */
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

    if (vkAllocateDescriptorSets(ctx->device, &alloc_info, &pipe->desc_set) != VK_SUCCESS) {
        /* Pool exhausted: report failure rather than hand back a pipeline with no set. */
        vkDestroyDescriptorSetLayout(ctx->device, pipe->desc_layout, NULL);
        free(pipe);
        return NULL;
    }

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
    svk_flush(ctx);  /* keep queue order with recorded work */

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
    svk_flush(ctx);
    if (pipe->desc_set) vkFreeDescriptorSets(ctx->device, ctx->descriptor_pool, 1, &pipe->desc_set);
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
/* ============================================================================
 * Compute batches (0.2.0), submitted asynchronously (0.7.0)
 *
 * Device-local buffers carry a 64-bit device address that shaders read
 * through GL_EXT_buffer_reference, passed in push constants: address
 * pipelines need no descriptor sets. Dispatches, copies and fills are
 * recorded into the open slot's command buffer, each followed by a full
 * compute and transfer memory barrier. Every SVK_SUBMIT_EVERY commands the
 * slot is submitted without waiting and recording moves to the next slot, so
 * the GPU works while the host records. The barrier's scopes cover earlier and
 * later submissions on the queue too, so commands still run in recorded order
 * across slots, and slots complete in order. A slot is reused only after its
 * fence; a flush submits the open slot and waits for every slot. Reads and
 * writes of device-local buffers go through a host-visible staging buffer and
 * flush, so they see all recorded work.
 * ============================================================================ */

#define SVK_XFER_CHUNK (64ull * 1024ull * 1024ull)

static void svk_destroy_buffer_now(svk_context ctx, svk_buffer buf);

/* Profiling queries for slot `s', created on first use. */
static int svk_slot_profiling(svk_context ctx, struct svk_slot_t* s) {
    if (s->query_pool) return 1;
    VkQueryPoolCreateInfo qi = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2 * SVK_SUBMIT_EVERY
    };
    if (vkCreateQueryPool(ctx->device, &qi, NULL, &s->query_pool) != VK_SUCCESS) return 0;
    s->prof_pipes = (struct svk_pipeline_t**) calloc(SVK_SUBMIT_EVERY, sizeof(struct svk_pipeline_t*));
    return s->prof_pipes != NULL;
}

/* Wait for slot `s' if it is in flight, then settle it: add its GPU times to its
   pipelines, reset its upload arena, destroy the buffers freed behind it. */
static int svk_slot_complete(svk_context ctx, struct svk_slot_t* s) {
    int ok = 1;
    if (s->in_flight) {
        struct timespec w0, w1;
        timespec_get(&w0, TIME_UTC);
        ok = vkWaitForFences(ctx->device, 1, &s->fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
        timespec_get(&w1, TIME_UTC);
        ctx->wait_ns += (uint64_t) ((w1.tv_sec - w0.tv_sec) * 1000000000LL + (w1.tv_nsec - w0.tv_nsec));
        vkResetFences(ctx->device, 1, &s->fence);
        s->in_flight = 0;
        if (ok && s->prof_count > 0 && s->query_pool) {
            uint64_t* stamps = (uint64_t*) malloc(sizeof(uint64_t) * 2 * s->prof_count);
            if (stamps && vkGetQueryPoolResults(ctx->device, s->query_pool, 0, 2 * s->prof_count,
                    sizeof(uint64_t) * 2 * s->prof_count, stamps, sizeof(uint64_t),
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
                for (uint32_t i = 0; i < s->prof_count; i++) {
                    svk_pipeline p = s->prof_pipes[i];
                    if (p && stamps[2 * i + 1] >= stamps[2 * i]) {
                        p->prof_ns += (uint64_t) ((double) (stamps[2 * i + 1] - stamps[2 * i]) * ctx->timestamp_period);
                        p->prof_calls++;
                    }
                }
            }
            free(stamps);
        }
    }
    s->prof_count = 0;
    for (uint32_t i = 0; i < s->arena_count; i++) s->arena_used[i] = 0;
    for (uint32_t i = 0; i < s->free_count; i++) svk_destroy_buffer_now(ctx, s->frees[i]);
    s->free_count = 0;
    return ok;
}

static int svk_batch_ensure_open(svk_context ctx) {
    if (ctx->batch_open) return 1;
    struct svk_slot_t* s = &ctx->slots[ctx->slot_cur];
    if (!svk_slot_complete(ctx, s)) return 0;     /* reuse a slot only after its fence */
    if (!s->cmd) {
        VkCommandBufferAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx->command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        if (vkAllocateCommandBuffers(ctx->device, &alloc_info, &s->cmd) != VK_SUCCESS) return 0;
    }
    if (!s->fence) {
        VkFenceCreateInfo fence_info = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (vkCreateFence(ctx->device, &fence_info, NULL, &s->fence) != VK_SUCCESS) return 0;
    }
    if (ctx->profiling && !svk_slot_profiling(ctx, s)) return 0;
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    if (vkBeginCommandBuffer(s->cmd, &begin_info) != VK_SUCCESS) return 0;
    ctx->batch_cmd = s->cmd;
    ctx->batch_open = 1;
    ctx->batch_count = 0;
    ctx->prof_count = 0;
    ctx->query_pool = s->query_pool;
    ctx->prof_pipes = s->prof_pipes;
    if (ctx->profiling) vkCmdResetQueryPool(s->cmd, s->query_pool, 0, 2 * SVK_SUBMIT_EVERY);
    return 1;
}

/* Submit the open slot without waiting; recording moves to the next slot. */
static int svk_submit(svk_context ctx) {
    struct svk_slot_t* s = &ctx->slots[ctx->slot_cur];
    int ok = 0;
    ctx->batch_open = 0;
    s->prof_count = ctx->prof_count;
    ctx->prof_count = 0;
    ctx->batch_count = 0;
    if (vkEndCommandBuffer(s->cmd) == VK_SUCCESS) {
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &s->cmd
        };
        ok = vkQueueSubmit(ctx->compute_queue, 1, &submit_info, s->fence) == VK_SUCCESS;
    }
    s->in_flight = ok;
    if (!ok) svk_slot_complete(ctx, s);   /* nothing will run: settle it now */
    ctx->slot_cur = (ctx->slot_cur + 1) % SVK_SLOTS;
    return ok;
}

static void svk_batch_barrier(svk_context ctx) {
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                         VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                         VK_ACCESS_HOST_READ_BIT
    };
    vkCmdPipelineBarrier(ctx->batch_cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
}

/* The barrier after an ordinary command: device work only (compute and transfer). The
   host-visible variant, svk_batch_barrier, is kept for the copies that a read waits on. */
static void svk_device_barrier(svk_context ctx) {
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                         VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT
    };
    vkCmdPipelineBarrier(ctx->batch_cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
}

static void svk_batch_recorded(svk_context ctx) {
    svk_device_barrier(ctx);
    ctx->batch_count++;
    ctx->since_flush++;
    if (ctx->batch_count >= SVK_SUBMIT_EVERY) svk_submit(ctx);
}

int svk_flush(svk_context ctx) {
    if (!ctx) return 0;
    int ok = 1;
    if (ctx->batch_open || svk_slots_in_flight(ctx) > 0) ctx->flush_count++;   /* one that waits on work */
    if (ctx->batch_open) ok = svk_submit(ctx);
    for (uint32_t i = 0; i < SVK_SLOTS; i++) {          /* oldest first */
        if (!svk_slot_complete(ctx, &ctx->slots[(ctx->slot_cur + i) % SVK_SLOTS])) ok = 0;
    }
    ctx->since_flush = 0;
    return ok;
}

int svk_set_profiling(svk_context ctx, int on) {
    if (!ctx) return 0;
    svk_flush(ctx);   /* a batch is profiled whole or not at all */
    ctx->profiling = on ? 1 : 0;
    return 1;
}

uint64_t svk_pipeline_profiled_ns(svk_pipeline pipe) { return pipe ? pipe->prof_ns : 0; }
uint64_t svk_pipeline_profiled_calls(svk_pipeline pipe) { return pipe ? pipe->prof_calls : 0; }
void svk_pipeline_reset_profile(svk_pipeline pipe) { if (pipe) { pipe->prof_ns = 0; pipe->prof_calls = 0; } }

static void svk_destroy_buffer_now(svk_context ctx, svk_buffer buf) {
    vkDestroyBuffer(ctx->device, buf->buffer, NULL);
    vkFreeMemory(ctx->device, buf->memory, NULL);
    free(buf);
}

void svk_free_buffer_deferred(svk_context ctx, svk_buffer buf) {
    if (!ctx || !buf) return;
        /* Recorded work may use it: free it behind the open slot, or else behind the last
           submitted one still in flight (slots complete in order); with none, free it now. */
    struct svk_slot_t* s = NULL;
    if (ctx->batch_open) {
        s = &ctx->slots[ctx->slot_cur];
    } else {
        struct svk_slot_t* last = &ctx->slots[(ctx->slot_cur + SVK_SLOTS - 1) % SVK_SLOTS];
        if (last->in_flight) s = last;
    }
    if (!s) {
        svk_destroy_buffer_now(ctx, buf);
        return;
    }
    if (s->free_count == s->free_capacity) {
        uint32_t cap = s->free_capacity ? 2 * s->free_capacity : 64;
        struct svk_buffer_t** grown = (struct svk_buffer_t**) realloc(s->frees, cap * sizeof(*grown));
        if (!grown) {                     /* cannot queue: run everything, then free */
            svk_flush(ctx);
            svk_destroy_buffer_now(ctx, buf);
            return;
        }
        s->frees = grown;
        s->free_capacity = cap;
    }
    s->frees[s->free_count++] = buf;
}

uint32_t svk_queued_frees(svk_context ctx) {
    uint32_t n = 0;
    if (ctx) for (uint32_t k = 0; k < SVK_SLOTS; k++) n += ctx->slots[k].free_count;
    return n;
}

uint64_t svk_flush_count(svk_context ctx) {
    return ctx ? ctx->flush_count : 0;
}

uint64_t svk_wait_ns(svk_context ctx) {
    return ctx ? ctx->wait_ns : 0;
}

uint32_t svk_slots_in_flight(svk_context ctx) {
    uint32_t n = 0;
    if (ctx) for (uint32_t k = 0; k < SVK_SLOTS; k++) n += ctx->slots[k].in_flight ? 1u : 0u;
    return n;
}

uint32_t svk_pending_commands(svk_context ctx) {
    return ctx ? ctx->since_flush : 0;   /* recorded since the last flush; some may have run */
}

int svk_has_device_address(svk_context ctx) {
    return ctx ? ctx->has_device_address : 0;
}

int svk_has_cooperative_matrix_bf16(svk_context ctx) {
    return ctx ? ctx->has_coop_bf16 : 0;
}

/* ----------------------------------------------------------------------------
 * Device-buffer pool (0.3.0). A recycled buffer is reusable at once: commands
 * run in recorded order with a full barrier after each, so its new first use
 * always follows its old last use. Matched by exact size (training repeats
 * shapes); bytes above pool_limit go back to the driver (deferred free).
 * ---------------------------------------------------------------------------- */

#define SVK_DEFAULT_POOL_LIMIT (1024ull * 1024ull * 1024ull)

static svk_buffer svk_pool_take(svk_context ctx, uint64_t size) {
    for (uint32_t i = ctx->pool_count; i > 0; i--) {
        svk_buffer b = ctx->pool[i - 1];
        if (b->size == size) {
            ctx->pool[i - 1] = ctx->pool[ctx->pool_count - 1];
            ctx->pool_count--;
            ctx->pool_bytes -= size;
            ctx->pool_hits++;
            return b;
        }
    }
    return NULL;
}

void svk_recycle_buffer(svk_context ctx, svk_buffer buf) {
    if (!ctx || !buf) return;
    uint64_t limit = ctx->pool_limit ? ctx->pool_limit : SVK_DEFAULT_POOL_LIMIT;
    if (!(buf->usage & SVK_BUFFER_DEVICE_LOCAL) || ctx->pool_bytes + buf->size > limit) {
        svk_free_buffer_deferred(ctx, buf);
        return;
    }
    if (ctx->pool_count == ctx->pool_capacity) {
        uint32_t cap = ctx->pool_capacity ? 2 * ctx->pool_capacity : 256;
        struct svk_buffer_t** grown = (struct svk_buffer_t**) realloc(ctx->pool, cap * sizeof(*grown));
        if (!grown) { svk_free_buffer_deferred(ctx, buf); return; }
        ctx->pool = grown;
        ctx->pool_capacity = cap;
    }
    ctx->pool[ctx->pool_count++] = buf;
    ctx->pool_bytes += buf->size;
}

void svk_trim_pool(svk_context ctx) {
    if (!ctx) return;
    while (ctx->pool_count > 0) {
        svk_buffer b = ctx->pool[--ctx->pool_count];
        ctx->pool_bytes -= b->size;
        svk_free_buffer_deferred(ctx, b);
    }
}

void svk_set_pool_limit(svk_context ctx, uint64_t bytes) {
    if (!ctx) return;
    ctx->pool_limit = bytes;
    while (ctx->pool_count > 0 && ctx->pool_bytes > bytes) {
        svk_buffer b = ctx->pool[--ctx->pool_count];
        ctx->pool_bytes -= b->size;
        svk_free_buffer_deferred(ctx, b);
    }
}

uint64_t svk_pool_bytes(svk_context ctx) { return ctx ? ctx->pool_bytes : 0; }
uint64_t svk_pool_hits(svk_context ctx) { return ctx ? ctx->pool_hits : 0; }

svk_buffer svk_create_device_buffer(svk_context ctx, uint64_t size) {
    if (!ctx || size == 0 || !ctx->has_device_address) return NULL;

    svk_buffer pooled = svk_pool_take(ctx, size);
    if (pooled) return pooled;

    svk_buffer buf = (svk_buffer)calloc(1, sizeof(struct svk_buffer_t));
    if (!buf) return NULL;
    buf->size = size;
    buf->usage = SVK_BUFFER_STORAGE | SVK_BUFFER_TRANSFER | SVK_BUFFER_DEVICE_LOCAL;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(ctx->device, &buffer_info, NULL, &buf->buffer) != VK_SUCCESS) {
        free(buf);
        return NULL;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->device, buf->buffer, &mem_reqs);
    uint32_t type = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &flags_info,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = type
    };
    if (type == UINT32_MAX || vkAllocateMemory(ctx->device, &alloc_info, NULL, &buf->memory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, buf->buffer, NULL);
        free(buf);
        return NULL;
    }
    vkBindBufferMemory(ctx->device, buf->buffer, buf->memory, 0);
    return buf;
}

uint64_t svk_buffer_address(svk_context ctx, svk_buffer buf) {
    if (!ctx || !buf || !ctx->has_device_address) return 0;
    VkBufferDeviceAddressInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buf->buffer
    };
    return (uint64_t) vkGetBufferDeviceAddress(ctx->device, &info);
}

static int svk_xfer_ensure(svk_context ctx, uint64_t size) {
    if (ctx->xfer_buffer && ctx->xfer_size >= size) return 1;
    if (ctx->xfer_buffer) {
        vkUnmapMemory(ctx->device, ctx->xfer_memory);
        vkDestroyBuffer(ctx->device, ctx->xfer_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->xfer_memory, NULL);
        ctx->xfer_buffer = VK_NULL_HANDLE;
        ctx->xfer_size = 0;
    }
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(ctx->device, &buffer_info, NULL, &ctx->xfer_buffer) != VK_SUCCESS) return 0;
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->device, ctx->xfer_buffer, &mem_reqs);
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    if (vkAllocateMemory(ctx->device, &alloc_info, NULL, &ctx->xfer_memory) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, ctx->xfer_buffer, NULL);
        ctx->xfer_buffer = VK_NULL_HANDLE;
        return 0;
    }
    vkBindBufferMemory(ctx->device, ctx->xfer_buffer, ctx->xfer_memory, 0);
    if (vkMapMemory(ctx->device, ctx->xfer_memory, 0, size, 0, &ctx->xfer_mapped) != VK_SUCCESS) return 0;
    ctx->xfer_size = size;
    return 1;
}

/* ----------------------------------------------------------------------------
 * Recorded uploads (0.5.0): the data is copied now into a host-visible arena chunk
 * and the copy to `buf' is recorded into the batch like any command, so nothing is
 * flushed. Each slot has its own arena (0.7.0), reset once the slot's work has
 * completed: up to SVK_ARENA_CHUNKS chunks of at least 4 MB.
 * ---------------------------------------------------------------------------- */

#define SVK_ARENA_MIN (4ull * 1024ull * 1024ull)

/* A chunk of the open slot's arena with `size' bytes free, or -1. */
static int svk_arena_chunk(svk_context ctx, uint64_t size) {
    struct svk_slot_t* s = &ctx->slots[ctx->slot_cur];
    for (uint32_t i = 0; i < s->arena_count; i++) {
        if (s->arena_size[i] - s->arena_used[i] >= size) return (int) i;
    }
    if (s->arena_count == SVK_ARENA_CHUNKS) return -1;
    uint32_t k = s->arena_count;
    uint64_t bytes = size > SVK_ARENA_MIN ? size : SVK_ARENA_MIN;
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(ctx->device, &bi, NULL, &s->arena_buffer[k]) != VK_SUCCESS) return -1;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(ctx->device, s->arena_buffer[k], &req);
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_memory_type(ctx->physical_device, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    if (vkAllocateMemory(ctx->device, &ai, NULL, &s->arena_memory[k]) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, s->arena_buffer[k], NULL);
        return -1;
    }
    vkBindBufferMemory(ctx->device, s->arena_buffer[k], s->arena_memory[k], 0);
    if (vkMapMemory(ctx->device, s->arena_memory[k], 0, bytes, 0, &s->arena_mapped[k]) != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, s->arena_buffer[k], NULL);
        vkFreeMemory(ctx->device, s->arena_memory[k], NULL);
        return -1;
    }
    s->arena_size[k] = bytes;
    s->arena_used[k] = 0;
    s->arena_count = k + 1;
    return (int) k;
}

int svk_record_write(svk_context ctx, svk_buffer buf, const void* data, uint64_t size, uint64_t offset) {
    if (!ctx || !buf || !data || size == 0) return 0;
    if (offset + size > buf->size) return 0;
    if (!svk_batch_ensure_open(ctx)) return 0;      /* first: the slot's arena is free only once open */
    int k = svk_arena_chunk(ctx, size);
    if (k < 0) return svk_write_buffer(ctx, buf, data, size, offset);   /* arena exhausted: the flushing path */
    struct svk_slot_t* s = &ctx->slots[ctx->slot_cur];
    uint64_t at = s->arena_used[k];
    memcpy((char*) s->arena_mapped[k] + at, data, (size_t) size);
    s->arena_used[k] = (at + size + 15u) & ~15ull;
    VkBufferCopy region = { .srcOffset = at, .dstOffset = offset, .size = size };
    vkCmdCopyBuffer(ctx->batch_cmd, s->arena_buffer[k], buf->buffer, 1, &region);
    svk_batch_recorded(ctx);
    return 1;
}

int svk_write_buffer(svk_context ctx, svk_buffer buf, const void* data, uint64_t size, uint64_t offset) {
    if (!ctx || !buf || !data) return 0;
    if (offset + size > buf->size) return 0;
    if (!svk_flush(ctx)) return 0;
    uint64_t done = 0;
    while (done < size) {
        uint64_t chunk = size - done;
        if (chunk > SVK_XFER_CHUNK) chunk = SVK_XFER_CHUNK;
        if (!svk_xfer_ensure(ctx, chunk)) return 0;
        memcpy(ctx->xfer_mapped, (const char*) data + done, (size_t) chunk);
        if (!svk_batch_ensure_open(ctx)) return 0;
        VkBufferCopy region = { .srcOffset = 0, .dstOffset = offset + done, .size = chunk };
        vkCmdCopyBuffer(ctx->batch_cmd, ctx->xfer_buffer, buf->buffer, 1, &region);
        svk_batch_barrier(ctx);
        if (!svk_flush(ctx)) return 0;
        done += chunk;
    }
    return 1;
}

int svk_read_buffer(svk_context ctx, svk_buffer buf, void* data, uint64_t size, uint64_t offset) {
    if (!ctx || !buf || !data) return 0;
    if (offset + size > buf->size) return 0;
    uint64_t done = 0;
    while (done < size) {
        uint64_t chunk = size - done;
        if (chunk > SVK_XFER_CHUNK) chunk = SVK_XFER_CHUNK;
        if (!ctx->xfer_buffer || ctx->xfer_size < chunk) {
            if (!svk_flush(ctx)) return 0;   /* the old staging buffer may be in use */
            if (!svk_xfer_ensure(ctx, chunk)) return 0;
        }
        if (!svk_batch_ensure_open(ctx)) return 0;
        VkBufferCopy region = { .srcOffset = offset + done, .dstOffset = 0, .size = chunk };
        vkCmdCopyBuffer(ctx->batch_cmd, buf->buffer, ctx->xfer_buffer, 1, &region);
        svk_batch_barrier(ctx);
        if (!svk_flush(ctx)) return 0;
        memcpy((char*) data + done, ctx->xfer_mapped, (size_t) chunk);
        done += chunk;
    }
    return 1;
}

int svk_record_copy(svk_context ctx, svk_buffer src, uint64_t src_offset, svk_buffer dst, uint64_t dst_offset, uint64_t size) {
    if (!ctx || !src || !dst || size == 0) return 0;
    if (src_offset + size > src->size || dst_offset + size > dst->size) return 0;
    if (!svk_batch_ensure_open(ctx)) return 0;
    VkBufferCopy region = { .srcOffset = src_offset, .dstOffset = dst_offset, .size = size };
    vkCmdCopyBuffer(ctx->batch_cmd, src->buffer, dst->buffer, 1, &region);
    svk_batch_recorded(ctx);
    return 1;
}

int svk_record_fill(svk_context ctx, svk_buffer buf, uint64_t offset, uint64_t size, uint32_t value) {
    if (!ctx || !buf || size == 0 || (offset & 3) || (size & 3)) return 0;
    if (offset + size > buf->size) return 0;
    if (!svk_batch_ensure_open(ctx)) return 0;
    vkCmdFillBuffer(ctx->batch_cmd, buf->buffer, offset, size, value);
    svk_batch_recorded(ctx);
    return 1;
}

svk_pipeline svk_create_address_pipeline(svk_context ctx, svk_shader shader) {
    if (!ctx || !shader) return NULL;
    svk_pipeline pipe = (svk_pipeline)calloc(1, sizeof(struct svk_pipeline_t));
    if (!pipe) return NULL;

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = SVK_MAX_PUSH_BYTES
    };
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range
    };
    if (vkCreatePipelineLayout(ctx->device, &layout_info, NULL, &pipe->layout) != VK_SUCCESS) {
        free(pipe);
        return NULL;
    }
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
        free(pipe);
        return NULL;
    }
    return pipe;
}

int svk_record_dispatch(svk_context ctx, svk_pipeline pipe, const void* push, uint32_t push_size,
                        uint32_t x, uint32_t y, uint32_t z) {
    if (!ctx || !pipe || push_size > SVK_MAX_PUSH_BYTES || (push_size > 0 && !push)) return 0;
    if (pipe->desc_set) return 0;   /* descriptor pipelines use svk_dispatch */
    if (!svk_batch_ensure_open(ctx)) return 0;
    vkCmdBindPipeline(ctx->batch_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pipeline);
    if (push_size > 0) {
        vkCmdPushConstants(ctx->batch_cmd, pipe->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
    }
    if (ctx->profiling) {
        ctx->prof_pipes[ctx->prof_count] = pipe;
        vkCmdWriteTimestamp(ctx->batch_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ctx->query_pool, 2 * ctx->prof_count);
    }
    vkCmdDispatch(ctx->batch_cmd, x, y, z);
    if (ctx->profiling) {
        vkCmdWriteTimestamp(ctx->batch_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ctx->query_pool, 2 * ctx->prof_count + 1);
        ctx->prof_count++;
    }
    svk_batch_recorded(ctx);
    return 1;
}
