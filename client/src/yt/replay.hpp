// Bancada desktop: todas as respostas vêm de arquivos sanitizados, inclusive as faixas MP4.
#pragma once
#include "client.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace yt {
class Replay final : public Transport {
    string directory;
public:
    explicit Replay(string dir) : directory(std::move(dir)) {}
    Response request(const string& url, const string& body, bool form) override {
        return send({url,body,{},form,{}});
    }
    Response send(const Request& request) override {
        const auto& url = request.url;
        string file;
        bool media = false;
        if (url.find("/watch?v=") != string::npos) file = "watch.html";
        else if (url.size() >= 8 && url.compare(url.size()-8,8,"/base.js") == 0) file = "base.js";
        else if (url.find("/youtubei/v1/player?") != string::npos) file = "player.json";
        else if (url.find("/youtubei/v1/") != string::npos) file = "browse.json";
        else if (url == "https://www.youtube.com/") file = "home.html";
        else if (url == "https://fixture.googlevideo.com/video") { file = "video.mp4"; media = true; }
        else if (url == "https://fixture.googlevideo.com/audio") { file = "audio.m4a"; media = true; }
        else if (url.find("https://127.0.0.1:") == 0 && url.find("/audio.m4a") != string::npos) { file = "audio.m4a"; media = true; }
        else return {0,{},"replay_unexpected_request"};
        FILE* input = std::fopen((directory+"/"+file).c_str(),"rb");
        if (!input) return {0,{},"replay_missing_file"};
        std::fseek(input,0,SEEK_END); long length = std::ftell(input);
        long long first = 0, last = length-1;
        bool range = false;
        for (const auto& header : request.headers) if (std::sscanf(header.c_str(),"Range: bytes=%lld-%lld",&first,&last) == 2) range = true;
        if (length <= 0 || length > 16*1024*1024 || first < 0 || first >= length || last < first) {
            std::fclose(input); return {416,{},"replay_range"};
        }
        last = std::min(last,static_cast<long long>(length-1));
        Response response; response.status = range ? 206 : 200;
        response.body.resize(size_t(last-first+1)); std::fseek(input,long(first),SEEK_SET);
        if (std::fread(response.body.data(),1,response.body.size(),input) != response.body.size()) response = {0,{},"replay_read"};
        std::fclose(input);
        if (range && media) response.contentRange = " bytes "+std::to_string(first)+"-"+std::to_string(last)+"/"+std::to_string(length);
        return response;
    }
};
}
