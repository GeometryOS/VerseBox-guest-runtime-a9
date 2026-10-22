#include "private/bionic_globals.h"

extern "C" libc_shared_globals* __loader_shared_globals();
extern "C" libc_shared_globals* namespace_shared_globals() {
    return __loader_shared_globals();
}
