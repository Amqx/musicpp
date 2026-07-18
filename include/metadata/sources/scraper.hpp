/**
 * @file scraper.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once

#include <string>
#include "source.hpp"
#include "types/results.hpp"

enum class ScraperRegions : size_t {
    AE, AG, AI, AM, AR, AT, AU, AZ, BB, BE,
    BG, BH, BM, BO, BR, BS, BW, BY, BZ, CA,
    CF, CH, CI, CL, CM, CN, CO, CR, CZ, DE,
    DK, DM, DO, EC, EE, EG, ES, FI, FR, GB,
    GD, GE, GN, GQ, GR, GT, GW, GY, HK, HN,
    HR, HU, ID, IE, IL, IN, IT, JM, JO, JP,
    KG, KN, KR, KW, KY, KZ, LA, LC, LI, LT,
    LU, LV, MA, MD, ME, MG, MK, ML, MO, MS,
    MT, MU, MX, MY, MZ, NE, NG, NI, NL, NO,
    NZ, OM, PA, PE, PH, PL, PR, PT, PY, QA,
    RO, RU, SA, SE, SG, SI, SK, SN, SR, SV,
    TC, TH, TJ, TM, TN, TR, TT, TW, UA, UG,
    US, UY, AZ_UZ, VC, VE, VG, VN, ZA,

    _COUNT
};

std::string to_string(ScraperRegions region);

bool isValidRegion(const std::string &region);

class Scraper : public MetadataWebSource {
public:
    explicit Scraper(const std::string &region);

    explicit Scraper(const ScraperRegions &region) : Scraper(to_string(region)) {
    };

    [[nodiscard]] std::string identify() override;

    [[nodiscard]] SearchResult searchTrack(const Track &track) override;

private:
    std::string _region;
    const std::string kIDENTITY = "Apple Music Web Scraper";
};