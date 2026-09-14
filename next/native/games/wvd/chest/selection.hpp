#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <random>

namespace wvd::games::chest {
// 每个宝箱拥有自己的候选池；只保存角色编号，不保存截图或输入许可。
class Selection {
  public:
    void reset();
    void clear_intent() { selected_.reset(); }
    void prepare(const std::array<bool, 6> &fear, int preferred, std::uint32_t seed);
    void attempted();
    std::optional<unsigned> selected() const { return selected_; }
    unsigned attempts() const { return attempts_; }
    unsigned available_mask() const;

  private:
    std::array<bool, 6> available_{true, true, true, true, true, true};
    std::optional<unsigned> selected_;
    unsigned attempts_{};
    std::optional<int> preferred_;
    std::uint32_t seed_{};
    std::mt19937 random_;
};
}
