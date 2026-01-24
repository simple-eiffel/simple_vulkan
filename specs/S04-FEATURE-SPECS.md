# S04 FEATURE SPECS - simple_vulkan

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## SIMPLE_VULKAN Features

### Factory Methods

| Feature | Signature | Description |
|---------|-----------|-------------|
| create_context | : VULKAN_CONTEXT | Initialize Vulkan |
| create_buffer | (CTX, INT64, INT): VULKAN_BUFFER | Allocate buffer |
| load_shader | (CTX, STRING): VULKAN_SHADER | Load SPIR-V |
| create_pipeline | (CTX, SHADER): VULKAN_PIPELINE | Create pipeline |
| create_image | (CTX, INT, INT, INT): VULKAN_IMAGE | Create image |

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| Buffer_storage | 0x01 | SSBO usage |
| Buffer_uniform | 0x02 | Uniform usage |
| Buffer_transfer | 0x04 | Transfer usage |
| Format_rgba8 | 0x01 | 8-bit RGBA |
| Format_rgba32f | 0x02 | 32-bit float RGBA |
| Vendor_nvidia | 0x10DE | NVIDIA |
| Vendor_amd | 0x1002 | AMD |
| Vendor_intel | 0x8086 | Intel |

## VULKAN_CONTEXT Features

| Feature | Signature | Description |
|---------|-----------|-------------|
| handle | POINTER | Opaque context handle |
| is_valid | BOOLEAN | Initialization success |
| device_name | STRING | GPU name |
| vendor_id | INTEGER | Vendor code |
| is_discrete_gpu | BOOLEAN | Discrete vs integrated |
| max_workgroup_size | INTEGER | Compute limit |
| is_nvidia | BOOLEAN | NVIDIA GPU? |
| is_amd | BOOLEAN | AMD GPU? |
| is_intel | BOOLEAN | Intel GPU? |
| dispose | | Release resources |

## VULKAN_BUFFER Features

| Feature | Signature | Description |
|---------|-----------|-------------|
| handle | POINTER | Opaque buffer handle |
| context | VULKAN_CONTEXT | Parent context |
| size | INTEGER_64 | Buffer size bytes |
| usage | INTEGER | Usage flags |
| is_valid | BOOLEAN | Creation success |
| upload | (PTR, INT64, INT64): BOOL | CPU to GPU |
| download | (PTR, INT64, INT64): BOOL | GPU to CPU |
| dispose | | Free buffer |

## VULKAN_SHADER Features

| Feature | Signature | Description |
|---------|-----------|-------------|
| handle | POINTER | Opaque shader handle |
| context | VULKAN_CONTEXT | Parent context |
| is_valid | BOOLEAN | Load success |
| dispose | | Free shader |

## VULKAN_PIPELINE Features

| Feature | Signature | Description |
|---------|-----------|-------------|
| handle | POINTER | Opaque pipeline handle |
| context | VULKAN_CONTEXT | Parent context |
| shader | VULKAN_SHADER | Associated shader |
| is_valid | BOOLEAN | Creation success |
| bind_buffer | (INT, BUFFER): BOOL | Bind SSBO |
| bind_image | (INT, IMAGE): BOOL | Bind image |
| set_push_constants | (PTR, INT): BOOL | Set constants |
| dispatch | (CTX, INT, INT, INT): BOOL | Execute |
| wait_idle | (CTX) | Synchronize |
| dispose | | Free pipeline |

## VULKAN_IMAGE Features

| Feature | Signature | Description |
|---------|-----------|-------------|
| handle | POINTER | Opaque image handle |
| context | VULKAN_CONTEXT | Parent context |
| width | INTEGER | Width pixels |
| height | INTEGER | Height pixels |
| format | INTEGER | Pixel format |
| is_valid | BOOLEAN | Creation success |
| bytes_per_pixel | INTEGER | BPP for format |
| total_bytes | INTEGER | W*H*BPP |
| download | (PTR): BOOL | Get pixel data |
| dispose | | Free image |
