#pragma once

#include <cstdint>

// Elite specialization ID -> name and -> icon path on render.guildwars2.com.
// IDs match /v2/specializations and Mumble Link's identity "spec" field.
//
// CANONICAL copy lives here (Pie UI's src/chat/SpecData.h). It is part of the vendorable
// ChatLinks unit: when ArenaNet ships a new elite spec, update THIS copy first, then re-vendor
// it to any consumer. See src/chat/ChatLinks.README.md.

namespace PieUI {
namespace SpecData {

// Returns nullptr if the spec id is unknown (core character, or a
// newly-added spec we haven't catalogued yet).
inline const char* GetEliteSpecName(uint32_t specId) {
    switch (specId) {
        // Heart of Thorns
        case  5: return "Druid";
        case  7: return "Daredevil";
        case 18: return "Berserker";
        case 27: return "Dragonhunter";
        case 34: return "Reaper";
        case 40: return "Chronomancer";
        case 43: return "Scrapper";
        case 48: return "Tempest";
        case 52: return "Herald";

        // Path of Fire
        case 55: return "Soulbeast";
        case 56: return "Weaver";
        case 57: return "Holosmith";
        case 58: return "Deadeye";
        case 59: return "Mirage";
        case 60: return "Scourge";
        case 61: return "Spellbreaker";
        case 62: return "Firebrand";
        case 63: return "Renegade";

        // End of Dragons
        case 64: return "Harbinger";
        case 65: return "Willbender";
        case 66: return "Virtuoso";
        case 67: return "Catalyst";
        case 68: return "Bladesworn";
        case 69: return "Vindicator";
        case 70: return "Mechanist";
        case 71: return "Specter";
        case 72: return "Untamed";

        // Visions of Eternity (verified via /v2/specializations?ids=all).
        case 73: return "Troubadour";  // Mesmer
        case 74: return "Paragon";     // Warrior
        case 75: return "Amalgam";     // Engineer
        case 76: return "Ritualist";   // Necromancer
        case 77: return "Antiquary";   // Thief
        case 78: return "Galeshot";    // Ranger
        case 79: return "Conduit";     // Revenant
        case 80: return "Evoker";      // Elementalist
        case 81: return "Luminary";    // Guardian

        default: return nullptr;
    }
}

// Elite spec icon path on render.guildwars2.com (after "/file/").
// Uses the `profession_icon_big` field — the recognisable badge that GW2's
// hero panel shows for an elite spec, not the smaller trait icon.
// Verified via /v2/specializations?ids=all on 2026-05-27.
inline const char* GetEliteSpecIconPath(uint32_t specId) {
    switch (specId) {
        case  5: return "F910F8BBCAF80355FFDB264160C899594300D7B2/1128574.png"; // Druid
        case  7: return "232D31C9A3076F6A6B41167FBDA7D50F70C82260/1128570.png"; // Daredevil
        case 18: return "E39819140FF49CB5C800B064E17EA221C909DFD5/1128566.png"; // Berserker
        case 27: return "1B3B15431B002C09BDABEB7C0E28FCF764FDDB23/1128572.png"; // Dragonhunter
        case 34: return "C9A2C304CFBFA3CBBF41DA0EE756C519F5050D5F/1128578.png"; // Reaper
        case 40: return "4C919A74282939EC004333292A93A4073CCDFFFD/1128568.png"; // Chronomancer
        case 43: return "21C007BE69070E099BF05B4ED04A2D2B90B8FB1F/1128580.png"; // Scrapper
        case 48: return "CDE1093102F02010B30FB210F2AC77A5F00C2535/1128582.png"; // Tempest
        case 52: return "513E3D33671326D32ABD070575017451AA229A08/1128576.png"; // Herald
        case 55: return "7257FBD9A35002DC5155062773D305353AF45E2F/1770214.png"; // Soulbeast
        case 56: return "06F17108155842F0C37F3E7EF901ABB17D0C9232/1670505.png"; // Weaver
        case 57: return "A84BD2D74D3239451E3FF4EFC0F6A146F3F6653E/1770224.png"; // Holosmith
        case 58: return "2BE4F4AB7F69206BBDABB20CACB1DC7911B33F4E/1770212.png"; // Deadeye
        case 59: return "A879752D07B7E8A154A952AC99D01EDC9FBC60A9/1770216.png"; // Mirage
        case 60: return "1556059855E5624F269B049F3D08B7B122B43906/1770220.png"; // Scourge
        case 61: return "342C1C032B5513EF1CC5082592A8D8A6EEC33197/1770222.png"; // Spellbreaker
        case 62: return "AA12C93CBF5D25E40409060D5CD69E27F72B0892/1770210.png"; // Firebrand
        case 63: return "132E53C7D89FB623402FE3A808EBF62B5C0A016F/1770218.png"; // Renegade
        case 64: return "3C3DDB0868067A172907A03A25710890923FDD97/2479359.png"; // Harbinger
        case 65: return "5BC4A4805DCBACAE47E7F9F7DEDE41A9AC25B10A/2479351.png"; // Willbender
        case 66: return "67EC28331F55782A7FFC386E455F5EE6913A126E/2479355.png"; // Virtuoso
        case 67: return "DE06FCD8ABFEC30139690E9E0B481AF4EDCE41B1/2491555.png"; // Catalyst
        case 68: return "713A5A366FB6A25F1C973C067202DA1B46E3CDAC/2491563.png"; // Bladesworn
        case 69: return "DAA84AB8799206A5560B9FC4E42B15AEDA14DD0B/2491559.png"; // Vindicator
        case 70: return "0C1E094E7708207A9AC6BE46C3CD39B46AEDE77A/2503656.png"; // Mechanist
        case 71: return "ECC123FEEAF1B8450B017253510113E4800C34B6/2503664.png"; // Specter
        case 72: return "AAAB98C6F974A456B5EA09EBC93DC47FCC40521F/2503660.png"; // Untamed
        case 73: return "0DF49BF0BFCCB7DEC6B4C29657E0CE09C990E84D/3680071.png"; // Troubadour
        case 74: return "41790FF97C08349151F9B813030FA7F4E06D0E78/3680091.png"; // Paragon
        case 75: return "53CA9852D74F9543E2F772E9B475A5E9CA64F2AE/3680063.png"; // Amalgam
        case 76: return "49677E53AAD2F1C2DD269739436A5E011DEEAB17/3680075.png"; // Ritualist
        case 77: return "97D6737DB5D2B2EE1713B906C1D41C76CCEC994A/3680087.png"; // Antiquary
        case 78: return "DF66F479DE7C1E4C0557CD9F2DA3D7EB74E0C73D/3680079.png"; // Galeshot
        case 79: return "044FF2E93DA71F57C3EB41A4695750FDE0C82908/3680083.png"; // Conduit
        case 80: return "630FAA28AA0C38D3190F40091B2701A10244A046/3680059.png"; // Evoker
        case 81: return "19D81DA43ACDC671E9695DD21BD944382855CF08/3680067.png"; // Luminary
        default: return nullptr;
    }
}

// Core profession name. Returns "" for 0/unknown.
// 1=Guardian, 2=Warrior, 3=Engineer, 4=Ranger, 5=Thief,
// 6=Elementalist, 7=Mesmer, 8=Necromancer, 9=Revenant.
inline const char* GetProfessionName(uint32_t prof) {
    switch (prof) {
        case 1: return "Guardian";
        case 2: return "Warrior";
        case 3: return "Engineer";
        case 4: return "Ranger";
        case 5: return "Thief";
        case 6: return "Elementalist";
        case 7: return "Mesmer";
        case 8: return "Necromancer";
        case 9: return "Revenant";
        default: return "";
    }
}

// Core profession icon path on render.guildwars2.com (after "/file/").
// Verified via /v2/professions on 2026-05-27.
// Returns nullptr for 0/unknown.
inline const char* GetProfessionIconPath(uint32_t prof) {
    switch (prof) {
        case 1: return "6E0D0AC6E0CE5C0C29B3D736ABEA070F4A58540E/156633.png"; // Guardian
        case 2: return "0A76324239946B79C061762095FAB2BDF7A1D8D7/156642.png"; // Warrior
        case 3: return "A94D00911BD47CDE39A104F90C7D07DE623554ED/156631.png"; // Engineer
        case 4: return "FEF2479DC197D40758A8D6E95201F4A7996EB357/156639.png"; // Ranger
        case 5: return "13A2C0EF23F23FF2084875629465279DDA807E3D/103581.png";  // Thief
        case 6: return "BBED46EB20C80D0DDE0F99402493C7E6FFAE1530/156629.png"; // Elementalist
        case 7: return "AF61567E16A83F145D6FB35D63BF01074A3A5AB9/156635.png"; // Mesmer
        case 8: return "CA5A4E96080FCF057C9DA0ED35C693477580421C/156637.png"; // Necromancer
        case 9: return "696A48DD61EE01FD1F4FBBBDB82D74611E04EA39/965717.png"; // Revenant
        default: return nullptr;
    }
}

} // namespace SpecData
} // namespace PieUI
