#include "text_layout.hpp"
#include "i18n.hpp"
#include "yt/locale.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>

int checks=0;
void check(bool value,const char* label) { ++checks; if (!value) { std::cerr<<"FAIL "<<label<<'\n'; std::exit(1); } }
std::vector<FriBidiChar> glyphs(const std::string& s) { return textlayout::shape(s).glyphs; }
bool contains(const std::vector<FriBidiChar>& v,const std::string& ascii) {
  return std::search(v.begin(),v.end(),ascii.begin(),ascii.end())!=v.end();
}
int main() {
  using namespace textlayout;
  check(glyphs("ببب")==std::vector<FriBidiChar>({0xFE90,0xFE92,0xFE91}),"initial medial final in visual order");
  check(glyphs("باب")==std::vector<FriBidiChar>({0xFE8F,0xFE8E,0xFE91}),"alef breaks joining on its left");
  check(glyphs("ب")==std::vector<FriBidiChar>({0xFE8F}),"isolated letter");
  check(glyphs("لا")==std::vector<FriBidiChar>({0xFEFB}),"isolated lam-alef, fill removed");
  check(glyphs("بلا")==std::vector<FriBidiChar>({0xFEFC,0xFE91}),"final lam-alef joins previous letter");
  check(glyphs("لأ")==std::vector<FriBidiChar>({0xFEF7}) && glyphs("لإ")==std::vector<FriBidiChar>({0xFEF9}) && glyphs("لآ")==std::vector<FriBidiChar>({0xFEF5}),"all mandatory lam-alef variants");
  check(glyphs("ب‌ب")==std::vector<FriBidiChar>({0xFE8F,0xFE8F}),"ZWNJ prevents joining and stays invisible");
  auto vowel=glyphs("بِسْمِ اللّٰهِ");
  check(std::count_if(vowel.begin(),vowel.end(),mark)==6,"harakat and shadda retained");
  auto rtl=shape("العربية Xbox 360 - 2026");
  check(rtl.rtl && rtl.hasArabic && contains(rtl.glyphs,"Xbox 360") && contains(rtl.glyphs,"2026"),"mixed title numbers and Latin run preserved");
  auto ltr=shape("Xbox 360 بالعربية (2026)");
  check(!ltr.rtl && ltr.hasArabic && contains(ltr.glyphs,"Xbox 360") && contains(ltr.glyphs,"2026"),"Latin-led mixed title remains LTR paragraph");
  check(contains(glyphs("مرحبا (Xbox 360)"),"(Xbox 360)"),"paired brackets and Latin in RTL paragraph");
  check(contains(glyphs("الشبكة 192.168.1.1:8009"),"192.168.1.1:8009"),"IPv4 and port are never reversed");
  check(glyphs("٣٦٠")==std::vector<FriBidiChar>({0x663,0x666,0x660}),"Arabic Indic digits keep digit order");
  check(contains(glyphs("العربية ⁦Xbox 360⁩ 2026"),"Xbox 360"),"bidi isolates supported");
  check(glyphs("English 123 (PT)")==std::vector<FriBidiChar>({'E','n','g','l','i','s','h',' ', '1','2','3',' ','(','P','T',')'}),"Latin drawing order unchanged");
  std::string bad="\xF0\x28\x8C\x28"; size_t pos=0;
  check(decode(bad,pos)==0xFFFD && pos==1 && decode(bad,pos)=='(',"malformed UTF-8 does not consume next ASCII character");
  check(i18n::language("ar")==i18n::Lang::AR && std::string(i18n::code(i18n::Lang::AR))=="ar","Arabic settings round trip");
  check(i18n::cycle(i18n::Lang::ES,1)==i18n::Lang::AR && i18n::cycle(i18n::Lang::EN,-1)==i18n::Lang::AR,"four-language cycle both ways");
  check(i18n::shortDate("2026-11-24",i18n::Lang::AR)=="24 نوفمبر 2026","Arabic date");
  check(std::string(yt::localeLanguage("ar"))=="ar" && std::string(yt::localeRegion("ar"))=="SA","native YouTube locale");
  check(i18n::localCount(i18n::Lang::PT,0,true)=="Vazio" && i18n::localCount(i18n::Lang::PT,0,false)=="Vazio","empty local collections have an explicit state");
  check(i18n::localCount(i18n::Lang::PT,1,false)=="1 vídeo" && i18n::localCount(i18n::Lang::PT,3,false)=="3 vídeos","Portuguese video count units");
  check(i18n::localCount(i18n::Lang::PT,1,true)=="1 posição" && i18n::localCount(i18n::Lang::PT,12,true)=="12 posições","Portuguese resume count units");
  check(i18n::localCount(i18n::Lang::EN,1,false)=="1 video" && i18n::localCount(i18n::Lang::EN,12,true)=="12 positions" && i18n::localCount(i18n::Lang::EN,0,false)=="Empty","English local count labels");
  check(i18n::localCount(i18n::Lang::ES,1,true)=="1 posición" && i18n::localCount(i18n::Lang::ES,3,false)=="3 videos" && i18n::localCount(i18n::Lang::ES,0,true)=="Vacío","Spanish local count labels");
  check(i18n::localCount(i18n::Lang::AR,12,true)=="مواضع: 12" && i18n::localCount(i18n::Lang::AR,3,false)=="فيديوهات: 3" && i18n::localCount(i18n::Lang::AR,0,true)=="فارغ","Arabic local counts retain a unit and western digits");
  check(std::string(i18n::get(i18n::Lang::PT,i18n::Key::LocalLimit))=="Até 200 vídeos · até 500 posições","limits explicitly say up to");
  for (size_t i=0;i<size_t(i18n::Key::Count);++i) {
    auto key=i18n::Key(i); auto ar=i18n::arabic(key);
    check(ar && (i==0 || *ar),"every current key explicitly translated, no English fallback masking omissions");
  }
  for (auto key:{i18n::Key::Keyboard1,i18n::Key::Keyboard2,i18n::Key::Keyboard3,i18n::Key::Keyboard4}) {
    std::string row=i18n::get(i18n::Lang::AR,key); size_t count=0;
    for (size_t i=0;i<row.size();++count) decode(row,i);
    check(count==10,"keyboard has ten Unicode characters per row");
  }
  Cache cache; std::string title="العربية Xbox 360 - 2026";
  const auto* first=&cache.get(title);
  auto start=std::chrono::steady_clock::now();
  bool stable=true;
  for (int i=0;i<100000;++i) stable &= &cache.get(title)==first;
  check(stable,"100000 cache hits keep shaped storage");
  auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
  for (size_t i=0;i<Cache::limit+100;++i) cache.get(std::to_string(i)+title);
  check(cache.lines.size()<=Cache::limit && cache.bytes<=Cache::byteLimit,"bounded text cache");
  std::cout<<checks<<" checks passed; 100000 cached layouts: "<<us<<" us\n";
}
