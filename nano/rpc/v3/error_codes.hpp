#pragma once

#include <string_view>

namespace nano::rpc::v3::errors
{
// Request errors (1000-1099)
constexpr std::string_view INVALID_JSON = "INVALID_JSON";
constexpr std::string_view MISSING_REQUIRED_FIELD = "MISSING_REQUIRED_FIELD";
constexpr std::string_view UNKNOWN_ACTION = "UNKNOWN_ACTION";
constexpr std::string_view INVALID_REQUEST_FORMAT = "INVALID_REQUEST_FORMAT";

// Account errors (1100-1199)
constexpr std::string_view ACCOUNT_NOT_FOUND = "ACCOUNT_NOT_FOUND";
constexpr std::string_view INVALID_ACCOUNT_FORMAT = "INVALID_ACCOUNT_FORMAT";
constexpr std::string_view ACCOUNT_INVALID = "ACCOUNT_INVALID";

// Block errors (1200-1299)
constexpr std::string_view BLOCK_NOT_FOUND = "BLOCK_NOT_FOUND";
constexpr std::string_view INVALID_BLOCK_HASH = "INVALID_BLOCK_HASH";
constexpr std::string_view BLOCK_INVALID = "BLOCK_INVALID";
constexpr std::string_view INVALID_BLOCK_TYPE = "INVALID_BLOCK_TYPE";
constexpr std::string_view BLOCK_CREATE_KEY_REQUIRED = "BLOCK_CREATE_KEY_REQUIRED";
constexpr std::string_view BLOCK_CREATE_BALANCE_MISMATCH = "BLOCK_CREATE_BALANCE_MISMATCH";
constexpr std::string_view BLOCK_CREATE_PUBLIC_KEY_MISMATCH = "BLOCK_CREATE_PUBLIC_KEY_MISMATCH";
constexpr std::string_view BLOCK_CREATE_REQUIREMENTS_STATE = "BLOCK_CREATE_REQUIREMENTS_STATE";
constexpr std::string_view BLOCK_CREATE_REQUIREMENTS_OPEN = "BLOCK_CREATE_REQUIREMENTS_OPEN";
constexpr std::string_view BLOCK_CREATE_REQUIREMENTS_RECEIVE = "BLOCK_CREATE_REQUIREMENTS_RECEIVE";
constexpr std::string_view BLOCK_CREATE_REQUIREMENTS_CHANGE = "BLOCK_CREATE_REQUIREMENTS_CHANGE";
constexpr std::string_view BLOCK_CREATE_REQUIREMENTS_SEND = "BLOCK_CREATE_REQUIREMENTS_SEND";
constexpr std::string_view BAD_PRIVATE_KEY = "BAD_PRIVATE_KEY";
constexpr std::string_view BAD_REPRESENTATIVE_NUMBER = "BAD_REPRESENTATIVE_NUMBER";
constexpr std::string_view BAD_DESTINATION = "BAD_DESTINATION";
constexpr std::string_view BAD_SOURCE = "BAD_SOURCE";
constexpr std::string_view BAD_PREVIOUS = "BAD_PREVIOUS";
constexpr std::string_view BAD_LINK = "BAD_LINK";
constexpr std::string_view INVALID_AMOUNT = "INVALID_AMOUNT";
constexpr std::string_view INVALID_BALANCE = "INVALID_BALANCE";
constexpr std::string_view INSUFFICIENT_BALANCE = "INSUFFICIENT_BALANCE";

// Wallet errors (1300-1399)
constexpr std::string_view WALLET_NOT_FOUND = "WALLET_NOT_FOUND";
constexpr std::string_view WALLET_LOCKED = "WALLET_LOCKED";
constexpr std::string_view WALLET_INVALID = "WALLET_INVALID";

// Network errors (1400-1499)
constexpr std::string_view NETWORK_ERROR = "NETWORK_ERROR";
constexpr std::string_view PEER_NOT_FOUND = "PEER_NOT_FOUND";

// Work errors (1500-1599)
constexpr std::string_view WORK_GENERATION_FAILED = "WORK_GENERATION_FAILED";
constexpr std::string_view WORK_VALIDATION_FAILED = "WORK_VALIDATION_FAILED";
constexpr std::string_view INSUFFICIENT_WORK = "INSUFFICIENT_WORK";
constexpr std::string_view DISABLED_WORK_GENERATION = "DISABLED_WORK_GENERATION";

// Permission errors (1600-1699)
constexpr std::string_view PERMISSION_DENIED = "PERMISSION_DENIED";
constexpr std::string_view CONTROL_DISABLED = "CONTROL_DISABLED";
constexpr std::string_view UNAUTHORIZED = "UNAUTHORIZED";

// Internal errors (1700-1799)
constexpr std::string_view INTERNAL_ERROR = "INTERNAL_ERROR";
constexpr std::string_view NOT_IMPLEMENTED = "NOT_IMPLEMENTED";
constexpr std::string_view SERVICE_UNAVAILABLE = "SERVICE_UNAVAILABLE";

// Add more error codes as needed...
}
