#pragma once
#include "common.hpp"
#include "app.hpp"
#include "yt/client.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>

namespace updates::reports {
inline constexpr const char* endpoint="https://tube360.hypermind.space/api/report";
inline string lower(string s) {for(char& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
inline bool word(char c) {return std::isalnum(static_cast<unsigned char>(c)) || c=='_' || c=='-';}
inline string screenName(string name) {
    name=name.substr(0,name.find_first_of("\r\n"));
    if(name=="Busca")return "Search";
    if(name=="Ajustes")return "Settings";
    if(name=="Navegacao")return "Navigation";
    for(auto allowed:{"Home","Player","Search","Settings","Navigation"})if(name==allowed)return name;
    return "startup/unknown";
}
inline string scrub(const string& input) {
    string out;
    for(size_t at=0;at<input.size();) {
        size_t end=input.find('\n',at);if(end==string::npos)end=input.size();
        string line=input.substr(at,end-at), folded=lower(line);
        bool secret=false;
        // Linhas inteiras: nomes/tokens podem conter espaços, aspas e delimitadores arbitrários.
        for(auto label:{"bearer","ya29.","token","cookie","authorization","cast","nest","name","nome","aparelho","conta","account","login","search","busca","query","api_key","client_secret","password","senha","oauth","email","@"})
            if(folded.find(label)!=string::npos){secret=true;break;}
        if(secret)line="[private line removed]";
        else {
            string safe;
            for(size_t i=0;i<line.size();) {
                size_t j=i;int groups=0;
                if(std::isdigit(static_cast<unsigned char>(line[i])) && (i==0 || !std::isdigit(static_cast<unsigned char>(line[i-1])))) {
                    for(int group=0;group<4;++group) {
                        size_t begin=j;while(j<line.size() && std::isdigit(static_cast<unsigned char>(line[j])))++j;
                        if(j==begin || j-begin>3)break;
                        ++groups;if(group<3){if(j==line.size() || line[j]!='.')break;++j;}
                    }
                }
                if(groups==4 && (j==line.size() || !std::isdigit(static_cast<unsigned char>(line[j])))){safe+="[ip]";i=j;continue;}
                unsigned char c=line[i++];safe+=c<32 && c!='\t'?' ':char(c);
            }
            line=std::move(safe);safe.clear();
            for(size_t i=0;i<line.size();) {
                if(word(line[i])) {
                    size_t j=i;while(j<line.size() && word(line[j]))++j;
                    if(j-i==11)safe+="[video]";else if(j-i>=32)safe+="[redacted]";else safe.append(line,i,j-i);
                    i=j;
                } else safe+=line[i++];
            }
            line=std::move(safe);
        }
        out+=line+'\n';at=end<input.size()?end+1:end;
    }
    // Um crash pode deixar o último caractere UTF-8 incompleto no disco.
    string valid;
    for(size_t i=0;i<out.size();) {
        unsigned char c=out[i];size_t n=c<128?1:c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0;
        bool ok=n && i+n<=out.size();
        for(size_t j=1;ok && j<n;++j)ok=(static_cast<unsigned char>(out[i+j])&0xc0)==0x80;
        if(ok && n>=3) {
            unsigned char next=out[i+1];
            if((c==0xe0 && next<0xa0) || (c==0xed && next>=0xa0) || (c==0xf0 && next<0x90) || (c==0xf4 && next>=0x90))ok=false;
        }
        if(ok){valid.append(out,i,n);i+=n;}else{valid+='?';++i;}
    }
    return valid;
}
inline string tail(const string& path,size_t limit=48*1024,size_t lines=300,long offset=0) {
    FILE* file=std::fopen(path.c_str(),"rb");if(!file)return {};
    std::fseek(file,0,SEEK_END);long size=std::ftell(file);
    if(offset>size)offset=0; // O logger do SDK pode truncar o arquivo ao abrir.
    bool partial=size-offset>long(limit);
    long start=partial?size-long(limit):offset;
    if(size<0 || std::fseek(file,start,SEEK_SET)){std::fclose(file);return {};}
    string data(size_t(size-start),'\0');data.resize(std::fread(data.data(),1,data.size(),file));std::fclose(file);
    // Nunca enviar o sufixo de uma linha cortada: poderia ser metade de um token/nome.
    if(partial){auto end=data.find('\n');data=end==string::npos?"":data.substr(end+1);}
    size_t begin=data.size(),count=0;
    while(begin>0){--begin;if(data[begin]=='\n' && ++count>lines){++begin;break;}}
    return data.substr(begin);
}
inline bool enabled(const string& dir) {
    if(exists(dir+"no-error-reports.txt"))return false;
    FILE* f=std::fopen((dir+"settings.txt").c_str(),"r");if(!f)return true;
    bool result=true;char line[512],key[128],value[128];
    while(std::fgets(line,sizeof(line),f))if(std::sscanf(line," %127[^= \t] = %127s",key,value)==2 && string(key)=="error_reports" && string(value)=="off")result=false;
    std::fclose(f);return result;
}
inline string make(const string& dir) {
    string marker=read(dir+"session.txt",2048);
    if(!exists(dir+"session.txt") || marker.rfind("clean\n",0)==0 || marker.rfind("reported\n",0)==0)return {};
    O fields;fields["version"]=J(string(appVersion));fields["build"]=J(std::to_string(buildId));
    auto metadata=[&](const char* name){auto pos=marker.rfind(name);if(pos==string::npos)return string{};pos+=std::strlen(name);return marker.substr(pos,marker.find_first_of(" \r\n",pos)-pos);};
    if(!metadata("version=").empty())fields["version"]=J(metadata("version="));
    if(!metadata("build=").empty())fields["build"]=J(metadata("build="));
    fields["screen"]=J(string("startup/unknown"));fields["error"]=J(string("unclean session (crash, hang or power loss)"));
    auto offset=[&](const char* name){auto key=string("\n")+name+"=";auto pos=marker.find(key);return pos==string::npos?0L:std::max(0L,std::strtol(marker.c_str()+pos+key.size(),nullptr,10));};
    string lastCheckpoint;
    bool handoff=marker.find("launcher=1\n")!=string::npos,appStarted=marker.find("app_started=1\n")!=string::npos;
    string logs,hardware="exploit=unknown\n",runtime=tail(dir+"tube360.log",48*1024,300,handoff?0:offset("tube360.log"));
    if(runtime.empty())runtime=tail(dir+"default.log",48*1024,300,offset("default.log"));
    if(handoff && !appStarted)runtime.clear();
    for(auto name:{"boot-app.log","ba.log","boot-early.log","be.log","startup.log"}) {
        string data=tail(dir+name,20*1024,100,offset(name));
        if(data.empty())continue;
        logs+=string("--- ")+name+" ---\n"+data;
        if(string(name)=="startup.log") {
            auto block=data.rfind("startup: "),reason=block==string::npos?string::npos:data.find('\n',block);
            if(reason!=string::npos) {
                auto detail=data.find('\n',reason+1),end=detail==string::npos?data.size():data.find('\n',detail+1);
                fields["error"]=J("startup.fail "+data.substr(reason+1,std::min(size_t(1500),end-(reason+1))));
            }
        }
        size_t pos=0;
        while(pos<data.size()) {
            auto end=data.find('\n',pos);if(end==string::npos)end=data.size();
            string line=data.substr(pos,end-pos);
            if(line.find("info main ")!=string::npos) {
                auto v=line.find("version=");if(v!=string::npos)fields["version"]=J(line.substr(v+8,line.find(' ',v+8)-(v+8)));
                auto b=line.find("build=");if(b!=string::npos)fields["build"]=J(line.substr(b+6,line.find(' ',b+6)-(b+6)));
            }
            if(line.find("detect video.board ")!=string::npos || line.find("environment.policy")!=string::npos || line.find("board.detect")!=string::npos)hardware+=line+'\n';
            auto checkpoint=line.find(" begin ");
            if(checkpoint!=string::npos)lastCheckpoint=line.substr(checkpoint+7,line.find(' ',checkpoint+7)-(checkpoint+7));
            if(line.find("end app.first-frame ")!=string::npos)fields["screen"]=J(string("Home"));
            if(line.find("error ")!=string::npos)fields["error"]=J(line.substr(line.find("error ")+6,1500));
            pos=end<data.size()?end+1:end;
        }
    }
    if(hardware.find("model=")==string::npos) {
        string current=tail(dir+"boot-launcher.log",20*1024,100);
        if(current.empty())current=tail(dir+"bl.log",20*1024,100);
        size_t at=current.rfind("detect video.board ");
        if(at!=string::npos)hardware+=current.substr(at,current.find('\n',at)-at)+'\n';
    }
    auto exception=runtime.rfind("excecao: codigo ");
    if(exception!=string::npos)fields["error"]=J(runtime.substr(exception,24));
    else if(text(fields["error"]).find("unclean session")==0 && !lastCheckpoint.empty())fields["error"]=J("unclean session; last checkpoint: "+lastCheckpoint);
    // A navegação já é gravada somente quando a tela muda.
    auto navigation=runtime.rfind("navegacao: ");
    if(navigation!=string::npos){auto arrow=runtime.find(" -> ",navigation),end=runtime.find('\n',navigation);if(arrow!=string::npos)fields["screen"]=J(runtime.substr(arrow+4,std::min(size_t(128),end-(arrow+4))));}
    fields["screen"]=J(screenName(text(fields["screen"])));
    logs+="--- app log ---\n"+runtime;
    fields["hardware"]=J(hardware.substr(hardware.size()>7000?hardware.size()-7000:0));
    // Limpa antes de truncar ou serializar; nenhum arquivo de conta é lido.
    for(auto& item:fields)if(item.first!="screen" && item.second.is<string>())item.second=J(scrub(item.second.get<string>()));
    logs=scrub(logs);
    size_t begin=logs.size(),count=0;
    while(begin>0){--begin;if(logs[begin]=='\n' && ++count>300){++begin;break;}}
    logs=logs.substr(begin);
    fields["log"]=J(logs);
    string bytes=J(fields).serialize();
    while(bytes.size()>maxDocument && !logs.empty()) {
        auto end=logs.find('\n');logs=end==string::npos?"":logs.substr(end+1);
        fields["log"]=J(logs);bytes=J(fields).serialize();
    }
    return bytes.size()<=maxDocument?bytes:string{};
}
inline string sanitize(const string& bytes) {
    J doc;if(!parse(bytes,doc) || !fields(doc,{"version","build","hardware","screen","error","log"}))return {};
    O safe;
    for(auto key:{"version","build","hardware","screen","error","log"}) {
        const auto& value=get(doc,key);if(!value.is<string>())return {};
        safe[key]=J(string(key)=="screen"?screenName(value.get<string>()):scrub(value.get<string>()));
    }
    auto out=J(safe).serialize();return out.size()<=maxDocument?out:string{};
}
inline string code(const yt::Response& response) {
    if(response.status<200 || response.status>=300 || !response.error.empty())return {};
    J doc;if(!parse(response.body,doc))return {};
    auto value=text(doc,"code");
    if(value.size()<9 || value.size()>24 || value.rfind("T360-",0))return {};
    for(size_t i=5;i<value.size();++i)if(!(value[i]>='A' && value[i]<='Z') && !(value[i]>='0' && value[i]<='9'))return {};
    return value;
}
}
