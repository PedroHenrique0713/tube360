#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#include "gfx.hpp"
#include "i18n.hpp"
#include <iostream>

int checks=0;
void check(bool value,const char* name) { ++checks; if (!value) { std::cerr<<"FAIL "<<name<<'\n'; std::exit(1); } }
int main(int argc,char** argv) {
  if (argc!=2) return 2;
  check(SDL_Init(SDL_INIT_VIDEO),"SDL initialization");
  gfx::Gfx g; check(gfx::init(g,1280,720,"Arabic text contracts"),"Vulkan renderer");
  check(gfx::loadFonts(g,std::string(argv[1])+"/",{{"inter-400.ttf",19},{"inter-700.ttf",42}}),"Inter fonts");
  check(!g.arabicAttempted && !g.arabicAtlas.img,"Noto not loaded during Latin font initialization");
  float latinWidth=gfx::textWidth(g,0,"Xbox 360");
  check(!g.arabicAttempted,"Latin measurement does not read Arabic font files");
  float mixedWidth=gfx::textWidth(g,0,"العربية Xbox 360");
  check(g.arabicAtlas.img && mixedWidth>latinWidth,"Arabic measurement loads fallback before fitting");
  check(!gfx::glyph(g.fonts[0],'X')->arabic && gfx::glyph(g.fonts[0],0xFE91)->arabic,"per-character Inter / Noto fallback");
  check(gfx::textWidth(g,0,"لا")==gfx::glyph(g.fonts[0],0xFEFB)->adv,"ligature measured as one glyph");
  check(std::abs(gfx::textWidth(g,0,"بِ")-gfx::textWidth(g,0,"ب"))<.01,"harakat add no horizontal advance");
  for (size_t k=0;k<size_t(i18n::Key::Count);++k) {
    auto value=std::string(i18n::get(i18n::Lang::AR,i18n::Key(k)));
    const auto& line=gfx::visualLine(g,value);
    for (auto cp:line.glyphs) if (textlayout::arabic(cp))
      check(g.fonts[0].glyphs.count(cp)!=0,"every translated Arabic glyph has a real font glyph");
  }
  std::string longTitle="جربت Xbox 360 في 2026... وانصدمت من كمية المتعة";
  for (float width:{0.f,2.f,10.f,60.f,120.f,250.f}) {
    auto fitted=gfx::ellipsize(g,0,longTitle,width);
    check(gfx::textWidth(g,0,fitted)<=width+.01f,"Arabic truncation fits its measured box");
    for (size_t i=0;i<fitted.size();) check(gfx::utf8(fitted,i)!=0xFFFD,"truncation keeps UTF-8 boundaries");
    const auto& lines=gfx::wrap(g,0,longTitle,width,2);
    check(lines.size()<=2,"wrapping respects line limit");
    for (const auto& line:lines) check(gfx::textWidth(g,0,line)<=width+.01f,"each wrapped Arabic line fits");
  }
  auto longWord=std::string("العربيةالعربيةالعربيةالعربية");
  for (const auto& line:gfx::wrap(g,0,longWord+" Xbox",60,2)) check(gfx::textWidth(g,0,line)<=60.01,"unbroken Arabic words cannot escape a card");
  gfx::begin(g);
  auto s=std::string("بِسْمِ اللّٰهِ — Xbox 360 (2026)");
  check(std::abs(gfx::text(g,0,s,100,100,gfx::rgb(0xffffff))-gfx::textWidth(g,0,s))<.01,"drawing and measuring share shaped glyph advances");
  gfx::end(g);
  std::cout<<checks<<" renderer checks passed\n";
  SDL_Quit();
}
