#include <stdio.h>
#include "mars/comm/xlogger/xloggerbase.h"
#include "mars/comm/xlogger/xlogger.h"
#include "mars/xlog/appender.h"

#include "mars/xlog/appender2.h"


#include <fmt/format.h>
#include <iostream>

using namespace mars::xlog;

namespace xlog {

#define xloggerfmt2(level, tag, file, func, line, ...)                                                                   \
    if ((!xlogger_IsEnabledFor(level)))                                                                               \
        ;                                                                                                             \
    else {                                                                                                            \
        XLogger(level, tag, file, func, line, false, XLOGGER_HOOK)                                                    \
        .WriteNoFormat(fmt::format(__VA_ARGS__).c_str());                                                      \
    }    

#define __xlogger_cpp_implfmt_tag2(level, tag, ...) xloggerfmt2(level, tag, __XFILE__, __XFUNCTION__, __LINE__, __VA_ARGS__)

#define xinfofmt2_tag1(...) __xlogger_cpp_implfmt_tag2(kLevelInfo, xlog2_tag1, __VA_ARGS__)
#define xinfofmt2_tag2(...) __xlogger_cpp_implfmt_tag2(kLevelInfo, xlog2_tag2, __VA_ARGS__)

}

int main()
{

    std::string logPath = "MyLog"; // use your log path
    std::string pubKey = "";       // use you pubkey for log encrypt

// init xlog
#ifdef NDEBUG
    xlogger_SetLevel(kLevelInfo);
    appender_set_console_log(false);
#else
    xlogger_SetLevel(kLevelDebug);
    appender_set_console_log(true);
#endif
    XLogConfig config1;
    config1.logdir_ = logPath;
    config1.nameprefix_ = "cdt";  // 文件名前缀。生成文件格式为 cdt_20241011.xlog
    config1.cache_days_ = 3;   
    //appender_open(config);
    XLogConfig config2;
    config2.logdir_ = logPath;
    config2.nameprefix_ = "gbk";  // 文件名前缀。生成文件格式为 cdt_20241011.xlog
    config2.cache_days_ = 3;
   
    mars::xlog2::appender_open1(config1);
    mars::xlog2::appender_open2(config2);

    for (int i = 0; i < 10; i++) {
        fmt::print("old bbbbb info={}", "elo");
        //xinfo2_tag1("old info aaaa");
        xinfofmt2_tag1("old bbbbb info={}", "elo");
        xinfo2("hello %s", "hkl");

    }
    xinfo2("info=1");
    mars::xlog2::appender_flush_sync();
    mars::xlog2::appender_close();
}