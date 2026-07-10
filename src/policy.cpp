#include "aether.hpp"

namespace aether {

bool TimeWindow::contains(Timestamp now) const {
    if (opens_at != 0 && now < opens_at) {
        return false;
    }
    if (closes_at != 0 && now > closes_at) {
        return false;
    }
    return true;
}

std::string TimeWindow::canonical() const {
    return join_fields({
        "time-window",
        id,
        std::to_string(opens_at),
        std::to_string(closes_at),
        source_cap.str(),
        std::to_string(plan_cap),
        label
    });
}

std::string AdmissionDecision::canonical() const {
    return join_fields({
        "admission",
        bool_json(accepted),
        window_id,
        code,
        remaining_source.str(),
        std::to_string(remaining_plans)
    });
}

void WindowPolicyBook::add(TimeWindow window) {
    if (window.id.empty()) {
        throw std::runtime_error("window id required");
    }
    if (window.closes_at != 0 && window.opens_at > window.closes_at) {
        throw std::runtime_error("window closes before it opens");
    }
    for (const auto& existing : windows_) {
        if (existing.id == window.id) {
            throw std::runtime_error("window id already exists");
        }
    }
    windows_.push_back(std::move(window));
    std::sort(windows_.begin(), windows_.end(), [](const TimeWindow& lhs, const TimeWindow& rhs) {
        if (lhs.opens_at != rhs.opens_at) {
            return lhs.opens_at < rhs.opens_at;
        }
        return lhs.id < rhs.id;
    });
}

std::vector<TimeWindow> WindowPolicyBook::windows() const {
    return windows_;
}

std::vector<TimeWindow> WindowPolicyBook::windows_for(Timestamp now) const {
    std::vector<TimeWindow> result;
    for (const auto& window : windows_) {
        if (window.contains(now)) {
            result.push_back(window);
        }
    }
    return result;
}

Amount WindowPolicyBook::used_in_window(const TimeWindow& window,
                                        const Intent& intent,
                                        const std::vector<ExposureBucket>& exposures) const {
    Amount used = Amount::zero();
    for (const auto& bucket : exposures) {
        if (bucket.key.intent_id != intent.id) {
            continue;
        }
        const bool touches_window =
            (bucket.first_seen == 0 && window.opens_at == 0) ||
            (bucket.last_seen >= window.opens_at && (window.closes_at == 0 || bucket.first_seen <= window.closes_at));
        if (!touches_window) {
            continue;
        }
        used = used.checked_add(bucket.used_source);
    }
    return used;
}

int WindowPolicyBook::plans_in_window(const TimeWindow& window,
                                      const Intent& intent,
                                      const ExecutionPlan& plan,
                                      const std::vector<ExposureBucket>& exposures) const {
    int count = 0;
    for (const auto& bucket : exposures) {
        if (bucket.key.intent_id != intent.id) {
            continue;
        }
        if (bucket.last_seen < window.opens_at) {
            continue;
        }
        if (window.closes_at != 0 && bucket.first_seen > window.closes_at) {
            continue;
        }
        count += bucket.fills;
    }
    if (plan.submitted_at >= window.opens_at && (window.closes_at == 0 || plan.submitted_at <= window.closes_at)) {
        count += static_cast<int>(plan.slices.size());
    }
    return count;
}

AdmissionDecision WindowPolicyBook::decide(const Intent& intent,
                                           const ExecutionPlan& plan,
                                           const std::vector<ExposureBucket>& exposures,
                                           Timestamp now) const {
    AdmissionDecision rejected;
    rejected.accepted = false;
    rejected.code = "window_unavailable";

    const auto active = windows_for(now);
    if (active.empty()) {
        return rejected;
    }

    for (const auto& window : active) {
        AdmissionDecision decision;
        decision.window_id = window.id;
        decision.accepted = false;
        decision.code = "window_capacity";

        const auto used_source = used_in_window(window, intent, exposures);
        const auto plan_source = plan.gross_source();
        if (window.source_cap.is_positive()) {
            if (used_source >= window.source_cap) {
                decision.remaining_source = Amount::zero();
            } else {
                decision.remaining_source = window.source_cap.checked_sub(used_source);
            }
        } else {
            decision.remaining_source = intent.max_source;
        }

        const auto used_plans = plans_in_window(window, intent, plan, exposures);
        if (window.plan_cap > 0) {
            decision.remaining_plans = std::max(0, window.plan_cap - used_plans);
        } else {
            decision.remaining_plans = static_cast<int>(plan.slices.size());
        }

        const bool source_ok = !window.source_cap.is_positive() || plan_source <= decision.remaining_source;
        const bool plans_ok = window.plan_cap <= 0 || used_plans <= window.plan_cap;
        if (source_ok && plans_ok) {
            decision.accepted = true;
            decision.code = "accepted";
            return decision;
        }

        rejected = decision;
    }

    return rejected;
}

std::string WindowPolicyBook::digest() const {
    std::vector<std::string> fields{"window-policy-book"};
    for (const auto& window : windows_) {
        fields.push_back(window.canonical());
    }
    return Digest::from_payload(join_fields(fields)).str();
}

std::string FillProjection::canonical() const {
    return join_fields({
        "fill-projection",
        slice_id,
        lane_id,
        source.str(),
        expected_target.str(),
        quoted_target.str(),
        slippage.str(),
        operator_fee.str(),
        protocol_fee.str(),
        rebate.str(),
        net_to_owner.str(),
        bool_json(meets_floor)
    });
}

std::string PlanProjection::canonical() const {
    return join_fields({
        "plan-projection",
        plan_id,
        source.str(),
        expected_target.str(),
        quoted_target.str(),
        net_to_owner.str(),
        total_fees.str(),
        std::to_string(slices),
        bool_json(all_slices_meet_floor)
    });
}

FillProjection ProjectionEngine::project_slice(const Ledger& ledger,
                                               const Intent& intent,
                                               const FillSlice& slice) const {
    FillProjection projection;
    projection.slice_id = slice.id;
    projection.lane_id = slice.lane_id;
    projection.source = slice.source_amount;
    projection.quoted_target = slice.quoted_target;

    if (!ledger.has_lane(slice.lane_id)) {
        projection.expected_target = Amount::zero();
        projection.slippage = Amount::zero();
        projection.meets_floor = false;
        return projection;
    }

    const auto& lane = ledger.lane(slice.lane_id);
    if (lane.source_asset != intent.source_asset || lane.target_asset != intent.target_asset) {
        projection.expected_target = Amount::zero();
        projection.slippage = Amount::zero();
        projection.meets_floor = false;
        return projection;
    }

    projection.expected_target = ledger.quote(intent.source_asset, intent.target_asset, slice.source_amount);
    if (projection.expected_target > projection.quoted_target) {
        projection.slippage = projection.expected_target.checked_sub(projection.quoted_target);
    } else {
        projection.slippage = Amount::zero();
    }

    projection.operator_fee =
        projection.quoted_target.checked_mul_bps_floor(lane.policy.operator_fee_bps);
    projection.protocol_fee =
        projection.quoted_target.checked_mul_bps_floor(lane.policy.protocol_fee_bps);
    projection.rebate =
        slice.maker_rebate ? projection.quoted_target.checked_mul_bps_floor(lane.policy.rebate_bps)
                           : Amount::zero();
    projection.net_to_owner =
        projection.quoted_target.checked_sub(
            projection.operator_fee.checked_add(projection.protocol_fee).checked_add(projection.rebate)
        );

    const auto required = projection.expected_target.checked_mul_bps_floor(intent.quote_floor_bps);
    projection.meets_floor = projection.quoted_target >= required;
    return projection;
}

PlanProjection ProjectionEngine::project_plan(const Ledger& ledger,
                                              const Intent& intent,
                                              const ExecutionPlan& plan) const {
    PlanProjection projection;
    projection.plan_id = plan.id;
    projection.all_slices_meet_floor = true;

    for (const auto& slice : plan.slices) {
        const auto fill = project_slice(ledger, intent, slice);
        projection.source = projection.source.checked_add(fill.source);
        projection.expected_target = projection.expected_target.checked_add(fill.expected_target);
        projection.quoted_target = projection.quoted_target.checked_add(fill.quoted_target);
        projection.net_to_owner = projection.net_to_owner.checked_add(fill.net_to_owner);
        projection.total_fees = projection.total_fees.checked_add(
            fill.operator_fee.checked_add(fill.protocol_fee).checked_add(fill.rebate)
        );
        projection.slices += 1;
        if (!fill.meets_floor) {
            projection.all_slices_meet_floor = false;
        }
    }

    if (plan.slices.empty()) {
        projection.all_slices_meet_floor = false;
    }

    return projection;
}

std::vector<PlanProjection> ProjectionEngine::project_plans(const Ledger& ledger,
                                                            const Intent& intent,
                                                            const std::vector<ExecutionPlan>& plans) const {
    std::vector<PlanProjection> result;
    for (const auto& plan : plans) {
        if (plan.intent_id != intent.id) {
            continue;
        }
        result.push_back(project_plan(ledger, intent, plan));
    }
    std::sort(result.begin(), result.end(), [](const PlanProjection& lhs, const PlanProjection& rhs) {
        if (lhs.plan_id != rhs.plan_id) {
            return lhs.plan_id < rhs.plan_id;
        }
        return lhs.quoted_target < rhs.quoted_target;
    });
    return result;
}

std::string ProjectionEngine::digest(const std::vector<PlanProjection>& projections) const {
    std::vector<std::string> fields{"projection-engine"};
    for (const auto& projection : projections) {
        fields.push_back(projection.canonical());
    }
    return Digest::from_payload(join_fields(fields)).str();
}

}  // namespace aether
