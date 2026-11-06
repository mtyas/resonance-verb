// SPDX-License-Identifier: AGPL-3.0-or-later
// Minimal CLAP host that reproduces clap-validator's "state-reproducibility" check:
// randomise every parameter through process() events, save the state, load it into a
// fresh instance and compare parameter values (and the re-saved bytes).
//   clap_state_check "<path>/Resonance Verb.clap"
#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
  #define NOMINMAX
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif
#include <random>
#include <string>
#include <vector>

namespace
{
const void* hostGetExtension (const clap_host_t*, const char*) { return nullptr; }
void hostNoop (const clap_host_t*) {}

clap_host_t makeHost()
{
    clap_host_t h {};
    h.clap_version = CLAP_VERSION;
    h.name = "rv clap_state_check";
    h.vendor = "Resonance Verb";
    h.url = "";
    h.version = "1";
    h.get_extension = hostGetExtension;
    h.request_restart = hostNoop;
    h.request_process = hostNoop;
    h.request_callback = hostNoop;
    return h;
}

struct EventList
{
    std::vector<clap_event_param_value_t> events;
    static uint32_t size (const clap_input_events_t* l) { return (uint32_t) static_cast<EventList*> (l->ctx)->events.size(); }
    static const clap_event_header_t* get (const clap_input_events_t* l, uint32_t i)
    {
        return &static_cast<EventList*> (l->ctx)->events[i].header;
    }
};
bool tryPush (const clap_output_events_t*, const clap_event_header_t*) { return true; }

struct MemStream
{
    std::vector<uint8_t> data;
    size_t readPos = 0;
    size_t chunk = 0;   // 0 = unlimited; otherwise max bytes per read (like the "buffered" test)
    static int64_t write (const clap_ostream_t* s, const void* buf, uint64_t n)
    {
        auto* m = static_cast<MemStream*> (s->ctx);
        m->data.insert (m->data.end(), (const uint8_t*) buf, (const uint8_t*) buf + n);
        return (int64_t) n;
    }
    static int64_t read (const clap_istream_t* s, void* buf, uint64_t n)
    {
        auto* m = static_cast<MemStream*> (s->ctx);
        uint64_t left = m->data.size() - m->readPos;
        uint64_t take = std::min<uint64_t> (n, left);
        if (m->chunk > 0) take = std::min<uint64_t> (take, m->chunk);
        std::memcpy (buf, m->data.data() + m->readPos, take);
        m->readPos += take;
        return (int64_t) take;
    }
};

struct Instance
{
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;

    Instance (const clap_plugin_factory_t* f, const clap_host_t* host, const char* id)
    {
        plugin = f->create_plugin (f, host, id);
        if (plugin == nullptr || ! plugin->init (plugin)) { std::puts ("create/init failed"); std::exit (2); }
        params = static_cast<const clap_plugin_params_t*> (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*> (plugin->get_extension (plugin, CLAP_EXT_STATE));
    }
    ~Instance() { plugin->destroy (plugin); }

    std::vector<double> values() const
    {
        std::vector<double> v;
        for (uint32_t i = 0; i < params->count (plugin); ++i)
        {
            clap_param_info_t info {};
            params->get_info (plugin, i, &info);
            double x = 0;
            params->get_value (plugin, info.id, &x);
            v.push_back (x);
        }
        return v;
    }

    /** clap-validator's way: params->flush() on a deactivated plugin, no processing. */
    void flushWith (EventList& events)
    {
        clap_input_events_t inEv { &events, EventList::size, EventList::get };
        clap_output_events_t outEv { nullptr, tryPush };
        params->flush (plugin, &inEv, &outEv);
    }

    void processWith (EventList& events)
    {
        // Give every declared audio port a buffer, as a real host does (main + sidechain inputs).
        auto* ports = static_cast<const clap_plugin_audio_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_AUDIO_PORTS));
        auto makeBuffers = [&] (bool isInput)
        {
            const uint32_t n = ports ? ports->count (plugin, isInput) : 0;
            std::vector<clap_audio_buffer_t> bufs (n);
            for (uint32_t i = 0; i < n; ++i)
            {
                clap_audio_port_info_t info {};
                ports->get (plugin, i, isInput, &info);
                bufs[i].channel_count = info.channel_count;
            }
            return bufs;
        };
        auto ins = makeBuffers (true), outs = makeBuffers (false);
        std::vector<std::vector<float>> storage;
        std::vector<std::vector<float*>> ptrs;
        auto attach = [&] (std::vector<clap_audio_buffer_t>& bufs)
        {
            for (auto& b : bufs)
            {
                ptrs.emplace_back();
                for (uint32_t c = 0; c < b.channel_count; ++c)
                {
                    storage.emplace_back (512, 0.0f);
                    ptrs.back().push_back (storage.back().data());
                }
            }
        };
        storage.reserve (64); ptrs.reserve (16);
        attach (ins); attach (outs);
        size_t k = 0;
        for (auto& b : ins)  b.data32 = ptrs[k++].data();
        for (auto& b : outs) b.data32 = ptrs[k++].data();

