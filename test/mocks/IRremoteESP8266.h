#pragma once

/// Enumerator for defining and numbering of supported IR protocol.
/// @note Always add to the end of the list and should never remove entries
///  or change order. Projects may save the type number for later usage
///  so numbering should always stay the same.
enum decode_type_t {
    UNKNOWN = -1,
    UNUSED = 0,
    RC5,
    RC6,
    NEC,
    SONY,
    PANASONIC,  // (5)
    JVC,
    SAMSUNG,
    WHYNTER,
    AIWA_RC_T501,
    LG,  // (10)
    SANYO,
    MITSUBISHI,
    DISH,
    SHARP,
    COOLIX,  // (15)
    DAIKIN,
    DENON,
    KELVINATOR,
    SHERWOOD,
    MITSUBISHI_AC,  // (20)
    RCMM,
    SANYO_LC7461,
    RC5X,
    GREE,
    PRONTO,  // Technically not a protocol, but an encoding. (25)
    NEC_LIKE,
    ARGO,
    TROTEC,
    NIKAI,
    RAW,          // Technically not a protocol, but an encoding. (30)
    GLOBALCACHE,  // Technically not a protocol, but an encoding.
    TOSHIBA_AC,
    FUJITSU_AC,
    MIDEA,
    MAGIQUEST,  // (35)
    LASERTAG,
    CARRIER_AC,
    HAIER_AC,
    MITSUBISHI2,
    HITACHI_AC,  // (40)
    HITACHI_AC1,
    HITACHI_AC2,
    GICABLE,
    HAIER_AC_YRW02,
    WHIRLPOOL_AC,  // (45)
    SAMSUNG_AC,
    LUTRON,
    ELECTRA_AC,
    PANASONIC_AC,
    PIONEER,  // (50)
    LG2,
    MWM,
    DAIKIN2,
    VESTEL_AC,
    TECO,  // (55)
    SAMSUNG36,
    TCL112AC,
    LEGOPF,
    MITSUBISHI_HEAVY_88,
    MITSUBISHI_HEAVY_152,  // 60
    DAIKIN216,
    SHARP_AC,
    GOODWEATHER,
    INAX,
    DAIKIN160,  // 65
    NEOCLIMA,
    DAIKIN176,
    DAIKIN128,
    AMCOR,
    DAIKIN152,  // 70
    MITSUBISHI136,
    MITSUBISHI112,
    HITACHI_AC424,
    SONY_38K,
    EPSON,  // 75
    SYMPHONY,
    HITACHI_AC3,
    DAIKIN64,
    AIRWELL,
    DELONGHI_AC,  // 80
    DOSHISHA,
    MULTIBRACKETS,
    CARRIER_AC40,
    CARRIER_AC64,
    HITACHI_AC344,  // 85
    CORONA_AC,
    MIDEA24,
    ZEPEAL,
    SANYO_AC,
    VOLTAS,  // 90
    METZ,
    TRANSCOLD,
    TECHNIBEL_AC,
    MIRAGE,
    ELITESCREENS,  // 95
    PANASONIC_AC32,
    MILESTAG2,
    ECOCLIM,
    XMP,
    TRUMA,  // 100
    HAIER_AC176,
    TEKNOPOINT,
    KELON,
    TROTEC_3550,
    SANYO_AC88,  // 105
    BOSE,
    ARRIS,
    RHOSS,
    AIRTON,
    COOLIX48,  // 110
    HITACHI_AC264,
    KELON168,
    HITACHI_AC296,
    DAIKIN200,
    HAIER_AC160,  // 115
    CARRIER_AC128,
    TOTO,
    CLIMABUTLER,
    TCL96AC,
    BOSCH144,  // 120
    SANYO_AC152,
    DAIKIN312,
    GORENJE,
    WOWWEE,
    CARRIER_AC84,  // 125
    YORK,
    // Add new entries before this one, and update it to point to the last entry.
    kLastDecodeType = YORK,
};
