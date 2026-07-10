#include "aether.hpp"

#include <cctype>

namespace aether {

std::string to_string(AccountRole role) {
    switch (role) {
        case AccountRole::User:
            return "user";
        case AccountRole::Operator:
            return "operator";
        case AccountRole::Vault:
            return "vault";
        case AccountRole::FeeCollector:
            return "fee_collector";
        case AccountRole::System:
            return "system";
    }
    return "unknown";
}

std::string to_string(IntentStatus status) {
    switch (status) {
        case IntentStatus::Draft:
            return "draft";
        case IntentStatus::Open:
            return "open";
        case IntentStatus::Filled:
            return "filled";
        case IntentStatus::Cancelled:
            return "cancelled";
        case IntentStatus::Expired:
            return "expired";
    }
    return "unknown";
}

std::string to_string(PlanStatus status) {
    switch (status) {
        case PlanStatus::Draft:
            return "draft";
        case PlanStatus::Matched:
            return "matched";
        case PlanStatus::Executed:
            return "executed";
        case PlanStatus::Rejected:
            return "rejected";
    }
    return "unknown";
}

std::string to_string(EventKind kind) {
    switch (kind) {
        case EventKind::AccountRegistered:
            return "account_registered";
        case EventKind::AssetRegistered:
            return "asset_registered";
        case EventKind::LaneRegistered:
            return "lane_registered";
        case EventKind::IntentAccepted:
            return "intent_accepted";
        case EventKind::IntentRejected:
            return "intent_rejected";
        case EventKind::IntentCancelled:
            return "intent_cancelled";
        case EventKind::IntentExpired:
            return "intent_expired";
        case EventKind::PlanMatched:
            return "plan_matched";
        case EventKind::PlanRejected:
            return "plan_rejected";
        case EventKind::PlanExecuted:
            return "plan_executed";
        case EventKind::Debit:
            return "debit";
        case EventKind::Credit:
            return "credit";
        case EventKind::Transfer:
            return "transfer";
        case EventKind::FeeAccrued:
            return "fee_accrued";
        case EventKind::ExposureUpdated:
            return "exposure_updated";
        case EventKind::ClockAdvanced:
            return "clock_advanced";
    }
    return "unknown";
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
                } else {
                    out << raw;
                }
                break;
        }
    }
    return out.str();
}

std::string bool_json(bool value) {
    return value ? "true" : "false";
}

std::string join_fields(const std::vector<std::string>& fields) {
    std::ostringstream out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) {
            out << "|";
        }
        out << fields[i].size() << ":" << fields[i];
    }
    return out.str();
}

std::string lowercase_hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::uint64_t stable_hash64(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ull;
    }
    hash ^= static_cast<std::uint64_t>(value.size());
    hash *= 1099511628211ull;
    return hash;
}

Amount Amount::zero() {
    return Amount{0};
}

Amount Amount::of(Units units) {
    if (units < 0) {
        throw std::runtime_error("negative amount");
    }
    return Amount{units};
}

bool Amount::is_zero() const {
    return units == 0;
}

bool Amount::is_positive() const {
    return units > 0;
}

std::string Amount::str() const {
    return std::to_string(units);
}

Amount Amount::checked_add(Amount rhs) const {
    if (rhs.units > 0 && units > std::numeric_limits<Units>::max() - rhs.units) {
        throw std::runtime_error("amount addition overflow");
    }
    return Amount::of(units + rhs.units);
}

Amount Amount::checked_sub(Amount rhs) const {
    if (rhs.units > units) {
        throw std::runtime_error("amount underflow");
    }
    return Amount::of(units - rhs.units);
}

Amount Amount::checked_mul_bps_floor(Units bps) const {
    if (bps < 0) {
        throw std::runtime_error("negative bps");
    }
    if (units != 0 && bps > std::numeric_limits<Units>::max() / units) {
        const long double scaled =
            static_cast<long double>(units) * static_cast<long double>(bps) /
            static_cast<long double>(kBasisPoints);
        if (scaled > static_cast<long double>(std::numeric_limits<Units>::max())) {
            throw std::runtime_error("amount bps overflow");
        }
        return Amount::of(static_cast<Units>(scaled));
    }
    return Amount::of((units * bps) / kBasisPoints);
}

Amount Amount::checked_mul_ratio_floor(Units numerator, Units denominator) const {
    if (numerator < 0 || denominator <= 0) {
        throw std::runtime_error("invalid ratio");
    }
    const long double scaled =
        static_cast<long double>(units) * static_cast<long double>(numerator) /
        static_cast<long double>(denominator);
    if (scaled < 0 || scaled > static_cast<long double>(std::numeric_limits<Units>::max())) {
        throw std::runtime_error("amount ratio overflow");
    }
    return Amount::of(static_cast<Units>(scaled));
}

bool Amount::operator==(Amount rhs) const {
    return units == rhs.units;
}

bool Amount::operator!=(Amount rhs) const {
    return units != rhs.units;
}

bool Amount::operator<(Amount rhs) const {
    return units < rhs.units;
}

bool Amount::operator<=(Amount rhs) const {
    return units <= rhs.units;
}

bool Amount::operator>(Amount rhs) const {
    return units > rhs.units;
}

bool Amount::operator>=(Amount rhs) const {
    return units >= rhs.units;
}

ValidationIssue ValidationIssue::of(std::string code, std::string message) {
    return ValidationIssue{std::move(code), std::move(message)};
}

Digest Digest::from_payload(const std::string& payload) {
    const auto first = stable_hash64("aether:digest:v1:" + payload);
    const auto second = stable_hash64("aether:digest:v1:tail:" + payload + lowercase_hex(first));
    return Digest{lowercase_hex(first) + lowercase_hex(second)};
}

bool Digest::empty() const {
    return value.empty();
}

std::string Digest::str() const {
    return value;
}

bool Digest::operator==(const Digest& rhs) const {
    return value == rhs.value;
}

bool Digest::operator<(const Digest& rhs) const {
    return value < rhs.value;
}

bool Signature::empty() const {
    return signer.empty() || value.empty();
}

std::string Asset::canonical() const {
    return join_fields({
        "asset",
        id,
        symbol,
        std::to_string(decimals),
        reserve_floor.str(),
        std::to_string(price),
    });
}

std::string PricePoint::canonical() const {
    return join_fields({
        "price",
        asset_id,
        std::to_string(price),
        std::to_string(updated_at),
    });
}

std::string LanePolicy::canonical() const {
    return join_fields({
        "lane-policy",
        std::to_string(min_output_bps),
        std::to_string(operator_fee_bps),
        std::to_string(protocol_fee_bps),
        std::to_string(rebate_bps),
        max_fragment_source.str(),
        bool_json(allow_partial),
        bool_json(enabled),
    });
}

std::string Lane::canonical() const {
    return join_fields({
        "lane",
        id,
        source_asset,
        target_asset,
        settlement_vault,
        policy.canonical(),
    });
}

std::string JournalEvent::canonical() const {
    return join_fields({
        to_string(kind),
        std::to_string(time),
        subject,
        asset_id,
        amount.str(),
        message,
    });
}

}  // namespace aether
