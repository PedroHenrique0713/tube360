#pragma once
#include "membership.hpp"
#include "picojson.h"

namespace yt {
inline bool membershipBadge(const picojson::value& v, bool badge = false) {
    if (v.is<picojson::array>()) {
        for (const auto& x : v.get<picojson::array>()) if (membershipBadge(x,badge)) return true;
    } else if (v.is<picojson::object>()) {
        for (const auto& kv : v.get<picojson::object>()) {
            const auto& k = kv.first; const auto& x = kv.second;
            if (k == "isMembersOnly" && x.is<bool>() && x.get<bool>()) return true;
            if (badge && x.is<std::string>()) {
                const auto& s = x.get<std::string>();
                if ((k == "style" || k == "badgeStyle") && (s == "BADGE_STYLE_TYPE_MEMBERS_ONLY" || s == "BADGE_MEMBERS_ONLY")) return true;
                if ((k == "iconType" || k == "iconName") && s == "SPONSORSHIP_STAR") return true;
                if ((k == "label" || k == "text" || k == "badgeText" || k == "simpleText" || k == "content") && membersLabel(s)) return true;
            }
            if (k == "menu" || k == "ownerBadges" || k == "navigationEndpoint" || k == "onSelectCommand" || k == "onLongPressCommand") continue;
            if (membershipBadge(x,badge || k == "badges" || k == "badge" || k.find("BadgeRenderer") != std::string::npos || k == "badgeViewModel")) return true;
        }
    }
    return false;
}
inline Membership membership(const picojson::value& v) {
    if (!membershipBadge(v)) return Membership::None;
    // Um endpoint watch e uma inscrição comum existem também para quem não pagou.
    if (v.is<picojson::object>()) {
        const auto& playable = v.get("isPlayable");
        if (playable.is<bool>() && playable.get<bool>()) return Membership::Accessible;
    }
    return Membership::Only;
}
}
