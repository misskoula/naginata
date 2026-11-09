// NAGINATA as a native CLAP plugin. Same DSP and the same 19 parameters, in the same order, as the VST3
// build (vst3/source/naginata_vst3.cpp). Stereo in and out, one DSP chain per channel.
// Trial builds: NAGINATA_TRIAL_EXPIRY (unix seconds, set at build time) mutes the output after that time.
#include "clap/clap.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/latency.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/factory/plugin-factory.h"

#include "../dsp/naginata_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifndef NAGINATA_TRIAL_EXPIRY
#define NAGINATA_TRIAL_EXPIRY 0  // 0 = no expiry (set by the build for trial builds)
#endif

namespace {

constexpr uint32_t kParamCount = 19;
constexpr char kPluginId[] = "com.misskoula.naginata";

struct ParamDef {
    const char* name;
    double min, max, def;
    bool stepped;
};

// Index == parameter id. Must match kDefs in vst3/source/naginata_vst3.cpp.
const ParamDef kDefs[kParamCount] = {
    {"NAGI Engage",             0, 1, 0,    true},
    {"NAGI Level",              0, 1, 0.7,  false},
    {"NAGI Tone",               0, 1, 0.45, false},
    {"NAGI Gain",               0, 1, 0.6,  false},
    {"NAGI Wet",                0, 1, 1,    false},
    {"NAGI Mid Off/Boost/Scoop", 0, 2, 0,   true},
    {"NAGI High Pass",          0, 1, 0,    false},
    {"NAGI Low Pass",           0, 1, 1,    false},
    {"NATA Engage",             0, 1, 0,    true},
    {"NATA Clean",              0, 1, 0,    false},
    {"NATA +",                  0, 1, 0.4,  false},
    {"NATA -",                  0, 1, 0.4,  false},
    {"NATA + Range",            0, 1, 0.7,  false},
    {"NATA - Range",            0, 1, 0.7,  false},
    {"NATA Wet",                0, 1, 1,    false},
    {"NATA Sum Mode",           0, 2, 1,    true},
    {"NATA Symmetrical",        0, 1, 1,    true},
    {"NAGI Harmonics",          0, 1, 0.4,  false},
    {"NAGI Harm. Depth",        0, 1, 1.0,  false},
};

bool trialExpired() {
    if (NAGINATA_TRIAL_EXPIRY == 0) return false;
    return static_cast<long long>(std::time(nullptr)) > static_cast<long long>(NAGINATA_TRIAL_EXPIRY);
}

struct Instance {
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    double values[kParamCount] = {};
    naginata::NaginataChain chains[2];
    double sampleRate = 48000.0;
    bool expired = false;

    Instance() {
        for (uint32_t i = 0; i < kParamCount; ++i) values[i] = kDefs[i].def;
    }
};

Instance* self(const clap_plugin_t* p) { return static_cast<Instance*>(p->plugin_data); }

// Parameter values are plain values, the same as the VST3 build feeds the DSP.
void applyValues(Instance* s) {
    const double* v = s->values;
    naginata::ChainParams cp;
    cp.fuzz.engaged = v[0] >= 0.5;
    cp.fuzz.level = float(v[1]);
    cp.fuzz.tone = float(v[2]);
    cp.fuzz.gain = float(v[3]);
    cp.fuzz.wet = float(v[4]);
    cp.fuzz.midMode = int(std::lround(v[5]));
    cp.fuzz.hpfKnob = float(v[6]);
    cp.fuzz.lpfKnob = float(v[7]);
    cp.fuzz.harmonics = float(v[17]);
    cp.fuzz.harmDepth = float(v[18]);

    cp.fold.engaged = v[8] >= 0.5;
    cp.fold.clean = float(v[9]);
    cp.fold.posFold = float(v[10]);
    cp.fold.negFold = float(v[11]);
    cp.fold.posThr = float(v[12]);
    cp.fold.negThr = float(v[13]);
    cp.fold.wet = float(v[14]);
    cp.fold.sumMode = int(std::lround(v[15]));
    cp.fold.symmetrical = v[16] >= 0.5;

    for (auto& c : s->chains) c.setParams(cp);
}

void setValue(Instance* s, uint32_t id, double value) {
    if (id >= kParamCount) return;
    const ParamDef& d = kDefs[id];
    double v = std::clamp(value, d.min, d.max);
    if (d.stepped) v = std::round(v);
    s->values[id] = v;
}

void prepareChains(Instance* s) {
    for (auto& c : s->chains) c.prepare(s->sampleRate);
}

// Apply every parameter event in an event list, in order.
void applyEvents(Instance* s, const clap_input_events_t* events) {
    if (!events) return;
    const uint32_t n = events->size(events);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* h = events->get(events, i);
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* pv = reinterpret_cast<const clap_event_param_value_t*>(h);
        setValue(s, pv->param_id, pv->value);
    }
}

