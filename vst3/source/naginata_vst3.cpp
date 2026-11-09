// Naginata VST3 processor and factory. Mirrors the CLAP parameter map (17 params, same order).
// Trial: NAGINATA_TRIAL_EXPIRY (unix seconds) is set at build time; after it passes, output is muted.
#include "naginata_vst3.h"
#include "naginata_editor.h"
#include <cstring>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#ifndef NAGINATA_TRIAL_EXPIRY
#define NAGINATA_TRIAL_EXPIRY 0  // 0 = no expiry (set by the build script for trial builds)
#endif

namespace {

constexpr int kParamCount = 19;

// Index == parameter tag == CLAP param id. Keep the order in sync with naginata_clap.cpp.
struct ParamDef {
    const char* title;
    double min, max, def;
    int32_t stepCount;  // 0 = continuous, 1 = toggle (two states), N = N+1 discrete states
    bool canAutomate;
};

const ParamDef kDefs[kParamCount] = {
    {"NAGI Engage",         0, 1, 0,   1, true},
    {"NAGI Level",          0, 1, 0.7, 0, true},
    {"NAGI Tone",           0, 1, 0.45, 0, true},
    {"NAGI Gain",           0, 1, 0.6, 0, true},
    {"NAGI Wet",            0, 1, 1,   0, true},
    {"NAGI Mid Off/Boost/Scoop", 0, 2, 0, 2, true},
    {"NAGI High Pass",      0, 1, 0,   0, true},
    {"NAGI Low Pass",       0, 1, 1,   0, true},
    {"NATA Engage",         0, 1, 0,   1, true},
    {"NATA Clean",          0, 1, 0,   0, true},
    {"NATA +",              0, 1, 0.4, 0, true},
    {"NATA -",              0, 1, 0.4, 0, true},
    {"NATA + Range",        0, 1, 0.7, 0, true},
    {"NATA - Range",        0, 1, 0.7, 0, true},
    {"NATA Wet",            0, 1, 1,   0, true},
    {"NATA Sum Mode",       0, 2, 1,   2, true},
    {"NATA Symmetrical",    0, 1, 1,   1, true},
    {"NAGI Harmonics",      0, 1, 0.4, 0, true},
    {"NAGI Harm. Depth",    0, 1, 1.0, 0, true},
};

bool trialExpired() {
    if (NAGINATA_TRIAL_EXPIRY == 0) return false;
    return static_cast<long long>(std::time(nullptr)) > static_cast<long long>(NAGINATA_TRIAL_EXPIRY);
}

}  // namespace

