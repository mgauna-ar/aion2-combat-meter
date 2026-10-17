// aion2meter-replay: runs a capture file through the meter's own pipeline and
// prints what it found. The game cannot run on the Mac, so this is where decoding
// work happens: record on the Windows PC (README, "Recording a capture"), replay here.

#include "report.hpp"

#include "aion2/version.hpp"
#include "capture/capture_file.hpp"
#include "capture/tcp_reassembler.hpp"
#include "meter/history_store.hpp"
#include "pipeline/pipeline.hpp"
#include "protocol/entity_id.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace aion2;

struct Options {
    std::string file;
    uint16_t port{13328};
    bool any_port{true};
    bool fold{true};
    bool quiet{false};
    std::string save_history;
    replay::ReportOptions report;
};

void usage() {
    std::fprintf(stderr,
                 "aion2meter-replay %s\n"
                 "usage: aion2meter-replay <capture.pcapng|.pcap> [options]\n"
                 "  --fights          a table per fight: damage, DPS, crits, boss, end reason\n"
                 "  --skills          --fights, and each player's skills, healing and damage\n"
                 "                    taken\n"
                 "  --events          every decoded event but heartbeats, one per line\n"
                 "  --opcodes         count, body sizes and parse failures per opcode\n"
                 "  --unknown         hex dump frames the parser does not read or failed on\n"
                 "  --dump OPS        hex dump frames of these opcodes: hex, comma separated\n"
                 "                    (0438,4136), or 'all'\n"
                 "  --limit N         dump at most N frames per opcode (default 20, 0 = no limit)\n"
                 "  --anonymize       print every name as #id, for output you will share\n"
                 "  --quiet           leave out the closing capture summary (for diffing)\n"
                 "  --port N          game server port (default 13328)\n"
                 "  --port-only       decode only flows from the server port; skip heartbeat\n"
                 "                    detection on other flows (ping reducers relay on others)\n"
                 "  --no-fold         keep large entity ids as they are (see AGENTS.md)\n"
                 "  --save-history D  save fights worth keeping to folder D, as the meter does\n",
                 VERSION_STRING);
}

std::optional<uint16_t> parse_opcode(std::string_view text) {
    if (text.empty() || text.size() > 4) return std::nullopt;
    unsigned value = 0;
    for (const char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
        else return std::nullopt;
    }
    return static_cast<uint16_t>(value);
}

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&](const char* name) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                return std::nullopt;
            }
            return std::string(argv[++i]);
        };
        if (arg == "--fights") {
            opt.report.fights = true;
        } else if (arg == "--skills") {
            opt.report.fights = true;
            opt.report.skills = true;
        } else if (arg == "--events") {
            opt.report.events = true;
        } else if (arg == "--opcodes") {
            opt.report.opcodes = true;
        } else if (arg == "--unknown") {
            opt.report.unknown = true;
        } else if (arg == "--anonymize") {
            opt.report.anonymize = true;
        } else if (arg == "--quiet") {
            opt.quiet = true;
        } else if (arg == "--port-only") {
            opt.any_port = false;
        } else if (arg == "--no-fold") {
            opt.fold = false;
        } else if (arg == "--port") {
            const auto v = value("--port");
            if (!v) return false;
            const long port = std::strtol(v->c_str(), nullptr, 10);
            if (port <= 0 || port > 65535) return false;
            opt.port = static_cast<uint16_t>(port);
        } else if (arg == "--limit") {
            const auto v = value("--limit");
            if (!v) return false;
            opt.report.dump_limit = static_cast<size_t>(std::strtoul(v->c_str(), nullptr, 10));
        } else if (arg == "--save-history") {
            const auto v = value("--save-history");
            if (!v) return false;
            opt.save_history = *v;
        } else if (arg == "--dump") {
            const auto v = value("--dump");
            if (!v) return false;
            if (*v == "all") {
                opt.report.dump_all = true;
                continue;
            }
            std::string_view rest = *v;
            while (!rest.empty()) {
                const size_t comma = rest.find(',');
                const auto op = parse_opcode(rest.substr(0, comma));
                if (!op) {
                    std::fprintf(stderr, "bad opcode in --dump: %s\n", v->c_str());
                    return false;
                }
                opt.report.dump.insert(*op);
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            }
        } else if (arg == "-h" || arg == "--help") {
            return false;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            return false;
        } else if (opt.file.empty()) {
            opt.file = std::string(arg);
        } else {
            std::fprintf(stderr, "one capture file at a time\n");
            return false;
        }
    }
    return !opt.file.empty();
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        usage();
        return 2;
    }
    proto::set_entity_id_folding(opt.fold);

    std::string error;
    auto reader = capture::CaptureFileReader::open(opt.file, error);
    if (!reader) {
        std::fprintf(stderr, "%s: %s\n", opt.file.c_str(), error.c_str());
        return 1;
    }

    pipeline::PipelineOptions options;
    options.demux.server_port = opt.port;
    options.demux.any_port = opt.any_port;
    pipeline::Pipeline pipe(options);
    replay::Report report(opt.report);
    std::optional<meter::HistoryStore> history;
    if (!opt.save_history.empty()) history.emplace(opt.save_history);
    size_t saved = 0;

    pipe.set_frame_observer([&](const proto::Frame& f, pipeline::FrameOutcome o) { report.on_frame(f, o); });
    pipe.set_event_observer([&](const proto::ParsedEvent& e) { report.on_event(e, pipe.engine().registry()); });
    const auto drain_fights = [&] {
        for (const auto& fight : pipe.take_finished()) {
            report.on_fight(fight);
            if (history && meter::worth_saving(fight, options.meter)) {
                const std::string why = history->save(fight);
                if (!why.empty()) std::fprintf(stderr, "cannot save fight %" PRIu64 ": %s\n", fight.id, why.c_str());
                else ++saved;
            }
        }
    };

    capture::CapturedPacket pkt;
    Micros last_ts = 0;
    while (reader->next(pkt)) {
        pipe.on_packet(pkt.link_type, pkt.data, pkt.ts);
        if (pkt.ts > last_ts) last_ts = pkt.ts;
        drain_fights();
    }
    if (!reader->error().empty()) std::fprintf(stderr, "warning: %s; stopped there\n", reader->error().c_str());

    // The capture ended: holes will never fill, a boss killed just before the end
    // still gets its grace and ends as a kill, and a fight still open ends here, as
    // the meter would once the game went quiet.
    pipe.tick(last_ts + std::max(capture::TcpReassembler::kHoleTimeout, from_seconds(options.meter.kill_grace_s)));
    pipe.end_fight(meter::EndReason::CaptureLost, last_ts);
    drain_fights();
    if (opt.quiet) return 0;

    std::printf("\n%s: %" PRIu64 " packets, %" PRIu64 " TCP segments, %" PRIu64 " frames, %zu opcodes, %.1f s, "
                "%zu fights\n",
                opt.report.anonymize ? "capture" : opt.file.c_str(), pipe.demux().packets(),
                pipe.demux().tcp_segments(), report.frames(), report.distinct_opcodes(), to_seconds(report.span()),
                report.fights());
    report.print_flows(pipe.demux());
    report.print_parse(pipe.parser().stats());
    if (opt.report.opcodes) report.print_opcodes();
    if (history) std::printf("saved %zu fights to %s\n", saved, opt.save_history.c_str());
    return 0;
}
