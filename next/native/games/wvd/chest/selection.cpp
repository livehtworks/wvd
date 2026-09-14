#include "selection.hpp"
#include <stdexcept>
#include <vector>

namespace wvd::games::chest {
void Selection::reset() {
    available_.fill(true);
    selected_.reset();
    attempts_ = 0;
    preferred_.reset();
}
unsigned Selection::available_mask() const {
    unsigned result = 0;
    for (unsigned i = 0; i < available_.size(); ++i)
        if (available_[i])
            result |= 1u << i;
    return result;
}
void Selection::prepare(const std::array<bool, 6> &fear, int preferred, std::uint32_t seed) {
    if (preferred < 0 || preferred > 6)
        throw std::runtime_error("CHEST_CHARACTER_INVALID");
    if (preferred_ && (*preferred_ != preferred || seed_ != seed))
        throw std::runtime_error("CHEST_SELECTION_POLICY_CHANGED");
    if (!preferred_) {
        preferred_ = preferred;
        seed_ = seed;
        random_.seed(seed);
    }
    selected_.reset();
    std::vector<unsigned> choices;
    for (unsigned i = 0; i < available_.size(); ++i) {
        available_[i] = available_[i] && !fear[i];
        if (available_[i])
            choices.push_back(i);
    }
    if (choices.empty())
        return;
    // 指定角色只享有首次尝试优先权；之后从剩余可用角色中抽选，允许再次抽到它。
    if (!attempts_ && preferred && available_[preferred - 1])
        selected_ = static_cast<unsigned>(preferred - 1);
    else
        selected_ = choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(random_)];
}
void Selection::attempted() {
    if (!selected_ || !available_[*selected_])
        throw std::runtime_error("CHEST_SELECTION_MISSING");
    ++attempts_;
    selected_.reset();
}
}
