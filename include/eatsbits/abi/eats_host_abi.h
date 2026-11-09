#ifndef EATS_HOST_ABI_H
#define EATS_HOST_ABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EATS_HOST_ABI_VERSION_MAJOR 1
#define EATS_HOST_ABI_VERSION_MINOR 0
#define EATS_HOST_ABI_VERSION ((EATS_HOST_ABI_VERSION_MAJOR << 16) | EATS_HOST_ABI_VERSION_MINOR)

/**
 * Universal ABI primitive and composite data types.
 */
typedef enum EatsAbiType {
    EATS_ABI_TYPE_VOID     = 0,
    EATS_ABI_TYPE_BOOL     = 1,
    EATS_ABI_TYPE_I32      = 2,
    EATS_ABI_TYPE_I64      = 3,
    EATS_ABI_TYPE_U32      = 4,
    EATS_ABI_TYPE_U64      = 5,
    EATS_ABI_TYPE_F32      = 6,
    EATS_ABI_TYPE_F64      = 7,
    EATS_ABI_TYPE_STRING   = 8,
    EATS_ABI_TYPE_POINTER  = 9,
    EATS_ABI_TYPE_BUFFER   = 10,
    EATS_ABI_TYPE_STRUCT   = 11,
    EATS_ABI_TYPE_U8       = 12,
    EATS_ABI_TYPE_I8       = 13
} EatsAbiType;

/**
 * Universal ABI status and return error codes.
 */
typedef enum EatsAbiStatus {
    EATS_ABI_OK                  = 0,
    EATS_ABI_ERROR_GENERIC       = -1,
    EATS_ABI_ERROR_NOT_FOUND     = -2,
    EATS_ABI_ERROR_INVALID_ARG   = -3,
    EATS_ABI_ERROR_TYPE_MISMATCH = -4,
    EATS_ABI_ERROR_BUFFER_TOO_SMALL = -5,
    EATS_ABI_ERROR_UNSUPPORTED   = -6
} EatsAbiStatus;

/**
 * Memory buffer access and ownership flags.
 */
enum EatsBufferFlags {
    EATS_BUFFER_READABLE  = 1 << 0,
    EATS_BUFFER_WRITABLE  = 1 << 1,
    EATS_BUFFER_READWRITE = (1 << 0) | (1 << 1),
    EATS_BUFFER_OWNED     = 1 << 2
};

/**
 * Generic contiguous linear memory buffer descriptor.
 * Decoupled from audio-specific planar buffers.
 */
typedef struct EatsMemoryBuffer {
    void* data;
    size_t sizeBytes;
    size_t elementSize;
    uint32_t elementType;   /* EatsAbiType */
    uint32_t flags;         /* EatsBufferFlags */
} EatsMemoryBuffer;

struct EatsStructDescriptor;

/**
 * C-compatible tagged union representing a generic ABI value.
 */
typedef struct EatsAbiValue {
    uint32_t type;  /* EatsAbiType */
    uint32_t flags;
    union {
        uint8_t          boolean;
        int32_t          i32;
        int64_t          i64;
        uint32_t         u32;
        uint64_t         u64;
        float            f32;
        double           f64;
        const char*      stringVal;
        void*            ptr;
        EatsMemoryBuffer buffer;
        struct {
            void* structPtr;
            const struct EatsStructDescriptor* descriptor;
        } structVal;
    } as;
} EatsAbiValue;

/**
 * Field reflection descriptor for C structs.
 */
typedef struct EatsFieldDescriptor {
    const char* name;
    uint32_t type;  /* EatsAbiType */
    size_t offset;
    size_t size;
    const struct EatsStructDescriptor* nestedStruct;
} EatsFieldDescriptor;

/**
 * Struct reflection descriptor for generic host/guest communication.
 */
typedef struct EatsStructDescriptor {
    const char* name;
    size_t structSize;
    uint32_t fieldCount;
    const EatsFieldDescriptor* fields;
} EatsStructDescriptor;

/**
 * Generic host function callback signature.
 * Returns EATS_ABI_OK (0) on success, or negative error code on failure.
 */
typedef int32_t (*EatsHostFunctionCallback)(void* context,
                                            const EatsAbiValue* args,
                                            uint32_t numArgs,
                                            EatsAbiValue* outResult);

