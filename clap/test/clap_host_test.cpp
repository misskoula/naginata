// Host-style test for NAGINATA.clap: loads the built library with dlopen, drives it through the CLAP
// entry point, and checks its output against the DSP core run directly.
//   build: g++ -std=c++17 -O2 -I../include -I../.. clap_host_test.cpp -ldl -o clap_host_test
//   run:   ./clap_host_test /path/to/NAGINATA.clap
#include "clap/clap.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/latency.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/factory/plugin-factory.h"

#include "dsp/naginata_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

struct EventList {
    std::vector<clap_event_param_value_t> events;
    static uint32_t size(const clap_input_events_t* l) {
        return uint32_t(static_cast<EventList*>(l->ctx)->events.size());
    }
    static const clap_event_header_t* get(const clap_input_events_t* l, uint32_t i) {
        return &static_cast<EventList*>(l->ctx)->events[i].header;
    }
};

clap_event_param_value_t makeParam(uint32_t time, clap_id id, double value) {
    clap_event_param_value_t e{};
    e.header.size = sizeof(e);
    e.header.time = time;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.header.flags = 0;
    e.param_id = id;
    e.note_id = -1;
    e.port_index = -1;
    e.channel = -1;
    e.key = -1;
    e.value = value;
    return e;
}

// Parameters as the plugin maps them, so the reference uses exactly the same settings.
naginata::ChainParams referenceParams(double gain) {
    naginata::ChainParams cp;
    cp.fuzz.engaged = true;
    cp.fuzz.level = 0.7f;
    cp.fuzz.tone = 0.45f;
    cp.fuzz.gain = float(gain);
    cp.fuzz.wet = 1.0f;
    cp.fuzz.midMode = 0;
    cp.fuzz.hpfKnob = 0.0f;
    cp.fuzz.lpfKnob = 1.0f;
    cp.fuzz.harmonics = 0.4f;
    cp.fuzz.harmDepth = 1.0f;
    cp.fold.engaged = false;
    cp.fold.clean = 0.0f;
    cp.fold.posFold = 0.4f;
    cp.fold.negFold = 0.4f;
    cp.fold.posThr = 0.7f;
    cp.fold.negThr = 0.7f;
    cp.fold.wet = 1.0f;
    cp.fold.sumMode = 1;
    cp.fold.symmetrical = true;
    return cp;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s NAGINATA.clap\n", argv[0]);
        return 2;
    }
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    check(lib != nullptr, "library loads");
    if (!lib) return 1;

    auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    check(entry != nullptr, "clap_entry symbol exported");
    if (!entry) return 1;
    check(entry->init(argv[1]), "entry init");

    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    check(factory != nullptr, "plugin factory available");
    check(factory && factory->get_plugin_count(factory) == 1, "one plugin in the factory");
    const clap_plugin_descriptor_t* desc = factory ? factory->get_plugin_descriptor(factory, 0) : nullptr;
    check(desc && std::strcmp(desc->id, "com.misskoula.naginata") == 0, "descriptor id");
    check(desc && std::strcmp(desc->name, "NAGINATA") == 0, "descriptor name is NAGINATA");

    clap_host_t host{};
    host.clap_version = CLAP_VERSION_INIT;
    const clap_plugin_t* plugin = factory->create_plugin(factory, &host, "com.misskoula.naginata");
    check(plugin != nullptr, "plugin created");
    if (!plugin) return 1;
    check(plugin->init(plugin), "plugin init");
    check(plugin->activate(plugin, 48000.0, 1, 1024), "plugin activate at 48 kHz");
    plugin->start_processing(plugin);

    auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    auto* ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    auto* latency = static_cast<const clap_plugin_latency_t*>(plugin->get_extension(plugin, CLAP_EXT_LATENCY));
    auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
    check(params && params->count(plugin) == 19, "19 parameters, as in the VST3 build");
    check(ports && ports->count(plugin, true) == 1 && ports->count(plugin, false) == 1, "one stereo in, one out");
    check(latency && latency->get(plugin) == 16, "latency is 16 samples");
    check(state != nullptr, "state extension present");

    // Process: a 110 Hz sine, with a gain change at sample 256 of the first block.
    const int kBlock = 512, kBlocks = 40, kTotal = kBlock * kBlocks;
    const double kRate = 48000.0, kF0 = 110.0, kAmp = 0.2;
    std::vector<float> input(kTotal);
    for (int i = 0; i < kTotal; ++i) input[i] = float(kAmp * std::sin(2.0 * naginata::kPi * kF0 * i / kRate));

    // Reference: the DSP core, with the same gain change applied at the same sample.
    naginata::NaginataChain ref;
    ref.prepare(kRate);
    ref.setParams(referenceParams(0.6));
    std::vector<float> expected(kTotal);
    for (int i = 0; i < kTotal; ++i) {
        if (i == 256) ref.setParams(referenceParams(0.9));
        expected[i] = ref.process(input[i]);
    }

    std::vector<float> outL(kTotal), outR(kTotal);
    double maxDiff = 0.0;
    double peak = 0.0;
    for (int b = 0; b < kBlocks; ++b) {
        EventList list;
        if (b == 0) {
            list.events.push_back(makeParam(0, 0, 1.0));    // NAGI Engage on
            list.events.push_back(makeParam(0, 3, 0.6));    // NAGI Gain
            list.events.push_back(makeParam(0, 17, 0.4));   // NAGI Harmonics
            list.events.push_back(makeParam(0, 18, 1.0));   // NAGI Harm. Depth
            list.events.push_back(makeParam(0, 1, 0.7));    // NAGI Level, as the reference
            list.events.push_back(makeParam(0, 2, 0.45));   // NAGI Tone
            list.events.push_back(makeParam(256, 3, 0.9));  // gain change mid-block
        }
        clap_input_events_t inEvents{};
        inEvents.ctx = &list;
        inEvents.size = EventList::size;
        inEvents.get = EventList::get;

        float* inPtr[2] = {&input[b * kBlock], &input[b * kBlock]};
        float* outPtr[2] = {&outL[b * kBlock], &outR[b * kBlock]};
        clap_audio_buffer_t inBuf{};
        inBuf.data32 = inPtr;
        inBuf.channel_count = 2;
        clap_audio_buffer_t outBuf{};
        outBuf.data32 = outPtr;
        outBuf.channel_count = 2;

        clap_process_t proc{};
        proc.steady_time = b * kBlock;
        proc.frames_count = kBlock;
        proc.audio_inputs = &inBuf;
        proc.audio_inputs_count = 1;
        proc.audio_outputs = &outBuf;
        proc.audio_outputs_count = 1;
        proc.in_events = &inEvents;
        proc.out_events = nullptr;
        plugin->process(plugin, &proc);
    }

    // The CLAP output is delayed by the latency, the reference is not: compare with the same delay.
    // Both chains start from the same state, so the reference output should match exactly.
    for (int i = 0; i < kTotal; ++i) {
        const double d = std::fabs(double(outL[i]) - double(expected[i]));
        if (d > maxDiff) maxDiff = d;
        if (std::fabs(outL[i]) > peak) peak = std::fabs(outL[i]);
    }
    check(maxDiff < 1e-6, "output matches the DSP core sample for sample (incl. mid-block change)");
    check(peak > 0.05, "output is audible");
    bool finite = true;
    for (int i = 0; i < kTotal; ++i) finite = finite && std::isfinite(outL[i]) && std::isfinite(outR[i]);
    check(finite, "all output samples finite");
    bool same = true;
    for (int i = 0; i < kTotal; ++i) same = same && (outL[i] == outR[i]);
    check(same, "identical mono input gives identical left and right output");

    // State: save, change a value, load, and read the value back.
    if (state) {
        struct Buf { std::vector<char> data; size_t pos = 0; } buf;
        clap_ostream_t os{};
        os.ctx = &buf;
        os.write = [](const clap_ostream_t* s, const void* p, uint64_t n) -> int64_t {
            auto* b = static_cast<Buf*>(s->ctx);
            const char* c = static_cast<const char*>(p);
            b->data.insert(b->data.end(), c, c + n);
            return int64_t(n);
        };
        check(state->save(plugin, &os), "state saved");

        double before = 0.0;
        params->get_value(plugin, 3, &before);
        EventList change;
        change.events.push_back(makeParam(0, 3, 0.2));
        clap_input_events_t ev{};
        ev.ctx = &change;
        ev.size = EventList::size;
        ev.get = EventList::get;
        params->flush(plugin, &ev, nullptr);

        clap_istream_t is{};
        is.ctx = &buf;
        buf.pos = 0;
        is.read = [](const clap_istream_t* s, void* p, uint64_t n) -> int64_t {
            auto* b = static_cast<Buf*>(s->ctx);
            const size_t avail = b->data.size() - b->pos;
            const size_t take = std::min<size_t>(avail, size_t(n));
            std::memcpy(p, b->data.data() + b->pos, take);
            b->pos += take;
            return int64_t(take);
        };
        check(state->load(plugin, &is), "state loaded");
        double after = 0.0;
        params->get_value(plugin, 3, &after);
        check(std::fabs(after - before) < 1e-12, "state restores the gain value");
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(lib);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
