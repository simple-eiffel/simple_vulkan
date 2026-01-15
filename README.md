# simple_vulkan

Vulkan GPU compute library for the Simple Eiffel ecosystem. Provides cross-platform GPU acceleration for NVIDIA, AMD, and Intel GPUs.

## Overview

simple_vulkan wraps Vulkan's compute shader capabilities in a simple, Eiffel-friendly API. It's designed for high-performance parallel computation, particularly SDF (Signed Distance Field) ray marching for real-time visualization.

## Features

- **Cross-platform GPU support**: Works with NVIDIA, AMD, and Intel GPUs
- **Automatic device selection**: Prioritizes discrete GPUs over integrated
- **Simple buffer management**: Easy upload/download between CPU and GPU
- **Compute shader support**: Load and execute SPIR-V compute shaders
- **Image output**: Create GPU images for rendering results
- **Push constants**: Fast-changing uniforms for real-time applications

## Prerequisites

### Windows

1. **Vulkan SDK** (required)
   - Download from: https://vulkan.lunarg.com/sdk/home
   - Version: 1.3.x or later recommended
   - The installer sets the `VULKAN_SDK` environment variable automatically

2. **Visual Studio Build Tools** (for compiling C library)
   - Visual Studio 2019 or 2022 with "Desktop development with C++" workload
   - Or standalone "Build Tools for Visual Studio"

3. **GPU with Vulkan support**
   - Most GPUs from 2016 or later support Vulkan
   - Check: https://vulkan.gpuinfo.org/

### Linux (Future)

```bash
# Ubuntu/Debian
sudo apt install vulkan-sdk libvulkan-dev

# Fedora
sudo dnf install vulkan-loader-devel vulkan-headers
```

## Building

### Step 1: Build the C Library

Open "x64 Native Tools Command Prompt for VS 2022" and run:

```batch
cd D:\prod\simple_vulkan
build_clib.bat
```

This creates `Clib\simple_vulkan.lib`.

### Step 2: Set Environment Variable

Ensure `SIMPLE_EIFFEL` points to your simple_* library root:

```batch
set SIMPLE_EIFFEL=D:\prod
```

### Step 3: Compile with EiffelStudio

```batch
ec -batch -config simple_vulkan.ecf -target simple_vulkan_tests -c_compile
```

### Step 4: Run Tests

```batch
EIFGENs\simple_vulkan_tests\W_code\simple_vulkan_tests.exe
```

## Usage

### Basic Example

```eiffel
local
    vk: SIMPLE_VULKAN
    ctx: VULKAN_CONTEXT
    buf: VULKAN_BUFFER
do
    -- Initialize
    create vk
    ctx := vk.create_context

    if ctx.is_valid then
        -- Print device info
        print ("GPU: " + ctx.device_name + "%N")
        print ("Discrete: " + ctx.is_discrete_gpu.out + "%N")

        -- Create a storage buffer
        buf := vk.create_buffer (ctx, 1024, vk.Buffer_storage)

        if buf.is_valid then
            -- Use buffer...
            buf.dispose
        end

        ctx.dispose
    else
        print ("Vulkan initialization failed%N")
    end
end
```

### GPU Compute Example

```eiffel
local
    vk: SIMPLE_VULKAN
    ctx: VULKAN_CONTEXT
    shader: VULKAN_SHADER
    pipeline: VULKAN_PIPELINE
    input_buf, output_buf: VULKAN_BUFFER
do
    create vk
    ctx := vk.create_context

    if ctx.is_valid then
        -- Load compute shader (SPIR-V)
        shader := vk.load_shader (ctx, "compute.spv")

        if shader.is_valid then
            -- Create pipeline
            pipeline := vk.create_pipeline (ctx, shader)

            -- Create buffers
            input_buf := vk.create_buffer (ctx, 4096, vk.Buffer_storage)
            output_buf := vk.create_buffer (ctx, 4096, vk.Buffer_storage)

            -- Bind to pipeline
            pipeline.bind_buffer (0, input_buf)
            pipeline.bind_buffer (1, output_buf)

            -- Upload input data
            -- input_buf.upload (...)

            -- Execute compute shader
            -- Workgroups: 64 groups of 64 threads = 4096 elements
            pipeline.dispatch (ctx, 64, 1, 1)
            pipeline.wait_idle (ctx)

            -- Download results
            -- output_buf.download (...)

            -- Cleanup
            pipeline.dispose
            input_buf.dispose
            output_buf.dispose
            shader.dispose
        end

        ctx.dispose
    end
end
```

