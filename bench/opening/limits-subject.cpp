#include "yt/media.hpp"
#include "local_library.hpp"
extern "C" int64_t range_seek(yt::RangeFile* f,int64_t off,int whence){return yt::RangeFile::seek(f,off,whence);}
extern "C" int range_read(yt::RangeFile* f,uint8_t* out,int n){return f->read(out,n);}
extern "C" void range_fetch(yt::RangeFile* f){f->fetch(f->lanes[0]);}
extern "C" bool local_decode(local::Library* f,const std::string* raw){return f->decode(*raw);}
