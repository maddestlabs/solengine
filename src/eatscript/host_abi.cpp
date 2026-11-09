#include "eatsbits/abi/host_registry.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace eatsbits::abi {

struct HostRegistry::Impl {
    struct FunctionRecord {
        EatsFunctionDescriptor descriptor;
        std::vector<uint32_t> paramTypesOwned;
        std::vector<std::string> paramNamesOwned;
        std::vector<const char*> paramNamePointers;
        std::string nameOwned;
        std::string docstringOwned;
        CppHostFn cppClosure;
    };

    std::unordered_map<std::string, FunctionRecord> functions;
    std::unordered_map<std::string, EatsStructDescriptor> structs;
    std::vector<void*> allocatedBuffers;
};

// C Trampoline Functions for EatsHostInterface
static int32_t trampoline_register_function(void* hostContext, const EatsFunctionDescriptor* funcDesc) {
    if (!hostContext || !funcDesc) return EATS_ABI_ERROR_INVALID_ARG;
    auto* reg = static_cast<HostRegistry*>(hostContext);
    return reg->registerFunction(*funcDesc) ? EATS_ABI_OK : EATS_ABI_ERROR_GENERIC;
}

static int32_t trampoline_register_struct(void* hostContext, const EatsStructDescriptor* structDesc) {
    if (!hostContext || !structDesc) return EATS_ABI_ERROR_INVALID_ARG;
    auto* reg = static_cast<HostRegistry*>(hostContext);
    return reg->registerStruct(*structDesc) ? EATS_ABI_OK : EATS_ABI_ERROR_GENERIC;
}

static int32_t trampoline_invoke_host(void* hostContext, const char* name, const EatsAbiValue* args, uint32_t numArgs, EatsAbiValue* outResult) {
    if (!hostContext || !name) return EATS_ABI_ERROR_INVALID_ARG;
    auto* reg = static_cast<HostRegistry*>(hostContext);
    return reg->invoke(name, args, numArgs, outResult);
}

static int32_t trampoline_log_message(void* hostContext, int level, const char* message) {
    if (!hostContext || !message) return EATS_ABI_ERROR_INVALID_ARG;
    auto* reg = static_cast<HostRegistry*>(hostContext);
    reg->log(level, message);
    return EATS_ABI_OK;
}

static void* trampoline_alloc_memory(void* hostContext, size_t sizeBytes) {
    if (!hostContext) return nullptr;
    auto* reg = static_cast<HostRegistry*>(hostContext);
    return reg->allocMemory(sizeBytes);
}

static void trampoline_free_memory(void* hostContext, void* ptr) {
    if (!hostContext || !ptr) return;
    auto* reg = static_cast<HostRegistry*>(hostContext);
    reg->freeMemory(ptr);
}

HostRegistry::HostRegistry() : impl_(std::make_unique<Impl>()) {}

HostRegistry::~HostRegistry() {
    if (impl_) {
        for (void* ptr : impl_->allocatedBuffers) {
            std::free(ptr);
        }
        impl_->allocatedBuffers.clear();
    }
}

HostRegistry::HostRegistry(HostRegistry&&) noexcept = default;
HostRegistry& HostRegistry::operator=(HostRegistry&&) noexcept = default;

bool HostRegistry::registerFunction(const EatsFunctionDescriptor& desc) {
    if (!desc.name || !desc.dispatch) return false;

    Impl::FunctionRecord rec{};
    rec.nameOwned = desc.name;
    rec.docstringOwned = desc.docstring ? desc.docstring : "";
    rec.descriptor = desc;
    rec.descriptor.name = rec.nameOwned.c_str();
    rec.descriptor.docstring = rec.docstringOwned.c_str();

    if (desc.paramCount > 0 && desc.paramTypes) {
        rec.paramTypesOwned.assign(desc.paramTypes, desc.paramTypes + desc.paramCount);
        rec.descriptor.paramTypes = rec.paramTypesOwned.data();
    } else {
        rec.descriptor.paramTypes = nullptr;
    }

    if (desc.paramCount > 0 && desc.paramNames) {
        rec.paramNamesOwned.reserve(desc.paramCount);
        rec.paramNamePointers.reserve(desc.paramCount);
        for (uint32_t i = 0; i < desc.paramCount; ++i) {
            rec.paramNamesOwned.emplace_back(desc.paramNames[i] ? desc.paramNames[i] : "");
            rec.paramNamePointers.push_back(rec.paramNamesOwned.back().c_str());
        }
        rec.descriptor.paramNames = rec.paramNamePointers.data();
    } else {
        rec.descriptor.paramNames = nullptr;
    }

    impl_->functions[rec.nameOwned] = std::move(rec);
    return true;
}

