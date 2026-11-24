#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace yt {
enum class Membership { None, Only, Accessible };
inline const char* membershipWire(Membership m) { return m == Membership::Accessible ? "member" : m == Membership::Only ? "members" : ""; }
inline Membership membershipWire(const std::string& s) { return s == "member" ? Membership::Accessible : s == "members" ? Membership::Only : Membership::None; }
inline bool membersLabel(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c) { return c < 128 ? std::tolower(c) : c; });
    return s == "members only" || s == "members-only" || s == "apenas para membros" || s == "só para membros" || s == "somente para membros";
}
inline bool membersReason(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c) { return c < 128 ? std::tolower(c) : c; });
    return s.find("members-only") != std::string::npos || s.find("members only") != std::string::npos ||
        s.find("channel's members") != std::string::npos || s.find("join this channel") != std::string::npos ||
        s.find("para membros") != std::string::npos || s.find("membro do canal") != std::string::npos ||
        s.find("membro deste canal") != std::string::npos || s.find("membros deste canal") != std::string::npos || s.find("membros do canal") != std::string::npos;
}
}
