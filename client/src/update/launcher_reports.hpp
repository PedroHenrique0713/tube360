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
inline bool installationValid(const string& value) {
    if(value.size()!=32)return false;
    for(char c:value)if(!(c>='0' && c<='9') && !(c>='a' && c<='f'))return false;
    return value!=string(32,'0');
}
inline string installation(const string& dir,const std::function<bool(unsigned char*,size_t)>& random) {
    auto id=read(dir+"error-report-installation.txt",128);
    if(!id.empty() && id.back()=='\n')id.pop_back();
    if(installationValid(id))return id;
    unsigned char bytes[16]{};if(!random(bytes,sizeof(bytes)))return {};
    const char* hex="0123456789abcdef";id.clear();
    for(auto b:bytes){id+=hex[b>>4];id+=hex[b&15];}
    return installationValid(id) && write(dir+"error-report-installation.txt",id+"\n")?id:string{};
}
inline string category(const string& error,const string& log) {
    auto e=lower(error),all=e+'\n'+lower(log);
    auto has=[](const string& text,std::initializer_list<const char*> words){for(auto word:words)if(text.find(word)!=string::npos)return true;return false;};
    if(has(all,{"excecao: codigo","exception code","segmentation fault","sigsegv","sigabrt","fault at ","fault code","assertion failed","panic"}) ||
       (has(all,{"gpu.fence","gpu fence"}) && has(all,{"failed","dead","timeout"})))return "crash";
    if(has(e,{"startup timeout","handoff timeout","did not reach"}))return "startup";
    auto cause=[&](const string& text)->string {
        if(has(text,{"login_required","youtube.auth_required","unplayable"}) || (has(text,{"403"}) && has(text,{"youtube","player","media","link","googlevideo"})))return "youtube";
        if(has(text,{"dns","could not resolve","tls","ssl","certificate","timeout","timed out","offline","no network","no connection","sem rede","sem conex","sem conexão","network unreachable"}))return "network";
        return {};
    };
    auto found=cause(e);if(!found.empty())return found;
    if(has(e,{"startup.fail","startup.incomplete","missing fonts","could not start","falha de partida"}))return "startup";
    if(has(all,{"unclean session","session: unclean","hang","travamento","watchdog expired"}))return "crash";
    found=cause(lower(log));if(!found.empty())return found;
    if(has(lower(log),{"startup.fail","startup.incomplete","missing fonts"}))return "startup";
    return "other";
}
inline string cause(const string& error,const string& log,const string& kind) {
    string evidence=lower(error);
    if(category(error,"")!=kind)evidence=lower(log);
    if(kind=="youtube")return evidence.find("login_required")!=string::npos || evidence.find("youtube.auth_required")!=string::npos?"youtube.auth_required":evidence.find("unplayable")!=string::npos?"youtube.unplayable":"youtube.media_403";
    if(kind=="network")return evidence.find("dns")!=string::npos || evidence.find("could not resolve")!=string::npos?"network.dns":evidence.find("tls")!=string::npos || evidence.find("ssl")!=string::npos || evidence.find("certificate")!=string::npos?"network.tls":evidence.find("timeout")!=string::npos || evidence.find("timed out")!=string::npos?"network.timeout":"network.offline";
    return error;
}
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
        for(auto label:{"bearer","ya29.","github_pat_","sapisid","__secure-","token","cookie","authorization","cast","nest","name","nome","aparelho","conta","account","login","search","busca","query","api_key","client_secret","password","senha","oauth","email","@","ssid","wifi","wi-fi","/home/","/users/","\\users\\"})
            if(folded.find(label)!=string::npos){secret=true;break;}
        // IPv6 (inclusive zone ID) e host:porta podem identificar a rede doméstica.
        for(size_t i=0;!secret && i<line.size();) {
            if(!word(line[i]) && line[i]!='.' && line[i]!=':' && line[i]!='%'){++i;continue;}
            size_t start=i,colons=0,last=0;
            while(i<line.size() && (word(line[i]) || line[i]=='.' || line[i]==':' || line[i]=='%')) {
                if(line[i]==':'){++colons;last=i;}++i;
            }
            bool port=colons==1 && last>start && last+1<i;
            for(size_t j=last+1;port && j<i;++j)port=line[j]>='0' && line[j]<='9';
            if(colons>=2 || port)secret=true;
        }
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
inline string make(const string& dir,const string& id={}) {
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
    if(text(fields["error"]).find("unclean session")==0 && text(fields["screen"])=="startup/unknown")fields["error"]=J("startup.incomplete last checkpoint: "+(lastCheckpoint.empty()?string("handoff"):lastCheckpoint));
    logs+="--- app log ---\n"+runtime;
    fields["hardware"]=J(hardware.substr(hardware.size()>7000?hardware.size()-7000:0));
    auto kind=category(text(fields["error"]),logs);
    fields["error"]=J(cause(text(fields["error"]),logs,kind));
    // Limpa antes de truncar ou serializar; nenhum arquivo de conta é lido.
    for(auto& item:fields)if(item.first!="screen" && item.second.is<string>())item.second=J(scrub(item.second.get<string>()));
    fields["category"]=J(kind);
    fields["installation_id"]=J(id);
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
inline string sanitize(const string& bytes,const string& id={}) {
    J doc;if(!parse(bytes,doc) || (!fields(doc,{"version","build","hardware","screen","error","log"}) && !fields(doc,{"version","build","hardware","screen","error","log","category","installation_id"})))return {};
    O safe;
    for(auto key:{"version","build","hardware","screen","error","log"}) {
        const auto& value=get(doc,key);if(!value.is<string>())return {};
        safe[key]=J(string(key)=="screen"?screenName(value.get<string>()):scrub(value.get<string>()));
    }
    auto kind=category(text(doc,"error"),text(doc,"log"));
    safe["error"]=J(scrub(cause(text(doc,"error"),text(doc,"log"),kind)));
    auto existing=text(doc,"installation_id");
    if(!installationValid(existing))existing=id;
    if(!installationValid(existing))return {};
    safe["installation_id"]=J(existing);safe["category"]=J(kind);
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