namespace Steinberg {
namespace Vst {

// One automation point from the host, with its offset inside the current block
struct ParamPoint {
    int32 offset;
    ParamID id;
    ParamValue value;
};

constexpr int32 kMaxPoints = 256;

//------------------------------------------------------------------------
NaginataVst3::NaginataVst3() {}

tresult PLUGIN_API NaginataVst3::initialize(FUnknown* context) {
    tresult result = SingleComponentEffect::initialize(context);
    if (result != kResultOk) return result;

    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    for (int32 i = 0; i < kParamCount; ++i) {
        const ParamDef& d = kDefs[i];
        String128 title{};
        UString(title, 128).fromAscii(d.title);
        int32 flags = d.canAutomate ? ParameterInfo::kCanAutomate : 0;
        parameters.addParameter(new RangeParameter(title, i, nullptr, d.min, d.max, d.def,
                                                   d.stepCount, flags));
    }
    return kResultOk;
}

tresult PLUGIN_API NaginataVst3::setupProcessing(ProcessSetup& setup) {
    tresult result = SingleComponentEffect::setupProcessing(setup);
    if (result != kResultOk) return result;
    sampleRate_ = setup.sampleRate;
    expired_ = trialExpired();
    prepareChains();
    return kResultOk;
}

tresult PLUGIN_API NaginataVst3::setActive(TBool state) {
    tresult result = SingleComponentEffect::setActive(state);
    if (state) {
        expired_ = trialExpired();  // read the clock once per activation, never on the audio path
        prepareChains();            // clear filter state when the plugin is (re)activated
    }
    return result;
}

tresult PLUGIN_API NaginataVst3::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

uint32 PLUGIN_API NaginataVst3::getLatencySamples() {
    return static_cast<uint32>(naginata::NaginataChain::kLatencySamples);
}

void NaginataVst3::prepareChains() {
    for (auto& c : chains_) c.prepare(sampleRate_);
}

// Plain values come from the SDK's parameter container, so host automation, GUI edits and
// state loads all feed the DSP the same way.
void NaginataVst3::applyParams() {
    double v[kParamCount];
    for (int32 i = 0; i < kParamCount; ++i) {
        Parameter* p = parameters.getParameter(i);
        v[i] = p ? p->toPlain(p->getNormalized()) : kDefs[i].def;
    }

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

    for (auto& c : chains_) c.setParams(cp);
}

// Render [start, end) of the block for every output channel. The DSP chain keeps state across calls.
void NaginataVst3::renderRange(const AudioBusBuffers* in, AudioBusBuffers& out, int32 channels,
                               int32 start, int32 end) {
    for (int32 c = 0; c < channels; ++c) {
        const float* src = (in && in->numChannels > 0)
                               ? in->channelBuffers32[c < in->numChannels ? c : 0]
                               : nullptr;
        float* dst = out.channelBuffers32[c];
        for (int32 i = start; i < end; ++i) {
            dst[i] = (expired_ || !src) ? 0.0f : chains_[c].process(src[i]);
        }
    }
}

tresult PLUGIN_API NaginataVst3::process(ProcessData& data) {
    // Gather host automation with sample offsets. Host automation does not arrive via
    // setParamNormalized, so it must be read here.
    ParamPoint points[kMaxPoints];
    int32 count = 0;
    if (data.inputParameterChanges) {
        const int32 queues = data.inputParameterChanges->getParameterCount();
        for (int32 i = 0; i < queues; ++i) {
            IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
            if (!q) continue;
            const ParamID id = q->getParameterId();
            const int32 n = q->getPointCount();
            for (int32 j = 0; j < n; ++j) {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (q->getPoint(j, offset, value) != kResultOk) continue;
                if (count < kMaxPoints) {
                    points[count++] = {offset, id, value};
                } else if (Parameter* p = parameters.getParameter(id)) {
                    p->setNormalized(value);  // overflow past the cap: applied at block start
                }
            }
        }
    }
    // Stable sort by offset so points apply in the order the host sent them
    for (int32 i = 1; i < count; ++i) {
        const ParamPoint t = points[i];
        int32 j = i - 1;
        while (j >= 0 && points[j].offset > t.offset) {
            points[j + 1] = points[j];
            --j;
        }
        points[j + 1] = t;
    }

    const AudioBusBuffers* in = data.numInputs > 0 ? &data.inputs[0] : nullptr;
    AudioBusBuffers* out = data.numOutputs > 0 ? &data.outputs[0] : nullptr;
    const bool audioOk = out && out->channelBuffers32 && data.symbolicSampleSize == kSample32;
    const int32 channels = audioOk ? (out->numChannels < 2 ? out->numChannels : 2) : 0;
    const int32 frames = data.numSamples;

    // Walk the block: apply each point at its offset, render the segment up to the next point
    int32 pos = 0;
    int32 next = 0;
    while (true) {
        while (next < count && points[next].offset <= pos) {
            if (Parameter* p = parameters.getParameter(points[next].id)) {
                p->setNormalized(points[next].value);
            }
            ++next;
        }
        applyParams();
        const int32 end = (next < count) ? std::min(points[next].offset, frames) : frames;
        if (audioOk) renderRange(in, *out, channels, pos, end);
        pos = end;
        if (pos >= frames) break;
    }

    if (audioOk) out->silenceFlags = 0;
    return kResultOk;
}

// State: normalized value of each parameter, as doubles, in tag order.
tresult PLUGIN_API NaginataVst3::getState(IBStream* state) {
    if (!state) return kInvalidArgument;
    IBStreamer streamer(state, kLittleEndian);
    for (int32 i = 0; i < kParamCount; ++i) {
        Parameter* p = parameters.getParameter(i);
        if (!streamer.writeDouble(p ? p->getNormalized() : 0.0)) return kResultFalse;
    }
    return kResultOk;
}

tresult PLUGIN_API NaginataVst3::setState(IBStream* state) {
    if (!state) return kInvalidArgument;
    IBStreamer streamer(state, kLittleEndian);
    for (int32 i = 0; i < kParamCount; ++i) {
        double norm = 0.0;
        if (!streamer.readDouble(norm)) return kResultFalse;
        Parameter* p = parameters.getParameter(i);
        if (p) p->setNormalized(norm);
    }
    return kResultOk;
}

//------------------------------------------------------------------------
// Factory
//------------------------------------------------------------------------
}  // namespace Vst
}  // namespace Steinberg

#define stringPluginName "NAGINATA"
#define stringCompanyName "misskoula"
#define stringCompanyWeb "https://misskoula.itch.io"
#define stringCompanyEmail ""
#define stringNaginataSubCategory "Fx|Distortion"
#define NAGINATA_VERSION "0.1.0"

using namespace Steinberg::Vst;

// Fixed class id for this plugin. Keep it stable across releases so DAWs can recall presets.
static const Steinberg::FUID NaginataProcessorUID(0x7A2D1C54, 0x9E3B4F10, 0x8C5A6B21, 0xD4E7F013);

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(NaginataProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               Vst::kDistributable,
               stringNaginataSubCategory,
               NAGINATA_VERSION,
               kVstVersionString,
               Steinberg::Vst::NaginataVst3::createInstance)
END_FACTORY

IPlugView* PLUGIN_API NaginataVst3::createView(FIDString name) {
    if (name && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0)
        return new naginata::NgntEditor(this);
    return nullptr;
}