// ----------------------------------------------------------------------- plugin callbacks

bool CLAP_ABI plugin_init(const clap_plugin_t*) { return true; }

void CLAP_ABI plugin_destroy(const clap_plugin_t* p) { delete self(p); }

bool CLAP_ABI plugin_activate(const clap_plugin_t* p, double sampleRate, uint32_t, uint32_t) {
    Instance* s = self(p);
    s->sampleRate = sampleRate;
    s->expired = trialExpired();  // read the clock once per activation, never on the audio path
    prepareChains(s);
    applyValues(s);
    return true;
}

void CLAP_ABI plugin_deactivate(const clap_plugin_t*) {}

bool CLAP_ABI plugin_start_processing(const clap_plugin_t*) { return true; }

void CLAP_ABI plugin_stop_processing(const clap_plugin_t*) {}

void CLAP_ABI plugin_reset(const clap_plugin_t* p) { prepareChains(self(p)); }

clap_process_status CLAP_ABI plugin_process(const clap_plugin_t* p, const clap_process_t* proc) {
    Instance* s = self(p);
    const uint32_t frames = proc->frames_count;
    if (frames == 0) return CLAP_PROCESS_CONTINUE;

    const clap_audio_buffer_t* in = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
    clap_audio_buffer_t* out = proc->audio_outputs_count > 0 ? &proc->audio_outputs[0] : nullptr;
    const bool audioOk = out && out->data32;
    const uint32_t channels = audioOk ? std::min<uint32_t>(out->channel_count, 2) : 0;
    const uint32_t eventCount = proc->in_events ? proc->in_events->size(proc->in_events) : 0;

    // Walk the block: apply each event at its time, render up to the next event.
    uint32_t pos = 0;
    uint32_t next = 0;
    do {
        while (next < eventCount) {
            const clap_event_header_t* h = proc->in_events->get(proc->in_events, next);
            if (h->time > pos) break;
            if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* pv = reinterpret_cast<const clap_event_param_value_t*>(h);
                setValue(s, pv->param_id, pv->value);
            }
            ++next;
        }
        applyValues(s);

        uint32_t end = frames;
        if (next < eventCount) {
            end = std::min<uint32_t>(proc->in_events->get(proc->in_events, next)->time, frames);
        }

        if (audioOk) {
            for (uint32_t c = 0; c < channels; ++c) {
                const float* src = (in && in->data32 && in->channel_count > 0)
                                       ? in->data32[c < in->channel_count ? c : 0]
                                       : nullptr;
                float* dst = out->data32[c];
                for (uint32_t i = pos; i < end; ++i) {
                    dst[i] = (s->expired || !src) ? 0.0f : s->chains[c].process(src[i]);
                }
            }
        }
        pos = end;
    } while (pos < frames);

    if (audioOk) out->constant_mask = 0;
    return CLAP_PROCESS_CONTINUE;
}

void CLAP_ABI plugin_on_main_thread(const clap_plugin_t*) {}

// ----------------------------------------------------------------------- audio ports

uint32_t CLAP_ABI ports_count(const clap_plugin_t*, bool) { return 1; }