/**
 * Descriptor describing an invokable host function.
 */
typedef struct EatsFunctionDescriptor {
    const char* name;
    const char* docstring;
    uint32_t returnType;           /* EatsAbiType */
    uint32_t paramCount;
    const uint32_t* paramTypes;    /* Array of EatsAbiType */
    const char* const* paramNames; /* Optional parameter names */
    EatsHostFunctionCallback dispatch;
} EatsFunctionDescriptor;

/**
 * Universal host interface provided to plugins/modules during initialization.
 */
typedef struct EatsHostInterface {
    uint32_t abiVersion;
    void* hostContext;
    int32_t (*register_function)(void* hostContext, const EatsFunctionDescriptor* funcDesc);
    int32_t (*register_struct)(void* hostContext, const EatsStructDescriptor* structDesc);
    int32_t (*invoke_host)(void* hostContext, const char* name, const EatsAbiValue* args, uint32_t numArgs, EatsAbiValue* outResult);
    int32_t (*log_message)(void* hostContext, int level, const char* message);
    void*   (*alloc_memory)(void* hostContext, size_t sizeBytes);
    void    (*free_memory)(void* hostContext, void* ptr);
} EatsHostInterface;

/**
 * Universal Module Descriptor for guest plugins, extensions, and CLI scripts.
 */
typedef struct EatsModuleDescriptor {
    const char* id;
    const char* name;
    const char* version;
    uint32_t abiVersion;
    void* (*init)(const EatsHostInterface* host);
    void  (*shutdown)(void* moduleInstance);
    uint32_t functionCount;
    const EatsFunctionDescriptor* functions;
    uint32_t structCount;
    const EatsStructDescriptor* structs;
} EatsModuleDescriptor;

/* --- Inline helper constructors for C/C++ --- */

static inline EatsAbiValue eats_abi_value_void(void) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_VOID;
    v.flags = 0;
    v.as.i64 = 0;
    return v;
}

static inline EatsAbiValue eats_abi_value_bool(uint8_t b) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_BOOL;
    v.flags = 0;
    v.as.boolean = b ? 1 : 0;
    return v;
}

static inline EatsAbiValue eats_abi_value_i32(int32_t val) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_I32;
    v.flags = 0;
    v.as.i32 = val;
    return v;
}

static inline EatsAbiValue eats_abi_value_i64(int64_t val) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_I64;
    v.flags = 0;
    v.as.i64 = val;
    return v;
}

static inline EatsAbiValue eats_abi_value_u32(uint32_t val) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_U32;
    v.flags = 0;
    v.as.u32 = val;
    return v;
}

static inline EatsAbiValue eats_abi_value_u64(uint64_t val) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_U64;
    v.flags = 0;
    v.as.u64 = val;
    return v;
}

static inline EatsAbiValue eats_abi_value_f32(float val) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_F32;
    v.flags = 0;
    v.as.f32 = val;
    return v;
}

static inline EatsAbiValue eats_abi_value_f64(double val) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_F64;
    v.flags = 0;
    v.as.f64 = val;
    return v;
}

static inline EatsAbiValue eats_abi_value_string(const char* str) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_STRING;
    v.flags = 0;
    v.as.stringVal = str;
    return v;
}

static inline EatsAbiValue eats_abi_value_pointer(void* ptr) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_POINTER;
    v.flags = 0;
    v.as.ptr = ptr;
    return v;
}

static inline EatsAbiValue eats_abi_value_buffer(void* data, size_t sizeBytes, size_t elementSize, uint32_t elementType, uint32_t flags) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_BUFFER;
    v.flags = 0;
    v.as.buffer.data = data;
    v.as.buffer.sizeBytes = sizeBytes;
    v.as.buffer.elementSize = elementSize;
    v.as.buffer.elementType = elementType;
    v.as.buffer.flags = flags;
    return v;
}

static inline EatsAbiValue eats_abi_value_struct(void* structPtr, const EatsStructDescriptor* desc) {
    EatsAbiValue v;
    v.type = EATS_ABI_TYPE_STRUCT;
    v.flags = 0;
    v.as.structVal.structPtr = structPtr;
    v.as.structVal.descriptor = desc;
    return v;
}

#ifdef __cplusplus
}
#endif

#endif /* EATS_HOST_ABI_H */
