#pragma once
#include "Core/Memory/StbiSourceEnvelope.h"
#include "Test/StbiAllocationFixture.h"
#include "ThirdParty/stb_image_write.h"
#include <string>

namespace cutum::test
{
struct StbiFormatFixture
{
  std::string name;
  StbiSourceDomain domain;
  std::vector<unsigned char> bytes,rgb;
  int tolerance{};
};
inline std::vector<StbiFormatFixture> MakeStbiFormatFixtures()
{
  using Bytes=std::vector<unsigned char>;
  const auto be=[](Bytes &b,uint32_t n,int width) {for(int i=width-1;i>=0;--i) b.push_back(static_cast<unsigned char>(n>>(8*i)));};
  const auto le=[](Bytes &b,size_t offset,uint32_t n,int width) {for(int i=0;i<width;++i) b[offset+i]=static_cast<unsigned char>(n>>(8*i));};
  const auto base=[](const char *name,StbiSourceFormat format,int w=2,int h=2) {
    StbiFormatFixture f;f.name=name;f.domain.format=format;f.domain.width=w;f.domain.height=h;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
      const std::array<unsigned char,3> value{static_cast<unsigned char>(17+23*x+7*y),
          static_cast<unsigned char>(41+11*x+13*y),static_cast<unsigned char>(79+5*x+19*y)};
      f.rgb.insert(f.rgb.end(),value.begin(),value.end());
    }
    return f;
  };
  std::vector<StbiFormatFixture> out;
  auto png=base("png",StbiSourceFormat::Png,64,64);png.bytes=MakeStbiAllocationPng();png.rgb.clear();
  for(int y=0;y<64;++y) for(int x=0;x<64;++x) {const auto rgb=StbiFixtureRgb(x,y);png.rgb.insert(png.rgb.end(),rgb.begin(),rgb.end());}
  png.domain.png_idat_capacity_upper=16384;png.domain.png_expanded_capacity_upper=64*64*3+64;out.push_back(std::move(png));
  auto jpeg=base("jpeg",StbiSourceFormat::Jpeg,8,8);jpeg.tolerance=3;
  for(size_t i=0;i<jpeg.rgb.size();i+=3) {jpeg.rgb[i]=90;jpeg.rgb[i+1]=130;jpeg.rgb[i+2]=170;}
  stbi_write_jpg_to_func([](void *context,void *data,int size) {
    auto &b=*static_cast<Bytes *>(context);auto *first=static_cast<unsigned char *>(data);b.insert(b.end(),first,first+size);
  },&jpeg.bytes,8,8,3,jpeg.rgb.data(),100);out.push_back(std::move(jpeg));
  auto bmp=base("bmp",StbiSourceFormat::Bmp);bmp.bytes.resize(54);
  bmp.bytes[0]='B';bmp.bytes[1]='M';le(bmp.bytes,2,70,4);le(bmp.bytes,10,54,4);le(bmp.bytes,14,40,4);
  le(bmp.bytes,18,2,4);le(bmp.bytes,22,2,4);le(bmp.bytes,26,1,2);le(bmp.bytes,28,24,2);le(bmp.bytes,34,16,4);
  for(int y=1;y>=0;--y) {for(int x=0;x<2;++x) {const size_t i=(y*2+x)*3;bmp.bytes.insert(bmp.bytes.end(),{bmp.rgb[i+2],bmp.rgb[i+1],bmp.rgb[i]});}bmp.bytes.insert(bmp.bytes.end(),{0,0});}
  out.push_back(std::move(bmp));
  for(bool palette:{false,true}) for(bool rle:{false,true}) {
    auto tga=base(palette?(rle?"tga_palette_rle":"tga_palette"):(rle?"tga_rle":"tga"),StbiSourceFormat::Tga);
    tga.domain.tga_palette_entries_upper=palette?4:0;tga.bytes.resize(18);
    tga.bytes[1]=palette?1:0;tga.bytes[2]=static_cast<unsigned char>((palette?1:2)+(rle?8:0));
    if(palette) {le(tga.bytes,5,4,2);tga.bytes[7]=24;}
    le(tga.bytes,12,2,2);le(tga.bytes,14,2,2);tga.bytes[16]=palette?8:24;tga.bytes[17]=0x20;
    if(palette) for(size_t i=0;i<tga.rgb.size();i+=3) tga.bytes.insert(tga.bytes.end(),{tga.rgb[i+2],tga.rgb[i+1],tga.rgb[i]});
    if(rle) tga.bytes.push_back(3); // four literal pixels
    if(palette) tga.bytes.insert(tga.bytes.end(),{0,1,2,3});
    else for(size_t i=0;i<tga.rgb.size();i+=3) tga.bytes.insert(tga.bytes.end(),{tga.rgb[i+2],tga.rgb[i+1],tga.rgb[i]});
    out.push_back(std::move(tga));
  }
  for(int depth:{8,16}) for(bool rle:{false,true}) {
    if(depth==16 && rle) continue;
    auto psd=base(depth==16?"psd16":rle?"psd_rle":"psd",StbiSourceFormat::Psd);
    psd.bytes={'8','B','P','S'};be(psd.bytes,1,2);psd.bytes.resize(12);be(psd.bytes,3,2);
    be(psd.bytes,2,4);be(psd.bytes,2,4);be(psd.bytes,depth,2);be(psd.bytes,3,2);
    for(int i=0;i<3;++i) be(psd.bytes,0,4);be(psd.bytes,rle?1:0,2);
    if(rle) for(int i=0;i<6;++i) be(psd.bytes,3,2);
    for(int c=0;c<3;++c) for(int y=0;y<2;++y) {
      if(rle) psd.bytes.push_back(1); // two literals in this channel row
      for(int x=0;x<2;++x) {const auto value=psd.rgb[(y*2+x)*3+c];psd.bytes.push_back(value);if(depth==16) psd.bytes.push_back(value);}
    }
    out.push_back(std::move(psd));
  }
  for(bool rle:{false,true}) {
    auto pic=base(rle?"pic_rle":"pic",StbiSourceFormat::Pic);pic.bytes.resize(92);
    pic.bytes[0]=0x53;pic.bytes[1]=0x80;pic.bytes[2]=0xf6;pic.bytes[3]=0x34;
    pic.bytes[88]='P';pic.bytes[89]='I';pic.bytes[90]='C';pic.bytes[91]='T';
    be(pic.bytes,2,2);be(pic.bytes,2,2);be(pic.bytes,0,4);be(pic.bytes,0,2);be(pic.bytes,0,2);
    pic.bytes.insert(pic.bytes.end(),{0,8,static_cast<unsigned char>(rle?1:0),0xe0});
    for(size_t i=0;i<pic.rgb.size();i+=3) {if(rle) pic.bytes.push_back(1);pic.bytes.insert(pic.bytes.end(),pic.rgb.begin()+i,pic.rgb.begin()+i+3);}
    out.push_back(std::move(pic));
  }
  auto gif=base("gif",StbiSourceFormat::Gif);gif.bytes={'G','I','F','8','9','a',2,0,2,0,0x81,0,0};
  gif.bytes.insert(gif.bytes.end(),gif.rgb.begin(),gif.rgb.end());
  gif.bytes.insert(gif.bytes.end(),{0x2c,0,0,0,0,2,0,2,0,0,2});
  const std::array<unsigned,9> codes{4,0,4,1,4,2,4,3,5};uint32_t bits=0;int count=0;Bytes compressed;
  for(const auto code:codes) {bits|=code<<count;count+=3;while(count>=8) {compressed.push_back(static_cast<unsigned char>(bits));bits>>=8;count-=8;}}
  if(count) compressed.push_back(static_cast<unsigned char>(bits));gif.bytes.push_back(static_cast<unsigned char>(compressed.size()));
  gif.bytes.insert(gif.bytes.end(),compressed.begin(),compressed.end());gif.bytes.insert(gif.bytes.end(),{0,0x3b});out.push_back(std::move(gif));
  for(bool rle:{false,true}) {
    auto hdr=base(rle?"hdr_rle":"hdr",StbiSourceFormat::Hdr,rle?8:2,2);std::fill(hdr.rgb.begin(),hdr.rgb.end(),255);
    const std::string header="#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X "+std::to_string(hdr.domain.width)+"\n";
    hdr.bytes.assign(header.begin(),header.end());
    for(int y=0;y<2;++y) {
      if(rle) {hdr.bytes.insert(hdr.bytes.end(),{2,2,0,8});for(int c=0;c<4;++c) hdr.bytes.insert(hdr.bytes.end(),{136,static_cast<unsigned char>(c==3?129:128)});}
      else for(int x=0;x<2;++x) hdr.bytes.insert(hdr.bytes.end(),{128,128,128,129});
    }
    out.push_back(std::move(hdr));
  }
  for(int mode:{0,1,2}) {
    auto pnm=base(mode==0?"ppm":mode==1?"pgm":"pgm16",StbiSourceFormat::Pnm);
    const std::string header=mode==0?"P6\n2 2\n255\n":mode==1?"P5\n2 2\n255\n":"P5\n2 2\n65535\n";
    pnm.bytes.assign(header.begin(),header.end());
    if(mode==0) pnm.bytes.insert(pnm.bytes.end(),pnm.rgb.begin(),pnm.rgb.end());
    else for(int p=0;p<4;++p) {
      const auto value=static_cast<unsigned char>(50+17*p);for(int c=0;c<3;++c) pnm.rgb[p*3+c]=value;
      pnm.bytes.push_back(value);if(mode==2) pnm.bytes.push_back(value);
    }
    out.push_back(std::move(pnm));
  }
  return out;
}
inline std::vector<unsigned char> MakeStbiOversizedPngGuess()
{
  auto png=MakeStbiAllocationPng();
  // IHDR RGBA16,16384x16384: accepted header domain but raw_len exceeds
  // signed initial_size. Tiny existing IDAT ensures no real giant pixels.
  png[16]=0;png[17]=0;png[18]=0x40;png[19]=0;
  png[20]=0;png[21]=0;png[22]=0x40;png[23]=0;png[24]=16;png[25]=6;
  // STBI ignores PNG CRC; this fixture targets the checked allocation path.
  return png;
}
} // namespace cutum::test
