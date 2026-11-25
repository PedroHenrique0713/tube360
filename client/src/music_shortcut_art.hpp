#pragma once

// Arte vetorial própria: sem rede, textura adicional ou alocação no desenho.
inline bool musicShortcutArt(gfx::Gfx& g, const std::string& id, float x, float y, float w, float h) {
  int kind=id=="local:music_recent" ? 0 : id=="local:music_library" ? 1 : id=="local:music_playlist" ? 2 : -1;
  if (kind<0) return false;
  const Color bases[]={rgb(0x33212f),rgb(0x25253e),rgb(0x3d2027)};
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
  } else {
    gfx::rect(g,cx-s*.26f,cy-s*.20f,s*.29f,s*.29f,accent,s*.145f);
    gfx::rect(g,cx-s*.03f,cy-s*.20f,s*.29f,s*.29f,accent,s*.145f);
    // O círculo termina tangente às diagonais, sem formar uma bolinha na ponta.
    gfx::triangle(g,cx-s*.25f,cy-s*.01f,cx+s*.25f,cy-s*.01f,cx+s*.0243f,cy+s*.2068f,accent);
    gfx::triangle(g,cx-s*.25f,cy-s*.01f,cx+s*.0243f,cy+s*.2068f,cx-s*.0243f,cy+s*.2068f,accent);
    gfx::rect(g,cx-s*.035f,cy+s*.1465f,s*.07f,s*.07f,accent,s*.035f);
  }
  return true;
}
