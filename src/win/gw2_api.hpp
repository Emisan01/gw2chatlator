// gw2_api.hpp — official, localised names from the public GW2 API
// (no API key needed). Feeds the glossary and the spell checker.
#pragma once

#include <string>

#include "core/glossary.hpp"

namespace gct {

struct NameFetchResult {
    bool ok = false;      // false if maps (the core category) could not be loaded
    NameTable names;      // "map:15" -> "Königintal", "spec:27" -> "Drachenjäger" ...
    std::wstring error;   // first problem, if any (also set on partial success)
};

// Blocking (a few seconds, ~15 requests). `lang` is "en", "de", "fr", "es" or "zh".
NameFetchResult FetchGw2Names(const std::string& lang);

}  // namespace gct
