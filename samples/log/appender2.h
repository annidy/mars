#include "mars/comm/xlogger/xloggerbase.h"
#include "mars/comm/xlogger/xlogger.h"
#include "mars/xlog/appender.h"

extern const char* sg_tag1;
extern const char* sg_tag2;

namespace sample {

	void appender_open1(const mars::xlog::XLogConfig& _config);
	void appender_open2(const mars::xlog::XLogConfig& _config);

	void appender_close();

	void appender_set_console_log1(bool _is_open);
	void appender_set_console_log2(bool _is_open);

	void appender_flush();
	void appender_flush_sync();

}

namespace mars {
namespace xlog {

#define __xlogger_cpp_impl_tag2(level, tag, ...) xlogger2(level, tag, __XFILE__, __XFUNCTION__, __LINE__, __VA_ARGS__)

#define xinfo2_tag1(...) __xlogger_cpp_impl_tag2(kLevelInfo, sg_tag1, __VA_ARGS__)
#define xinfo2_tag2(...) __xlogger_cpp_impl_tag2(kLevelInfo, sg_tag2, __VA_ARGS__)

}
}