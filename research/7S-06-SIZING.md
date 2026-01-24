# 7S-06 SIZING - simple_vulkan


**Date**: 2026-01-23

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## Codebase Metrics

### Source Files
| File | Lines | Purpose |
|------|-------|---------|
| simple_vulkan.e | ~125 | Main facade/factory |
| vulkan_context.e | ~195 | Device context |
| vulkan_buffer.e | ~165 | GPU buffer wrapper |
| vulkan_shader.e | ~115 | Shader module |
| vulkan_pipeline.e | ~205 | Compute pipeline |
| vulkan_image.e | ~165 | GPU image wrapper |
| **Total** | ~970 | |

### Class Count
- 6 production classes
- 2 test classes (test_app.e, lib_tests.e)

## Complexity Analysis

### SIMPLE_VULKAN (Factory)
- Features: ~12
- Cyclomatic complexity: Very Low
- Simple factory methods

### VULKAN_CONTEXT
- Features: ~15
- Cyclomatic complexity: Low
- Initialization and query

### VULKAN_PIPELINE
- Features: ~12
- Cyclomatic complexity: Medium
- Binding and dispatch logic

## Memory Footprint

| Component | Typical Size (Eiffel) | GPU Memory |
|-----------|----------------------|------------|
| VULKAN_CONTEXT | ~100 bytes | Varies |
| VULKAN_BUFFER | ~80 bytes | User specified |
| VULKAN_SHADER | ~60 bytes | Shader size |
| VULKAN_PIPELINE | ~100 bytes | Driver managed |
| VULKAN_IMAGE | ~100 bytes | W*H*BPP |

## Performance Characteristics

| Operation | Expected Time |
|-----------|--------------|
| Create context | 50-500ms |
| Create buffer | 1-10ms |
| Load shader | 5-50ms |
| Create pipeline | 10-100ms |
| Upload/Download | Size dependent |
| Dispatch | Async (GPU) |
