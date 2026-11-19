#include "registry.h"

#include <ktmw32.h>
#include <versionhelpers.h>
#include <cerrno>
#include <cwchar>

namespace menu {
namespace {

enum class Entry { Absent, EmptyKey, EmptyString, Conflict };
constexpr wchar_t serverName[] = L"InprocServer32";
constexpr REGSAM registryView = KEY_WOW64_64KEY;

struct Key {
    HKEY value = nullptr;
    Key() = default;
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() { if (value) RegCloseKey(value); }
};

struct Transaction {
    HANDLE value = CreateTransaction(nullptr, nullptr, 0, 0, 0, 5000, nullptr);
    ~Transaction() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

LSTATUS Open(HKEY root, const wchar_t* path, REGSAM access, Key& result, HANDLE transaction) {
    access |= registryView;
    if (transaction) {
        return RegOpenKeyTransactedW(root, path, 0, access, &result.value, transaction, nullptr);
    }
    return RegOpenKeyExW(root, path, 0, access, &result.value);
}

bool IsMissing(LSTATUS status) {
    return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
}

LSTATUS Counts(HKEY key, DWORD& subkeys, DWORD& values) {
    return RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr,
                            &values, nullptr, nullptr, nullptr, nullptr);
}

LSTATUS ReadEntry(HKEY root, const wchar_t* path, HANDLE transaction, Entry& entry) {
    Key classKey;
    LSTATUS status = Open(root, path, KEY_QUERY_VALUE, classKey, transaction);
    if (IsMissing(status)) return ERROR_SUCCESS;
    if (status != ERROR_SUCCESS) return status;

    Key server;
    status = Open(classKey.value, serverName, KEY_READ, server, transaction);
    if (IsMissing(status)) return ERROR_SUCCESS;
    if (status != ERROR_SUCCESS) return status;

    DWORD subkeys = 0;
    DWORD values = 0;
    status = Counts(server.value, subkeys, values);
    if (status != ERROR_SUCCESS) return status;
    if (subkeys || values > 1) {
        entry = Entry::Conflict;
        return ERROR_SUCCESS;
    }

    wchar_t data = L'\0';
    DWORD size = sizeof(data);
    DWORD type = 0;
    status = RegQueryValueExW(server.value, nullptr, nullptr, &type,
                              reinterpret_cast<BYTE*>(&data), &size);
    if (IsMissing(status)) {
        entry = values == 0 ? Entry::EmptyKey : Entry::Conflict;
    } else if (status == ERROR_MORE_DATA) {
        entry = Entry::Conflict;
    } else if (status != ERROR_SUCCESS) {
        return status;
    } else {
        const bool empty = size == 0 || (size == sizeof(data) && data == L'\0');
        entry = type == REG_SZ && empty ? Entry::EmptyString : Entry::Conflict;
    }
    return ERROR_SUCCESS;
}

LSTATUS ReadEntries(HKEY root, const Paths& paths, HANDLE transaction,
                    std::array<Entry, 2>& entries) {
    for (size_t i = 0; i < paths.size(); ++i) {
        const LSTATUS status = ReadEntry(root, paths[i], transaction, entries[i]);
        if (status != ERROR_SUCCESS) return status;
    }
    return ERROR_SUCCESS;
}

State Classify(const std::array<Entry, 2>& entries, Recipe recipe) {
    const auto [legacy, current] = entries;
    if (recipe == Recipe::Unknown || legacy == Entry::Conflict || current == Entry::Conflict) {
        return State::Check;
    }
    if (legacy == Entry::Absent && current == Entry::Absent) return State::Default;
    if (legacy == Entry::EmptyString &&
        (recipe == Recipe::Legacy || current == Entry::EmptyString)) {
        return State::Classic;
    }
    return State::Incomplete;
}

LSTATUS Create(HKEY root, const wchar_t* path, Key& key, HANDLE transaction) {
    return RegCreateKeyTransactedW(root, path, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                   KEY_READ | KEY_WRITE | registryView, nullptr,
                                   &key.value, nullptr, transaction, nullptr);
}

LSTATUS SetClassic(HKEY root, const wchar_t* path, HANDLE transaction) {
    Key classKey;
    LSTATUS status = Create(root, path, classKey, transaction);
    if (status != ERROR_SUCCESS) return status;
    Key server;
    status = Create(classKey.value, serverName, server, transaction);
    if (status != ERROR_SUCCESS) return status;

    constexpr wchar_t empty[] = L"";
    return RegSetValueExW(server.value, nullptr, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(empty), sizeof(empty));
}

LSTATUS RemoveOverride(HKEY root, const wchar_t* path, HANDLE transaction) {
    DWORD subkeys = 0;
    DWORD values = 0;
    {
        Key classKey;
        LSTATUS status = Open(root, path, KEY_READ, classKey, transaction);
        if (status != ERROR_SUCCESS) return status;

        // The transaction has already verified the entire leaf is the recognized override.
        // An empty InprocServer32 key can itself be the legacy override.
        status = RegDeleteKeyTransactedW(classKey.value, serverName, registryView, 0,
                                         transaction, nullptr);
        if (status != ERROR_SUCCESS) return status;
        status = Counts(classKey.value, subkeys, values);
        if (status != ERROR_SUCCESS) return status;
    }
    if (subkeys || values) return ERROR_SUCCESS;
    return RegDeleteKeyTransactedW(root, path, registryView, 0, transaction, nullptr);
}

}  // namespace

Recipe DetectRecipe() {
    if (IsWindowsServer()) return Recipe::Unknown;
    constexpr wchar_t versionPath[] = LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)";
    wchar_t build[16]{};
    DWORD bytes = sizeof(build);
    constexpr DWORD view = RRF_SUBKEY_WOW6464KEY;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, versionPath, L"CurrentBuildNumber",
                     RRF_RT_REG_SZ | view, nullptr, build, &bytes) != ERROR_SUCCESS) {
        return Recipe::Unknown;
    }
    if (!*build || std::wcsspn(build, L"0123456789") != std::wcslen(build)) return Recipe::Unknown;
    errno = 0;
    const unsigned long number = std::wcstoul(build, nullptr, 10);
    if (errno == ERANGE) return Recipe::Unknown;
    if (number == 22000 || number == 22621 || number == 22631) return Recipe::Legacy;
    if (number > 26300) return Recipe::Dual;
    if (number != 26300) return Recipe::Unknown;

    DWORD revision = 0;
    bytes = sizeof(revision);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, versionPath, L"UBR", RRF_RT_REG_DWORD | view,
                     nullptr, &revision, &bytes) != ERROR_SUCCESS) return Recipe::Unknown;
    return revision >= 9974 ? Recipe::Dual : Recipe::Unknown;
}

