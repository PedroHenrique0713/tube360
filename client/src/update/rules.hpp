#pragma once
#include "common.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace updates {
struct Profile {
    string name, code, version, agent;
    J device;
    std::vector<string> headers;
};
struct Rules {
    int version=1;
    std::map<string,string> strings;
    std::map<string,int> limits;
    std::vector<string> timestampMarkers, fallbackStatuses;
    std::vector<Profile> players;
    Profile tv, samsung;
    const string& s(const string& k) const { return strings.at(k); }
    int n(const string& k) const { return limits.at(k); }
    string api(const string& route,bool music=false) const;
};
bool decodeRules(const string&,Rules&);
class RuleStore {
    mutable std::mutex mu;
    string path, key, activeBytes, activeSig, goodBytes, goodSig;
    std::shared_ptr<const Rules> active;
    int highest=1, rejected=0, failures=0, successes=0;
    bool save();
public:
    RuleStore(string directory,string publicKey);
    std::shared_ptr<const Rules> snapshot() const;
    bool accept(const string&,const string&,string& error);
    void observe(int version,bool success,bool incompatible);
    int highWater() const;
    int failureCount() const;
};
void setRuleStore(std::shared_ptr<RuleStore>);
std::shared_ptr<const Rules> rules();
void observe(int version,bool success,bool incompatible);
}
