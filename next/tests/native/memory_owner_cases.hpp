#pragma once

namespace closure {
inline int memory_owner_census(const char *source_path, const char *frame_path) {
    const auto *output = std::getenv("WVD_CLOSURE_ROOT");
    check(output && *output, "WVD_CLOSURE_ROOT_REQUIRED");
    const auto directory = std::filesystem::path(output) / "ownership-bundle";
    check(!std::filesystem::exists(directory), "OWNERSHIP_OUTPUT_ALREADY_EXISTS");
    const auto source = std::filesystem::absolute(source_path);
    recognition::Bundle bundle{directory, "ownership-census", {}};
    std::uint64_t copied_bytes{};
    for (const auto &file : std::filesystem::recursive_directory_iterator(source)) {
        if (!file.is_regular_file()) continue;
        const auto relative = std::filesystem::relative(file.path(), source);
        const auto target = directory / relative;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(file.path(), target);
        copied_bytes += file.file_size();
        const auto encoded = relative.generic_u8string();
        const std::string name(reinterpret_cast<const char *>(encoded.data()), encoded.size());
        bundle.files.push_back({name, platform::file_sha256(target)});
    }
    const auto sample = [] {
        const auto memory = platform::sample_memory();
        J result{{"process_ok", memory.process_ok}, {"private_bytes", memory.private_bytes},
            {"handles", memory.handle_count}, {"process_id", memory.process_id},
            {"process_created_100ns", memory.process_created_100ns}};
        const auto counts = platform::MemoryOwnerLifetime::counts();
        for (std::size_t i = 0; i < counts.size(); ++i)
            result["objects"][std::to_string(i)] = {{"created", counts[i].created},
                {"destroyed", counts[i].destroyed}, {"live", counts[i].live}, {"ready", counts[i].ready}};
        return result;
    };
    J report{{"scope", "one real OCR initialization/inference/destruction; isolated copy; no device"},
        {"source_pack", source.generic_string()}, {"source_frame_sha256", platform::file_sha256(frame_path)},
        {"copied_file_bytes", copied_bytes}, {"samples", J::array()}};
    report["samples"].push_back({{"phase", "before_service"}, {"memory", sample()}});
    storage::LoggingPolicy logging;
    logging.level = storage::LogLevel::Debug;
    logging.memory = true;
    auto service = std::make_shared<recognition::Service>(bundle, games::vision::native_handlers(
        read("packs/wvd/manifest.json").value("aliases", J::object()), "zh-Hant"),
        std::shared_ptr<recognition::MatchBudget>{}, std::filesystem::path(output) / "recognition-memory.log", 1, 1, logging);
    std::weak_ptr<const platform::BundleLease> weak_lease = service->bundle().lease;
    report["samples"].push_back({{"phase", "service_ready"}, {"memory", sample()}, {"owners", service->ownership_snapshot()}});
    auto image = cv::imread(frame_path, cv::IMREAD_COLOR);
    check(!image.empty() && image.cols == 900 && image.rows == 1600, "OWNERSHIP_FRAME_INVALID");
    contracts::FrameEnvelope frame;
    frame.identity = {"recorded-ownership", "wvd", bundle.revision, "900x1600", 1, 1, 0,
        {900, 1600}, {900, 1600}, std::chrono::steady_clock::now(), "BGR8", 1, "replay", "jp.co.drecom.wizardry.daphne"};
    frame.raw_bgr = std::make_shared<const std::vector<std::uint8_t>>(image.data, image.data + image.total() * image.elemSize());
    recognition::OcrParameters parameters;
    parameters.language = "zh-Hant";
    parameters.expected_text = {"HP"};
    recognition::Request request{"ownership.ocr", "1", {0, 1150, 900, 450}, parameters};
    const auto observed = service->evaluate(frame, frame.identity, request);
    check(observed.outcome != contracts::RecognitionOutcome::Error, "OWNERSHIP_OCR:" + observed.error_code);
    report["samples"].push_back({{"phase", "ocr_ready"}, {"memory", sample()}, {"owners", service->ownership_snapshot()}});
    check(service->ownership_snapshot().at("bundle_lease").at("held_file_bytes") == copied_bytes,
        "OWNERSHIP_PINNED_FILES_MUST_ACCOUNT_EXACTLY");
    const auto live_before = platform::MemoryOwnerLifetime::counts();
    service.reset();
    const auto after = platform::MemoryOwnerLifetime::counts();
    report["samples"].push_back({{"phase", "service_released"}, {"memory", sample()}, {"lease_released", weak_lease.expired()}});
    frame.raw_bgr.reset();
    image.release();
    report["samples"].push_back({{"phase", "test_frames_released"}, {"memory", sample()}});
    std::ofstream(std::filesystem::path(output) / "memory-owner-census.json") << report.dump(2);
    check(weak_lease.expired() && after[0].live + 1 == live_before[0].live &&
        after[1].live + 1 == live_before[1].live, "OWNERSHIP_RELEASE_MUST_REMOVE_SERVICE_MODEL_AND_PINNED_COPY");
    std::cout << "memory owners: pinned file bytes accounted exactly, one real zh-Hant OCR ready/released, lifetime and owner boundary evidence PASS\n";
    return 0;
}
}
