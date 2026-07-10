#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aether {

using Units = std::int64_t;
using Timestamp = std::int64_t;

constexpr Units kBasisPoints = 10'000;
constexpr Units kPriceScale = 1'000'000;

enum class AccountRole {
    User,
    Operator,
    Vault,
    FeeCollector,
    System,
};

enum class IntentStatus {
    Draft,
    Open,
    Filled,
    Cancelled,
    Expired,
};

enum class PlanStatus {
    Draft,
    Matched,
    Executed,
    Rejected,
};

enum class EventKind {
    AccountRegistered,
    AssetRegistered,
    LaneRegistered,
    IntentAccepted,
    IntentRejected,
    IntentCancelled,
    IntentExpired,
    PlanMatched,
    PlanRejected,
    PlanExecuted,
    Debit,
    Credit,
    Transfer,
    FeeAccrued,
    ExposureUpdated,
    ClockAdvanced,
};

std::string to_string(AccountRole role);
std::string to_string(IntentStatus status);
std::string to_string(PlanStatus status);
std::string to_string(EventKind kind);
std::string json_escape(const std::string& value);
std::string bool_json(bool value);
std::string join_fields(const std::vector<std::string>& fields);
std::string lowercase_hex(std::uint64_t value);
std::uint64_t stable_hash64(const std::string& value);

struct Amount {
    Units units = 0;

    static Amount zero();
    static Amount of(Units units);

    bool is_zero() const;
    bool is_positive() const;
    std::string str() const;

    Amount checked_add(Amount rhs) const;
    Amount checked_sub(Amount rhs) const;
    Amount checked_mul_bps_floor(Units bps) const;
    Amount checked_mul_ratio_floor(Units numerator, Units denominator) const;

    bool operator==(Amount rhs) const;
    bool operator!=(Amount rhs) const;
    bool operator<(Amount rhs) const;
    bool operator<=(Amount rhs) const;
    bool operator>(Amount rhs) const;
    bool operator>=(Amount rhs) const;
};

struct ValidationIssue {
    std::string code;
    std::string message;

    static ValidationIssue of(std::string code, std::string message);
};

struct Digest {
    std::string value;

    static Digest from_payload(const std::string& payload);

    bool empty() const;
    std::string str() const;
    bool operator==(const Digest& rhs) const;
    bool operator<(const Digest& rhs) const;
};

struct Signature {
    std::string signer;
    std::string value;

    bool empty() const;
};

struct Asset {
    std::string id;
    std::string symbol;
    std::uint8_t decimals = 6;
    Amount reserve_floor;
    Units price = kPriceScale;

    std::string canonical() const;
};

struct PricePoint {
    std::string asset_id;
    Units price = kPriceScale;
    Timestamp updated_at = 0;

    std::string canonical() const;
};

struct LanePolicy {
    Units min_output_bps = 9'900;
    Units operator_fee_bps = 6;
    Units protocol_fee_bps = 2;
    Units rebate_bps = 1;
    Amount max_fragment_source = Amount::of(0);
    bool allow_partial = true;
    bool enabled = true;

    std::string canonical() const;
};

struct Lane {
    std::string id;
    std::string source_asset;
    std::string target_asset;
    std::string settlement_vault;
    LanePolicy policy;

    std::string canonical() const;
};

struct BalanceLine {
    std::string asset_id;
    Amount available;
    Amount reserved;
};

struct Account {
    std::string id;
    std::string label;
    AccountRole role = AccountRole::User;
    std::string public_key;
    std::map<std::string, Amount> balances;
    std::map<std::string, Amount> reserved;
    bool active = true;

    Amount available(const std::string& asset_id) const;
    Amount locked(const std::string& asset_id) const;
    std::vector<BalanceLine> lines() const;
    std::string canonical() const;
};

struct JournalEvent {
    EventKind kind = EventKind::Transfer;
    Timestamp time = 0;
    std::string subject;
    std::string asset_id;
    Amount amount;
    std::string message;

    std::string canonical() const;
};

struct TransferLeg {
    std::string from;
    std::string to;
    std::string asset_id;
    Amount amount;
    std::string memo;
};

struct LedgerTotals {
    Amount user_source_debited;
    Amount user_target_credited;
    Amount operator_fees;
    Amount protocol_fees;
    Amount rebates;
    Amount vault_reserves;
    Amount circulating_source;
    Amount circulating_target;
};

