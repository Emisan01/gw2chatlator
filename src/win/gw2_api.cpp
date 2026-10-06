// gw2_api.cpp
#include "core/i18n.hpp"
#include "gw2_api.hpp"

#include <cwchar>

#include "core/json.hpp"
#include "core/text.hpp"
#include "http.hpp"

namespace gct {
namespace {

struct Category {
    const char* key;      // prefix in the name table
    const wchar_t* path;  // API endpoint
    bool eliteOnly;       // specializations: core specs are not chat vocabulary
};

const Category kCategories[] = {
    {"map", L"/v2/maps", false},
    {"prof", L"/v2/professions", false},
    {"spec", L"/v2/specializations", true},
    {"wvw", L"/v2/wvw/objectives", false},
    {"mount", L"/v2/mounts/types", false},
};

std::string IdToString(const JsonValue* id) {
    if (!id) return {};
    if (id->type == JsonValue::Type::String) return id->s;
    if (id->type == JsonValue::Type::Number) return std::to_string(static_cast<long long>(id->n));
    return {};
}

// Loads every page of one endpoint. Returns an error text or empty on success.
std::wstring FetchCategory(const Category& c, const std::string& lang, NameTable& out) {
    const std::wstring wlang = FromUtf8(lang);
    int totalPages = 1;
    for (int page = 0; page < totalPages && page < 50; ++page) {
        const std::wstring path =
            std::wstring(c.path) + L"?page=" + std::to_wstring(page) + L"&page_size=200&lang=" + wlang;
        const HttpResponse http = HttpsRequest(L"GET", L"api.guildwars2.com", path, L"", "", L"X-Page-Total");
        if (!http.transportOk) return L"GW2-API: " + http.error;
        if (http.status != 200) return L"GW2-API " + std::wstring(c.path) + L": HTTP " + std::to_wstring(http.status);

        JsonValue root;
        if (!ParseJson(http.body, root) || !root.IsArray()) return L"GW2 API: " + Tr(L"answer not readable");
        for (const JsonValue& item : root.arr) {
            if (c.eliteOnly && !item.GetBool("elite")) continue;
            const std::string id = IdToString(item.Get("id"));
            const std::string name = item.GetString("name");
            if (id.empty() || name.empty()) continue;
            out[std::string(c.key) + ":" + id] = FromUtf8(name);
        }

        if (!http.extraHeader.empty()) {
            const long total = std::wcstol(http.extraHeader.c_str(), nullptr, 10);
            if (total > 0) totalPages = static_cast<int>(total);
        }
    }
    return {};
}

}  // namespace

NameFetchResult FetchGw2Names(const std::string& lang) {
    NameFetchResult r;
    for (const Category& c : kCategories) {
        const std::wstring err = FetchCategory(c, lang, r.names);
        if (!err.empty() && r.error.empty()) r.error = err;
        if (!err.empty() && std::string(c.key) == "map") return r;  // maps are the core; give up
    }
    r.ok = !r.names.empty();
    return r;
}

}  // namespace gct
