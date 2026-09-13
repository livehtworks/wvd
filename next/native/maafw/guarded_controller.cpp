#include "guarded_controller.hpp"

namespace wvd::maafw {
using namespace contracts;
GuardedController::GuardedController(devices::InputGate &gate, CallbackActivity &activity)
    : gate_(gate), activity_(activity) {
    callbacks_.connect = [](void *p) -> MaaBool {
        return invoke(p, [](auto &s) {
            s.connected_ = s.gate_.connect();
            return s.connected_.load();
        });
    };
    callbacks_.connected = [](void *p) -> MaaBool {
        return invoke(p, [](auto &s) { return s.connected_.load(); });
    };
    callbacks_.request_uuid = [](void *p, MaaStringBuffer *out) -> MaaBool {
        return invoke(p, [&](auto &s) {
            return MaaStringBufferSet(out, s.gate_.policy().device_id.c_str());
        });
    };
    callbacks_.get_features = [](void *) -> MaaControllerFeature { return 0; };
    callbacks_.get_info = [](void *p, MaaStringBuffer *out) -> MaaBool {
        return invoke(p, [&](auto &) {
            return MaaStringBufferSet(out, R"({"type":"guarded","offline_only":true})");
        });
    };
    callbacks_.screencap = [](void *p, MaaImageBuffer *out) -> MaaBool {
        return invoke(p, [&](auto &s) {
            auto raw = s.gate_.capture();
            if (raw.encoded.empty() ||
                !MaaImageBufferSetEncoded(out, raw.encoded.data(), raw.encoded.size()) ||
                !MaaImageBufferGetRawData(out) || MaaImageBufferType(out) != 16 ||
                MaaImageBufferWidth(out) != raw.size.width ||
                MaaImageBufferHeight(out) != raw.size.height)
                throw std::runtime_error("CAPTURE_DECODE_INVALID");
            auto size = s.gate_.policy().recognition_size;
            // 外层提供统一识别尺寸；SDK 再看到的比例是 1，只有 InputGate 对内层映射原始坐标。
            return MaaImageBufferResize(out, size.width, size.height);
        });
    };
    callbacks_.click = [](int x, int y, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::Click;
            c.x = x;
            c.y = y;
            return s.input(c);
        });
    };
    callbacks_.swipe = [](int x, int y, int x2, int y2, int duration, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::Swipe;
            c.x = x;
            c.y = y;
            c.x2 = x2;
            c.y2 = y2;
            c.duration = duration;
            return s.input(c);
        });
    };
    callbacks_.touch_down = [](int contact, int x, int y, int pressure, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::TouchDown;
            c.contact = contact;
            c.x = x;
            c.y = y;
            c.pressure = pressure;
            return s.input(c);
        });
    };
    callbacks_.touch_move = [](int contact, int x, int y, int pressure, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::TouchMove;
            c.contact = contact;
            c.x = x;
            c.y = y;
            c.pressure = pressure;
            return s.input(c);
        });
    };
    callbacks_.touch_up = [](int contact, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::TouchUp;
            c.contact = contact;
            return s.input(c);
        });
    };
    callbacks_.click_key = [](int key, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::ClickKey;
            c.key = key;
            return s.input(c);
        });
    };
    callbacks_.key_down = [](int key, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::KeyDown;
            c.key = key;
            return s.input(c);
        });
    };
    callbacks_.key_up = [](int key, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::KeyUp;
            c.key = key;
            return s.input(c);
        });
    };
    callbacks_.input_text = [](const char *text, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::Text;
            c.text = text ? text : "";
            return s.input(c);
        });
    };
    callbacks_.start_app = [](const char *text, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::StartApp;
            c.text = text ? text : "";
            return s.input(c);
        });
    };
    callbacks_.stop_app = [](const char *text, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::StopApp;
            c.text = text ? text : "";
            return s.input(c);
        });
    };
    callbacks_.scroll = [](int x, int y, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::Scroll;
            c.x = x;
            c.y = y;
            return s.input(c);
        });
    };
    callbacks_.relative_move = [](int x, int y, void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::RelativeMove;
            c.x = x;
            c.y = y;
            return s.input(c);
        });
    };
    callbacks_.shell = [](const char *text, int64_t, void *p, MaaStringBuffer *) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::Shell;
            c.text = text ? text : "";
            return s.input(c);
        });
    };
    callbacks_.inactive = [](void *p) -> MaaBool {
        return invoke(p, [&](auto &s) {
            Command c;
            c.kind = ActionKind::Inactive;
            return s.input(c);
        });
    };
}
} // namespace wvd::maafw