void HostRegistry::registerHostFunction(const std::string& name,
                                       uint32_t returnType,
                                       std::vector<uint32_t> paramTypes,
                                       CppHostFn fn,
                                       const std::string& docstring) {
    Impl::FunctionRecord rec{};
    rec.nameOwned = name;
    rec.docstringOwned = docstring;
    rec.paramTypesOwned = std::move(paramTypes);
    rec.cppClosure = std::move(fn);

    rec.descriptor.name = rec.nameOwned.c_str();
    rec.descriptor.docstring = rec.docstringOwned.c_str();
    rec.descriptor.returnType = returnType;
    rec.descriptor.paramCount = static_cast<uint32_t>(rec.paramTypesOwned.size());
    rec.descriptor.paramTypes = rec.paramTypesOwned.empty() ? nullptr : rec.paramTypesOwned.data();
    rec.descriptor.paramNames = nullptr;

    rec.descriptor.dispatch = [](void* context, const EatsAbiValue* args, uint32_t numArgs, EatsAbiValue* outResult) -> int32_t {
        if (!context) return EATS_ABI_ERROR_INVALID_ARG;
        auto* closure = static_cast<CppHostFn*>(context);
        std::vector<EatsAbiValue> argVec;
        argVec.reserve(numArgs);
        for (uint32_t i = 0; i < numArgs; ++i) {
            argVec.push_back(args[i]);
        }
        EatsAbiValue res = (*closure)(argVec);
        if (outResult) {
            *outResult = res;
        }
        return EATS_ABI_OK;
    };

    auto& stored = impl_->functions[name];
    stored = std::move(rec);
    // Point context to the stored closure
    stored.descriptor.name = stored.nameOwned.c_str();
    stored.descriptor.docstring = stored.docstringOwned.c_str();
    if (!stored.paramTypesOwned.empty()) {
        stored.descriptor.paramTypes = stored.paramTypesOwned.data();
    }
}

const EatsFunctionDescriptor* HostRegistry::findFunction(const std::string& name) const {
    auto it = impl_->functions.find(name);
    return it != impl_->functions.end() ? &it->second.descriptor : nullptr;
}

bool HostRegistry::hasFunction(const std::string& name) const {
    return impl_->functions.find(name) != impl_->functions.end();
}