        plugin->activate (plugin, 48000.0, 32, 512);
        plugin->start_processing (plugin);
        clap_input_events_t inEv { &events, EventList::size, EventList::get };
        clap_output_events_t outEv { nullptr, tryPush };
        clap_process_t p {};
        p.frames_count = 512;
        p.steady_time = 0;
        p.audio_inputs = ins.data();   p.audio_inputs_count = (uint32_t) ins.size();
        p.audio_outputs = outs.data(); p.audio_outputs_count = (uint32_t) outs.size();
        p.in_events = &inEv; p.out_events = &outEv;
        plugin->process (plugin, &p);
        plugin->stop_processing (plugin);
        plugin->deactivate (plugin);
    }
};
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage: clap_state_check <plugin.clap>"); return 2; }
    std::string bin = argv[1];
#ifdef __APPLE__
    {
        const auto slash = bin.find_last_of ('/');
        std::string name = bin.substr (slash + 1);
        name = name.substr (0, name.size() - 5);   // strip ".clap"
        bin += "/Contents/MacOS/" + name;
    }
#endif
    std::setvbuf (stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
    HMODULE lib = LoadLibraryA (bin.c_str());
    if (lib == nullptr) { std::printf ("LoadLibrary failed: %lu\n", (unsigned long) GetLastError()); return 2; }
    auto* entry = reinterpret_cast<const clap_plugin_entry_t*> (GetProcAddress (lib, "clap_entry"));
#else
    void* lib = dlopen (bin.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (lib == nullptr) { std::printf ("dlopen failed: %s\n", dlerror()); return 2; }
    auto* entry = static_cast<const clap_plugin_entry_t*> (dlsym (lib, "clap_entry"));
#endif
    if (entry == nullptr || ! entry->init (argv[1])) { std::puts ("clap_entry init failed"); return 2; }
    auto* factory = static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID));
    const auto* desc = factory->get_plugin_descriptor (factory, 0);
    clap_host_t host = makeHost();

    int failures = 0;
    struct Mode { const char* name; bool viaFlush; size_t chunk; };
    for (const Mode mode : { Mode { "process, basic", false, 0 }, Mode { "process, buffered (23-byte reads)", false, 23 },
                             Mode { "flush while deactivated (clap-validator)", true, 0 },
                             Mode { "flush while deactivated, buffered", true, 17 } })
    {
        const size_t chunk = mode.chunk;
        std::vector<double> expected, savedA;
        MemStream saved;
        {
            Instance a (factory, &host, desc->id);
            EventList ev;
            std::mt19937 rng (1234);
            for (uint32_t i = 0; i < a.params->count (a.plugin); ++i)
            {
                clap_param_info_t info {};
                a.params->get_info (a.plugin, i, &info);
                double v = info.min_value + (info.max_value - info.min_value) * std::uniform_real_distribution<double> (0, 1) (rng);
                if (info.flags & CLAP_PARAM_IS_STEPPED) v = std::round (v);
                clap_event_param_value_t e {};
                e.header.size = sizeof (e); e.header.time = 0; e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.header.type = CLAP_EVENT_PARAM_VALUE; e.header.flags = 0;
                e.param_id = info.id; e.cookie = info.cookie; e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = v;
                ev.events.push_back (e);
            }
            if (mode.viaFlush) a.flushWith (ev); else a.processWith (ev);
            expected = a.values();
            clap_ostream_t os { &saved, MemStream::write };
            if (! a.state->save (a.plugin, &os)) { std::puts ("save failed"); return 1; }
        }
        MemStream resaved;
        std::vector<double> got;
        {
            Instance b (factory, &host, desc->id);
            saved.chunk = chunk;
            clap_istream_t is { &saved, MemStream::read };
            if (! b.state->load (b.plugin, &is)) { std::puts ("load failed"); return 1; }
            got = b.values();
            clap_ostream_t os { &resaved, MemStream::write };
            b.state->save (b.plugin, &os);
        }
        int wrong = 0;
        for (size_t i = 0; i < expected.size(); ++i)
            if (std::abs (expected[i] - got[i]) > 1e-4 * std::max (1.0, std::abs (expected[i])))
            {
                if (wrong < 5) std::printf ("    param %zu: expected %.4f got %.4f\n", i, expected[i], got[i]);
                ++wrong;
            }
        const bool sameBytes = saved.data == resaved.data;
        std::printf ("  [%s] %s: %d of %zu parameters differ after reload; re-saved bytes %s\n",
                     wrong == 0 && sameBytes ? "PASS" : "FAIL", mode.name,
                     wrong, expected.size(), sameBytes ? "identical" : "DIFFERENT");
        failures += (wrong != 0 || ! sameBytes);
    }
    entry->deinit();
#ifdef _WIN32
    FreeLibrary (lib);
#else
    dlclose (lib);
#endif
    std::printf ("%s\n", failures ? "CLAP STATE CHECK FAILED" : "CLAP STATE CHECK PASSED");
    return failures ? 1 : 0;
}
