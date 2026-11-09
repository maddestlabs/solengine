#ifndef EATS_HOST_REGISTRY_HPP
#define EATS_HOST_REGISTRY_HPP

#include "eats_host_abi.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <cstring>
#include <cstdlib>

namespace eatsbits::eatscript {
    class Evaluator;
    struct Value;
}

namespace eatsbits::abi {

/**
 * Modern C++ Host Reflection Registry & Dispatcher.
 * Bridges native host functions, struct reflection descriptors, and memory buffers
 * to the C-compatible EatsHostInterface and Eatscript Evaluator.
 */
class HostRegistry {
public:
    HostRegistry();
    ~HostRegistry();

    // Non-copyable, movable
    HostRegistry(const HostRegistry&) = delete;
    HostRegistry& operator=(const HostRegistry&) = delete;
    HostRegistry(HostRegistry&&) noexcept;
    HostRegistry& operator=(HostRegistry&&) noexcept;

    // --- Function Registration ---
    bool registerFunction(const EatsFunctionDescriptor& desc);

    using CppHostFn = std::function<EatsAbiValue(const std::vector<EatsAbiValue>& args)>;

    void registerHostFunction(const std::string& name,
                              uint32_t returnType,
                              std::vector<uint32_t> paramTypes,
                              CppHostFn fn,
                              const std::string& docstring = "");

    [[nodiscard]] const EatsFunctionDescriptor* findFunction(const std::string& name) const;
    [[nodiscard]] bool hasFunction(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> getRegisteredFunctionNames() const;

    // --- Struct Reflection Registration ---
    bool registerStruct(const EatsStructDescriptor& desc);
    [[nodiscard]] const EatsStructDescriptor* findStruct(const std::string& name) const;
    [[nodiscard]] bool hasStruct(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> getRegisteredStructNames() const;

    // --- Dynamic Struct Field Inspection & Mutation ---
    static EatsAbiValue getFieldValue(const EatsStructDescriptor& desc,
                                      const void* structInstance,
                                      const std::string& fieldName);

    static bool setFieldValue(const EatsStructDescriptor& desc,
                              void* structInstance,
                              const std::string& fieldName,
                              const EatsAbiValue& value);

    // --- Invocation ---
    int32_t invoke(const std::string& name,
                   const EatsAbiValue* args,
                   uint32_t numArgs,
                   EatsAbiValue* outResult);

    int32_t invoke(const std::string& name,
                   const std::vector<EatsAbiValue>& args,
                   EatsAbiValue* outResult);

    // --- C-ABI EatsHostInterface Bridge ---
    EatsHostInterface getHostInterface();

    // --- Memory Buffer Management ---
    void* allocMemory(size_t sizeBytes);
    void  freeMemory(void* ptr);

    EatsMemoryBuffer createBuffer(size_t sizeBytes,
                                 size_t elementSize = 1,
                                 uint32_t elementType = EATS_ABI_TYPE_U8,
                                 uint32_t flags = EATS_BUFFER_READWRITE);
    void releaseBuffer(EatsMemoryBuffer& buffer);

    // --- Logging ---
    using LogCallback = std::function<void(int level, const std::string& message)>;
    void setLogCallback(LogCallback cb) { logCb_ = std::move(cb); }
    void log(int level, const std::string& message);
    const std::vector<std::string>& getLogHistory() const noexcept { return logHistory_; }
    void clearLogHistory() noexcept { logHistory_.clear(); }

    // --- Eatscript Evaluator Bi-directional Bridge ---
    void bindToEvaluator(eatscript::Evaluator& evaluator);

    static eatscript::Value abiToScriptValue(const EatsAbiValue& val);
    static EatsAbiValue scriptToAbiValue(const eatscript::Value& val, uint32_t targetType = EATS_ABI_TYPE_VOID);

    // Struct <-> Eatscript Dict conversion via reflection
    static eatscript::Value structToScriptDict(const EatsStructDescriptor& desc, const void* structInstance);
    static bool scriptDictToStruct(const EatsStructDescriptor& desc, const eatscript::Value& dict, void* structInstance);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    LogCallback logCb_;
    std::vector<std::string> logHistory_;
};

} // namespace eatsbits::abi

#endif // EATS_HOST_REGISTRY_HPP