## API Reference

### SIMPLE_VULKAN (Facade)

| Feature | Description |
|---------|-------------|
| `create_context` | Initialize Vulkan and select GPU |
| `create_buffer (ctx, size, usage)` | Create GPU buffer |
| `create_image (ctx, w, h, format)` | Create GPU image |
| `load_shader (ctx, path)` | Load SPIR-V shader |
| `create_pipeline (ctx, shader)` | Create compute pipeline |

### VULKAN_CONTEXT

| Feature | Description |
|---------|-------------|
| `is_valid` | Check if initialization succeeded |
| `device_name` | GPU name (e.g., "NVIDIA GeForce RTX 5070 Ti") |
| `vendor_id` | Vendor ID (0x10DE=NVIDIA, 0x1002=AMD, 0x8086=Intel) |
| `is_discrete_gpu` | True if discrete (vs integrated) |
| `max_workgroup_size` | Max compute threads per workgroup |
| `dispose` | Release resources |

### VULKAN_BUFFER

| Feature | Description |
|---------|-------------|
| `is_valid` | Check if creation succeeded |
| `size` | Buffer size in bytes |
| `upload (data, size, offset)` | Copy CPU → GPU |
| `download (data, size, offset)` | Copy GPU → CPU |
| `dispose` | Free buffer |

### VULKAN_PIPELINE

| Feature | Description |
|---------|-------------|
| `is_valid` | Check if creation succeeded |
| `bind_buffer (binding, buf)` | Bind buffer to slot |
| `bind_image (binding, img)` | Bind image to slot |
| `set_push_constants (data, size)` | Set fast uniforms |
| `dispatch (ctx, x, y, z)` | Execute compute shader |
| `wait_idle (ctx)` | Wait for GPU completion |
| `dispose` | Free pipeline |

### Buffer Usage Flags

| Flag | Value | Description |
|------|-------|-------------|
| `Buffer_storage` | 0x01 | Shader storage buffer (SSBO) |
| `Buffer_uniform` | 0x02 | Uniform buffer |
| `Buffer_transfer` | 0x04 | Transfer source/destination |

### Image Formats

| Format | Value | Bytes/Pixel | Description |
|--------|-------|-------------|-------------|
| `Format_rgba8` | 0x01 | 4 | 8-bit RGBA |
| `Format_rgba32f` | 0x02 | 16 | 32-bit float RGBA |

## Writing Compute Shaders

Compute shaders must be written in GLSL and compiled to SPIR-V.

### Example Shader (compute.comp)

```glsl
#version 450

layout(local_size_x = 64) in;

layout(std430, binding = 0) buffer InputBuffer {
    float input_data[];
};

layout(std430, binding = 1) buffer OutputBuffer {
    float output_data[];
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    output_data[idx] = input_data[idx] * 2.0;
}
```

### Compiling to SPIR-V

```batch
%VULKAN_SDK%\Bin\glslc.exe compute.comp -o compute.spv
```

## Integration with simple_sdf

simple_vulkan provides GPU acceleration for simple_sdf's ray marching:

```eiffel
-- See simple_sdf/demo/sdf_vulkan_demo.e for full example
```

## Troubleshooting

### "Vulkan initialization failed"

1. Check GPU supports Vulkan: https://vulkan.gpuinfo.org/
2. Update GPU drivers
3. Verify Vulkan SDK installation: Run `vulkaninfo` from command prompt

### "Cannot open vulkan-1.lib"

Ensure `VULKAN_SDK` environment variable is set correctly.

### "simple_vulkan.lib not found"

Run `build_clib.bat` first to compile the C library.

## License

Part of the Simple Eiffel ecosystem. See LICENSE file for details.

## See Also

- [simple_sdf](../simple_sdf/) - SDF primitives and ray marching
- [Vulkan SDK](https://vulkan.lunarg.com/) - Official Vulkan development tools
- [SPIR-V](https://www.khronos.org/spir/) - Shader binary format
