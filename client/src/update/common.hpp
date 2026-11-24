#pragma once
#include "picojson.h"
#include <cstdint>
#include <functional>
#include <string>

namespace updates {
using std::string;
using Trace = std::function<void(const string&)>;
using J = picojson::value;
using O = picojson::object;
using A = picojson::array;
constexpr size_t maxDocument = 64*1024;
const J& get(const J&, const string&);
string text(const J&);
string text(const J&, const string&);
int number(const J&, int low, int high);
bool fields(const J&, std::initializer_list<const char*>);
bool parse(const string&, J&);
bool ascii(const string&, size_t max, bool empty = false);
string host(const string&);
bool youtubeUrl(const string&);
bool updateUrl(const string&);
string read(const string&, size_t max = maxDocument);
bool write(const string&, const string&);
bool exists(const string&);
string sha256(const string&);
string fileHash(const string&, const Trace& trace = {});
bool verify(const string& bytes, const string& signature, const string& key);
string envelope(const string&, const string&);
bool unpack(const string&, string& bytes, string& signature);
}
