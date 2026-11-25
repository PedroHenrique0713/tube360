#pragma once
// Bancada: preserva URLs, TLS e redirects reais do cliente, mas liga SOMENTE ao servidor local.
#include <curl/curl.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
static CURLcode migrationPerform(CURL* handle) {
    const char* port=std::getenv("TUBE360_MIGRATION_PORT");
    const char* file=std::getenv("TUBE360_MIGRATION_CA");
    if(!port || !file)return CURLE_FAILED_INIT;
    std::ifstream input(file);
    std::string ca((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    if(ca.empty())return CURLE_FAILED_INIT;
    curl_blob blob{ca.data(),ca.size(),CURL_BLOB_COPY};
    std::string mapping="::127.0.0.1:"+std::string(port);
    auto* connections=curl_slist_append(nullptr,mapping.c_str());
    if(!connections)return CURLE_OUT_OF_MEMORY;
    if(curl_easy_setopt(handle,CURLOPT_CONNECT_TO,connections)!=CURLE_OK ||
       curl_easy_setopt(handle,CURLOPT_CAINFO_BLOB,&blob)!=CURLE_OK) {
        curl_slist_free_all(connections);return CURLE_FAILED_INIT;
    }
    auto result=curl_easy_perform(handle);
    curl_easy_setopt(handle,CURLOPT_CONNECT_TO,nullptr);
    curl_slist_free_all(connections);
    return result;
}
#define curl_easy_perform migrationPerform
