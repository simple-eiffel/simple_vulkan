# S01 PROJECT INVENTORY - simple_vulkan

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## Project Structure

```
simple_vulkan/
  simple_vulkan.ecf         # Eiffel configuration file
  src/
    simple_vulkan.e         # Main facade/factory
    vulkan_context.e        # Device context wrapper
    vulkan_buffer.e         # GPU buffer wrapper
    vulkan_shader.e         # Shader module wrapper
    vulkan_pipeline.e       # Compute pipeline wrapper
    vulkan_image.e          # GPU image wrapper
  c/
    simple_vulkan.h         # C header (Vulkan interface)
    simple_vulkan.c         # C implementation
  testing/
    test_app.e              # Test application entry
    lib_tests.e             # Test cases
  research/                 # 7S research documents
  specs/                    # Specification documents
```

## Source Files

| File | Type | Lines | Description |
|------|------|-------|-------------|
| simple_vulkan.e | Class | ~125 | Factory for GPU resources |
| vulkan_context.e | Class | ~195 | Vulkan device context |
| vulkan_buffer.e | Class | ~165 | GPU buffer management |
| vulkan_shader.e | Class | ~115 | SPIR-V shader wrapper |
| vulkan_pipeline.e | Class | ~205 | Compute pipeline |
| vulkan_image.e | Class | ~165 | GPU image wrapper |

## Test Files

| File | Type | Tests | Description |
|------|------|-------|-------------|
| test_app.e | Root | - | Test application entry point |
| lib_tests.e | Tests | TBD | Library test cases |

## Dependencies

### Internal (simple_* ecosystem)
- None (standalone)

### External
- Vulkan SDK (runtime)
- C compiler (for inline C)

### Standard Library
- base (C_STRING, POINTER)

## Build Targets

| Target | Type | Description |
|--------|------|-------------|
| simple_vulkan | Library | Main library target |
| simple_vulkan_tests | Executable | Test runner |
