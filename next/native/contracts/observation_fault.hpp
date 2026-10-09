#pragma once
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <json.hpp>
#include "recognition.hpp"

namespace wvd::contracts {
// 只有设备适配层能把已分类的只读故障送到这里。不能由识别 NoHit、任意
// std::exception 或已经发送的输入反向推断“可以重试”。这里不携带点击命令。
enum class ReadFaultKind { Timeout, TransportUnavailable, MetadataUnavailable, ApplicationUnavailable };
enum class ReadFaultStage { Capture, InputContext };
struct ReadFault {
    ReadFaultKind kind{ReadFaultKind::Timeout};
    ReadFaultStage stage{ReadFaultStage::Capture};
    std::string code;
    std::string operation;
    std::chrono::milliseconds timeout{};
    std::chrono::milliseconds elapsed{};
    nlohmann::json details = nullptr;
};
// 只证明同一绑定实例的连接交接，不改旧输入回执，也不授权重放。
struct ObservationReconnect {
    std::string device_id, instance_id, created_identity;
    std::uint64_t before{}, after{};
};
struct InstanceExitProof {
    std::string device_id, instance_id, created_identity;
    std::uint64_t invalidated_connection{}, restored_connection{};
    std::chrono::steady_clock::time_point observed_at{};
    std::uint64_t transaction{};
};
inline bool exit_covers_input(const InstanceExitProof &proof, const FrameIdentity &before,
    std::chrono::steady_clock::time_point submitted_at) noexcept {
    return proof.transaction && !proof.created_identity.empty() &&
        proof.device_id == before.device_id && proof.instance_id == before.instance_id &&
        proof.created_identity == before.instance_created_identity &&
        proof.invalidated_connection && proof.invalidated_connection == before.connection_generation &&
        submitted_at >= before.captured_at && submitted_at <= proof.observed_at &&
        proof.observed_at <= std::chrono::steady_clock::now();
}
inline nlohmann::json instance_exit_json(const InstanceExitProof &p) {
    return {{"device_id",p.device_id},{"instance_id",p.instance_id},{"created_identity",p.created_identity},
        {"invalidated_connection",p.invalidated_connection},{"restored_connection",p.restored_connection},
        {"observed_at_ns",std::chrono::duration_cast<std::chrono::nanoseconds>(p.observed_at.time_since_epoch()).count()},
        {"transaction",p.transaction}};
}
inline bool exit_covers_receipts(const nlohmann::json &proof, const nlohmann::json &receipts) noexcept {
    try {
        if (!proof.is_object() || !receipts.is_array() || receipts.empty()) return false;
        using Clock = std::chrono::steady_clock;
        const auto point = [](std::int64_t ns) { return Clock::time_point{
            std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds{ns})}; };
        const InstanceExitProof p{proof.at("device_id"),proof.at("instance_id"),proof.at("created_identity"),
            proof.at("invalidated_connection"),proof.at("restored_connection"),
            point(proof.at("observed_at_ns")),proof.at("transaction")};
        for (const auto &receipt : receipts) {
            FrameIdentity before;
            before.device_id = receipt.at("device_id"); before.instance_id = receipt.at("instance_id");
            before.instance_created_identity = receipt.at("instance_created_identity");
            before.connection_generation = receipt.at("connection_generation");
            before.captured_at = point(receipt.at("captured_at_ns"));
            if (!exit_covers_input(p,before,point(receipt.at("submitted_at_ns")))) return false;
        }
        return true;
    } catch (...) { return false; }
}
// 切回前台不等于重连，也不伪造连接代次；退出后拉起才要求重新进入游戏。
struct ObservationRecovery {
    std::optional<ObservationReconnect> reconnect;
    bool application_restarted{};
    bool foreground_restored{};
    bool instance_restarted{};
    std::optional<InstanceExitProof> instance_exit;
};
class ObservationUnavailable final : public std::runtime_error {
  public:
    explicit ObservationUnavailable(ReadFault fault)
        : std::runtime_error(fault.code), fault_(std::move(fault)) {}
    const ReadFault &fault() const noexcept { return fault_; }
  private:
    ReadFault fault_;
};
// 本策略只约束一次连续读取故障，不是网络页面响应期限。正常读取恢复后
// 重新开始下一次故障区间；Run 的总墙钟期限不延长。初始候选值须实机核对。
struct ObservationRecoveryPolicy {
    std::chrono::milliseconds outage_limit{60000};
    std::chrono::milliseconds first_delay{250};
    std::chrono::milliseconds max_delay{2000};
    // 连续没有恢复到业务现场的异常窗口，和读取故障退避分别计时。
    std::chrono::milliseconds exception_timeout{60000};
    void validate() const {
        if (outage_limit.count() <= 0 || first_delay.count() <= 0 ||
            max_delay < first_delay || max_delay >= outage_limit || exception_timeout.count() <= 0)
            throw std::invalid_argument("OBSERVATION_RECOVERY_POLICY_INVALID");
    }
};
} // namespace wvd::contracts
