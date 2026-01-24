# S02 CLASS CATALOG - simple_vulkan

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## Class Hierarchy

```
SIMPLE_VULKAN
  - Factory for GPU resources
  - No inheritance

VULKAN_CONTEXT
  - Device context wrapper
  - No inheritance

VULKAN_BUFFER
  - GPU buffer wrapper
  - No inheritance

VULKAN_SHADER
  - Shader module wrapper
  - No inheritance

VULKAN_PIPELINE
  - Compute pipeline wrapper
  - No inheritance

VULKAN_IMAGE
  - GPU image wrapper
  - No inheritance
```

## Class Descriptions

### SIMPLE_VULKAN

**Purpose**: Factory facade for creating GPU resources

**Responsibilities**:
- Create Vulkan contexts
- Create GPU buffers, shaders, pipelines, images
- Define buffer usage and image format constants

**Key Features**:
- `create_context`: Initialize Vulkan
- `create_buffer`: Allocate GPU buffer
- `load_shader`: Load SPIR-V shader
- `create_pipeline`: Create compute pipeline
- `create_image`: Create GPU image

### VULKAN_CONTEXT

**Purpose**: Vulkan instance and device wrapper

**Responsibilities**:
- Initialize Vulkan instance
- Select best available GPU
- Provide device information
- Manage device lifecycle

**Key Features**:
- `device_name`: GPU name string
- `vendor_id`: GPU vendor code
- `is_discrete_gpu`: Discrete vs integrated
- `max_workgroup_size`: Compute limits
- `dispose`: Release resources

### VULKAN_BUFFER

**Purpose**: GPU memory buffer wrapper

**Responsibilities**:
- Allocate GPU buffer memory
- Upload data from CPU to GPU
- Download data from GPU to CPU
- Manage buffer lifecycle

**Key Features**:
- `upload`: CPU to GPU transfer
- `download`: GPU to CPU transfer
- `size`: Buffer size in bytes
- `dispose`: Free GPU memory

### VULKAN_SHADER

**Purpose**: SPIR-V compute shader wrapper

**Responsibilities**:
- Load shader from file or memory
- Manage shader module lifecycle

**Key Features**:
- `make`: Load from file path
- `make_from_memory`: Load from buffer
- `dispose`: Release shader

### VULKAN_PIPELINE

**Purpose**: Compute pipeline with bindings

**Responsibilities**:
- Create pipeline from shader
- Bind buffers and images
- Set push constants
- Dispatch compute work
- Synchronization

**Key Features**:
- `bind_buffer`: Bind SSBO
- `bind_image`: Bind image
- `set_push_constants`: Fast uniforms
- `dispatch`: Execute compute
- `wait_idle`: Synchronize

### VULKAN_IMAGE

**Purpose**: GPU image for compute output

**Responsibilities**:
- Create GPU image
- Download image data
- Manage image lifecycle

**Key Features**:
- `width`, `height`: Dimensions
- `format`: Pixel format
- `download`: Get pixel data
- `dispose`: Release image
