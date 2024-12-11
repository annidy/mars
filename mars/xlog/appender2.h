#include "appender.h"

extern MARS_EXPORT char* xlog2_tag1;
extern MARS_EXPORT char* xlog2_tag2;

namespace mars {

namespace xlog2 {

MARS_EXPORT void appender_open1(const mars::xlog::XLogConfig& _config);
MARS_EXPORT void appender_open2(const mars::xlog::XLogConfig& _config);

MARS_EXPORT void appender_close();

MARS_EXPORT void appender_set_console_log1(bool _is_open);
MARS_EXPORT void appender_set_console_log2(bool _is_open);

MARS_EXPORT void appender_flush();
MARS_EXPORT void appender_flush_sync();

}

namespace xlog {

#define __xlogger_cpp_impl_tag2(level, tag, ...) xlogger2(level, tag, __XFILE__, __XFUNCTION__, __LINE__, __VA_ARGS__)

#define xinfo2_tag1(...) __xlogger_cpp_impl_tag2(kLevelInfo, xlog2_tag1, __VA_ARGS__)
#define xinfo2_tag2(...) __xlogger_cpp_impl_tag2(kLevelInfo, xlog2_tag2, __VA_ARGS__)

}
}