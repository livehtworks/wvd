#pragma once

namespace skill_level_contract_test {
inline int run(const std::filesystem::path &pack, const std::filesystem::path &output) {
    using namespace wvd;
    using namespace closure;
    check(!std::filesystem::exists(output), "SKILL_LEVEL_OUTPUT_NOT_FRESH");
    std::filesystem::create_directories(output);
    const auto manifest = read((pack / "manifest.json").string().c_str());
    recognition::Bundle origin{pack, manifest.at("revision"), {}};
    for (const auto &member : manifest.at("files"))
        origin.files.push_back({member.at("path"), member.at("sha256")});
    C graph("skill.level.contract");
    J probes = J::array();
    for (int level = 1; level <= 7; ++level)
        probes.push_back({{"mode", "skill_level"}, {"level", level}});
    graph.observe("Entry", {{"mode", "any"}, {"conditions", probes}}, {"Terminal"});
    const auto flow = graph.finish();
    recognition::Bundle bundle{output / "bundle", origin.revision, {}};
    bundle.snapshot_parent = output / "snapshots";
    std::set<std::string> copied;
    for (const auto &name : flow.images) {
        const auto source = games::vision::resolve_image_source(origin, manifest.value("aliases", J::object()), name);
        if (!copied.insert(source.relative_path).second) continue;
        const auto target = bundle.root / source.relative_path;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(pack / source.relative_path, target);
        bundle.files.push_back({source.relative_path, platform::file_sha256(target)});
    }
    recognition::Service service(bundle, games::vision::native_handlers(
        manifest.value("aliases", J::object()), "zh-Hant"));
    const auto fixtures = std::filesystem::absolute("tests/fixtures/skill-level");
    const auto cases = read((fixtures / "cases.json").string().c_str());
    J results = J::array();
    std::uint64_t frame_id{};
    for (const auto &sample : cases.at("frames")) {
        const auto path = fixtures / sample.at("file").get<std::string>();
        check(platform::file_sha256(path) == sample.at("sha256").get<std::string>(), "SKILL_LEVEL_FRAME_CHANGED");
        const auto pixels = cv::imread(path.string(), cv::IMREAD_COLOR);
        check(pixels.size() == cv::Size(900, 1600), "SKILL_LEVEL_FRAME_INVALID");
        contracts::FrameEnvelope frame;
        frame.identity.device_id = "recorded-level"; frame.identity.game_id = "wvd";
        frame.identity.pack_revision = bundle.revision; frame.identity.viewport_id = "900x1600";
        frame.identity.generation = 1;
        frame.identity.raw_size = frame.identity.recognition_size = {900, 1600};
        frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(
            pixels.data, pixels.data + pixels.total() * pixels.elemSize());
        for (int level = 1; level <= 7; ++level) for (const bool selected : {false, true}) {
            frame.identity.frame_id = ++frame_id;
            frame.identity.captured_at = frame.identity.capture_finished_at = std::chrono::steady_clock::now();
            const bool expected = selected ? level == 5 : !(sample.at("ripple").get<bool>() && level == 3);
            const J condition{{"mode", "skill_level"}, {"level", level}, {"selected", selected}};
            const auto observed = service.evaluate(frame, frame.identity, {"level", "1", {0, 0, 900, 1600},
                recognition::CustomParameters{"WvdVision", condition}});
            results.push_back({{"frame", sample.at("file")}, {"level", level}, {"selected", selected},
                {"expected_hit", expected}, {"outcome", int(observed.outcome)}, {"evidence", observed.evidence}});
            std::ofstream(output / "results.json") << results.dump(2);
            check(observed.outcome == (expected ? contracts::RecognitionOutcome::Hit : contracts::RecognitionOutcome::NoHit),
                "SKILL_LEVEL_IDENTITY_CHANGED:" + sample.at("file").get<std::string>() + ":" + std::to_string(level));
            if (expected) {
                check(observed.action_eligible && observed.box.has_value(), "SKILL_LEVEL_TARGET_MISSING");
                const int x = level == 7 ? 99 : 99 + (level - 1) * 125;
                const int y = level == 7 ? 1281 : 1211;
                check(observed.box->x == x && observed.box->y == y &&
                    observed.box->width == 60 && observed.box->height == 34, "SKILL_LEVEL_WRONG_BUTTON");
            } else check(!observed.box.has_value(), "SKILL_LEVEL_REJECTED_BUT_CLICKABLE");
        }
    }
    std::cout << "PASS 28 real-frame level targets/selection checks; Lv5 cannot confirm Lv3 under touch ripple\n";
    return 0;
}
}
