# S06 BOUNDARIES - simple_vulkan

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## System Boundaries

```
+--------------------------------------------------+
|                    Client Code                    |
+--------------------------------------------------+
                        |
                        v
+--------------------------------------------------+
|                  SIMPLE_VULKAN                    |
|  (Factory - create_context, create_buffer, ...)  |
+--------------------------------------------------+
         |         |         |          |
         v         v         v          v
+--------+  +------+  +--------+  +-------+
|CONTEXT |  |BUFFER|  |PIPELINE|  | IMAGE |
+--------+  +------+  +--------+  +-------+
         \      |       /           /
          \     |      /           /
           v    v     v           v
+--------------------------------------------------+
|              C Layer (simple_vulkan.h)            |
|   svk_init, svk_create_buffer, svk_dispatch, ... |
+--------------------------------------------------+
                        |
                        v
+--------------------------------------------------+
|              Vulkan Driver / GPU                  |
+--------------------------------------------------+
```

## Interface Boundaries

### Public Interface (Eiffel)
- SIMPLE_VULKAN: Factory methods
- VULKAN_CONTEXT: Device management
- VULKAN_BUFFER: Data transfer
- VULKAN_SHADER: Shader loading
- VULKAN_PIPELINE: Compute dispatch
- VULKAN_IMAGE: Image output

### C Interface (simple_vulkan.h)
- `svk_init()`: Create context
- `svk_create_buffer()`: Allocate buffer
- `svk_upload_buffer()`, `svk_download_buffer()`: Transfer
- `svk_load_shader()`: Load SPIR-V
- `svk_create_pipeline()`: Create pipeline
- `svk_dispatch()`: Execute compute
- `svk_cleanup()`: Dispose resources

## Data Boundaries

### Input Data
| Data | Source | Validation |
|------|--------|------------|
| SPIR-V shaders | Files | Path existence |
| Buffer data | Client | Size/offset checks |
| Parameters | Client | Preconditions |

### Output Data
| Data | Destination | Format |
|------|-------------|--------|
| Buffer data | Client POINTER | Raw bytes |
| Image data | Client POINTER | RGBA pixels |
| Status | BOOLEAN | Success/failure |

## Trust Boundaries

| Zone | Trust Level | Validation |
|------|-------------|------------|
| Eiffel code | Trusted | Contracts |
| C layer | Trusted | None (internal) |
| SPIR-V shaders | Semi-trusted | Driver validates |
| GPU driver | Trusted | System component |
