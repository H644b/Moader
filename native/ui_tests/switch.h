#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
using u32=uint32_t;
using Result=uint32_t;
constexpr unsigned Module_Libnx=345,LibnxError_BadInput=2;
#define MAKERESULT(m,d) ((m)|((d)<<9))
#define R_FAILED(r) ((r)!=0)
#define R_SUCCEEDED(r) ((r)==0)
inline Result nifmGetCurrentIpAddress(u32* ip){*ip=0;return 0;}
inline void randomGet(void* output,size_t size){std::memset(output,42,size);}