class Journal {
  public:
    void push(EventKind kind,
              Timestamp time,
              std::string subject,
              std::string asset_id,
              Amount amount,
              std::string message);
    const std::vector<JournalEvent>& events() const;
    std::vector<JournalEvent> filter(EventKind kind) const;
    std::string digest() const;

  private:
    std::vector<JournalEvent> events_;
};

class Ledger {
  public:
    void register_asset(const Asset& asset, Timestamp now);
    void register_lane(const Lane& lane, Timestamp now);
    void register_account(const Account& account, Timestamp now);
    void authorize_operator(const std::string& account_id);

    bool has_asset(const std::string& asset_id) const;
    bool has_account(const std::string& account_id) const;
    bool has_lane(const std::string& lane_id) const;
    bool is_operator(const std::string& account_id) const;

    const Asset& asset(const std::string& asset_id) const;
    const Lane& lane(const std::string& lane_id) const;
    const Account& account(const std::string& account_id) const;
    Account& account_mut(const std::string& account_id);

    std::vector<Asset> assets() const;
    std::vector<Lane> lanes() const;
    std::vector<Account> accounts() const;

    Amount balance_of(const std::string& account_id, const std::string& asset_id) const;
    void credit(const std::string& account_id,
                const std::string& asset_id,
                Amount amount,
                Timestamp now,
                const std::string& memo);
    void debit(const std::string& account_id,
               const std::string& asset_id,
               Amount amount,
               Timestamp now,
               const std::string& memo);
    void transfer(const TransferLeg& leg, Timestamp now);

    Amount quote(const std::string& source_asset, const std::string& target_asset, Amount source) const;
    Amount total_for_asset(const std::string& asset_id) const;
    LedgerTotals totals(const std::string& source_asset, const std::string& target_asset) const;
    bool verify_non_negative() const;

    Journal& journal();
    const Journal& journal() const;
    std::string digest() const;

  private:
    std::map<std::string, Asset> assets_;
    std::map<std::string, Lane> lanes_;
    std::map<std::string, Account> accounts_;
    std::set<std::string> operators_;
    Journal journal_;
};

struct PartialPolicy {
    bool allow_partial = true;
    std::string mode = "adaptive";
    int max_slices = 4;
    Amount min_slice_source = Amount::of(1);
    Amount max_slice_source = Amount::of(0);
    std::string schedule = "rolling";
    std::string strategy_ref = "standard";

    std::string canonical_public() const;
    std::string canonical_execution() const;
};

struct Intent {
    std::string id;
    std::string owner;
    std::string source_asset;
    std::string target_asset;
    Amount max_source;
    Amount min_target;
    Units quote_floor_bps = 9'850;
    Timestamp valid_after = 0;
    Timestamp expires_at = 0;
    std::uint64_t nonce = 0;
    PartialPolicy partial;
    IntentStatus status = IntentStatus::Draft;
    Digest digest;
    Signature signature;
    Timestamp created_at = 0;

    std::string canonical_public() const;
    std::string canonical_execution() const;
    bool expired(Timestamp now) const;
    bool active(Timestamp now) const;
};

struct FillSlice {
    std::string id;
    std::string lane_id;
    Amount source_amount;
    Amount quoted_target;
    Units price_bps = kBasisPoints;
    int ordinal = 0;
    bool maker_rebate = true;

    std::string canonical() const;
};

struct ExecutionPlan {
    std::string id;
    std::string operator_id;
    std::string intent_id;
    Digest intent_digest;
    std::string strategy_id;
    std::vector<FillSlice> slices;
    Timestamp submitted_at = 0;
    PlanStatus status = PlanStatus::Draft;
    std::string reason;

    Amount gross_source() const;
    Amount gross_target() const;
    std::string canonical() const;
};

struct MatchResult {
    bool accepted = false;
    std::vector<ValidationIssue> issues;
    Amount gross_source;
    Amount gross_target;
    Amount operator_fee;
    Amount protocol_fee;
    Amount rebate;

    static MatchResult ok(Amount gross_source,
                          Amount gross_target,
                          Amount operator_fee,
                          Amount protocol_fee,
                          Amount rebate);
    static MatchResult reject(std::vector<ValidationIssue> issues);
    std::string reason() const;
};

struct ExposureKey {
    std::string intent_id;
    std::string strategy_id;

    bool operator<(const ExposureKey& rhs) const;
    std::string str() const;
};

struct ExposureBucket {
    ExposureKey key;
    Amount used_source;
    Amount delivered_target;
    int fills = 0;
    Timestamp first_seen = 0;
    Timestamp last_seen = 0;

