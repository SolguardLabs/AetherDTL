#include "aether.hpp"

namespace aether {
namespace {

std::string first_account_with_role(const Ledger& ledger, AccountRole role) {
    for (const auto& account : ledger.accounts()) {
        if (account.role == role) {
            return account.id;
        }
    }
    return "";
}

Amount safe_target_net(Amount quoted, Amount operator_fee, Amount protocol_fee, Amount rebate) {
    return quoted.checked_sub(operator_fee.checked_add(protocol_fee).checked_add(rebate));
}

}  // namespace

SettlementEngine::SettlementEngine() = default;

Ledger& SettlementEngine::ledger() {
    return ledger_;
}

const Ledger& SettlementEngine::ledger() const {
    return ledger_;
}

Signer& SettlementEngine::signer() {
    return signer_;
}

const Signer& SettlementEngine::signer() const {
    return signer_;
}

IntentBook& SettlementEngine::intents() {
    return intents_;
}

const IntentBook& SettlementEngine::intents() const {
    return intents_;
}

Timestamp SettlementEngine::now() const {
    return now_;
}

void SettlementEngine::set_time(Timestamp now) {
    now_ = now;
    intents_.expire_due(now_, ledger_.journal());
}

void SettlementEngine::advance_time(Timestamp delta) {
    if (delta < 0) {
        throw std::runtime_error("negative time delta");
    }
    now_ += delta;
    ledger_.journal().push(
        EventKind::ClockAdvanced,
        now_,
        "clock",
        "",
        Amount::zero(),
        "clock advanced"
    );
    intents_.expire_due(now_, ledger_.journal());
}

Intent SettlementEngine::sign_intent(Intent intent) {
    intent.digest = Digest::from_payload(intent.canonical_public());
    intent.signature = signer_.sign(intent.owner, intent.canonical_public());
    return intent;
}

bool SettlementEngine::submit_intent(Intent intent) {
    std::vector<ValidationIssue> issues;

    if (intent.id.empty()) {
        issues.push_back(ValidationIssue::of("intent_id", "intent id is required"));
    }
    if (intents_.contains(intent.id)) {
        issues.push_back(ValidationIssue::of("intent_duplicate", "intent id already exists"));
    }
    if (!ledger_.has_account(intent.owner)) {
        issues.push_back(ValidationIssue::of("owner", "intent owner is not registered"));
    }
    if (!ledger_.has_asset(intent.source_asset)) {
        issues.push_back(ValidationIssue::of("source_asset", "intent source asset is not registered"));
    }
    if (!ledger_.has_asset(intent.target_asset)) {
        issues.push_back(ValidationIssue::of("target_asset", "intent target asset is not registered"));
    }
    if (!intent.max_source.is_positive()) {
        issues.push_back(ValidationIssue::of("max_source", "intent source amount must be positive"));
    }
    if (!intent.min_target.is_positive()) {
        issues.push_back(ValidationIssue::of("min_target", "intent target floor must be positive"));
    }
    if (intent.expires_at <= intent.valid_after) {
        issues.push_back(ValidationIssue::of("time_bounds", "intent expiration must be after activation"));
    }
    if (now_ > intent.expires_at) {
        issues.push_back(ValidationIssue::of("expired", "intent is already expired"));
    }
    if (intent.partial.max_slices < 0) {
        issues.push_back(ValidationIssue::of("partial_slices", "partial slice count cannot be negative"));
    }

    const auto expected_digest = Digest::from_payload(intent.canonical_public());
    if (!(intent.digest == expected_digest)) {
        issues.push_back(ValidationIssue::of("digest", "intent digest does not match payload"));
    }

    bool signature_ok = false;
    try {
        signature_ok = signer_.verify(intent.owner, intent.canonical_public(), intent.signature);
    } catch (const std::exception&) {
        signature_ok = false;
    }
    if (!signature_ok) {
        issues.push_back(ValidationIssue::of("signature", "intent signature is not valid"));
    }

    if (!issues.empty()) {
        const auto message = MatchResult::reject(issues).reason();
        ledger_.journal().push(
            EventKind::IntentRejected,
            now_,
            intent.id,
            intent.source_asset,
            intent.max_source,
            message
        );
        return false;
    }

    intent.status = IntentStatus::Open;
    intent.created_at = now_;
    intents_.add(intent);
    ledger_.journal().push(
        EventKind::IntentAccepted,
        now_,
        intent.id,
        intent.source_asset,
        intent.max_source,
        "intent accepted"
    );
    return true;
}

bool SettlementEngine::cancel_intent(const std::string& intent_id, const std::string& owner) {
    try {
        intents_.cancel(intent_id, owner, now_, ledger_.journal());
        return true;
    } catch (const std::exception& error) {
        ledger_.journal().push(
            EventKind::IntentRejected,
            now_,
            intent_id,
            "",
            Amount::zero(),
            std::string("cancel rejected:") + error.what()
        );
        return false;
    }
}

ExecutionPlan SettlementEngine::execute_plan(ExecutionPlan plan) {
    intents_.expire_due(now_, ledger_.journal());

    if (!intents_.contains(plan.intent_id)) {
        plan.status = PlanStatus::Rejected;
        plan.reason = "intent_missing";
        plans_.push_back(plan);
        ledger_.journal().push(
            EventKind::PlanRejected,
            now_,
            plan.id,
            "",
            Amount::zero(),
            plan.reason
        );
        return plan;
    }

    auto& intent = intents_.require_mut(plan.intent_id);
    const auto match = matcher_.evaluate(ledger_, intent, plan, now_);
    if (!match.accepted) {
        record_rejection(plan, match);
        return plan;
    }

    auto& bucket = bucket_for(intent, plan);
    const auto projected_source = bucket.used_source.checked_add(match.gross_source);
    if (projected_source > intent.max_source) {
        record_rejection(
            plan,
            MatchResult::reject({
                ValidationIssue::of("source_window", "execution plan exceeds visible source window")
            })
        );
        return plan;
    }

    plan.status = PlanStatus::Matched;
    plan.reason = "accepted";
    ledger_.journal().push(
        EventKind::PlanMatched,
        now_,
        plan.id,
        intent.source_asset,
        match.gross_source,
        "plan matched"
    );

    apply_execution(intent, plan, match, bucket);
    plan.status = PlanStatus::Executed;
    plan.reason = "executed";
    plans_.push_back(plan);
    return plan;
}

const std::vector<ExecutionPlan>& SettlementEngine::plans() const {
    return plans_;
}

std::vector<ExposureBucket> SettlementEngine::exposures() const {
    std::vector<ExposureBucket> result;
    for (const auto& [_, bucket] : exposures_) {
        result.push_back(bucket);
    }
    return result;
}

EngineRiskView SettlementEngine::risk_view() const {
    EngineRiskView view;

    for (const auto& intent : intents_.intents()) {
        if (intent.status == IntentStatus::Open) {
            view.open_intents += 1;
        }
        if (intent.status == IntentStatus::Cancelled) {
            view.cancelled_intents += 1;
        }
        if (intent.status == IntentStatus::Expired) {
            view.expired_intents += 1;
        }
    }

    for (const auto& plan : plans_) {
        if (plan.status == PlanStatus::Rejected) {
            view.rejected_plans += 1;
        }
        if (plan.status == PlanStatus::Executed) {
            view.executed_plans += 1;
        }
    }

    view.exposure_buckets = static_cast<int>(exposures_.size());
    for (const auto& [_, bucket] : exposures_) {
        view.visible_source_used = view.visible_source_used.checked_add(bucket.used_source);
        view.visible_target_delivered = view.visible_target_delivered.checked_add(bucket.delivered_target);
    }

    return view;
}

EngineInvariants SettlementEngine::invariants() const {
    EngineInvariants result;
    result.ledger_non_negative = ledger_.verify_non_negative();

    for (const auto& intent : intents_.intents()) {
        bool signature_ok = false;
        try {
            signature_ok = signer_.verify(intent.owner, intent.canonical_public(), intent.signature);
        } catch (const std::exception&) {
            signature_ok = false;
        }
        if (!signature_ok) {
            result.signatures_valid = false;
        }
    }

    for (const auto& plan : plans_) {
        if (!intents_.contains(plan.intent_id)) {
            result.plans_have_intents = false;
        }
    }

    for (const auto& [_, bucket] : exposures_) {
        if (!intents_.contains(bucket.key.intent_id)) {
            result.lifecycle_consistent = false;
            continue;
        }
        const auto& intent = intents_.require(bucket.key.intent_id);
        if (bucket.used_source > intent.max_source) {
            result.local_limits_hold = false;
        }
    }

    for (const auto& intent : intents_.intents()) {
        if (intent.status == IntentStatus::Filled) {
            bool has_full_bucket = false;
            for (const auto& [_, bucket] : exposures_) {
                if (bucket.key.intent_id == intent.id && bucket.used_source >= intent.max_source) {
                    has_full_bucket = true;
                }
            }
            if (!has_full_bucket) {
                result.lifecycle_consistent = false;
            }
        }
    }

    return result;
}

std::string SettlementEngine::digest() const {
    std::vector<std::string> fields{
        "engine",
        std::to_string(now_),
        ledger_.digest(),
    };

    for (const auto& intent : intents_.intents()) {
        fields.push_back(intent.canonical_public());
        fields.push_back(to_string(intent.status));
        fields.push_back(intent.digest.str());
        fields.push_back(intent.signature.value);
    }

    for (const auto& plan : plans_) {
        fields.push_back(plan.canonical());
    }

    for (const auto& [_, bucket] : exposures_) {
        fields.push_back(bucket.canonical());
    }

    return Digest::from_payload(join_fields(fields)).str();
}

void SettlementEngine::apply_execution(const Intent& intent,
                                       const ExecutionPlan& plan,
                                       const MatchResult& result,
                                       ExposureBucket& bucket) {
    const auto fee_collector = first_account_with_role(ledger_, AccountRole::FeeCollector);

    for (const auto& slice : plan.slices) {
        const auto& lane = ledger_.lane(slice.lane_id);
        const auto operator_fee = slice.quoted_target.checked_mul_bps_floor(lane.policy.operator_fee_bps);
        const auto protocol_fee = slice.quoted_target.checked_mul_bps_floor(lane.policy.protocol_fee_bps);
        const auto rebate =
            slice.maker_rebate ? slice.quoted_target.checked_mul_bps_floor(lane.policy.rebate_bps)
                               : Amount::zero();
        const auto recipient_target =
            safe_target_net(slice.quoted_target, operator_fee, protocol_fee, rebate);

        ledger_.transfer(
            TransferLeg{
                intent.owner,
                plan.operator_id,
                intent.source_asset,
                slice.source_amount,
                "intent source inventory"
            },
            now_
        );

        ledger_.debit(
            lane.settlement_vault,
            intent.target_asset,
            slice.quoted_target,
            now_,
            "settlement vault payout"
        );
        ledger_.credit(
            intent.owner,
            intent.target_asset,
            recipient_target,
            now_,
            "intent target receipt"
        );
        ledger_.credit(
            plan.operator_id,
            intent.target_asset,
            operator_fee,
            now_,
            "operator settlement fee"
        );
        ledger_.journal().push(
            EventKind::FeeAccrued,
            now_,
            plan.operator_id,
            intent.target_asset,
            operator_fee,
            "operator fee accrued"
        );

        if (!fee_collector.empty() && protocol_fee.is_positive()) {
            ledger_.credit(
                fee_collector,
                intent.target_asset,
                protocol_fee,
                now_,
                "protocol settlement fee"
            );
            ledger_.journal().push(
                EventKind::FeeAccrued,
                now_,
                fee_collector,
                intent.target_asset,
                protocol_fee,
                "protocol fee accrued"
            );
        }

        if (rebate.is_positive()) {
            ledger_.credit(
                intent.owner,
                intent.target_asset,
                rebate,
                now_,
                "maker rebate"
            );
            ledger_.journal().push(
                EventKind::FeeAccrued,
                now_,
                intent.owner,
                intent.target_asset,
                rebate,
                "rebate accrued"
            );
        }
    }

    bucket.used_source = bucket.used_source.checked_add(result.gross_source);
    bucket.delivered_target = bucket.delivered_target.checked_add(result.gross_target);
    bucket.fills += static_cast<int>(plan.slices.size());
    if (bucket.first_seen == 0) {
        bucket.first_seen = now_;
    }
    bucket.last_seen = now_;

    ledger_.journal().push(
        EventKind::ExposureUpdated,
        now_,
        bucket.key.str(),
        intent.source_asset,
        result.gross_source,
        "exposure updated"
    );

    ledger_.journal().push(
        EventKind::PlanExecuted,
        now_,
        plan.id,
        intent.source_asset,
        result.gross_source,
        "plan executed"
    );

    auto& stored_intent = intents_.require_mut(intent.id);
    if (bucket.used_source >= stored_intent.max_source) {
        stored_intent.status = IntentStatus::Filled;
    }
}

ExposureBucket& SettlementEngine::bucket_for(const Intent& intent, const ExecutionPlan& plan) {
    const ExposureKey key{intent.id, plan.strategy_id};
    auto found = exposures_.find(key);
    if (found == exposures_.end()) {
        ExposureBucket bucket;
        bucket.key = key;
        found = exposures_.insert({key, bucket}).first;
    }
    return found->second;
}

void SettlementEngine::record_rejection(ExecutionPlan& plan, const MatchResult& result) {
    plan.status = PlanStatus::Rejected;
    plan.reason = result.reason();
    plans_.push_back(plan);
    ledger_.journal().push(
        EventKind::PlanRejected,
        now_,
        plan.id,
        "",
        Amount::zero(),
        plan.reason
    );
}

}  // namespace aether
