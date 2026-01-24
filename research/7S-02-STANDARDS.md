# 7S-02 STANDARDS - simple_vulkan

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## Applicable Standards

### Vulkan Standards
- **Vulkan 1.0**: Core Vulkan specification
- **SPIR-V 1.0**: Shader intermediate representation
- **Vulkan Compute**: Compute pipeline functionality

### GPU Vendor Standards
- **NVIDIA**: Vendor ID 0x10DE
- **AMD**: Vendor ID 0x1002
- **Intel**: Vendor ID 0x8086

### Eiffel Standards
- **ECMA-367**: Eiffel language standard
- **Void Safety**: Full void-safe implementation
- **Inline C**: External C interface pattern

## API Design Standards

### Resource Management
- All GPU resources require explicit `dispose` call
- Invalid handles checked via `is_valid`
- Context required for all GPU operations

### Buffer Types
| Flag | Value | Description |
|------|-------|-------------|
| Buffer_storage | 0x01 | Shader Storage Buffer (SSBO) |
| Buffer_uniform | 0x02 | Uniform Buffer |
| Buffer_transfer | 0x04 | Transfer source/destination |

### Image Formats
| Format | Value | Bytes/Pixel |
|--------|-------|-------------|
| Format_rgba8 | 0x01 | 4 |
| Format_rgba32f | 0x02 | 16 |

## C Interface Standards

All Vulkan operations use inline C externals with `<simple_vulkan.h>` header. Functions follow `svk_` prefix convention.
