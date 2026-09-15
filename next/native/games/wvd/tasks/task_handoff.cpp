#include "task_handoff.hpp"
#include "gold_income.hpp"
#include "workflow_session.hpp"
#include "games/wvd/state.hpp"
#include "games/wvd/recovery/boot.hpp"
#include "platform/windows/file_digest.hpp"
#include "storage/run_store.hpp"
#include <fstream>

namespace wvd::games::tasks {
using J = nlohmann::json;
using namespace std::chrono_literals;
namespace {
std::string digest(const J &value) {
    const auto text = value.dump();
    return platform::bytes_sha256({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}
J task_json(const WvdQuestDefinition &task) {
    return {{"id", task.id}, {"type", task.type}, {"source", task.source}};
}
J read_record(const std::filesystem::path &path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 32 * 1024 * 1024)
        throw std::runtime_error("HANDOFF_RESULT_INVALID");
    std::ifstream input(path, std::ios::binary);
    J result;
    if (!input || !(input >> result))
        throw std::runtime_error("HANDOFF_RESULT_INVALID");
    return result;
}
maafw::RecognitionRequest request(const J &parameters) {
    return {"wvd.unknown_leap", "1", {0, 0, 900, 1600},
        maafw::RecognitionRequest::CustomParameters{"WvdVision", parameters}};
}
}
J freeze_handoff_source(const runtime::RunDefinition &definition, const WvdProfile &profile,
                        const WvdQuestDefinition &task, const WvdQuestCatalog &catalog) {
    if (!definition.state_factory || definition.state_factory->implementation.id != "wvd.state" ||
        definition.state_factory->parameters.at("profile") != profile.values ||
        profile.values.at("FARM_TARGET").get<std::string>() != task.id || profile.sources.size() != profile.values.size())
        throw std::runtime_error("HANDOFF_SOURCE_PROFILE_MISMATCH");
    const auto &target = catalog.at("7000G");
    if (target.type != "quest" || task.id.empty() || task.source != catalog.at(task.id).source ||
        task.type != catalog.at(task.id).type)
        throw std::runtime_error("HANDOFF_SOURCE_TASK_MISMATCH");
    J source{{"schema", 1}, {"request_id", definition.request_id},
        {"task", task_json(task)}, {"target_task", task_json(target)},
        {"profile_digest", digest(profile.values)}, {"profile_sources", profile.sources},
        {"selected_section", profile.selected_section},
        {"legacy_document_digest", digest(profile.legacy_document)},
        {"legacy_passthrough_digest", digest(profile.legacy_passthrough)}};
    source["digest"] = digest(source);
    return source;
}
void validate_handoff_source(const J &source, const J &profile) {
    auto body = source;
    const auto expected = body.at("digest").get<std::string>();
    body.erase("digest");
    if (body.at("schema") != 1 || digest(body) != expected ||
        body.at("profile_digest").get<std::string>() != digest(profile) ||
        profile.at("FARM_TARGET") != body.at("task").at("id") ||
        body.at("target_task").at("id") != "7000G" || body.at("target_task").at("type") != "quest" ||
        !body.at("profile_sources").is_object() || !body.at("selected_section").is_string() ||
        body.at("request_id").get<std::string>().empty())
        throw std::runtime_error("HANDOFF_SOURCE_INVALID");
    for (const auto &[key, value] : profile.items()) {
        (void)value;
        if (!body.at("profile_sources").contains(key) || !body.at("profile_sources").at(key).is_string())
            throw std::runtime_error("HANDOFF_SOURCE_INVALID");
    }
}
bool handoff_has_unconfirmed_effect(const J &business) {
    for (const char *pointer : {"/inn_payment_pending", "/karma_pending", "/bounty_report_pending",
            "/special_dialogue_pending", "/manual_separation/transfer_pending",
            "/featured_visit/pending", "/golden_chest/leap_pending", "/sandman/leap_pending",
            "/gold_income/pending", "/bull_cave/leap_pending", "/steel_trial/pending",
            "/repel_forces/pending", "/bounty_cycle/transfer_pending", "/fishing/casting_pending",
            "/fordraig/pending", "/cave_of_separation/pending",
            "/fishing/reward_pending", "/fishing/transfer_pending"})
        if (business.at(J::json_pointer(pointer)).get<bool>())
            return true;
    return false;
}
bool observe_unknown_leap(maafw::Context &context, const J &parameters) {
    if (context.cancelled())
        return false;
    const auto frame = context.capture();
    // 复用原unknown.window的计数，不能让一次模板确认算作第六次未知。
    J unknown_parameters{{"mode", "unknown_exhausted"}, {"max_tries", 4}};
    // 节点识别后会重新截图，专属正常页条件也必须随之复核，不能沿用旧帧的否定结果。
    if (parameters.contains("extra_known"))
        unknown_parameters["extra_known"] = parameters.at("extra_known");
    const auto unknown = context.recognize(frame, request(unknown_parameters));
    if (unknown.outcome == contracts::RecognitionOutcome::Error)
        throw std::runtime_error(unknown.error_code);
    if (unknown.outcome != contracts::RecognitionOutcome::Hit)
        return false;
    const auto leap = context.recognize(frame,
        request({{"mode", "template"}, {"image", "cursedWheel_timeLeap"}, {"threshold", .8}}));
    if (leap.outcome == contracts::RecognitionOutcome::Error)
        throw std::runtime_error(leap.error_code);
    if (leap.outcome != contracts::RecognitionOutcome::Hit)
        return false;
    const auto samples = unknown.evidence.at("evidence").at("samples").get<std::uint64_t>();
    J receipt;
    bool changed = false;
    const bool accepted = context.with_business_state([&](contracts::BusinessRunState &base) {
        if (context.cancelled())
            return false;
        // unknown是boolean-only信息，绝不将它兑换成输入许可；这里只冻结业务意图。
        if (!context.current_observation(unknown) || !context.current_observation(leap))
            throw std::runtime_error("LEAP_OBSERVATION_STALE");
        auto &state = dynamic_cast<WvdRunState &>(base);
        changed = state.observe_unknown_leap(samples, frame.identity.generation, frame.identity.frame_id);
        receipt = state.summary().at("handoff_intent");
        return changed;
    });
    if (accepted && changed)
        context.business_event("leap.intent", receipt);
    return accepted && changed;
}
void register_task_handoff(runtime::BehaviorRegistry &registry) {
    registry.add_action({"wvd.unknown_leap", "1"},
        [](maafw::Context &context, const J &parameters, const J &) { return observe_unknown_leap(context, parameters); });
}
contracts::BehaviorBinding unknown_leap_binding() {
    return {"WvdUnknownLeap", {"wvd.unknown_leap", "1"}, J::object()};
}
TaskHandoff::TaskHandoff(runtime::RunCoordinator &coordinator,
                        std::shared_ptr<const runtime::BehaviorRegistry> registry,
                        maafw::Bundle assets, J aliases, std::optional<maafw::Bundle> mod, bool allow_download)
    : coordinator_(coordinator), registry_(std::move(registry)), assets_(std::move(assets)),
      aliases_(std::move(aliases)), mod_(std::move(mod)), allow_download_(allow_download) {
    if (!registry_ || !registry_->sealed())
        throw std::runtime_error("REGISTRY_NOT_SEALED");
}
contracts::RunSnapshot TaskHandoff::start_source(runtime::RunDefinition definition,
    const WvdProfile &profile, const WvdQuestDefinition &task, const WvdQuestCatalog &catalog,
    std::shared_ptr<devices::DeviceBackend> backend) {
    std::lock_guard lock(mutex_);
    if (!backend || !backend->offline())
        throw std::runtime_error("HANDOFF_OFFLINE_ONLY");
    if (source_)
        throw std::runtime_error("HANDOFF_SOURCE_ALREADY_STARTED");
    provenance_ = freeze_handoff_source(definition, profile, task, catalog);
    definition.state_factory->parameters["handoff_source"] = provenance_;
    std::lock_guard starting(start_stop_mutex_);
    if (stopped_)
        throw std::runtime_error("HANDOFF_STOPPED");
    const auto snapshot = coordinator_.start(definition, backend);
    backend_ = std::move(backend);
    source_ = std::move(definition);
    source_run_ = snapshot.run_id;
    return snapshot;
}
void TaskHandoff::request_stop() {
    // 编译下一包期间也可立即停止；不能等poll的文件发布锁才发出旧Run停止请求。
    stopped_ = true;
    std::lock_guard starting(start_stop_mutex_);
    coordinator_.request_stop();
}
std::optional<contracts::RunSnapshot> TaskHandoff::poll(const std::filesystem::path &destination) {
    std::lock_guard lock(mutex_);
    if (!source_ || stopped_)
        return std::nullopt;
    if (next_) {
        std::lock_guard starting(start_stop_mutex_);
        if (stopped_)
            return std::nullopt;
        return coordinator_.start(*next_, backend_); // 同一请求交回核心幂等表，不启动第二Run。
    }
    if (!coordinator_.wait_for(0ms))
        return std::nullopt; // 比quiescent更晚：结果提交和设备租约释放都已退出。
    const auto snapshot = coordinator_.snapshot();
    if (snapshot.run_id != source_run_)
        throw std::runtime_error("HANDOFF_SOURCE_RUN_CHANGED");
    if (!snapshot.quiescent || !snapshot.result_saved || !snapshot.storage_error.empty())
        throw std::runtime_error("HANDOFF_RESULT_NOT_COMMITTED");
    if (snapshot.state != contracts::RunState::Interrupted || snapshot.reason != "RECOVERY_REQUIRED" ||
        snapshot.sessions.empty() || snapshot.sessions.back().at("reason") != "leap.unknown")
        return std::nullopt;
    const auto &intent = snapshot.business.at("handoff_intent");
    if (!intent.is_object() || intent.at("kind") != "turn_to_7000G")
        return std::nullopt;
    if (snapshot.business.at("handoff_source") != provenance_ ||
        intent.at("run_identity") != snapshot.business.at("run_identity") ||
        intent.at("generation") != snapshot.generation || intent.at("unknown_samples") < 5 ||
        handoff_has_unconfirmed_effect(snapshot.business))
        throw std::runtime_error("HANDOFF_RESULT_SOURCE_MISMATCH");
    const auto directory = coordinator_.run_directory();
    const auto saved = read_record(directory / "result.json");
    const auto run = read_record(directory / "run.json");
    if (saved.at("business") != snapshot.business || saved.at("state") != "Interrupted" ||
        !saved.at("quiescent").get<bool>() || !saved.at("result_saved").get<bool>() ||
        saved.at("run_id") != source_run_ ||
        run.at("definition").at("request_id").get<std::string>() != source_->request_id ||
        run.at("definition").at("state_factory").at("parameters").at("handoff_source") != provenance_)
        throw std::runtime_error("HANDOFF_RESULT_SOURCE_MISMATCH");

    // 到这里才产生新业务请求。沿用已经选定的33字段，只有FARM_TARGET换成7000G；
    // 不重读配置，不重新挑选7000G区段，不写用户profile/config，也不重置旧Run统计。
    auto profile = source_->state_factory->parameters.at("profile");
    profile["FARM_TARGET"] = "7000G";
    const auto &target = provenance_.at("target_task");
    runtime::RunDefinition next;
    next.request_id = "handoff-" + digest({{"source", provenance_.at("digest")}, {"intent", intent.at("intent_id")}});
    next.policy = source_->policy;
    auto workflow = gold_income_cycle({target.at("id"), target.at("type"), target.at("source")}, allow_download_);
    if (source_->recover) {
        workflow = recovery::with_boot_recovery(workflow, allow_download_);
        next.recover = source_->recover;
        next.recovery_limit = source_->recovery_limit;
    }
    next.initial = publish_workflow(workflow, assets_, *registry_, destination, aliases_, mod_ ? &*mod_ : nullptr);
    if (next.recover) {
        // 来源可能是StageN多图，7000G是新的单图；不能继承旧阶段的恢复入口表。
        next.recover->parameters["boot_entries"] = {{next.initial.checkpoint_node, "Boot_Entry"}};
    }
    next.policy.pack_revision = next.initial.bundle.revision;
    next.state_factory = wvd_state_binding(profile);
    devices::LifecycleTarget lifecycle_target;
    if (next.recover) {
        const auto &p = next.recover->parameters;
        lifecycle_target = {p.at("device_id"), p.at("instance_id"), p.at("application_id"),
            p.at("vpn_application_id"), p.at("vpn_required")};
    }
    recovery::bind_initial_vpn(next, lifecycle_target, profile);
    auto sources = provenance_.at("profile_sources");
    sources["FARM_TARGET"] = "HANDOFF:turn_to_7000G";
    next.state_factory->parameters["handoff_parent"] = {
        {"source", provenance_}, {"intent", intent}, {"profile_sources", sources},
        {"result_sha256", platform::file_sha256(directory / "result.json")},
        {"source_run_id", source_run_}, {"source_instance", run.at("instance")}, {"request_id", next.request_id},
        {"asset_revision", assets_.revision}, {"mod_revision", mod_ ? J(mod_->revision) : J(nullptr)},
        {"allow_download", allow_download_}};
    next_ = std::move(next);
    std::lock_guard starting(start_stop_mutex_);
    if (stopped_)
        return std::nullopt;
    if (coordinator_.snapshot().run_id != source_run_ || !coordinator_.wait_for(0ms))
        throw std::runtime_error("HANDOFF_SOURCE_RUN_CHANGED");
    const auto started = coordinator_.start(*next_, backend_);
    // 与生产start一样，不能宣称取消覆盖任意阻塞。若停止在交接瞬间到达，立即关闭新Run。
    if (stopped_)
        coordinator_.request_stop();
    return started;
}
} // namespace wvd::games::tasks