std::vector<std::string> HostRegistry::getRegisteredFunctionNames() const {
    std::vector<std::string> names;
    names.reserve(impl_->functions.size());
    for (const auto& [name, _] : impl_->functions) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool HostRegistry::registerStruct(const EatsStructDescriptor& desc) {
    if (!desc.name || desc.structSize == 0) return false;
    impl_->structs[desc.name] = desc;
    return true;
}

const EatsStructDescriptor* HostRegistry::findStruct(const std::string& name) const {
    auto it = impl_->structs.find(name);
    return it != impl_->structs.end() ? &it->second : nullptr;
}

bool HostRegistry::hasStruct(const std::string& name) const {
    return impl_->structs.find(name) != impl_->structs.end();
}

std::vector<std::string> HostRegistry::getRegisteredStructNames() const {
    std::vector<std::string> names;
    names.reserve(impl_->structs.size());
    for (const auto& [name, _] : impl_->structs) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

EatsAbiValue HostRegistry::getFieldValue(const EatsStructDescriptor& desc,
                                         const void* structInstance,
                                         const std::string& fieldName) {
    if (!structInstance) return eats_abi_value_void();

    for (uint32_t i = 0; i < desc.fieldCount; ++i) {
        const auto& f = desc.fields[i];
        if (f.name && fieldName == f.name) {
            const auto* bytePtr = static_cast<const uint8_t*>(structInstance) + f.offset;
            switch (static_cast<EatsAbiType>(f.type)) {
                case EATS_ABI_TYPE_BOOL:
                    return eats_abi_value_bool(*reinterpret_cast<const uint8_t*>(bytePtr));
                case EATS_ABI_TYPE_I32:
                    return eats_abi_value_i32(*reinterpret_cast<const int32_t*>(bytePtr));
                case EATS_ABI_TYPE_I64:
                    return eats_abi_value_i64(*reinterpret_cast<const int64_t*>(bytePtr));
                case EATS_ABI_TYPE_U32:
                    return eats_abi_value_u32(*reinterpret_cast<const uint32_t*>(bytePtr));
                case EATS_ABI_TYPE_U64:
                    return eats_abi_value_u64(*reinterpret_cast<const uint64_t*>(bytePtr));
                case EATS_ABI_TYPE_F32:
                    return eats_abi_value_f32(*reinterpret_cast<const float*>(bytePtr));
                case EATS_ABI_TYPE_F64:
                    return eats_abi_value_f64(*reinterpret_cast<const double*>(bytePtr));
                case EATS_ABI_TYPE_STRING:
                    return eats_abi_value_string(*reinterpret_cast<const char* const*>(bytePtr));
                case EATS_ABI_TYPE_POINTER:
                    return eats_abi_value_pointer(*reinterpret_cast<void* const*>(bytePtr));
                case EATS_ABI_TYPE_BUFFER: {
                    const auto* buf = reinterpret_cast<const EatsMemoryBuffer*>(bytePtr);
                    return eats_abi_value_buffer(buf->data, buf->sizeBytes, buf->elementSize, buf->elementType, buf->flags);
                }
                case EATS_ABI_TYPE_STRUCT:
                    return eats_abi_value_struct(const_cast<uint8_t*>(bytePtr), f.nestedStruct);
                default:
                    return eats_abi_value_void();
            }
        }
    }
    return eats_abi_value_void();
}

bool HostRegistry::setFieldValue(const EatsStructDescriptor& desc,
                                 void* structInstance,
                                 const std::string& fieldName,
                                 const EatsAbiValue& value) {
    if (!structInstance) return false;

    for (uint32_t i = 0; i < desc.fieldCount; ++i) {
        const auto& f = desc.fields[i];
        if (f.name && fieldName == f.name) {
            auto* bytePtr = static_cast<uint8_t*>(structInstance) + f.offset;
            switch (static_cast<EatsAbiType>(f.type)) {
                case EATS_ABI_TYPE_BOOL:
                    *reinterpret_cast<uint8_t*>(bytePtr) = value.as.boolean ? 1 : 0;
                    return true;
                case EATS_ABI_TYPE_I32:
                    if (value.type == EATS_ABI_TYPE_I32) *reinterpret_cast<int32_t*>(bytePtr) = value.as.i32;
                    else if (value.type == EATS_ABI_TYPE_F64) *reinterpret_cast<int32_t*>(bytePtr) = static_cast<int32_t>(value.as.f64);
                    else *reinterpret_cast<int32_t*>(bytePtr) = value.as.i32;
                    return true;
                case EATS_ABI_TYPE_I64:
                    if (value.type == EATS_ABI_TYPE_I64) *reinterpret_cast<int64_t*>(bytePtr) = value.as.i64;
                    else if (value.type == EATS_ABI_TYPE_F64) *reinterpret_cast<int64_t*>(bytePtr) = static_cast<int64_t>(value.as.f64);
                    else *reinterpret_cast<int64_t*>(bytePtr) = value.as.i64;
                    return true;
                case EATS_ABI_TYPE_U32:
                    if (value.type == EATS_ABI_TYPE_U32) *reinterpret_cast<uint32_t*>(bytePtr) = value.as.u32;
                    else if (value.type == EATS_ABI_TYPE_F64) *reinterpret_cast<uint32_t*>(bytePtr) = static_cast<uint32_t>(value.as.f64);
                    else *reinterpret_cast<uint32_t*>(bytePtr) = value.as.u32;
                    return true;
                case EATS_ABI_TYPE_U64:
                    if (value.type == EATS_ABI_TYPE_U64) *reinterpret_cast<uint64_t*>(bytePtr) = value.as.u64;
                    else if (value.type == EATS_ABI_TYPE_F64) *reinterpret_cast<uint64_t*>(bytePtr) = static_cast<uint64_t>(value.as.f64);
                    else *reinterpret_cast<uint64_t*>(bytePtr) = value.as.u64;
                    return true;
                case EATS_ABI_TYPE_F32:
                    if (value.type == EATS_ABI_TYPE_F32) *reinterpret_cast<float*>(bytePtr) = value.as.f32;
                    else if (value.type == EATS_ABI_TYPE_F64) *reinterpret_cast<float*>(bytePtr) = static_cast<float>(value.as.f64);
                    else *reinterpret_cast<float*>(bytePtr) = static_cast<float>(value.as.i32);
                    return true;
                case EATS_ABI_TYPE_F64:
                    if (value.type == EATS_ABI_TYPE_F64) *reinterpret_cast<double*>(bytePtr) = value.as.f64;
                    else if (value.type == EATS_ABI_TYPE_F32) *reinterpret_cast<double*>(bytePtr) = static_cast<double>(value.as.f32);
                    else if (value.type == EATS_ABI_TYPE_I32) *reinterpret_cast<double*>(bytePtr) = static_cast<double>(value.as.i32);
                    else *reinterpret_cast<double*>(bytePtr) = value.as.f64;
                    return true;
                case EATS_ABI_TYPE_STRING:
                    *reinterpret_cast<const char**>(bytePtr) = value.as.stringVal;
                    return true;
                case EATS_ABI_TYPE_POINTER:
                    *reinterpret_cast<void**>(bytePtr) = value.as.ptr;
                    return true;
                case EATS_ABI_TYPE_BUFFER:
                    *reinterpret_cast<EatsMemoryBuffer*>(bytePtr) = value.as.buffer;
                    return true;
                case EATS_ABI_TYPE_STRUCT:
                    if (value.as.structVal.structPtr && f.size > 0) {
                        std::memcpy(bytePtr, value.as.structVal.structPtr, f.size);
                        return true;
                    }
                    return false;
                default:
                    return false;
            }
        }
    }
    return false;
}

int32_t HostRegistry::invoke(const std::string& name,
                             const EatsAbiValue* args,
                             uint32_t numArgs,
                             EatsAbiValue* outResult) {
    auto it = impl_->functions.find(name);
    if (it == impl_->functions.end()) {
        return EATS_ABI_ERROR_NOT_FOUND;
    }

    auto& rec = it->second;
    void* context = rec.cppClosure ? static_cast<void*>(&rec.cppClosure) : static_cast<void*>(this);
    return rec.descriptor.dispatch(context, args, numArgs, outResult);
}

int32_t HostRegistry::invoke(const std::string& name,
                             const std::vector<EatsAbiValue>& args,
                             EatsAbiValue* outResult) {
    return invoke(name, args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()), outResult);
}

EatsHostInterface HostRegistry::getHostInterface() {
    EatsHostInterface iface{};
    iface.abiVersion = EATS_HOST_ABI_VERSION;
    iface.hostContext = this;
    iface.register_function = trampoline_register_function;
    iface.register_struct = trampoline_register_struct;
    iface.invoke_host = trampoline_invoke_host;
    iface.log_message = trampoline_log_message;
    iface.alloc_memory = trampoline_alloc_memory;
    iface.free_memory = trampoline_free_memory;
    return iface;
}

void* HostRegistry::allocMemory(size_t sizeBytes) {
    if (sizeBytes == 0) return nullptr;
    void* ptr = std::calloc(1, sizeBytes);
    if (ptr) {
        impl_->allocatedBuffers.push_back(ptr);
    }
    return ptr;
}

void HostRegistry::freeMemory(void* ptr) {
    if (!ptr) return;
    auto it = std::find(impl_->allocatedBuffers.begin(), impl_->allocatedBuffers.end(), ptr);
    if (it != impl_->allocatedBuffers.end()) {
        impl_->allocatedBuffers.erase(it);
    }
    std::free(ptr);
}

EatsMemoryBuffer HostRegistry::createBuffer(size_t sizeBytes,
                                           size_t elementSize,
                                           uint32_t elementType,
                                           uint32_t flags) {
    void* ptr = allocMemory(sizeBytes);
    EatsMemoryBuffer buf{};
    buf.data = ptr;
    buf.sizeBytes = sizeBytes;
    buf.elementSize = elementSize > 0 ? elementSize : 1;
    buf.elementType = elementType;
    buf.flags = flags | EATS_BUFFER_OWNED;
    return buf;
}

void HostRegistry::releaseBuffer(EatsMemoryBuffer& buffer) {
    if (buffer.data && (buffer.flags & EATS_BUFFER_OWNED)) {
        freeMemory(buffer.data);
        buffer.data = nullptr;
        buffer.sizeBytes = 0;
    }
}

void HostRegistry::log(int level, const std::string& message) {
    logHistory_.push_back(message);
    if (logCb_) {
        logCb_(level, message);
    }
}

eatscript::Value HostRegistry::abiToScriptValue(const EatsAbiValue& val) {
    using namespace eatsbits::eatscript;
    switch (static_cast<EatsAbiType>(val.type)) {
        case EATS_ABI_TYPE_VOID:
            return Value();
        case EATS_ABI_TYPE_BOOL:
            return Value(val.as.boolean != 0);
        case EATS_ABI_TYPE_I32:
            return Value(static_cast<double>(val.as.i32));
        case EATS_ABI_TYPE_I64:
            return Value(static_cast<double>(val.as.i64));
        case EATS_ABI_TYPE_U32:
            return Value(static_cast<double>(val.as.u32));
        case EATS_ABI_TYPE_U64:
            return Value(static_cast<double>(val.as.u64));
        case EATS_ABI_TYPE_F32:
            return Value(static_cast<double>(val.as.f32));
        case EATS_ABI_TYPE_F64:
            return Value(val.as.f64);
        case EATS_ABI_TYPE_STRING:
            return Value(val.as.stringVal ? std::string(val.as.stringVal) : std::string());
        case EATS_ABI_TYPE_BUFFER: {
            // Buffer converted to a list of numbers or dict descriptor
            std::map<std::string, Value> bDict;
            bDict["size"] = Value(static_cast<double>(val.as.buffer.sizeBytes));
            bDict["element_size"] = Value(static_cast<double>(val.as.buffer.elementSize));
            bDict["element_type"] = Value(static_cast<double>(val.as.buffer.elementType));
            return Value(bDict);
        }
        case EATS_ABI_TYPE_STRUCT: {
            if (val.as.structVal.structPtr && val.as.structVal.descriptor) {
                return structToScriptDict(*val.as.structVal.descriptor, val.as.structVal.structPtr);
            }
            return Value();
        }
        case EATS_ABI_TYPE_POINTER:
        default:
            return Value();
    }
}

EatsAbiValue HostRegistry::scriptToAbiValue(const eatscript::Value& val, uint32_t targetType) {
    if (val.isNil()) {
        return eats_abi_value_void();
    }
    if (val.isBoolean()) {
        return eats_abi_value_bool(val.asBoolean() ? 1 : 0);
    }
    if (val.isNumber()) {
        double d = val.asNumber();
        switch (static_cast<EatsAbiType>(targetType)) {
            case EATS_ABI_TYPE_I32: return eats_abi_value_i32(static_cast<int32_t>(d));
            case EATS_ABI_TYPE_I64: return eats_abi_value_i64(static_cast<int64_t>(d));
            case EATS_ABI_TYPE_U32: return eats_abi_value_u32(static_cast<uint32_t>(d));
            case EATS_ABI_TYPE_U64: return eats_abi_value_u64(static_cast<uint64_t>(d));
            case EATS_ABI_TYPE_F32: return eats_abi_value_f32(static_cast<float>(d));
            case EATS_ABI_TYPE_BOOL: return eats_abi_value_bool(d != 0.0 ? 1 : 0);
            case EATS_ABI_TYPE_F64:
            default:
                return eats_abi_value_f64(d);
        }
    }
    if (val.isString()) {
        return eats_abi_value_string(val.asString().c_str());
    }
    return eats_abi_value_void();
}

eatscript::Value HostRegistry::structToScriptDict(const EatsStructDescriptor& desc, const void* structInstance) {
    using namespace eatsbits::eatscript;
    if (!structInstance) return Value();

    std::map<std::string, Value> dict;
    for (uint32_t i = 0; i < desc.fieldCount; ++i) {
        const auto& f = desc.fields[i];
        if (f.name) {
            EatsAbiValue fieldVal = getFieldValue(desc, structInstance, f.name);
            dict[f.name] = abiToScriptValue(fieldVal);
        }
    }
    return Value(dict);
}

bool HostRegistry::scriptDictToStruct(const EatsStructDescriptor& desc, const eatscript::Value& dict, void* structInstance) {
    if (!structInstance || !dict.isDict()) return false;

    for (uint32_t i = 0; i < desc.fieldCount; ++i) {
        const auto& f = desc.fields[i];
        if (f.name && dict.hasKey(f.name)) {
            eatscript::Value v = dict.get(f.name);
            EatsAbiValue abiVal = scriptToAbiValue(v, f.type);
            setFieldValue(desc, structInstance, f.name, abiVal);
        }
    }
    return true;
}

void HostRegistry::bindToEvaluator(eatscript::Evaluator& evaluator) {
    using namespace eatsbits::eatscript;

    // 1. Register host namespace container
    evaluator.setVariable("host", Value(std::map<std::string, Value>{}));

    // 2. Generic dynamic dispatcher host.invoke("function_name", *args)
    evaluator.registerMemberFunction("host", "invoke", [this](const std::vector<Value>& args, const auto&) -> Value {
        if (args.empty()) return Value();
        std::string fnName = args[0].asString();
        std::vector<EatsAbiValue> abiArgs;
        abiArgs.reserve(args.size() > 1 ? args.size() - 1 : 0);

        const auto* desc = findFunction(fnName);
        for (size_t i = 1; i < args.size(); ++i) {
            uint32_t targetType = EATS_ABI_TYPE_VOID;
            if (desc && desc->paramTypes && (i - 1) < desc->paramCount) {
                targetType = desc->paramTypes[i - 1];
            }
            abiArgs.push_back(scriptToAbiValue(args[i], targetType));
        }

        EatsAbiValue outResult = eats_abi_value_void();
        int32_t status = invoke(fnName, abiArgs, &outResult);
        if (status == EATS_ABI_OK) {
            return abiToScriptValue(outResult);
        }
        return Value();
    });

    // 3. Register introspection functions
    evaluator.registerMemberFunction("host", "functions", [this](const auto&, const auto&) -> Value {
        std::vector<Value> fList;
        for (const auto& name : getRegisteredFunctionNames()) {
            fList.emplace_back(name);
        }
        return Value(fList);
    });

    evaluator.registerMemberFunction("host", "structs", [this](const auto&, const auto&) -> Value {
        std::vector<Value> sList;
        for (const auto& name : getRegisteredStructNames()) {
            sList.emplace_back(name);
        }
        return Value(sList);
    });

    evaluator.registerMemberFunction("host", "log", [this](const std::vector<Value>& args, const auto&) -> Value {
        std::string msg;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) msg += " ";
            msg += args[i].toString();
        }
        log(0, msg);
        return Value(true);
    });

    // 4. Bind each registered host function directly under host.<name> and global <name>
    for (const auto& [name, rec] : impl_->functions) {
        std::string fnName = name;
        auto hostDispatcher = [this, fnName](const std::vector<Value>& args, const auto&) -> Value {
            const auto* desc = findFunction(fnName);
            std::vector<EatsAbiValue> abiArgs;
            abiArgs.reserve(args.size());
            for (size_t i = 0; i < args.size(); ++i) {
                uint32_t targetType = EATS_ABI_TYPE_VOID;
                if (desc && desc->paramTypes && i < desc->paramCount) {
                    targetType = desc->paramTypes[i];
                }
                abiArgs.push_back(scriptToAbiValue(args[i], targetType));
            }
            EatsAbiValue outResult = eats_abi_value_void();
            int32_t status = invoke(fnName, abiArgs, &outResult);
            if (status == EATS_ABI_OK) {
                return abiToScriptValue(outResult);
            }
            return Value();
        };

        evaluator.registerMemberFunction("host", fnName, hostDispatcher);
        // Also register globally if not already set
        if (!evaluator.hasFunction(fnName)) {
            evaluator.registerFunction(fnName, hostDispatcher);
        }
    }
}

} // namespace eatsbits::abi
