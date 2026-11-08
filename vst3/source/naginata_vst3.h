// Naginata VST3 processor. Single-component effect: stereo in/out, two series units from the DSP core.
#pragma once

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "../../dsp/naginata_dsp.h"

namespace Steinberg {
namespace Vst {

class NaginataVst3 : public SingleComponentEffect {
public:
    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE;

public:
    NaginataVst3();

    static FUnknown* createInstance(void*) { return static_cast<IAudioProcessor*>(new NaginataVst3); }

    //---from IComponent---
    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE;
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE;
    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE;
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE;

    //---from IAudioProcessor---
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) SMTG_OVERRIDE;
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) SMTG_OVERRIDE;
    tresult PLUGIN_API process(ProcessData& data) SMTG_OVERRIDE;
    uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;

    OBJ_METHODS(NaginataVst3, SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

private:
    void applyParams();
    void prepareChains();
    void renderRange(const AudioBusBuffers* in, AudioBusBuffers& out, int32 channels,
                     int32 start, int32 end);

    naginata::NaginataChain chains_[2];  // one per channel: DSP state is per channel
    double sampleRate_ = 48000.0;
    bool expired_ = false;               // trial state, read once per activation, not per block
};

}  // namespace Vst
}  // namespace Steinberg
