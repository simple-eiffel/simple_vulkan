# S05 CONSTRAINTS - simple_vulkan

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## Technical Constraints

### Platform
- **Target OS**: Windows (primary), Linux (potential)
- **GPU**: Vulkan 1.0+ compatible (NVIDIA, AMD, Intel)
- **Compiler**: EiffelStudio 25.02+
- **External**: Vulkan SDK installed

### Vulkan Requirements
- Vulkan 1.0 minimum
- Compute queue support
- SPIR-V 1.0 shaders

### Shader Format
- Pre-compiled SPIR-V only
- Use glslc or similar for GLSL to SPIR-V
- Compute shaders only

## Resource Constraints

### Buffer Limits
| Limit | Typical Value |
|-------|--------------|
| Max buffer size | GPU dependent (~2GB) |
| Max bindings | 8 |
| Push constant size | 128-256 bytes |

### Image Limits
| Limit | Typical Value |
|-------|--------------|
| Max dimensions | 16384 x 16384 |
| Supported formats | RGBA8, RGBA32F |

### Workgroup Limits
| Limit | Typical Value |
|-------|--------------|
| Max workgroup size | 256-1024 |
| Max dispatch | 65535 x 65535 x 65535 |

## Lifecycle Constraints

### Disposal Order
Resources must be disposed in reverse creation order:
1. Images and buffers (created last)
2. Pipelines
3. Shaders
4. Context (created first, disposed last)

### Thread Safety
- All operations assumed single-threaded
- No SCOOP considerations (yet)
- Vulkan command buffers not exposed

## Performance Constraints

| Operation | Expected Overhead |
|-----------|------------------|
| Context creation | 50-500ms (once) |
| Buffer creation | 1-10ms |
| Shader load | 5-50ms |
| Pipeline creation | 10-100ms |
| Dispatch | Minimal CPU, async GPU |
