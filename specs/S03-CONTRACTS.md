# S03 CONTRACTS - simple_vulkan

**BACKWASH** - Generated: 2026-01-23
**Library**: simple_vulkan

## SIMPLE_VULKAN Contracts

### create_context
```eiffel
create_context: VULKAN_CONTEXT
    ensure
        result_attached: Result /= Void
```

### create_buffer
```eiffel
create_buffer (a_ctx: VULKAN_CONTEXT; a_size: INTEGER_64; a_usage: INTEGER): VULKAN_BUFFER
    require
        ctx_valid: a_ctx /= Void and then a_ctx.is_valid
        positive_size: a_size > 0
    ensure
        result_attached: Result /= Void
```

### load_shader
```eiffel
load_shader (a_ctx: VULKAN_CONTEXT; a_spv_path: STRING): VULKAN_SHADER
    require
        ctx_valid: a_ctx /= Void and then a_ctx.is_valid
        path_attached: a_spv_path /= Void
    ensure
        result_attached: Result /= Void
```

### create_pipeline
```eiffel
create_pipeline (a_ctx: VULKAN_CONTEXT; a_shader: VULKAN_SHADER): VULKAN_PIPELINE
    require
        ctx_valid: a_ctx /= Void and then a_ctx.is_valid
        shader_valid: a_shader /= Void and then a_shader.is_valid
    ensure
        result_attached: Result /= Void
```

## VULKAN_CONTEXT Contracts

### make
```eiffel
make
    ensure
        valid_implies_handle: is_valid implies handle /= default_pointer
```

### device_name
```eiffel
device_name: STRING
    require
        valid: is_valid
    ensure
        result_attached: Result /= Void
```

### dispose
```eiffel
dispose
    ensure
        disposed: not is_valid
        handle_cleared: handle = default_pointer
```

## VULKAN_BUFFER Contracts

### upload
```eiffel
upload (a_data: POINTER; a_size: INTEGER_64; a_offset: INTEGER_64): BOOLEAN
    require
        valid: is_valid
        data_attached: a_data /= default_pointer
        valid_size: a_size > 0
        valid_range: a_offset >= 0 and then a_offset + a_size <= size
```

### download
```eiffel
download (a_data: POINTER; a_size: INTEGER_64; a_offset: INTEGER_64): BOOLEAN
    require
        valid: is_valid
        data_attached: a_data /= default_pointer
        valid_size: a_size > 0
        valid_range: a_offset >= 0 and then a_offset + a_size <= size
```

## VULKAN_PIPELINE Contracts

### bind_buffer
```eiffel
bind_buffer (a_binding: INTEGER; a_buffer: VULKAN_BUFFER): BOOLEAN
    require
        valid: is_valid
        buffer_valid: a_buffer /= Void and then a_buffer.is_valid
        valid_binding: a_binding >= 0 and a_binding < Max_bindings
```

### dispatch
```eiffel
dispatch (a_ctx: VULKAN_CONTEXT; a_x, a_y, a_z: INTEGER): BOOLEAN
    require
        valid: is_valid
        ctx_valid: a_ctx /= Void and then a_ctx.is_valid
        positive_counts: a_x > 0 and a_y > 0 and a_z > 0
```

## Class Invariants

### All Resource Classes
```eiffel
invariant
    valid_handle: is_valid implies handle /= default_pointer
    context_attached: context /= Void  -- (for child resources)
```
