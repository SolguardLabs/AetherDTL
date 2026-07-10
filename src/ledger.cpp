#include "aether.hpp"

namespace aether {

Amount Account::available(const std::string& asset_id) const {
    const auto found = balances.find(asset_id);
    if (found == balances.end()) {
        return Amount::zero();
    }
    return found->second;
}

Amount Account::locked(const std::string& asset_id) const {
    const auto found = reserved.find(asset_id);
    if (found == reserved.end()) {
        return Amount::zero();
    }
    return found->second;
}

std::vector<BalanceLine> Account::lines() const {
    std::set<std::string> assets;
    for (const auto& [asset_id, _] : balances) {
        assets.insert(asset_id);
    }
    for (const auto& [asset_id, _] : reserved) {
        assets.insert(asset_id);
    }

    std::vector<BalanceLine> result;
    for (const auto& asset_id : assets) {
        result.push_back(BalanceLine{
            asset_id,
            available(asset_id),
            locked(asset_id),
        });
    }
    return result;
}

std::string Account::canonical() const {
    std::vector<std::string> fields{
        "account",
        id,
        label,
        to_string(role),
        public_key,
        bool_json(active),
    };
    for (const auto& line : lines()) {
        fields.push_back(join_fields({
            line.asset_id,
            line.available.str(),
            line.reserved.str(),
        }));
    }
    return join_fields(fields);
}

void Journal::push(EventKind kind,
                   Timestamp time,
                   std::string subject,
                   std::string asset_id,
                   Amount amount,
                   std::string message) {
    events_.push_back(JournalEvent{
        kind,
        time,
        std::move(subject),
        std::move(asset_id),
        amount,
        std::move(message),
    });
}

const std::vector<JournalEvent>& Journal::events() const {
    return events_;
}

std::vector<JournalEvent> Journal::filter(EventKind kind) const {
    std::vector<JournalEvent> result;
    for (const auto& event : events_) {
        if (event.kind == kind) {
            result.push_back(event);
        }
    }
    return result;
}

std::string Journal::digest() const {
    std::vector<std::string> fields{"journal"};
    for (const auto& event : events_) {
        fields.push_back(event.canonical());
    }
    return Digest::from_payload(join_fields(fields)).str();
}

void Ledger::register_asset(const Asset& asset, Timestamp now) {
    if (asset.id.empty()) {
        throw std::runtime_error("asset id required");
    }
    if (assets_.find(asset.id) != assets_.end()) {
        throw std::runtime_error("asset already registered");
    }
    if (asset.price <= 0) {
        throw std::runtime_error("asset price must be positive");
    }
    assets_.insert({asset.id, asset});
    journal_.push(
        EventKind::AssetRegistered,
        now,
        asset.id,
        asset.id,
        Amount::zero(),
        "asset registered"
    );
}

void Ledger::register_lane(const Lane& lane, Timestamp now) {
    if (lane.id.empty()) {
        throw std::runtime_error("lane id required");
    }
    if (!has_asset(lane.source_asset) || !has_asset(lane.target_asset)) {
        throw std::runtime_error("lane assets missing");
    }
    if (lanes_.find(lane.id) != lanes_.end()) {
        throw std::runtime_error("lane already registered");
    }
    lanes_.insert({lane.id, lane});
    journal_.push(
        EventKind::LaneRegistered,
        now,
        lane.id,
        lane.target_asset,
        Amount::zero(),
        "lane registered"
    );
}

void Ledger::register_account(const Account& account, Timestamp now) {
    if (account.id.empty()) {
        throw std::runtime_error("account id required");
    }
    if (accounts_.find(account.id) != accounts_.end()) {
        throw std::runtime_error("account already registered");
    }
    for (const auto& [asset_id, amount] : account.balances) {
        if (!has_asset(asset_id)) {
            throw std::runtime_error("account balance asset missing");
        }
        if (amount.units < 0) {
            throw std::runtime_error("negative account balance");
        }
    }
    accounts_.insert({account.id, account});
    journal_.push(
        EventKind::AccountRegistered,
        now,
        account.id,
        "",
        Amount::zero(),
        "account registered"
    );
}

void Ledger::authorize_operator(const std::string& account_id) {
    if (!has_account(account_id)) {
        throw std::runtime_error("operator account missing");
    }
    operators_.insert(account_id);
}

bool Ledger::has_asset(const std::string& asset_id) const {
    return assets_.find(asset_id) != assets_.end();
}

bool Ledger::has_account(const std::string& account_id) const {
    return accounts_.find(account_id) != accounts_.end();
}

bool Ledger::has_lane(const std::string& lane_id) const {
    return lanes_.find(lane_id) != lanes_.end();
}

bool Ledger::is_operator(const std::string& account_id) const {
    return operators_.find(account_id) != operators_.end();
}

const Asset& Ledger::asset(const std::string& asset_id) const {
    const auto found = assets_.find(asset_id);
    if (found == assets_.end()) {
        throw std::runtime_error("asset not found");
    }
    return found->second;
}

const Lane& Ledger::lane(const std::string& lane_id) const {
    const auto found = lanes_.find(lane_id);
    if (found == lanes_.end()) {
        throw std::runtime_error("lane not found");
    }
    return found->second;
}

const Account& Ledger::account(const std::string& account_id) const {
    const auto found = accounts_.find(account_id);
    if (found == accounts_.end()) {
        throw std::runtime_error("account not found");
    }
    return found->second;
}

Account& Ledger::account_mut(const std::string& account_id) {
    const auto found = accounts_.find(account_id);
    if (found == accounts_.end()) {
        throw std::runtime_error("account not found");
    }
    return found->second;
}

std::vector<Asset> Ledger::assets() const {
    std::vector<Asset> result;
    for (const auto& [_, asset] : assets_) {
        result.push_back(asset);
    }
    return result;
}

std::vector<Lane> Ledger::lanes() const {
    std::vector<Lane> result;
    for (const auto& [_, lane] : lanes_) {
        result.push_back(lane);
    }
    return result;
}

std::vector<Account> Ledger::accounts() const {
    std::vector<Account> result;
    for (const auto& [_, account] : accounts_) {
        result.push_back(account);
    }
    return result;
}

Amount Ledger::balance_of(const std::string& account_id, const std::string& asset_id) const {
    return account(account_id).available(asset_id);
}

void Ledger::credit(const std::string& account_id,
                    const std::string& asset_id,
                    Amount amount,
                    Timestamp now,
                    const std::string& memo) {
    if (!amount.is_positive()) {
        throw std::runtime_error("credit amount must be positive");
    }
    if (!has_asset(asset_id)) {
        throw std::runtime_error("credit asset missing");
    }
    auto& acct = account_mut(account_id);
    const auto current = acct.available(asset_id);
    acct.balances[asset_id] = current.checked_add(amount);
    journal_.push(EventKind::Credit, now, account_id, asset_id, amount, memo);
}

void Ledger::debit(const std::string& account_id,
                   const std::string& asset_id,
                   Amount amount,
                   Timestamp now,
                   const std::string& memo) {
    if (!amount.is_positive()) {
        throw std::runtime_error("debit amount must be positive");
    }
    if (!has_asset(asset_id)) {
        throw std::runtime_error("debit asset missing");
    }
    auto& acct = account_mut(account_id);
    const auto current = acct.available(asset_id);
    if (current < amount) {
        throw std::runtime_error("insufficient balance");
    }
    acct.balances[asset_id] = current.checked_sub(amount);
    journal_.push(EventKind::Debit, now, account_id, asset_id, amount, memo);
}

void Ledger::transfer(const TransferLeg& leg, Timestamp now) {
    if (leg.from == leg.to) {
        return;
    }
    debit(leg.from, leg.asset_id, leg.amount, now, "transfer debit:" + leg.memo);
    credit(leg.to, leg.asset_id, leg.amount, now, "transfer credit:" + leg.memo);
    journal_.push(EventKind::Transfer, now, leg.from + "->" + leg.to, leg.asset_id, leg.amount, leg.memo);
}

Amount Ledger::quote(const std::string& source_asset, const std::string& target_asset, Amount source) const {
    const auto& src = asset(source_asset);
    const auto& dst = asset(target_asset);
    return source.checked_mul_ratio_floor(src.price, dst.price);
}

Amount Ledger::total_for_asset(const std::string& asset_id) const {
    Amount total = Amount::zero();
    for (const auto& [_, account] : accounts_) {
        total = total.checked_add(account.available(asset_id));
        total = total.checked_add(account.locked(asset_id));
    }
    return total;
}

LedgerTotals Ledger::totals(const std::string& source_asset, const std::string& target_asset) const {
    LedgerTotals result;

    for (const auto& [_, account] : accounts_) {
        if (account.role == AccountRole::Vault) {
            for (const auto& [asset_id, amount] : account.balances) {
                (void)asset_id;
                result.vault_reserves = result.vault_reserves.checked_add(amount);
            }
        } else {
            result.circulating_source =
                result.circulating_source.checked_add(account.available(source_asset));
            result.circulating_target =
                result.circulating_target.checked_add(account.available(target_asset));
        }
    }

    for (const auto& event : journal_.events()) {
        if (event.kind == EventKind::Debit && event.asset_id == source_asset) {
            const auto found = accounts_.find(event.subject);
            if (found != accounts_.end() && found->second.role == AccountRole::User) {
                result.user_source_debited = result.user_source_debited.checked_add(event.amount);
            }
        }
        if (event.kind == EventKind::Credit && event.asset_id == target_asset) {
            const auto found = accounts_.find(event.subject);
            if (found != accounts_.end() && found->second.role == AccountRole::User) {
                result.user_target_credited = result.user_target_credited.checked_add(event.amount);
            }
        }
        if (event.kind == EventKind::FeeAccrued) {
            const auto found = accounts_.find(event.subject);
            if (found != accounts_.end() && found->second.role == AccountRole::Operator) {
                result.operator_fees = result.operator_fees.checked_add(event.amount);
            } else if (found != accounts_.end() && found->second.role == AccountRole::FeeCollector) {
                result.protocol_fees = result.protocol_fees.checked_add(event.amount);
            } else {
                result.rebates = result.rebates.checked_add(event.amount);
            }
        }
    }

    return result;
}

bool Ledger::verify_non_negative() const {
    for (const auto& [_, account] : accounts_) {
        for (const auto& [__, amount] : account.balances) {
            if (amount.units < 0) {
                return false;
            }
        }
        for (const auto& [__, amount] : account.reserved) {
            if (amount.units < 0) {
                return false;
            }
        }
    }
    return true;
}

Journal& Ledger::journal() {
    return journal_;
}

const Journal& Ledger::journal() const {
    return journal_;
}

std::string Ledger::digest() const {
    std::vector<std::string> fields{"ledger"};
    for (const auto& [_, asset] : assets_) {
        fields.push_back(asset.canonical());
    }
    for (const auto& [_, lane] : lanes_) {
        fields.push_back(lane.canonical());
    }
    for (const auto& [_, account] : accounts_) {
        fields.push_back(account.canonical());
    }
    for (const auto& op : operators_) {
        fields.push_back("operator:" + op);
    }
    fields.push_back(journal_.digest());
    return Digest::from_payload(join_fields(fields)).str();
}

}  // namespace aether
