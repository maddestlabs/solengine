#ifndef EATS_PLUGIN_ABI_H
#define EATS_PLUGIN_ABI_H

#include <stdint.h>
#include "eats_host_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Standard multi-channel audio buffer passed to DSP process callbacks.
 * Inputs and outputs are planar float arrays: buffer->outputs[channel][sample].
 */
struct EatsAudioBuffer {
    const float* const* inputs;
    float* const* outputs;
    uint32_t numChannels;
    uint32_t numSamples;
};

/**
 * Standard C-ABI plugin descriptor interface for Eatscript native plugins,
 * compiled AOT instruments, and built-in synthesizer / effect modules.
 */
struct EatsPluginDescriptor {
    const char* id;
    const char* name;
    uint32_t numParams;
    void* (*create_instance)(double sampleRate);
    void  (*destroy_instance)(void* instance);
    void  (*process)(void* instance, const struct EatsAudioBuffer* buffer);
    void  (*set_param)(void* instance, uint32_t paramId, float value);
    void  (*note_on)(void* instance, uint8_t pitch, float velocity);
    void  (*note_off)(void* instance, uint8_t pitch);
};

#ifdef __cplusplus
}
#endif

#endif // EATS_PLUGIN_ABI_H
