#pragma once
#include <json.hpp>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>

namespace wvd::runtime {
// The batch watcher remains the owner throughout a sample. This gate never
// releases a batch, starts a worker, or clears an input receipt.
class MeasurementBarrier final {
  public:
    using J=nlohmann::json;
    using Clock=std::chrono::steady_clock;
    void arm(const std::string &controller, const J &configuration, unsigned endpoints,
             std::chrono::milliseconds hold, std::chrono::milliseconds total) {
        if(controller.empty() || controller.size()>128 || !configuration.is_object() ||
            configuration.dump().size()>4096 || endpoints<2 || endpoints>4 ||
            hold<std::chrono::milliseconds{10} || hold>std::chrono::seconds{30} ||
            total<hold || total>std::chrono::seconds{1200}) throw std::runtime_error("MEASUREMENT_ARM_INVALID");
        std::lock_guard lock(mutex_);
        if(armed_) throw std::runtime_error("MEASUREMENT_ALREADY_ARMED");
        controller_=controller; configuration_=configuration; remaining_=endpoints;
        hold_=hold; deadline_=Clock::now()+total; armed_=true; held_=false;
        sequence_=0; endpoints_=J::array(); state_="armed";
    }
    bool armed() const { std::lock_guard lock(mutex_); return armed_; }
    void joined_boundary(J endpoint, const std::function<bool()> &cancelled) {
        std::unique_lock lock(mutex_);
        if(!armed_) return;
        if(cancelled() || Clock::now()>=deadline_) { finish(cancelled()?"cancelled":"timed_out");return; }
        if(endpoint.value("phase","")!="worker_joined" || endpoint.value("release_scope","")!="worker" ||
            !endpoint.value("input_clean",false) || !endpoint.value("cleanup_complete",false) ||
            !endpoint.value("heap_maintenance_complete",false) ||
            !endpoint.value("process_id",0ULL) || !endpoint.value("process_created_100ns",0ULL))
            throw std::runtime_error("MEASUREMENT_BOUNDARY_INVALID");
        endpoint["schema_version"]=1;endpoint["controller_id"]=controller_;
        endpoint["sequence"]=++sequence_;endpoint["configuration"]=configuration_;
        endpoint["monotonic_ns"]=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        endpoint["acknowledged"]=false;
        endpoints_.push_back(std::move(endpoint)); held_=true;state_="held";
        const auto until=std::min(deadline_,Clock::now()+hold_);
        while(held_ && !cancelled() && Clock::now()<until)
            wake_.wait_until(lock,std::min(until,Clock::now()+std::chrono::milliseconds{20}));
        if(cancelled()) finish("cancelled");
        else if(held_) finish("timed_out");
    }
    void release(const std::string &controller, std::uint64_t sequence) {
        std::lock_guard lock(mutex_);
        if(controller!=controller_ || sequence!=sequence_ || !sequence)
            throw std::runtime_error("MEASUREMENT_ACK_IDENTITY_MISMATCH");
        if(!held_) {
            if(!endpoints_.empty() && endpoints_.back().value("acknowledged",false)) return;
            throw std::runtime_error("MEASUREMENT_ACK_AFTER_EXPIRY");
        }
        endpoints_.back()["acknowledged"]=true; held_=false;
        if(--remaining_==0) finish("completed");else state_="armed";
        wake_.notify_all();
    }
    void cancel() { std::lock_guard lock(mutex_);finish("cancelled"); }
    J status() const {
        std::lock_guard lock(mutex_);
        return {{"schema_version",1},{"enabled",armed_},{"held",held_},{"state",state_},
            {"controller_id",controller_},{"sequence",sequence_},{"endpoints",endpoints_}};
    }
  private:
    void finish(const char *state) {armed_=false;held_=false;state_=state;wake_.notify_all();}
    mutable std::mutex mutex_;std::condition_variable wake_;
    bool armed_{},held_{};unsigned remaining_{};std::uint64_t sequence_{};
    std::string controller_,state_{"disabled"};J configuration_=J::object(),endpoints_=J::array();
    Clock::time_point deadline_{};std::chrono::milliseconds hold_{};
};
}