State Inspect(HKEY root, const Paths& paths, Recipe recipe) {
    std::array<Entry, 2> entries{};
    if (ReadEntries(root, paths, nullptr, entries) != ERROR_SUCCESS) return State::Check;
    return Classify(entries, recipe);
}

ChangeResult Change(HKEY root, const Paths& paths, Recipe recipe, bool classic) {
    if (recipe == Recipe::Unknown) return {ERROR_NOT_SUPPORTED, false};
    Transaction transaction;
    if (transaction.value == INVALID_HANDLE_VALUE) return {static_cast<LSTATUS>(GetLastError()), false};

    std::array<Entry, 2> entries{};
    LSTATUS status = ReadEntries(root, paths, transaction.value, entries);
    if (status != ERROR_SUCCESS) return {status, false};

    const State previous = Classify(entries, recipe);
    const State target = classic ? State::Classic : State::Default;
    if (previous == State::Check) return {ERROR_INVALID_DATA, false};
    if (previous == target) return {};

    for (size_t i = 0; i < paths.size(); ++i) {
        if (classic && (i == 0 || recipe == Recipe::Dual) && entries[i] != Entry::EmptyString) {
            status = SetClassic(root, paths[i], transaction.value);
        } else if (!classic && entries[i] != Entry::Absent) {
            status = RemoveOverride(root, paths[i], transaction.value);
        }
        if (status != ERROR_SUCCESS) return {status, false};
    }

    entries = {};
    status = ReadEntries(root, paths, transaction.value, entries);
    if (status != ERROR_SUCCESS) return {status, false};
    if (Classify(entries, recipe) != target) return {ERROR_INVALID_DATA, false};
    if (!CommitTransaction(transaction.value)) return {static_cast<LSTATUS>(GetLastError()), false};
    return {ERROR_SUCCESS, true};
}

}  // namespace menu
