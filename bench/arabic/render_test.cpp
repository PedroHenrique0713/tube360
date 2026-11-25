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
    const auto line=textlayout::shape(value);
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
  const std::string emoji="👨‍👩‍👧 🪴😀";
  check(gfx::glyph(g.fonts[0],0x1f600)==nullptr,"unsupported glyph has no question-mark replacement");
  check(gfx::textWidth(g,0,emoji)==0 && gfx::ellipsize(g,0,emoji,50).empty() && gfx::wrap(g,0,emoji,50,2).empty(),"emoji-only title is empty in measure, fit and wrap");
  check(gfx::textWidth(g,0,"Question? 😀")==gfx::textWidth(g,0,"Question?"),"literal question mark is retained and only emoji is omitted");
  check(gfx::preparedText(g,0,"ب😀ب").visual.glyphs==textlayout::shape("بب").glyphs,"real Noto coverage preserves Arabic joining around emoji");
  for(const auto& title:{std::string("🪴 العربية Xbox 360 (2026) 😀"),std::string("🪴 A long Latin title with unsupported symbols 漢 😀"),emoji})
    for(int font:{0,1})for(float width:{0.f,2.f,10.f,60.f,120.f,250.f}){
      auto fitted=gfx::ellipsize(g,font,title,width);
      check(gfx::textWidth(g,font,fitted)<=width+.01f,"omitted glyphs do not distort ellipsis width");
      for(size_t i=0;i<fitted.size();)check(gfx::utf8(fitted,i)!=0xfffd,"filtered fit keeps UTF-8 boundaries");
      for(const auto& line:gfx::wrap(g,font,title,width,2))check(gfx::textWidth(g,font,line)<=width+.01f,"filtered wrap keeps every line within its box");
    }
  // Stable titles reuse preparation and fitting; no font scans/shaping on subsequent frames.
  std::string cached="🪴 العربية Xbox 360 😀";gfx::ellipsize(g,0,cached,100);gfx::wrap(g,0,cached,100,2);
  auto builds=g.textCache.builds;
  for(int i=0;i<10000;++i){gfx::textWidth(g,0,cached);gfx::ellipsize(g,0,cached,100);gfx::wrap(g,0,cached,100,2);}
  check(g.textCache.builds==builds,"10000 rendered-title cache hits do not prepare text again");
  gfx::begin(g);
  size_t before=g.verts.size();
  check(gfx::text(g,0,emoji,100.5f,100,gfx::rgb(0xffffff))==0 && gfx::textBox(g,0,emoji,100,100,100,gfx::rgb(0xffffff))==0 && gfx::textGrad(g,0,emoji,100,100,gfx::rgb(0xffffff),gfx::rgb(0xffffff))==0,"all text entrypoints omit an emoji-only title without advance");
  check(g.verts.size()==before,"emoji-only title emits no replacement geometry");
  auto s=std::string("بِسْمِ اللّٰهِ — Xbox 360 (2026)");
  check(std::abs(gfx::text(g,0,s,100,100,gfx::rgb(0xffffff))-gfx::textWidth(g,0,s))<.01,"drawing and measuring share shaped glyph advances");
  gfx::end(g);
  auto oldRevision=g.fontRevision;
  check(gfx::loadFonts(g,std::string(argv[1])+"/",{{"inter-400.ttf",25},{"inter-700.ttf",52}}),"reload fonts for large text");
  check(g.fontRevision!=oldRevision && g.textCache.entries==0 && g.fitCache.bytes==0,"font replacement invalidates prepared glyph coverage and fits");
  check(gfx::textWidth(g,0,emoji)==0 && gfx::textWidth(g,0,"العربية")>0,"omission and Arabic fallback still work after font reload");
  std::cout<<checks<<" renderer checks passed\n";
  SDL_Quit();
}
