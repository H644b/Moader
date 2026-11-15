#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
namespace moader_ui {
// Bounded RGBA8 framebuffer export. The fixed app output is 2,764,854 bytes.
inline std::string bitmap(const uint32_t* pixels,unsigned width,unsigned height,unsigned stride_bytes){
    if(!pixels||!width||!height||width>1280||height>720||stride_bytes<width*4||stride_bytes%4)
        throw std::invalid_argument("Invalid screenshot dimensions.");
    const unsigned row_bytes=(width*3+3)&~3u;
    std::string bytes;bytes.reserve(54+row_bytes*height);
    auto word=[&](uint32_t value,unsigned n){for(unsigned i=0;i<n;++i)bytes.push_back((value>>(8*i))&255);};
    bytes+="BM";word(54+row_bytes*height,4);word(0,4);word(54,4);word(40,4);word(width,4);word(height,4);
    word(1,2);word(24,2);word(0,4);word(row_bytes*height,4);word(2835,4);word(2835,4);word(0,4);word(0,4);
    for(unsigned y=height;y>0;--y){const auto* row=pixels+(y-1)*(stride_bytes/4);
        for(unsigned x=0;x<width;++x){bytes.push_back((row[x]>>16)&255);bytes.push_back((row[x]>>8)&255);bytes.push_back(row[x]&255);}
        bytes.append(row_bytes-width*3,'\0');
    }
    return bytes;
}
}
