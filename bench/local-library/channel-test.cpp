#include "yt/catalog.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
using namespace yt::catalog;
using yt::string;
int checks=0;
void check(bool ok,const char* what){++checks;if(!ok){std::cerr<<"FAIL "<<what<<'\n';std::exit(1);}}
J read(const string& name){std::ifstream in(name);std::stringstream bytes;bytes<<in.rdbuf();J j;check(parse(bytes.str(),j),"recording parses");return j;}
int main(int argc,char** argv){
  if(argc!=2)return 2;string root=argv[1];
  for(auto tab:{"videos","live","playlists","shorts"}){
    auto path=root+(string(tab)=="shorts"?"/fixtures/channel-":"/../catalog/fixtures/channel-")+tab+".json";
    auto p=decode(read(path),false);check(!p.items.empty(),"each recorded channel tab has items");
    for(const auto& v:p.items){check(!v.image.empty() && imageUrl(v.image)==v.image,"every recorded card yields accepted artwork");if(string(tab)=="shorts")check(v.type=="video" && videoId(v.id) && v.title!=v.id,"recorded Shorts title and endpoint");}
    std::cout<<tab<<": "<<p.items.size()<<" cards, all image URLs accepted\n";
  }
  check(imageUrl("//i.ytimg.com/vi/test0000001/hqdefault.jpg")=="https://i.ytimg.com/vi/test0000001/hqdefault.jpg","protocol relative normalizes");
  for(auto url:{"//localhost/img","//i.ytimg.com.evil/img","/image.jpg","https://i.ytimg.com@localhost/img","https://i.ytimg.com:80/img"})check(imageUrl(url).empty(),"relative normalization retains host restrictions");
  J variants;check(parse(R"({"thumbnailViewModel":{"image":{"sources":[{"url":"//i.ytimg.com/vi_webp/test0000001/hq720.webp","width":720}]}},"secondary":{"thumbnails":[{"url":"https://yt3.ggpht.com/avatar","width":32}]}})",variants),"variant parses");
  check(thumbnail(variants)=="https://i.ytimg.com/vi_webp/test0000001/hq720.webp","new image source beats secondary avatar");
  check(jpegThumbnail(thumbnail(variants))=="https://i.ytimg.com/vi/test0000001/mqdefault.jpg","webp JPEG alternative");
  check(jpegThumbnail("https://yt3.ggpht.com/vi/test0000001/image.webp").empty(),"no URL rewriting outside exact ytimg host");
  check(thumbnail(read(root+"/fixtures/channel-shorts.json")).size()>0,"recorded thumbnailViewModel");
  std::cout<<checks<<" channel artwork checks passed\n";
}
