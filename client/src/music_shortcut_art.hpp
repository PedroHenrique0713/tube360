#pragma once
#include "icons.hpp"

// O atlas de ícones (glyph) é definido em ui_cards.hpp, que inclui este arquivo antes.
static void glyph(gfx::Gfx& g, int which, bool big, float x, float y, Color c);

// Arte vetorial própria: sem rede, textura adicional ou alocação no desenho.
inline bool musicShortcutArt(gfx::Gfx& g, const std::string& id, float x, float y, float w, float h) {
  int kind=id=="local:music_recent" ? 0 : id=="local:music_library" ? 1 : id=="local:music_playlist" ? 2 : id=="local:music_songs" ? 3 : id=="local:music_albums" ? 4 : id=="local:music_artists" ? 5 : -1;
  if (kind<0) return false;
  const Color bases[]={rgb(0x33212f),rgb(0x25253e),rgb(0x3d2027),rgb(0x2b2a3d),rgb(0x33262c),rgb(0x23312f)};
  gfx::hgrad(g,x,y,w,h,bases[kind],ui::SURFACE2,8);
  float s=std::min(w,h),cx=x+w/2,cy=y+h/2;
  Color ink=ui::TEXT,accent=ui::A2;
  if(kind==0) {
    gfx::rect(g,cx-s*.26f,cy-s*.26f,s*.52f,s*.52f,accent,s*.26f);
    gfx::rect(g,cx-s*.22f,cy-s*.22f,s*.44f,s*.44f,bases[kind],s*.22f);
    gfx::rect(g,cx-s*.02f,cy-s*.15f,s*.04f,s*.17f,ink,2);
    gfx::rect(g,cx,cy-s*.02f,s*.14f,s*.04f,ink,2);
  } else if(kind==1) {
    for(int row=0;row<3;++row) {
      float yy=cy-s*.22f+row*s*.19f;
      gfx::rect(g,cx-s*.26f,yy,s*.08f,s*.08f,accent,3);
      gfx::rect(g,cx-s*.10f,yy+s*.02f,s*.34f,s*.04f,ink,2);
    }
  } else if(kind==2) {  // curtidas: joinha (o coração de duas bolas e um triângulo não se lia a distância de TV)
    glyph(g,icons::THUMB_UP_FILL,true,std::round(cx-24),std::round(cy-24),accent);  // 48 px lógicos em qualquer escala
  } else if(kind==3) {  // músicas: nota
    gfx::rect(g,cx-s*.17f,cy+s*.04f,s*.2f,s*.2f,accent,s*.1f);
    gfx::rect(g,cx+s*.01f,cy-s*.26f,s*.05f,s*.4f,ink,2);
    gfx::triangle(g,cx+s*.06f,cy-s*.26f,cx+s*.24f,cy-s*.16f,cx+s*.06f,cy-s*.1f,ink);
  } else if(kind==4) {  // álbuns: disco dentro da capa
    gfx::rect(g,cx-s*.25f,cy-s*.25f,s*.5f,s*.5f,ink,4);
    gfx::rect(g,cx-s*.21f,cy-s*.21f,s*.42f,s*.42f,bases[kind],2);
    gfx::rect(g,cx-s*.15f,cy-s*.15f,s*.3f,s*.3f,accent,s*.15f);
    gfx::rect(g,cx-s*.04f,cy-s*.04f,s*.08f,s*.08f,bases[kind],s*.04f);
  } else {  // artistas: pessoa
    gfx::rect(g,cx-s*.1f,cy-s*.24f,s*.2f,s*.2f,accent,s*.1f);
    gfx::rect(g,cx-s*.2f,cy+s*.02f,s*.4f,s*.22f,ink,s*.1f);
  }
  return true;
}
