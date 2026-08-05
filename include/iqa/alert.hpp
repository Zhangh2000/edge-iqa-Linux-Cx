#pragma once

#include "iqa/frame_metrics.hpp"

#include <optional>
#include <string>

namespace iqa {

enum class AlertStatus {
    Unassessed,
    Normal,
    Warning,
    Alarm,
};

struct AlertConfig {
    std::optional<double> brightness_min;
    std::optional<double> sharpness_min;
    int trigger_frames = 3;
    int recovery_frames = 3;

    bool enabled() const;
};

struct AlertDecision {
    AlertStatus status = AlertStatus::Unassessed;
    std::string reason;
    int violation_streak = 0;
    int recovery_streak = 0;
};

class AlertStateMachine {
public:
    explicit AlertStateMachine(AlertConfig config);

    AlertDecision evaluate(const FrameMetrics& metrics);

private:
    AlertConfig config_;
    AlertStatus status_ = AlertStatus::Unassessed;
    int violation_streak_ = 0;
    int recovery_streak_ = 0;
    std::string active_reason_;
};

const char* alertStatusName(AlertStatus status);

}  // namespace iqa
