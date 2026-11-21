#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cutum::test
{
inline std::array<unsigned char,3> StbiFixtureRgb(int x,int y)
{return {static_cast<unsigned char>(3*x+y),static_cast<unsigned char>(5*y+7),static_cast<unsigned char>(x^y)};}

// Authored RGB PNG: stored DEFLATE and split IDAT chunks deliberately exercise
// stb_image's compressed-input growth/reallocation, without another encoder.
inline std::vector<unsigned char> MakeStbiAllocationPng()
{
  using Bytes=std::vector<unsigned char>;
  const auto big=[](Bytes &out,uint32_t value) {for(int i=3;i>=0;--i) out.push_back(static_cast<unsigned char>(value>>(8*i)));};
  const auto chunk=[&](Bytes &out,const std::array<char,4> &type,const Bytes &data) {
    big(out,static_cast<uint32_t>(data.size()));const size_t start=out.size();
    out.insert(out.end(),type.begin(),type.end());out.insert(out.end(),data.begin(),data.end());
    uint32_t crc=0xffffffffu;
    for(size_t i=start;i<out.size();++i) {
      crc^=out[i];for(int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u*(crc&1u));
    }
    big(out,~crc);
  };
  Bytes raw;
  for(int y=0;y<64;++y) {
    raw.push_back(0);
    for(int x=0;x<64;++x) {const auto rgb=StbiFixtureRgb(x,y);raw.insert(raw.end(),rgb.begin(),rgb.end());}
  }
  const auto length=static_cast<uint16_t>(raw.size());
  Bytes zlib{0x78,0x01,0x01,static_cast<unsigned char>(length),static_cast<unsigned char>(length>>8),
      static_cast<unsigned char>(~length),static_cast<unsigned char>((~length)>>8)};
  zlib.insert(zlib.end(),raw.begin(),raw.end());
  uint32_t a=1,b=0;for(const auto c:raw) {a=(a+c)%65521;b=(b+a)%65521;}big(zlib,(b<<16)|a);
  Bytes png{137,80,78,71,13,10,26,10},ihdr;
  big(ihdr,64);big(ihdr,64);ihdr.insert(ihdr.end(),{8,2,0,0,0});chunk(png,{'I','H','D','R'},ihdr);
  for(size_t offset=0;offset<zlib.size();offset+=1024) {
    const size_t end=std::min(zlib.size(),offset+1024);
    chunk(png,{'I','D','A','T'},Bytes(zlib.begin()+offset,zlib.begin()+end));
  }
  chunk(png,{'I','E','N','D'},{});return png;
}
} // namespace cutum::test