bool CLAP_ABI ports_get(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = isInput ? 0 : 1;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo In" : "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = isInput ? 1 : 0;  // out pairs with in: the DSP runs in place safely
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts = {ports_count, ports_get};

// ----------------------------------------------------------------------- parameters

uint32_t CLAP_ABI params_count(const clap_plugin_t*) { return kParamCount; }

bool CLAP_ABI params_get_info(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index >= kParamCount) return false;
    const ParamDef& d = kDefs[index];
    std::memset(info, 0, sizeof(*info));
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (d.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::snprintf(info->name, sizeof(info->name), "%s", d.name);
    info->min_value = d.min;
    info->max_value = d.max;
    info->default_value = d.def;
    return true;
}

bool CLAP_ABI params_get_value(const clap_plugin_t* p, clap_id id, double* out) {
    if (id >= kParamCount) return false;
    *out = self(p)->values[id];
    return true;
}

bool CLAP_ABI params_value_to_text(const clap_plugin_t*, clap_id id, double value, char* out, uint32_t size) {
    if (id >= kParamCount) return false;
    std::snprintf(out, size, "%.3g", value);
    return true;
}

bool CLAP_ABI params_text_to_value(const clap_plugin_t*, clap_id id, const char* text, double* out) {
    if (id >= kParamCount || !text) return false;
    *out = std::strtod(text, nullptr);
    return true;
}

void CLAP_ABI params_flush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
    Instance* s = self(p);
    applyEvents(s, in);
    applyValues(s);
}

const clap_plugin_params_t kParams = {
    params_count, params_get_info, params_get_value, params_value_to_text, params_text_to_value, params_flush};

// ----------------------------------------------------------------------- latency and state

uint32_t CLAP_ABI latency_get(const clap_plugin_t*) {
    return static_cast<uint32_t>(naginata::NaginataChain::kLatencySamples);
}

const clap_plugin_latency_t kLatency = {latency_get};

// State: the plain value of each parameter, as doubles, in id order.
bool CLAP_ABI state_save(const clap_plugin_t* p, const clap_ostream_t* stream) {
    const Instance* s = self(p);
    const auto bytes = static_cast<uint64_t>(sizeof(double) * kParamCount);
    return stream->write(stream, s->values, bytes) == static_cast<int64_t>(bytes);
}

bool CLAP_ABI state_load(const clap_plugin_t* p, const clap_istream_t* stream) {
    Instance* s = self(p);
    double values[kParamCount];
    const auto bytes = static_cast<uint64_t>(sizeof(double) * kParamCount);
    if (stream->read(stream, values, bytes) != static_cast<int64_t>(bytes)) return false;
    for (uint32_t i = 0; i < kParamCount; ++i) setValue(s, i, values[i]);
    applyValues(s);
    return true;
}

const clap_plugin_state_t kState = {state_save, state_load};

const void* CLAP_ABI plugin_get_extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParams;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatency;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
    return nullptr;
}

// ----------------------------------------------------------------------- factory and entry

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION,
                           CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    kPluginId,
    "NAGINATA",
    "misskoula",
    "",
    "",
    "",
    "0.1.0",
    "Fuzz into wavefolder",
    kFeatures,
};

uint32_t CLAP_ABI factory_count(const clap_plugin_factory_t*) { return 1; }

const clap_plugin_descriptor_t* CLAP_ABI factory_descriptor(const clap_plugin_factory_t*, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_t* CLAP_ABI factory_create(const clap_plugin_factory_t*, const clap_host_t* host,
                                              const char* pluginId) {
    if (!pluginId || std::strcmp(pluginId, kPluginId) != 0) return nullptr;
    auto* s = new Instance();
    s->host = host;
    s->plugin.desc = &kDescriptor;
    s->plugin.plugin_data = s;
    s->plugin.init = plugin_init;
    s->plugin.destroy = plugin_destroy;
    s->plugin.activate = plugin_activate;
    s->plugin.deactivate = plugin_deactivate;
    s->plugin.start_processing = plugin_start_processing;
    s->plugin.stop_processing = plugin_stop_processing;
    s->plugin.reset = plugin_reset;
    s->plugin.process = plugin_process;
    s->plugin.get_extension = plugin_get_extension;
    s->plugin.on_main_thread = plugin_on_main_thread;
    return &s->plugin;
}

const clap_plugin_factory_t kFactory = {factory_count, factory_descriptor, factory_create};

bool CLAP_ABI entry_init(const char*) { return true; }

void CLAP_ABI entry_deinit() {}

const void* CLAP_ABI entry_get_factory(const char* factoryId) {
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

}  // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, entry_init, entry_deinit, entry_get_factory};
