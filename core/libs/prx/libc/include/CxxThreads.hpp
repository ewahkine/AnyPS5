#ifndef CORE_LIBS_PRX_LIBC_CXXTHREADS_HPP
#define CORE_LIBS_PRX_LIBC_CXXTHREADS_HPP

#include "SceTypes.hpp"

struct CxxThreadApi {
    int (APS5_VABI *create)(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
    int (APS5_VABI *join)(Pthread thread, void** retval);
    Pthread (APS5_VABI *self)();
};

void CxxThreadApiRegister_nid_no_patch(const CxxThreadApi& api);

#endif
