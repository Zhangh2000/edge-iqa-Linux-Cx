#include "iqa/alert.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace iqa {
namespace {

std::string joinReasons(const std::vector<std::string>& reasons) {
    std::string result;
    for (const std::string& reason : reasons) {
        if (!result.empty()) {
            result += ';';
        }
        result += reason;
    }
    return result;
}

}  // namespace

bool AlertConfig::enabled() const {
    return brightness_min.has_value() || sharpness_min.has_value();
}

AlertStateMachine::AlertStateMachine(AlertConfig config) : config_(std::move(config)) {
    if (config_.trigger_frames <= 0 || config_.recovery_frames <= 0) {
        throw std::invalid_argument("alert frame counts must be positive");
    }
    status_ = config_.enabled() ? AlertStatus::Normal : AlertStatus::Unassessed;
}

AlertDecision AlertStateMachine::evaluate(const FrameMetrics& metrics) {
    if (!config_.enabled()) {
        return {};
    }

    std::vector<std::string> reasons;
    if (config_.brightness_min && metrics.brightness_mean < *config_.brightness_min) {
        reasons.emplace_back("dark");
    }
    if (config_.sharpness_min &&
        metrics.sharpness_laplacian_variance < *config_.sharpness_min) {
        reasons.emplace_back("blur");
    }

    if (!reasons.empty()) {
        ++violation_streak_;
        recovery_streak_ = 0;
        active_reason_ = joinReasons(reasons);
        if (status_ != AlertStatus::Alarm) {
            status_ = violation_streak_ >= config_.trigger_frames
                ? AlertStatus::Alarm
                : AlertStatus::Warning;
        }
    } else {
        violation_streak_ = 0;
        if (status_ == AlertStatus::Alarm) {
            ++recovery_streak_;
            if (recovery_streak_ >= config_.recovery_frames) {
                status_ = AlertStatus::Normal;
                recovery_streak_ = 0;
                active_reason_.clear();
            }
        } else {
            status_ = AlertStatus::Normal;
            recovery_streak_ = 0;
            active_reason_.clear();
        }
    }

    AlertDecision decision;
    decision.status = status_;
    decision.reason = active_reason_;
    decision.violation_streak = violation_streak_;
    decision.recovery_streak = recovery_streak_;
    return decision;
}

const char* alertStatusName(AlertStatus status) {
    switch (status) {
        case AlertStatus::Unassessed:
            return "UNASSESSED";
        case AlertStatus::Normal:
            return "NORMAL";
        case AlertStatus::Warning:
            return "WARNING";
        case AlertStatus::Alarm:
            return "ALARM";
    }
    return "UNASSESSED";
}

}  // namespace iqa
