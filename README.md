<p align="center">
  <img src="https://raw.githubusercontent.com/simple-eiffel/simple_vulkan/master/docs/images/logo.svg" alt="simple_ library logo" width="400">
</p>

# simple_vulkan

**[Documentation](https://simple-eiffel.github.io/simple_vulkan/)** | **[GitHub](https://github.com/simple-eiffel/simple_vulkan)**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Eiffel](https://img.shields.io/badge/Eiffel-25.02-blue.svg)](https://www.eiffel.org/)
[![Design by Contract](https://img.shields.io/badge/DbC-enforced-orange.svg)]()

Vulkan GPU compute library for Eiffel with cross-platform GPU acceleration.

Part of the [Simple Eiffel](https://github.com/simple-eiffel) ecosystem.

## Status

**Production** - 7 tests passing, supports NVIDIA, AMD, and Intel GPUs

## Overview

SIMPLE_VULKAN provides GPU compute capabilities through Vulkan, enabling high-performance parallel computation on modern GPUs. Designed for compute-intensive tasks like SDF ray marching, it achieves **63 FPS at 4K resolution** on supported hardware.

## Quick Start

```eiffel
local
    vk: SIMPLE_VULKAN
    ctx: VULKAN_CONTEXT
do
    create vk
    ctx := vk.create_context

    if ctx.is_valid then
        print ("GPU: " + ctx.device_name)
        print ("Discrete: " + ctx.is_discrete_gpu.out)

        -- Create buffers, load shaders, dispatch compute...

        ctx.dispose
    end
end
```

## Features

- **Cross-Platform GPU** - Works on NVIDIA, AMD, and Intel GPUs via Vulkan
- **Automatic Device Selection** - Prioritizes discrete GPUs over integrated
- **Buffer Management** - Create, upload, and download GPU buffers
- **Compute Shaders** - Load and execute SPIR-V compute shaders
- **Image Output** - Create GPU images for rendering results
- **Push Constants** - Fast-changing uniforms for real-time applications
- **Vendor Detection** - Query GPU vendor for vendor-specific optimizations

## Installation

1. **Install Vulkan SDK** from https://vulkan.lunarg.com/sdk/home

2. Set the ecosystem environment variable:
```
SIMPLE_EIFFEL=D:\prod
```

3. Build the C library (one-time):
```batch
cd D:\prod\simple_vulkan
build_clib.bat
```

4. Add to ECF:
```xml
<library name="simple_vulkan" location="$SIMPLE_EIFFEL/simple_vulkan/simple_vulkan.ecf"/>
```

## GPU Compute Example

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

            -- Create and bind buffers
            input_buf := vk.create_buffer (ctx, 4096, vk.Buffer_storage)
            output_buf := vk.create_buffer (ctx, 4096, vk.Buffer_storage)
            pipeline.bind_buffer (0, input_buf)
            pipeline.bind_buffer (1, output_buf)

            -- Upload data, dispatch, download results
            input_buf.upload (data.item, 4096, 0)
            pipeline.dispatch (ctx, 64, 1, 1)
            pipeline.wait_idle (ctx)
            output_buf.download (result.item, 4096, 0)

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

## Writing Compute Shaders

Compute shaders must be GLSL compiled to SPIR-V:

```glsl
#version 450
layout(local_size_x = 64) in;

layout(std430, binding = 0) buffer InputBuffer { float data[]; };
layout(std430, binding = 1) buffer OutputBuffer { float result[]; };

void main() {
    uint idx = gl_GlobalInvocationID.x;
    result[idx] = data[idx] * 2.0;
}
```

Compile with:
```batch
%VULKAN_SDK%\Bin\glslc.exe shader.comp -o shader.spv
```

## Dependencies

- Vulkan SDK (https://vulkan.lunarg.com/)
- No Eiffel library dependencies

## Performance

Tested on NVIDIA GeForce RTX 5070 Ti:
- Context creation: <10ms
- Buffer operations: Near-instant
- 4K SDF ray marching: 63 FPS

## License

MIT License