    std::string canonical() const;
};

class Signer {
  public:
    void register_secret(const std::string& account_id, const std::string& secret);
    Signature sign(const std::string& account_id, const std::string& payload) const;
    bool verify(const std::string& account_id, const std::string& payload, const Signature& signature) const;
    std::string public_key(const std::string& account_id) const;

  private:
    std::map<std::string, std::string> secrets_;
};

class IntentBook {
  public:
    bool contains(const std::string& intent_id) const;
    void add(Intent intent);
    Intent& require_mut(const std::string& intent_id);
    const Intent& require(const std::string& intent_id) const;
    void cancel(const std::string& intent_id, const std::string& owner, Timestamp now, Journal& journal);
    void expire_due(Timestamp now, Journal& journal);
    std::vector<Intent> intents() const;

  private:
    std::map<std::string, Intent> intents_;
};

class Matcher {
  public:
    MatchResult evaluate(const Ledger& ledger, const Intent& intent, const ExecutionPlan& plan, Timestamp now) const;

  private:
    std::optional<ValidationIssue> check_slice_assets(const Ledger& ledger,
                                                      const Intent& intent,
                                                      const FillSlice& slice) const;
    std::optional<ValidationIssue> check_slice_amounts(const Lane& lane,
                                                       const Intent& intent,
                                                       const FillSlice& slice) const;
    std::optional<ValidationIssue> check_price(const Intent& intent,
                                               const FillSlice& slice,
                                               Amount expected_target) const;
};

struct EngineRiskView {
    int open_intents = 0;
    int cancelled_intents = 0;
    int expired_intents = 0;
    int rejected_plans = 0;
    int executed_plans = 0;
    int exposure_buckets = 0;
    Amount visible_source_used;
    Amount visible_target_delivered;
};

struct EngineInvariants {
    bool ledger_non_negative = true;
    bool signatures_valid = true;
    bool plans_have_intents = true;
    bool local_limits_hold = true;
    bool lifecycle_consistent = true;
};

class SettlementEngine;

struct AccountRiskLine {
    std::string account_id;
    AccountRole role = AccountRole::User;
    std::string asset_id;
    Amount available;
    Amount reserved;
    Amount notional_value;
    bool below_floor = false;

    std::string canonical() const;
};

struct LaneRiskLine {
    std::string lane_id;
    std::string source_asset;
    std::string target_asset;
    Amount observed_source;
    Amount observed_target;
    Amount operator_fees;
    Amount protocol_fees;
    int executed_slices = 0;
    bool enabled = true;

    std::string canonical() const;
};

struct IntentAuditLine {
    std::string intent_id;
    std::string owner;
    IntentStatus status = IntentStatus::Draft;
    Amount max_source;
    Amount visible_source;
    Amount visible_target;
    int bucket_count = 0;
    int plan_count = 0;
    bool signature_ok = false;

    std::string canonical() const;
};

struct OperatorAuditLine {
    std::string operator_id;
    Amount source_received;
    Amount target_fees;
    int accepted_plans = 0;
    int rejected_plans = 0;
    int slices = 0;

    std::string canonical() const;
};

struct ReconciliationFrame {
    std::string digest;
    Amount total_user_source;
    Amount total_user_target;
    Amount total_vault_source;
    Amount total_vault_target;
    Amount total_operator_source;
    Amount total_operator_target;
    Amount total_fee_target;
    int accounts = 0;
    int assets = 0;
    int lanes = 0;
    int intents = 0;
    int plans = 0;
    int events = 0;

    std::string canonical() const;
};

class Reconciler {
  public:
    std::vector<AccountRiskLine> account_lines(const SettlementEngine& engine) const;
    std::vector<LaneRiskLine> lane_lines(const SettlementEngine& engine) const;
    std::vector<IntentAuditLine> intent_lines(const SettlementEngine& engine) const;
    std::vector<OperatorAuditLine> operator_lines(const SettlementEngine& engine) const;
    ReconciliationFrame frame(const SettlementEngine& engine,
                              const std::string& source_asset,
                              const std::string& target_asset) const;
    std::string digest(const SettlementEngine& engine) const;
    std::map<std::string, Amount> source_by_strategy(const SettlementEngine& engine) const;
    std::map<std::string, Amount> target_by_operator(const SettlementEngine& engine) const;
    std::map<std::string, Amount> rejected_source_by_reason(const SettlementEngine& engine) const;
    bool account_floors_hold(const SettlementEngine& engine) const;
    bool lane_activity_is_supported(const SettlementEngine& engine) const;
    bool intent_lifecycle_is_ordered(const SettlementEngine& engine) const;
    bool frame_is_consistent(const SettlementEngine& engine,
                             const std::string& source_asset,
                             const std::string& target_asset) const;

