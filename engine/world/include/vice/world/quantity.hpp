#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <vice/world/event.hpp>
namespace vice::world {
inline constexpr std::size_t quantity_type_capacity = 4096, quantity_account_capacity = 65536,
                             quantity_accounts_per_holder = 64, quantity_query_limit = 64;
struct QuantityTypeId {
    PersistentId value{};
    friend bool operator==(const QuantityTypeId &, const QuantityTypeId &) = default;
};
struct QuantityTypeDesc {
    QuantityTypeId id;
    std::string name;
    std::uint32_t version = 1, quanta_per_unit = 1;
    friend bool operator==(const QuantityTypeDesc &, const QuantityTypeDesc &) = default;
};
struct QuantityKey {
    PersistentId holder{};
    QuantityTypeId type;
    friend bool operator==(const QuantityKey &, const QuantityKey &) = default;
};
struct QuantityAccount {
    QuantityKey key;
    std::int64_t amount = 0, capacity = 0;
    friend bool operator==(const QuantityAccount &, const QuantityAccount &) = default;
};
enum class QuantityOperation : std::uint32_t { produce, consume, transfer };
enum class QuantityResult : std::uint32_t {
    success,
    invalid_argument,
    invalid_type,
    invalid_owner,
    missing_account,
    insufficient_quantity,
    capacity_exceeded,
    nonempty_account,
    duplicate_account,
    schema_conflict,
    storage_limit,
    event_rejected
};
struct QuantityTransaction {
    QuantityOperation operation = QuantityOperation::transfer;
    QuantityTypeId type;
    PersistentId source{}, target{};
    std::int64_t amount = 0;
    std::vector<EventRef> parents;
};
struct QuantityReceipt {
    QuantityResult result = QuantityResult::invalid_argument;
    WorldEventResult event_result = WorldEventResult::success;
    EventRef event;
    std::int64_t source_before = 0, source_after = 0, target_before = 0, target_after = 0;
    explicit operator bool() const noexcept { return result == QuantityResult::success; }
};
struct QuantityTypePage {
    std::vector<QuantityTypeDesc> entries;
    bool has_more = false;
};
[[nodiscard]] ComponentTypeId quantity_receipt_type() noexcept;
[[nodiscard]] const ComponentTypeDesc &quantity_receipt_schema();
[[nodiscard]] bool valid_quantity_type(const QuantityTypeDesc &);
[[nodiscard]] const char *quantity_result_name(QuantityResult) noexcept;
bool read_quantity_type(std::istream &, QuantityTypeDesc &);
void write_quantity_type(std::ostream &, const QuantityTypeDesc &);
bool read_quantity_account(std::istream &, QuantityAccount &);
void write_quantity_account(std::ostream &, const QuantityAccount &);
std::string serialize_quantity_type(const QuantityTypeDesc &);
std::string serialize_quantity_account(const QuantityAccount &);
} // namespace vice::world
