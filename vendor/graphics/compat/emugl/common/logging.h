#pragma once

namespace emugl {
using Logger = void (*)(const char*, ...);
extern Logger emugl_logger;
extern Logger emugl_cxt_logger;
}
#define GL_LOG(...) emugl::emugl_logger(__VA_ARGS__)
#define GL_CXT_LOG(...) emugl::emugl_cxt_logger(__VA_ARGS__)
