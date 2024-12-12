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
}