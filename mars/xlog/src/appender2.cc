#include "mars/xlog/appender2.h"
#include "mars/xlog/src/xlogger_appender.h"
#include "mars/comm/thread/mutex.h"
#include "mars/comm/thread/lock.h"


char *xlog2_tag1 = "tag1";
char *xlog2_tag2 = "tag2";

namespace mars {
namespace xlog2 {
    
using namespace mars::xlog;
using namespace mars::comm;

////////////////////////////////////////////////////////////////////////////////////
static XloggerAppender* sg_appender1 = nullptr;
static XloggerAppender* sg_appender2 = nullptr;

static bool sg_release_guard = true;
static bool sg_console_log_open1 = false;
static bool sg_console_log_open2 = false;
static mars::comm::Mutex sg_mutex;
static uint64_t sg_max_byte_size1 = 0;
static uint64_t sg_max_byte_size2 = 0;
static long sg_max_alive_time = 0;

void xlogger_appender(const XLoggerInfo* _info, const char* _log) {
    if (sg_release_guard) {
        return;
    }
    if (_info->tag == ::xlog2_tag1)
        sg_appender1->Write(_info, _log);
    else if (_info->tag == ::xlog2_tag2)
        sg_appender2->Write(_info, _log);
}

static void appender_release_default_appender() {
    if (sg_release_guard) {
        return;
    }
    sg_release_guard = true;
    sg_appender1->Close();
    sg_appender2->Close();
    //  本函数只会在 exit 的时候调用，所以干脆不释放对象了，防止多线程导致的 crash
    // XloggerAppender::Release(sg_default_appender);
}

void appender_open1(const XLogConfig& _config) {
    assert(!_config.logdir_.empty());

    if (nullptr != sg_appender1) {
        sg_appender1->WriteTips2File("appender has already been opened. _dir:%s _nameprefix:%s",
            _config.logdir_.c_str(),
            _config.nameprefix_.c_str());
        return;
    }

    sg_appender1 = XloggerAppender::NewInstance(_config, sg_max_byte_size1);
    sg_appender1->SetConsoleLog(sg_console_log_open1);
    if (sg_max_alive_time > 0) {
        sg_appender1->SetMaxAliveDuration(sg_max_alive_time);
    }
    sg_release_guard = false;
    xlogger_SetAppender(&xlogger_appender);
    //BOOT_RUN_EXIT(appender_release_default_appender);
}
void appender_open2(const XLogConfig& _config) {
    assert(!_config.logdir_.empty());

    if (nullptr != sg_appender2) {
        sg_appender2->WriteTips2File("appender has already been opened. _dir:%s _nameprefix:%s",
            _config.logdir_.c_str(),
            _config.nameprefix_.c_str());
        return;
    }

    sg_appender2 = XloggerAppender::NewInstance(_config, sg_max_byte_size1);
    sg_appender2->SetConsoleLog(sg_console_log_open1);
    if (sg_max_alive_time > 0) {
        sg_appender2->SetMaxAliveDuration(sg_max_alive_time);
    }
    sg_release_guard = false;
    xlogger_SetAppender(&xlogger_appender);
    //BOOT_RUN_EXIT(appender_release_default_appender);
}

void appender_flush() {
    if (sg_release_guard) {
        return;
    }
    sg_appender1->Flush();
    sg_appender2->Flush();
}

void appender_flush_sync() {
    if (sg_release_guard) {
        return;
    }
    sg_appender1->FlushSync();
    sg_appender2->FlushSync();
}

void appender_close() {
    ScopedLock lock(sg_mutex);
    if (sg_release_guard) {
        return;
    }
    sg_release_guard = true;
    sg_appender1->Close();
    XloggerAppender::DelayRelease(sg_appender1);
    sg_appender1 = nullptr;

    sg_appender2->Close();
    XloggerAppender::DelayRelease(sg_appender2);
    sg_appender2 = nullptr;
}

void appender_setmode(TAppenderMode _mode) {
    if (sg_release_guard) {
        return;
    }
    sg_appender1->SetMode(_mode);
    sg_appender2->SetMode(_mode);
}


void appender_set_console_log1(bool _is_open) {
    sg_console_log_open1 = _is_open;
    if (sg_release_guard) {
        return;
    }
    sg_appender1->SetConsoleLog(_is_open);
}
void appender_set_console_log2(bool _is_open) {
    sg_console_log_open2 = _is_open;
    if (sg_release_guard) {
        return;
    }
    sg_appender2->SetConsoleLog(_is_open);
}

} // namespace xlog2
} // namespace mars