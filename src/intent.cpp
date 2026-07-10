#include "aether.hpp"

namespace aether {

std::string PartialPolicy::canonical_public() const {
    return join_fields({
        "partial",
        bool_json(allow_partial),
        mode,
        std::to_string(max_slices),
        min_slice_source.str(),
        max_slice_source.str(),
        schedule,
    });
}

std::string PartialPolicy::canonical_execution() const {
    return join_fields({
        canonical_public(),
        strategy_ref,
    });
}

std::string Intent::canonical_public() const {
    return join_fields({
        "intent",
        id,
        owner,
        source_asset,
        target_asset,
        max_source.str(),
        min_target.str(),
        std::to_string(quote_floor_bps),
        std::to_string(valid_after),
        std::to_string(expires_at),
        std::to_string(nonce),
        partial.canonical_public(),
    });
}

std::string Intent::canonical_execution() const {
    return join_fields({
        canonical_public(),
        partial.canonical_execution(),
    });
}

bool Intent::expired(Timestamp now) const {
    return expires_at > 0 && now > expires_at;
}

bool Intent::active(Timestamp now) const {
    return status == IntentStatus::Open && now >= valid_after && !expired(now);
}

std::string FillSlice::canonical() const {
    return join_fields({
        "slice",
        id,
        lane_id,
        source_amount.str(),
        quoted_target.str(),
        std::to_string(price_bps),
        std::to_string(ordinal),
        bool_json(maker_rebate),
    });
}

Amount ExecutionPlan::gross_source() const {
    Amount total = Amount::zero();
    for (const auto& slice : slices) {
        total = total.checked_add(slice.source_amount);
    }
    return total;
}

Amount ExecutionPlan::gross_target() const {
    Amount total = Amount::zero();
    for (const auto& slice : slices) {
        total = total.checked_add(slice.quoted_target);
    }
    return total;
}

std::string ExecutionPlan::canonical() const {
    std::vector<std::string> fields{
        "plan",
        id,
        operator_id,
        intent_id,
        intent_digest.str(),
        strategy_id,
        std::to_string(submitted_at),
        to_string(status),
        reason,
    };
    for (const auto& slice : slices) {
        fields.push_back(slice.canonical());
    }
    return join_fields(fields);
}

MatchResult MatchResult::ok(Amount gross_source,
                            Amount gross_target,
                            Amount operator_fee,
                            Amount protocol_fee,
                            Amount rebate) {
    MatchResult result;
    result.accepted = true;
    result.gross_source = gross_source;
    result.gross_target = gross_target;
    result.operator_fee = operator_fee;
    result.protocol_fee = protocol_fee;
    result.rebate = rebate;
    return result;
}

MatchResult MatchResult::reject(std::vector<ValidationIssue> issues) {
    MatchResult result;
    result.accepted = false;
    result.issues = std::move(issues);
    return result;
}

std::string MatchResult::reason() const {
    if (accepted) {
        return "accepted";
    }
    if (issues.empty()) {
        return "rejected";
    }
    std::vector<std::string> parts;
    for (const auto& issue : issues) {
        parts.push_back(issue.code + ":" + issue.message);
    }
    return join_fields(parts);
}

bool ExposureKey::operator<(const ExposureKey& rhs) const {
    if (intent_id != rhs.intent_id) {
        return intent_id < rhs.intent_id;
    }
    return strategy_id < rhs.strategy_id;
}

std::string ExposureKey::str() const {
    return intent_id + ":" + strategy_id;
}

std::string ExposureBucket::canonical() const {
    return join_fields({
        "exposure",
        key.intent_id,
        key.strategy_id,
        used_source.str(),
        delivered_target.str(),
        std::to_string(fills),
        std::to_string(first_seen),
        std::to_string(last_seen),
    });
}

void Signer::register_secret(const std::string& account_id, const std::string& secret) {
    if (account_id.empty()) {
        throw std::runtime_error("signer account required");
    }
    if (secret.empty()) {
        throw std::runtime_error("signer secret required");
    }
    secrets_[account_id] = secret;
}

Signature Signer::sign(const std::string& account_id, const std::string& payload) const {
    const auto found = secrets_.find(account_id);
    if (found == secrets_.end()) {
        throw std::runtime_error("signer secret missing");
    }
    const auto digest = Digest::from_payload(join_fields({
        "signature",
        account_id,
        found->second,
        payload,
    }));
    return Signature{account_id, digest.str()};
}

bool Signer::verify(const std::string& account_id,
                    const std::string& payload,
                    const Signature& signature) const {
    if (signature.signer != account_id || signature.empty()) {
        return false;
    }
    const auto expected = sign(account_id, payload);
    return expected.value == signature.value;
}

std::string Signer::public_key(const std::string& account_id) const {
    const auto found = secrets_.find(account_id);
    if (found == secrets_.end()) {
        return "";
    }
    return Digest::from_payload("public:" + account_id + ":" + found->second).str().substr(0, 24);
}

bool IntentBook::contains(const std::string& intent_id) const {
    return intents_.find(intent_id) != intents_.end();
}

void IntentBook::add(Intent intent) {
    if (intent.id.empty()) {
        throw std::runtime_error("intent id required");
    }
    if (contains(intent.id)) {
        throw std::runtime_error("intent already exists");
    }
    intents_.insert({intent.id, std::move(intent)});
}

Intent& IntentBook::require_mut(const std::string& intent_id) {
    const auto found = intents_.find(intent_id);
    if (found == intents_.end()) {
        throw std::runtime_error("intent not found");
    }
    return found->second;
}

const Intent& IntentBook::require(const std::string& intent_id) const {
    const auto found = intents_.find(intent_id);
    if (found == intents_.end()) {
        throw std::runtime_error("intent not found");
    }
    return found->second;
}

void IntentBook::cancel(const std::string& intent_id,
                        const std::string& owner,
                        Timestamp now,
                        Journal& journal) {
    auto& intent = require_mut(intent_id);
    if (intent.owner != owner) {
        throw std::runtime_error("intent owner mismatch");
    }
    if (intent.status != IntentStatus::Open) {
        throw std::runtime_error("intent not open");
    }
    intent.status = IntentStatus::Cancelled;
    journal.push(
        EventKind::IntentCancelled,
        now,
        intent.id,
        intent.source_asset,
        Amount::zero(),
        "intent cancelled"
    );
}

void IntentBook::expire_due(Timestamp now, Journal& journal) {
    for (auto& [_, intent] : intents_) {
        if (intent.status == IntentStatus::Open && intent.expired(now)) {
            intent.status = IntentStatus::Expired;
            journal.push(
                EventKind::IntentExpired,
                now,
                intent.id,
                intent.source_asset,
                Amount::zero(),
                "intent expired"
            );
        }
    }
}

std::vector<Intent> IntentBook::intents() const {
    std::vector<Intent> result;
    for (const auto& [_, intent] : intents_) {
        result.push_back(intent);
    }
    return result;
}

}  // namespace aether
