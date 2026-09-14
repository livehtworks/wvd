#pragma once
#include <chrono>
#include <filesystem>
#include <json.hpp>
#include <memory>

namespace wvd::platform {
// 一次性查询对象拥有子进程、Job、管道和 OVERLAPPED；取消请求可从调用方线程发出。
// run 返回清理未完成时仍持有这些对象，禁止发起下一次查询。
class MetadataQuery {
  public:
    MetadataQuery();
    ~MetadataQuery();
    MetadataQuery(const MetadataQuery &) = delete;
    MetadataQuery &operator=(const MetadataQuery &) = delete;
    void cancel();
    nlohmann::json run(const std::filesystem::path &executable, int index,
                       std::chrono::milliseconds budget = std::chrono::seconds(10));
    bool finish_cleanup(std::chrono::milliseconds budget = std::chrono::seconds(2));

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wvd::platform