  private:
    Amount notional(const Ledger& ledger, const std::string& asset_id, Amount amount) const;
    Amount event_sum(const SettlementEngine& engine,
                     EventKind kind,
                     const std::string& asset_id,
                     const std::function<bool(const JournalEvent&)>& predicate) const;
};

struct RouteQuote {
    std::string lane_id;
    std::string operator_id;
    Amount source_amount;
    Amount expected_target;
    Amount operator_fee;
    Amount protocol_fee;
    Amount rebate;
    Amount net_target;
    Units quality_bps = 0;

    std::string canonical() const;
};

struct RouteCandidate {
    std::string route_id;
    std::vector<RouteQuote> quotes;
    Amount gross_source;
    Amount gross_target;
    Amount total_fees;
    Units blended_quality_bps = 0;

    std::string canonical() const;
};

class RouteCatalog {
  public:
    std::vector<RouteQuote> quotes_for(const SettlementEngine& engine,
                                       const Intent& intent,
                                       Amount slice_source) const;
    std::vector<RouteCandidate> candidates_for(const SettlementEngine& engine,
                                               const Intent& intent,
                                               Amount total_source,
                                               int parts) const;
    RouteCandidate best_candidate(const SettlementEngine& engine,
                                  const Intent& intent,
                                  Amount total_source,
                                  int parts) const;
    std::string digest_candidates(const std::vector<RouteCandidate>& candidates) const;

  private:
    std::vector<std::string> operators(const SettlementEngine& engine) const;
    RouteQuote quote_lane(const Ledger& ledger,
                          const Lane& lane,
                          const std::string& operator_id,
                          Amount source) const;
};

struct TimeWindow {
    std::string id;
    Timestamp opens_at = 0;
    Timestamp closes_at = 0;
    Amount source_cap;
    int plan_cap = 0;
    std::string label;

    bool contains(Timestamp now) const;
    std::string canonical() const;
};

struct AdmissionDecision {
    bool accepted = false;
    std::string window_id;
    std::string code;
    Amount remaining_source;
    int remaining_plans = 0;

    std::string canonical() const;
};

class WindowPolicyBook {
  public:
    void add(TimeWindow window);
    std::vector<TimeWindow> windows() const;
    std::vector<TimeWindow> windows_for(Timestamp now) const;
    AdmissionDecision decide(const Intent& intent,
                             const ExecutionPlan& plan,
                             const std::vector<ExposureBucket>& exposures,
                             Timestamp now) const;
    std::string digest() const;

  private:
    Amount used_in_window(const TimeWindow& window,
                          const Intent& intent,
                          const std::vector<ExposureBucket>& exposures) const;
    int plans_in_window(const TimeWindow& window,
                        const Intent& intent,
                        const ExecutionPlan& plan,
                        const std::vector<ExposureBucket>& exposures) const;

    std::vector<TimeWindow> windows_;
};

struct FillProjection {
    std::string slice_id;
    std::string lane_id;
    Amount source;
    Amount expected_target;
    Amount quoted_target;
    Amount slippage;
    Amount operator_fee;
    Amount protocol_fee;
    Amount rebate;
    Amount net_to_owner;
    bool meets_floor = false;

    std::string canonical() const;
};

struct PlanProjection {
    std::string plan_id;
    Amount source;
    Amount expected_target;
    Amount quoted_target;
    Amount net_to_owner;
    Amount total_fees;
    int slices = 0;
    bool all_slices_meet_floor = false;

    std::string canonical() const;
};

class ProjectionEngine {
  public:
    FillProjection project_slice(const Ledger& ledger,
                                 const Intent& intent,
                                 const FillSlice& slice) const;
    PlanProjection project_plan(const Ledger& ledger,
                                const Intent& intent,
                                const ExecutionPlan& plan) const;
    std::vector<PlanProjection> project_plans(const Ledger& ledger,
                                              const Intent& intent,
                                              const std::vector<ExecutionPlan>& plans) const;
    std::string digest(const std::vector<PlanProjection>& projections) const;
};

struct OperatorScore {
    std::string operator_id;
    Amount source_handled;
    Amount target_fees;
    Amount target_inventory;
    int accepted_plans = 0;
    int rejected_plans = 0;
    int total_slices = 0;
    Units reliability_bps = 0;
    Units inventory_bps = 0;
    Units score_bps = 0;

    std::string canonical() const;
    std::string tier() const;
    bool active() const;
};

class OperatorScorer {
  public:
    OperatorScore score(const SettlementEngine& engine,
                        const std::string& operator_id,
                        const std::string& target_asset) const;
    std::vector<OperatorScore> score_all(const SettlementEngine& engine,
                                         const std::string& target_asset) const;
    std::optional<OperatorScore> best_operator(const SettlementEngine& engine,
                                               const std::string& target_asset) const;
    std::string digest(const std::vector<OperatorScore>& scores) const;

  private:
    Units reliability(int accepted, int rejected) const;
    Units inventory_depth(const Ledger& ledger,
                          const std::string& operator_id,
                          const std::string& target_asset) const;
};

class SettlementEngine {
  public:
    SettlementEngine();

    Ledger& ledger();
    const Ledger& ledger() const;
    Signer& signer();
    const Signer& signer() const;
    IntentBook& intents();
    const IntentBook& intents() const;

    Timestamp now() const;
    void set_time(Timestamp now);
    void advance_time(Timestamp delta);

    Intent sign_intent(Intent intent);
    bool submit_intent(Intent intent);
    bool cancel_intent(const std::string& intent_id, const std::string& owner);
    ExecutionPlan execute_plan(ExecutionPlan plan);

    const std::vector<ExecutionPlan>& plans() const;
    std::vector<ExposureBucket> exposures() const;

    EngineRiskView risk_view() const;
    EngineInvariants invariants() const;
    std::string digest() const;

  private:
    void apply_execution(const Intent& intent,
                         const ExecutionPlan& plan,
                         const MatchResult& result,
                         ExposureBucket& bucket);
    ExposureBucket& bucket_for(const Intent& intent, const ExecutionPlan& plan);
    void record_rejection(ExecutionPlan& plan, const MatchResult& result);

    Timestamp now_ = 0;
    Ledger ledger_;
    Signer signer_;
    IntentBook intents_;
    Matcher matcher_;
    std::map<ExposureKey, ExposureBucket> exposures_;
    std::vector<ExecutionPlan> plans_;
};

struct ScenarioResult {
    std::string name;
    SettlementEngine engine;
    std::vector<std::string> notes;
};

struct ScenarioAccounts {
    std::string alice = "alice";
    std::string bob = "bob";
    std::string operator_a = "operator-a";
    std::string operator_b = "operator-b";
    std::string fee_collector = "fee-collector";
    std::string usdc_vault = "vault-usdc";
    std::string eur_vault = "vault-eur";
    std::string gbp_vault = "vault-gbp";
};

class ScenarioFactory {
  public:
    ScenarioResult baseline() const;
    ScenarioResult partial_fill() const;
    ScenarioResult expiration() const;
    ScenarioResult cancellation() const;
    ScenarioResult matching_controls() const;
    ScenarioResult operator_rotation() const;

  private:
    SettlementEngine base_engine(Timestamp now) const;
    Intent base_intent(const ScenarioAccounts& accounts, Timestamp now) const;
    ExecutionPlan plan_for(const Intent& intent,
                           std::string plan_id,
                           std::string operator_id,
                           std::string strategy_id,
                           std::vector<FillSlice> slices,
                           Timestamp now) const;
    FillSlice slice(std::string id,
                    std::string lane_id,
                    Units source,
                    Units target,
                    int ordinal) const;
};

class JsonWriter {
  public:
    explicit JsonWriter(std::ostream& out);

    void write_scenario(const ScenarioResult& result);

  private:
    void write_accounts(const SettlementEngine& engine);
    void write_assets(const SettlementEngine& engine);
    void write_lanes(const SettlementEngine& engine);
    void write_intents(const SettlementEngine& engine);
    void write_plans(const SettlementEngine& engine);
    void write_exposures(const SettlementEngine& engine);
    void write_events(const SettlementEngine& engine);
    void write_totals(const SettlementEngine& engine);
    void write_risk(const SettlementEngine& engine);
    void write_invariants(const SettlementEngine& engine);
    void write_notes(const ScenarioResult& result);

    std::ostream& out_;
};

std::vector<std::string> scenario_names();
ScenarioResult run_scenario(const std::string& name);
int run_cli(int argc, char** argv);

}  // namespace aether
