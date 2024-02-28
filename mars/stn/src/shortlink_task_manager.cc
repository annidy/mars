// Tencent is pleased to support the open source community by making Mars available.
// Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.

// Licensed under the MIT License (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://opensource.org/licenses/MIT

// Unless required by applicable law or agreed to in writing, software distributed under the License is
// distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
// either express or implied. See the License for the specific language governing permissions and
// limitations under the License.

/*
 * shortlink_task_manager.cc
 *
 *  Created on: 2012-8-24
 *      Author: zhouzhijie
 */

#include "shortlink_task_manager.h"

#include <algorithm>
#include <set>

#include "boost/bind.hpp"
#include "mars/app/app.h"
#include "mars/comm/autobuffer.h"
#include "mars/comm/move_wrapper.h"
#include "mars/comm/platform_comm.h"
#include "mars/comm/thread/lock.h"
#include "mars/comm/time_utils.h"
#include "mars/comm/xlogger/xlogger.h"
#ifdef ANDROID
#include "mars/comm/android/wakeuplock.h"
#endif

#include "config.h"
#include "dynamic_timeout.h"
#include "mars/app/app_manager.h"
#include "mars/stn/stn_manager.h"
#include "net_channel_factory.h"
#include "weak_network_logic.h"

using namespace mars::stn;
using namespace mars::app;
using namespace mars::comm;
using namespace mars::app;

#define AYNC_HANDLER asyncreg_.Get()
#define RETURN_SHORTLINK_SYNC2ASYNC_FUNC_TITLE(func, title) RETURN_SYNC2ASYNC_FUNC_TITLE(func, title, )

// boost::function<size_t (const std::string& _user_id, std::vector<std::string>& _host_list, bool _strict_match)>
// ShortLinkTaskManager::get_real_host_; boost::function<void (const int _error_type, const int _error_code, const int
// _use_ip_index)> ShortLinkTaskManager::task_connection_detail_; boost::function<int (TaskProfile& _profile)>
// ShortLinkTaskManager::choose_protocol_; boost::function<void (const TaskProfile& _profile)>
// ShortLinkTaskManager::on_timeout_or_remote_shutdown_; boost::function<void (uint32_t _version,
// mars::stn::TlsHandshakeFrom _from)> ShortLinkTaskManager::on_handshake_ready_; boost::function<bool (const
// std::vector<std::string> _host_list)> ShortLinkTaskManager::can_use_tls_; boost::function<bool (int _error_code)>
// ShortLinkTaskManager::should_intercept_result_;

#define COROUTINE_STACK_SIZE (2 * 512 * 1024)
#ifdef __ANDROID__
#define MAIN_THREAD_STACK_SIZE (1024 * 1024 * 7)
#define COROUTINE_TOTAL_STACK_SIZE (1024 * 1024 * 6)
#else
#define MAIN_THREAD_STACK_SIZE (0)
#define MAX_CONCURRENT_COUNT (200)
#endif

ShortLinkTaskManager::ShortLinkTaskManager(boot::Context* _context,
                                           std::shared_ptr<NetSource> _netsource,
                                           DynamicTimeout& _dynamictimeout,
                                           MessageQueue::MessageQueue_t _messagequeueid)
: context_(_context)
, asyncreg_(MessageQueue::InstallAsyncHandler(_messagequeueid))
, net_source_(_netsource)
, default_use_proxy_(true)
, tasks_continuous_fail_count_(0)
, dynamic_timeout_(_dynamictimeout)
#ifdef ANDROID
, wakeup_lock_(new WakeUpLock())
#endif
{
    xdebug_function(TSF "mars2");
    xinfo_function(TSF "handler:(%_,%_), ShortLinkTaskManager messagequeue_id=%_",
                   asyncreg_.Get().queue,
                   asyncreg_.Get().seq,
                   MessageQueue::Handler2Queue(asyncreg_.Get()));

    __OnOwlInit();
}

ShortLinkTaskManager::~ShortLinkTaskManager() {
    xinfo_function(TSF "mars2");
    already_release_manager_ = true;
    asyncreg_.CancelAndWait();
    xinfo2(TSF "lst_cmd_ count=%0", lst_cmd_.size());
    __BatchErrorRespHandle(kEctLocal, kEctLocalReset, kTaskFailHandleTaskEnd, Task::kInvalidTaskID, false);
#ifdef ANDROID
    delete wakeup_lock_;
#endif
    __OnOwlUnInit();
}

bool ShortLinkTaskManager::StartTask(const Task& _task, PrepareProfile& _prepare_profile) {
    xverbose_function();

    if (_task.send_only) {
        xassert2(false);
        xerror2(TSF "taskid:%_, short link should have resp", _task.taskid);
        return false;
    }

    xinfo2(TSF "task is long-polling task:%_, cgi:%_, timeout:%_",
           _task.long_polling,
           _task.cgi,
           _task.long_polling_timeout);

    xdebug2(TSF "taskid:%0", _task.taskid);

    TaskProfile task(_task, _prepare_profile);
    task.link_type = Task::kChannelShort;

    // lst_cmd_.push_back(task);
    // lst_cmd_.sort(__CompareTask);
    //__RunLoop();

    OwlTask owl_task;
    owl_task.task = _task;
    owl_task.prepare_profile = _prepare_profile;
    owl_task.action = kAddTask;
    owl_scope_->co_launch([=] {
        owl_channel_->send(owl_task);
    });

    return true;
}

bool ShortLinkTaskManager::StopTask(uint32_t _taskid) {
    xverbose_function();

    std::list<TaskProfile>::iterator first = lst_cmd_.begin();
    std::list<TaskProfile>::iterator last = lst_cmd_.end();

    while (first != last) {
        if (_taskid == first->task.taskid) {
            xinfo2(TSF "find the task, taskid:%0", _taskid);

            // TODO cpan begin
            /*
            __DeleteShortLink(first->running_id);
            lst_cmd_.erase(first);
            */
            __OnDeleteOwlJob(first->owl_req_job);
            first->owl_req_job = 0;
            // Task task = first->task;
            // task.need_erase = true;

            OwlTask task;
            task.task = first->task;
            task.action = kRemoveTask;
            task.need_erase = true;

            owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
            owl_scope_->co_launch([=] {
                owl_channel_->send(task);
                xdebug2(TSF "OWL send task %_", task.task.taskid);
            });

            return true;
        }

        ++first;
    }

    return false;
}

bool ShortLinkTaskManager::HasTask(uint32_t _taskid) const {
    xverbose_function();

    std::list<TaskProfile>::const_iterator first = lst_cmd_.begin();
    std::list<TaskProfile>::const_iterator last = lst_cmd_.end();

    while (first != last) {
        if (_taskid == first->task.taskid) {
            return true;
        }
        ++first;
    }

    return false;
}

void ShortLinkTaskManager::ClearTasks() {
    xverbose_function();

    xinfo2(TSF "cmd size:%0", lst_cmd_.size());

    for (std::list<TaskProfile>::iterator it = lst_cmd_.begin(); it != lst_cmd_.end(); ++it) {
        //__DeleteShortLink(it->running_id);
        __OnDeleteOwlJob(it->owl_req_job);
        it->owl_req_job = 0;
        // Task task = it->task;
        // task.need_erase = true;

        OwlTask task;
        task.task = it->task;
        task.action = kRemoveTask;
        task.need_erase = true;

        owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
        owl_scope_->co_launch([=] {
            owl_channel_->send(task);
            xdebug2(TSF "OWL send task %_", task.task.taskid);
        });
    }

    // delete lst_cmd list in owl
    // lst_cmd_.clear();
}

unsigned int ShortLinkTaskManager::GetTasksContinuousFailCount() {
    return tasks_continuous_fail_count_;
}

void ShortLinkTaskManager::__RunLoopJob() {
    OwlTask owl_task;
    owl_task.action = kRunLoop;
    owl_scope_->co_launch([=] {
        owl_channel_->send(owl_task);
    });
}

void ShortLinkTaskManager::__RunLoop() {
    if (lst_cmd_.empty()) {
#ifdef ANDROID
        /*cancel the last wakeuplock*/
        if (context_->GetManager<AppManager>() != nullptr) {
            wakeup_lock_->Lock(context_->GetManager<AppManager>()->GetConfig<int>(kKeyShortLinkWakeupLockEmptyCMD,
                                                                                  kShortLinkWakeupLockEmptyCMD));
        } else {
            xinfo2(TSF "appmanager no exist.");
            wakeup_lock_->Lock(kShortLinkWakeupLockEmptyCMD);
        }
#endif
        return;
    }
    owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
    // owl_scope_->co_launch([=]{
    __RunOnTimeout();
    __RunOnStartTask();
    //});

    if (!lst_cmd_.empty()) {
#ifdef ANDROID
        if (context_->GetManager<AppManager>() != nullptr) {
            wakeup_lock_->Lock(context_->GetManager<AppManager>()->GetConfig<int>(kKeyShortLinkWakeupLockRunCMD,
                                                                                  kShortLinkWakeupLockRunCMD));
        } else {
            xinfo2(TSF "appmanager no exist.");
            wakeup_lock_->Lock(kShortLinkWakeupLockRunCMD);
        }
#endif
        MessageQueue::FasterMessage(asyncreg_.Get(),
                                    MessageQueue::Message((MessageQueue::MessageTitle_t)this,
                                                          boost::bind(&ShortLinkTaskManager::__RunLoopJob, this),
                                                          "ShortLinkTaskManager::__RunLoop"),
                                    MessageQueue::MessageTiming(DEF_TASK_RUN_LOOP_TIMING));
    } else {
#ifdef ANDROID
        /*cancel the last wakeuplock*/
        if (context_->GetManager<AppManager>() != nullptr) {
            wakeup_lock_->Lock(context_->GetManager<AppManager>()->GetConfig<int>(kKeyShortLinkWakeupLockEmptyCMD,
                                                                                  kShortLinkWakeupLockEmptyCMD));
        } else {
            xinfo2(TSF "appmanager no exist.");
            wakeup_lock_->Lock(kShortLinkWakeupLockEmptyCMD);
        }
#endif
    }
}

void ShortLinkTaskManager::__RunOnTimeout() {
    xverbose2(TSF "lst_cmd_ size=%0", lst_cmd_.size());
    socket_pool_.CleanTimeout();

    std::list<TaskProfile>::iterator first = lst_cmd_.begin();
    std::list<TaskProfile>::iterator last = lst_cmd_.end();

    uint64_t cur_time = ::gettickcount();

    while (first != last) {
        std::list<TaskProfile>::iterator next = first;
        ++next;

        ErrCmdType err_type = kEctLocal;
        int socket_timeout_code = 0;
        // xinfo2(TSF"task is long-polling task:%_,%_, cgi:%_,%_, timeout:%_, id %_",first->task.long_polling,
        // first->transfer_profile.task.long_polling, first->transfer_profile.task.cgi,first->task.cgi,
        // first->transfer_profile.task.long_polling_timeout, (void*)first->running_id);

        if (cur_time - first->start_task_time >= first->task_timeout) {
            err_type = kEctLocal;
            socket_timeout_code = kEctLocalTaskTimeout;
        } else if (first->running_id && 0 < first->transfer_profile.start_send_time
                   && cur_time - first->transfer_profile.start_send_time
                          >= first->transfer_profile.read_write_timeout) {
            xerror2(TSF "task read-write timeout, taskid:%_, wworker:%_, nStartSendTime:%_, nReadWriteTimeOut:%_",
                    first->task.taskid,
                    (void*)first->running_id,
                    first->transfer_profile.start_send_time / 1000,
                    first->transfer_profile.read_write_timeout / 1000);
            err_type = kEctHttp;
            socket_timeout_code = kEctHttpReadWriteTimeout;
        } else if (first->running_id && first->task.long_polling && 0 < first->transfer_profile.start_send_time
                   && 0 == first->transfer_profile.last_receive_pkg_time
                   && cur_time - first->transfer_profile.start_send_time
                          >= (uint64_t)first->task.long_polling_timeout) {
            xerror2(TSF "task long-polling timeout, taskid:%_, wworker:%_, nStartSendTime:%_, nLongPollingTimeout:%_",
                    first->task.taskid,
                    (void*)first->running_id,
                    first->transfer_profile.start_send_time / 1000,
                    first->task.long_polling_timeout / 1000);
            err_type = kEctHttp;
            socket_timeout_code = kEctHttpLongPollingTimeout;
        } else if (first->running_id && !first->task.long_polling && 0 < first->transfer_profile.start_send_time
                   && 0 == first->transfer_profile.last_receive_pkg_time
                   && cur_time - first->transfer_profile.start_send_time >= first->transfer_profile.first_pkg_timeout) {
            xerror2(TSF "task first-pkg timeout taskid:%_, wworker:%_, nStartSendTime:%_, nfirstpkgtimeout:%_",
                    first->task.taskid,
                    (void*)first->running_id,
                    first->transfer_profile.start_send_time / 1000,
                    first->transfer_profile.first_pkg_timeout / 1000);
            err_type = kEctHttp;
            socket_timeout_code = kEctHttpFirstPkgTimeout;
        } else if (first->running_id && 0 < first->transfer_profile.start_send_time
                   && 0 < first->transfer_profile.last_receive_pkg_time
                   && cur_time - first->transfer_profile.last_receive_pkg_time
                          >= ((kMobile != getNetInfo()) ? kWifiPackageInterval : kGPRSPackageInterval)) {
            xerror2(TSF "task pkg-pkg timeout, taskid:%_, wworker:%_, nLastRecvTime:%_, pkg-pkg timeout:%_",
                    first->task.taskid,
                    (void*)first->running_id,
                    first->transfer_profile.last_receive_pkg_time / 1000,
                    ((kMobile != getNetInfo()) ? kWifiPackageInterval : kGPRSPackageInterval) / 1000);
            err_type = kEctHttp;
            socket_timeout_code = kEctHttpPkgPkgTimeout;
        } else {
            // pass
        }

        if (0 != socket_timeout_code) {
            std::string ip = first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile().ip : "";
            std::string host = first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile().host : "";
            int port = first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile().port : 0;
            dynamic_timeout_.CgiTaskStatistic(first->task.cgi, kDynTimeTaskFailedPkgLen, 0);
            __OnRequestTimeout(reinterpret_cast<ShortLinkInterface*>(first->running_id), socket_timeout_code);
            __SetLastFailedStatus(first);
            __SingleRespHandle(
                first,
                err_type,
                socket_timeout_code,
                err_type == kEctLocal ? kTaskFailHandleTaskTimeout : kTaskFailHandleDefault,
                0,
                first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile() : ConnectProfile());
            xassert2(fun_notify_network_err_);
            fun_notify_network_err_(__LINE__, err_type, socket_timeout_code, ip, host, port);
        }

        first = next;
    }
}

void ShortLinkTaskManager::__RunOnStartTask() {
    zdebug_function();
    std::list<TaskProfile>::iterator first = lst_cmd_.begin();
    std::list<TaskProfile>::iterator last = lst_cmd_.end();

    uint64_t curtime = ::gettickcount();
    int sent_count = 0;

    while (first != last) {
        std::list<TaskProfile>::iterator next = first;
        ++next;

        if (first->running_id) {
            ++sent_count;
            first = next;
            continue;
        }

        if (first->owl_req_job) {
            ++sent_count;
            first = next;
            continue;
        }

        // retry time interval is 1 second, but when last connect is quic, retry now
        if (first->retry_time_interval > curtime - first->retry_start_time) {
            if (first->history_transfer_profiles.empty()
                || first->history_transfer_profiles.back().connect_profile.transport_protocol
                       != Task::kTransportProtocolQUIC) {
                xinfo2(TSF "retry interval, taskid:%0, task retry late task, wait:%1, retry:%2",
                       first->task.taskid,
                       (curtime - first->transfer_profile.loop_start_task_time) / 1000,
                       first->history_transfer_profiles.empty() ? "false" : "true");
                first = next;
                continue;
            }
        }

        // proxy
        first->use_proxy =
            (first->remain_retry_count == 0 && first->task.retry_count > 0) ? !default_use_proxy_ : default_use_proxy_;

        //
        if (!first->history_transfer_profiles.empty()) {
            // retry task, force use tcp.
            xassert2(!first->task.shortlink_fallback_hostlist.empty());
            xwarn2(TSF "taskid:%_ retry, fallback to tcp with host.cnt %_",
                   first->task.taskid,
                   first->task.shortlink_fallback_hostlist.size());

            first->task.transport_protocol = Task::kTransportProtocolTCP;
            first->task.shortlink_host_list = first->task.shortlink_fallback_hostlist;
        }

        Task task = first->task;
        std::vector<std::string> hosts = task.shortlink_host_list;
        ShortlinkConfig config(first->use_proxy, /*use_tls=*/true);
#ifndef DISABLE_QUIC_PROTOCOL
        if (!task.quic_host_list.empty() && (task.transport_protocol & Task::kTransportProtocolQUIC)
            && 0 == first->err_code) {
            //.task允许重试且允许走quic，任务也没有出错（首次连接？）,则走quic.
            if (task.retry_count > 0 && net_source_->CanUseQUIC()) {
                config.use_proxy = false;
                config.use_quic = true;
                config.quic.alpn = "h1";
                config.quic.enable_0rtt = true;
                TimeoutSource source;
                config.quic.conn_timeout_ms = net_source_->GetQUICConnectTimeoutMs(task.cgi, &source);
                xinfo2_if(source != TimeoutSource::kClientDefault,
                          TSF "taskid:%_ qctimeout %_ source %_",
                          task.taskid,
                          config.quic.conn_timeout_ms,
                          source);
                hosts = task.quic_host_list;

                first->transfer_profile.connect_profile.quic_conn_timeout_ms = config.quic.conn_timeout_ms;
                first->transfer_profile.connect_profile.quic_conn_timeout_source = source;
            } else {
                xwarn2(TSF "taskid:%_ quic disabled or cant retry %_", first->task.taskid, task.retry_count);
            }
        }
#endif
        size_t realhost_cnt = hosts.size();
        if (get_real_host_) {
            first->transfer_profile.begin_first_get_host_time = gettickcount();
            realhost_cnt = get_real_host_(task.user_id, hosts, /*_strict_match=*/config.use_quic);
            first->transfer_profile.end_first_get_host_time = gettickcount();

        } else {
            xwarn2(TSF "mars2 get_real_host_ is null.");
        }

        if (realhost_cnt == 0 && config.use_quic) {
            xwarn2(TSF "taskid:%_ no quic hosts.", first->task.taskid);
            //.使用quic但拿到的host为空（一般是svr部署问题），则回退到tcp.
            config = ShortlinkConfig(first->use_proxy, /*use_tls=*/true);
            hosts = task.shortlink_host_list;
            first->transfer_profile.begin_retry_get_host_time = gettickcount();
            realhost_cnt = get_real_host_(task.user_id, hosts, /*_strict_match=*/false);
            first->transfer_profile.end_retry_get_host_time = gettickcount();
        }

        first->task.shortlink_host_list = hosts;

        if (hosts.empty()) {
            continue;
        }
        std::string host = hosts.front();
#ifndef DISABLE_QUIC_PROTOCOL
        if (config.use_quic) {
            config.quic.hostname = host;
        }
#endif
        xinfo2_if(!first->task.long_polling,
                  TSF "need auth cgi %_ , host %_ need auth %_",
                  first->task.cgi,
                  host,
                  first->task.need_authed);
        // make sure login
        if (first->task.need_authed) {
            first->transfer_profile.begin_make_sure_auth_time = gettickcount();
            bool ismakesureauthsuccess = context_->GetManager<StnManager>()->MakesureAuthed(host, first->task.user_id);
            first->transfer_profile.end_make_sure_auth_time = gettickcount();
            xinfo2_if(!first->task.long_polling && first->task.priority >= 0,
                      TSF "auth result %_ host %_",
                      ismakesureauthsuccess,
                      host);

            if (!ismakesureauthsuccess) {
                xinfo2_if(curtime % 3 == 1, TSF "makeSureAuth retsult=%0", ismakesureauthsuccess);
                first = next;
                continue;
            }
        }

        bool use_tls = true;
        if (can_use_tls_) {
            use_tls = !can_use_tls_(hosts);
            xdebug2(TSF "cgi can use tls: %_, host: %_", use_tls, hosts[0]);
        }
        config.use_tls = use_tls;

        xinfo2(TSF "OWL cpan cgi %_ priority %_", first->task.cgi, first->task.priority);
        owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
        auto job = owl_scope_->co_launch([=] {
            AutoBuffer bufreq;
            AutoBuffer buffer_extension;
            int error_code = 0;

            // client_sequence_id 在buf2resp这里生成,防止重试sequence_id一样
            first->task.client_sequence_id = context_->GetManager<StnManager>()->GenSequenceId();
            xinfo2(TSF "client_sequence_id:%_", first->task.client_sequence_id);

            first->transfer_profile.begin_req2buf_time = gettickcount();
            if (!context_->GetManager<StnManager>()->Req2Buf(first->task.taskid,
                                                             first->task.user_context,
                                                             first->task.user_id,
                                                             bufreq,
                                                             buffer_extension,
                                                             error_code,
                                                             Task::kChannelShort,
                                                             host,
                                                             first->task.client_sequence_id)) {
                first->transfer_profile.end_req2buf_time = gettickcount();
                __SingleRespHandle(
                    first,
                    kEctEnDecode,
                    error_code,
                    kTaskFailHandleTaskEnd,
                    0,
                    first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile() : ConnectProfile());
                // first = next;
                // continue;
                first->owl_req_job = 0;  // set req job to 0 when finish.
                return;
            }
            first->transfer_profile.end_req2buf_time = gettickcount();

            //雪崩检测
            xassert2(fun_anti_avalanche_check_);

            if (!fun_anti_avalanche_check_(first->task, bufreq.Ptr(), (int)bufreq.Length())) {
                __SingleRespHandle(
                    first,
                    kEctLocal,
                    kEctLocalAntiAvalanche,
                    kTaskFailHandleTaskEnd,
                    0,
                    first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile() : ConnectProfile());
                // first = next;
                // continue;
                first->owl_req_job = 0;  // set req job to 0 when finish.
                return;
            }

            std::string intercept_data;
            if (task_intercept_.GetInterceptTaskInfo(first->task.cgi, intercept_data)) {
                xwarn2(TSF "task has been intercepted");
                AutoBuffer body;
                AutoBuffer extension;
                int err_code = 0;
                unsigned short server_sequence_id = 0;
                body.Write(intercept_data.data(), intercept_data.length());
                first->transfer_profile.received_size = body.Length();
                first->transfer_profile.receive_data_size = body.Length();
                first->transfer_profile.last_receive_pkg_time = ::gettickcount();
                int handle_type = context_->GetManager<StnManager>()->Buf2Resp(first->task.taskid,
                                                                               first->task.user_context,
                                                                               first->task.user_id,
                                                                               body,
                                                                               extension,
                                                                               err_code,
                                                                               Task::kChannelShort,
                                                                               server_sequence_id);
                xinfo2(TSF "server_sequence_id:%_", server_sequence_id);
                first->task.server_sequence_id = server_sequence_id;
                ConnectProfile profile;
                __SingleRespHandle(first,
                                   kEctEnDecode,
                                   err_code,
                                   handle_type,
                                   (unsigned int)first->transfer_profile.receive_data_size,
                                   profile);
                // first = next;
                // continue;
                first->owl_req_job = 0;  // set req job to 0 when finish.
                return;
            }

            first->transfer_profile.loop_start_task_time = ::gettickcount();
            first->transfer_profile.first_pkg_timeout = __FirstPkgTimeout(first->task.server_process_cost,
                                                                          bufreq.Length(),
                                                                          sent_count,
                                                                          dynamic_timeout_.GetStatus());
            first->current_dyntime_status =
                (first->task.server_process_cost <= 0) ? dynamic_timeout_.GetStatus() : kEValuating;
            if (first->transfer_profile.task.long_polling) {
                first->transfer_profile.read_write_timeout =
                    __ReadWriteTimeout(first->transfer_profile.task.long_polling_timeout);
            } else {
                first->transfer_profile.read_write_timeout =
                    __ReadWriteTimeout(first->transfer_profile.first_pkg_timeout);
            }
            first->transfer_profile.send_data_size = bufreq.Length();

            ShortLinkInterface* worker = ShortLinkChannelFactory::Create(context_,
                                                                         MessageQueue::Handler2Queue(asyncreg_.Get()),
                                                                         net_source_,
                                                                         first->task,
                                                                         config);
            worker->func_host_filter = get_real_host_strict_match_;
            worker->func_add_weak_net_info = std::bind(&ShortLinkTaskManager::__OnAddWeakNetInfo,
                                                       this,
                                                       std::placeholders::_1,
                                                       std::placeholders::_2);
            worker->OnSend.set(boost::bind(&ShortLinkTaskManager::__OnSend, this, _1), worker, AYNC_HANDLER);
            worker->OnRecv.set(boost::bind(&ShortLinkTaskManager::__OnRecv, this, _1, _2, _3), worker, AYNC_HANDLER);
            worker->OnResponse.set(boost::bind(&ShortLinkTaskManager::__OnResponse, this, _1, _2, _3, _4, _5, _6, _7),
                                   worker,
                                   AYNC_HANDLER);
            worker->GetCacheSocket = boost::bind(&ShortLinkTaskManager::__OnGetCacheSocket, this, _1);
            worker->OnHandshakeCompleted = boost::bind(&ShortLinkTaskManager::__OnHandshakeCompleted, this, _1, _2);

            if (!debug_host_.empty()) {
                worker->SetDebugHost(debug_host_);
            }

            first->running_id = (intptr_t)worker;

            // TODO cpan
            /*
            xassert2(worker && first->running_id);
            if (!first->running_id) {
                xwarn2(TSF "task add into shortlink readwrite fail cgi:%_, cmdid:%_, taskid:%_",
                       first->task.cgi,
                       first->task.cmdid,
                       first->task.taskid);
                //first = next;
                //continue;
                return;
            }
            */

            worker->func_network_report.set(fun_notify_network_err_);
            if (choose_protocol_) {
                worker->SetUseProtocol(choose_protocol_(*first));
            }
            worker->SendRequest(bufreq, buffer_extension);

            xinfo2_if(first->task.priority >= 0,
                      TSF
                      "task add into shortlink readwrite cgi:%_, cmdid:%_, taskid:%_, work:%_, size:%_, "
                      "timeout(firstpkg:%_, rw:%_, task:%_), retry:%_, long-polling:%_, useProxy:%_, tls:%_",
                      first->task.cgi,
                      first->task.cmdid,
                      first->task.taskid,
                      (ShortLinkInterface*)first->running_id,
                      first->transfer_profile.send_data_size,
                      first->transfer_profile.first_pkg_timeout / 1000,
                      first->transfer_profile.read_write_timeout / 1000,
                      first->task_timeout / 1000,
                      first->remain_retry_count,
                      first->task.long_polling,
                      first->use_proxy,
                      use_tls);
            first->owl_req_job = 0;  // set req job to 0 when finish.
            // TODO cpan
            //++sent_count;
        });
        first->owl_req_job = (intptr_t)job.get();
        xinfo2(TSF "OWL owl job %_", first->owl_req_job);
        first = next;
    }
}

struct find_seq {
 public:
    bool operator()(const TaskProfile& _value) {
        return p_worker == (ShortLinkInterface*)_value.running_id;
    }

 public:
    ShortLinkInterface* p_worker;
};

void ShortLinkTaskManager::__OnResponse(ShortLinkInterface* _worker,
                                        ErrCmdType _err_type,
                                        int _status,
                                        AutoBuffer& __body,
                                        AutoBuffer& __extension,
                                        bool _cancel_retry,
                                        ConnectProfile& _conn_profile) {
    //    auto __body = std::make_shared<AutoBuffer>(body);
    //    auto __extension = std::make_shared<AutoBuffer>(extension);
    move_wrapper<AutoBuffer> _body(__body);
    move_wrapper<AutoBuffer> _extension(__extension);
    owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
    owl_scope_->co_launch([this, _worker, _err_type, _status, _body, _extension, _cancel_retry, _conn_profile] {
        xdebug2(TSF "worker=%0, _err_type=%1, _status=%2, _body.lenght=%3, _cancel_retry=%4",
                _worker,
                _err_type,
                _status,
                _body->Length(),
                _cancel_retry);

        fun_shortlink_response_(_status);

        std::list<TaskProfile>::iterator it =
            __LocateBySeq((intptr_t)_worker);  // must used iter pWorker, not used aSelf. aSelf may be destroy already

        if (lst_cmd_.end() == it) {
            xerror2(TSF "task no found: status:%_, worker:%_", _status, _worker);
            // it->owl_resp_job = 0;//set resp job to 0 when finish.
            return;
        }

        if (_worker->IsKeepAlive() && _conn_profile.socket_fd != INVALID_SOCKET) {
            if (_err_type != kEctOK) {
                _conn_profile.closefunc(_conn_profile.socket_fd);
                if (_status != kEctSocketShutdown) {  // ignore server close error
                    socket_pool_.Report(_conn_profile.is_reused_fd, false, false);
                }
            } else if (_conn_profile.ip_index >= 0 && _conn_profile.ip_index < (int)_conn_profile.ip_items.size()) {
                IPPortItem item = _conn_profile.ip_items[_conn_profile.ip_index];
                CacheSocketItem cache_item(item,
                                           _conn_profile.socket_fd,
                                           _conn_profile.keepalive_timeout,
                                           _conn_profile.closefunc,
                                           _conn_profile.createstream_func,
                                           _conn_profile.issubstream_func);
                if (!socket_pool_.AddCache(cache_item)) {
                    _conn_profile.closefunc(cache_item.socket_fd);
                }
            } else {
                xassert2(false, "not match");
            }
        }

        if (_err_type != kEctOK) {
            if (_err_type == kEctSocket && _status == kEctSocketMakeSocketPrepared) {
                dynamic_timeout_.CgiTaskStatistic(it->task.cgi, kDynTimeTaskFailedPkgLen, 0);
                __SetLastFailedStatus(it);
            }

            if (_err_type != kEctLocal && _err_type != kEctCanceld) {
                if (_conn_profile.transport_protocol == Task::kTransportProtocolQUIC) {
                    // quic失败,临时屏蔽20分钟，直到下一次网络切换或者20分钟后再尝试.
                    xwarn2(TSF "disable quic. err %_:%_", _err_type, _status);
                    net_source_->DisableQUIC();

                    //.increment retry count when first quic failed.
                    if (it->history_transfer_profiles.empty()) {
                        ++it->remain_retry_count;
                    }
                }
            }

            if (_err_type == kEctSocket) {
                it->force_no_retry = _cancel_retry;
            }
            if (_status == kEctHandshakeMisunderstand) {
                it->remain_retry_count++;
            }
            __SingleRespHandle(it, _err_type, _status, kTaskFailHandleDefault, _body->Length(), _conn_profile);
            // it->owl_resp_job = 0;//set resp job to 0 when finish.
            return;
        }

        it->transfer_profile.received_size = _body->Length();
        it->transfer_profile.receive_data_size = _body->Length();
        it->transfer_profile.last_receive_pkg_time = ::gettickcount();

        // TODO cpan begin
        //    move_wrapper<AutoBuffer> body(_body);
        //    move_wrapper<AutoBuffer> extension(_extension);
        //    owl_scope_->co_launch([this, it, body, extension, _conn_profile]{
        int err_code = 0;
        unsigned short server_sequence_id = 0;
        it->transfer_profile.begin_buf2resp_time = gettickcount();

        int handle_type = context_->GetManager<StnManager>()->Buf2Resp(it->task.taskid,
                                                                       it->task.user_context,
                                                                       it->task.user_id,
                                                                       _body,
                                                                       _extension,
                                                                       err_code,
                                                                       Task::kChannelShort,
                                                                       server_sequence_id);
        xinfo2_if(it->task.priority >= 0, TSF "err_code %_ ", err_code);
        xinfo2(TSF "server_sequence_id:%_", server_sequence_id);
        it->transfer_profile.end_buf2resp_time = gettickcount();
        it->task.server_sequence_id = server_sequence_id;
        socket_pool_.Report(_conn_profile.is_reused_fd, true, handle_type == kTaskFailHandleNoError);
        if (should_intercept_result_ && should_intercept_result_(err_code)) {
            task_intercept_.AddInterceptTask(it->task.cgi, std::string((const char*)_body->Ptr(), _body->Length()));
        }

        switch (handle_type) {
            case kTaskFailHandleNoError: {
                dynamic_timeout_.CgiTaskStatistic(
                    it->task.cgi,
                    (unsigned int)it->transfer_profile.send_data_size + (unsigned int)_body->Length(),
                    ::gettickcount() - it->transfer_profile.start_send_time);
                __SingleRespHandle(it,
                                   kEctOK,
                                   err_code,
                                   handle_type,
                                   (unsigned int)it->transfer_profile.receive_data_size,
                                   _conn_profile);
                xassert2(fun_notify_network_err_);
                fun_notify_network_err_(__LINE__,
                                        kEctOK,
                                        err_code,
                                        _conn_profile.ip,
                                        _conn_profile.host,
                                        _conn_profile.port);
            } break;
            case kTaskFailHandleSessionTimeout: {
                xassert2(fun_notify_retry_all_tasks);
                xwarn2(TSF "task decode error session timeout taskid:%_, cmdid:%_, cgi:%_",
                       it->task.taskid,
                       it->task.cmdid,
                       it->task.cgi);
                fun_notify_retry_all_tasks(kEctEnDecode, err_code, handle_type, it->task.taskid, it->task.user_id);
            } break;
            case kTaskFailHandleRetryAllTasks: {
                xassert2(fun_notify_retry_all_tasks);
                xwarn2(TSF "task decode error retry all task taskid:%_, cmdid:%_, cgi:%_",
                       it->task.taskid,
                       it->task.cmdid,
                       it->task.cgi);
                fun_notify_retry_all_tasks(kEctEnDecode, err_code, handle_type, it->task.taskid, it->task.user_id);
            } break;
            case kTaskFailHandleTaskEnd: {
                __SingleRespHandle(it,
                                   kEctEnDecode,
                                   err_code,
                                   handle_type,
                                   (unsigned int)it->transfer_profile.receive_data_size,
                                   _conn_profile);
            } break;
            case kTaskFailHandleDefault: {
                xerror2(TSF "task decode error handle_type:%_, err_code:%_, pWorker:%_, taskid:%_ body dump:%_",
                        handle_type,
                        err_code,
                        (void*)it->running_id,
                        it->task.taskid,
                        xlogger_memory_dump(_body->Ptr(), _body->Length()));
                __SingleRespHandle(it,
                                   kEctEnDecode,
                                   err_code,
                                   handle_type,
                                   (unsigned int)it->transfer_profile.receive_data_size,
                                   _conn_profile);
                xassert2(fun_notify_network_err_);
                fun_notify_network_err_(__LINE__,
                                        kEctEnDecode,
                                        handle_type,
                                        _conn_profile.ip,
                                        _conn_profile.host,
                                        _conn_profile.port);
            } break;
            default: {
                xerror2(TSF "task decode error fail_handle:%_, taskid:%_, context id:%_",
                        handle_type,
                        it->task.taskid,
                        it->task.user_id);
                //#ifdef __APPLE__
                //            //.test only.
                //            const char* pbuffer = (const char*)_body->Ptr();
                //            for (size_t off = 0; off < _body->Length();){
                //                size_t len = std::min((size_t)512, _body->Length() - off);
                //                xerror2(TSF"[%_-%_] %_", off, off + len, xlogger_memory_dump(pbuffer + off, len));
                //                off += len;
                //            }
                //#endif
                __SingleRespHandle(it,
                                   kEctEnDecode,
                                   err_code,
                                   handle_type,
                                   (unsigned int)it->transfer_profile.receive_data_size,
                                   _conn_profile);
                xassert2(fun_notify_network_err_);
                fun_notify_network_err_(__LINE__,
                                        kEctEnDecode,
                                        handle_type,
                                        _conn_profile.ip,
                                        _conn_profile.host,
                                        _conn_profile.port);
                break;
            }
        }
        //    });
        // it->owl_resp_job = 0;//set resp job to 0 when finish.
    });  // co_launch job
}

void ShortLinkTaskManager::__OnSend(ShortLinkInterface* _worker) {
    std::list<TaskProfile>::iterator it = __LocateBySeq((intptr_t)_worker);

    if (lst_cmd_.end() != it) {
        if (it->transfer_profile.first_start_send_time == 0)
            it->transfer_profile.first_start_send_time = ::gettickcount();
        it->transfer_profile.start_send_time = ::gettickcount();
        xdebug2(TSF "taskid:%_, worker:%_, nStartSendTime:%_",
                it->task.taskid,
                _worker,
                it->transfer_profile.start_send_time / 1000);
    }
}

void ShortLinkTaskManager::__OnRecv(ShortLinkInterface* _worker, unsigned int _cached_size, unsigned int _total_size) {
    xverbose_function();
    std::list<TaskProfile>::iterator it = __LocateBySeq((intptr_t)_worker);

    if (lst_cmd_.end() != it) {
        if (it->transfer_profile.last_receive_pkg_time == 0)
            // WeakNetworkLogic::Singleton::Instance()->OnPkgEvent(true, (int)(::gettickcount() -
            // it->transfer_profile.start_send_time));
            net_source_->GetWeakNetworkLogic()->OnPkgEvent(
                true,
                (int)(::gettickcount() - it->transfer_profile.start_send_time));
        else
            // WeakNetworkLogic::Singleton::Instance()->OnPkgEvent(false, (int)(::gettickcount() -
            // it->transfer_profile.last_receive_pkg_time));
            net_source_->GetWeakNetworkLogic()->OnPkgEvent(
                false,
                (int)(::gettickcount() - it->transfer_profile.last_receive_pkg_time));
        it->transfer_profile.last_receive_pkg_time = ::gettickcount();
        it->transfer_profile.received_size = _cached_size;
        it->transfer_profile.receive_data_size = _total_size;
        xdebug2(TSF "worker:%_, last_recvtime:%_, cachedsize:%_, totalsize:%_",
                _worker,
                it->transfer_profile.last_receive_pkg_time / 1000,
                _cached_size,
                _total_size);
    } else {
        xwarn2(TSF "not found worker:%_", _worker);
    }
}

void ShortLinkTaskManager::RedoTasks() {
    xinfo_function();
    xinfo_function("OWL ");
    std::list<TaskProfile>::iterator first = lst_cmd_.begin();
    std::list<TaskProfile>::iterator last = lst_cmd_.end();

    while (first != last) {
        std::list<TaskProfile>::iterator next = first;
        ++next;

        first->last_failed_dyntime_status = 0;

        if (first->running_id) {
            xinfo2(TSF "task redo, taskid:%_", first->task.taskid);
            __OnDeleteOwlJob(first->owl_req_job);
            first->owl_req_job = 0;
            __SingleRespHandle(first,
                               kEctLocal,
                               kEctLocalCancel,
                               kTaskFailHandleDefault,
                               0,
                               ((ShortLinkInterface*)first->running_id)->Profile());
        } else {
            xinfo2(TSF "OWL taskid %_ is no running", first->task.taskid);
        }

        first = next;
    }

    socket_pool_.Clear();

    //这个可以直接跑，没有清空lst_cmd
    //__RunLoop();
    OwlTask owl_task;
    owl_task.action = kRunLoop;
    owl_scope_->co_launch([=] {
        owl_channel_->send(owl_task);
    });
}

void ShortLinkTaskManager::TouchTasks() {
    //__RunLoop();
    OwlTask owl_task;
    owl_task.action = kRunLoop;
    owl_scope_->co_launch([=] {
        owl_channel_->send(owl_task);
    });
}

void ShortLinkTaskManager::RetryTasks(ErrCmdType _err_type, int _err_code, int _fail_handle, uint32_t _src_taskid) {
    xverbose_function();
    xinfo2(TSF "RetryTasks taskid %_ ", _src_taskid);
    __BatchErrorRespHandle(_err_type, _err_code, _fail_handle, _src_taskid);

    // TODO cpan 要等owl做完才可以RunLoop
    //__RunLoop();
    OwlTask owl_task;
    owl_task.action = kRunLoop;
    owl_scope_->co_launch([=] {
        owl_channel_->send(owl_task);
    });
}

void ShortLinkTaskManager::__BatchErrorRespHandle(ErrCmdType _err_type,
                                                  int _err_code,
                                                  int _fail_handle,
                                                  uint32_t _src_taskid,
                                                  bool _callback_runing_task_only) {
    xassert2(kEctOK != _err_type);
    xdebug2(TSF "ect=%0, errcode=%1 taskid:=%2", _err_type, _err_code, _src_taskid);

    std::list<TaskProfile>::iterator first = lst_cmd_.begin();
    std::list<TaskProfile>::iterator last = lst_cmd_.end();

    while (first != last) {
        std::list<TaskProfile>::iterator next = first;
        ++next;

        if (_callback_runing_task_only && !first->running_id) {
            first = next;
            continue;
        }

        if (_fail_handle == kTaskFailHandleSessionTimeout && !first->task.need_authed) {
            first = next;
            continue;
        }

        xinfo2(TSF "axauth sessiontime id %_, cgi %_ taskid %_", _src_taskid, first->task.cgi, first->task.taskid);

        if (_fail_handle == kTaskFailHandleSessionTimeout && _src_taskid != 0 && first->task.taskid == _src_taskid
            && first->allow_sessiontimeout_retry) {  // retry task when sessiontimeout
            xinfo2(TSF "axauth to timeout queue %_, cgi %_ ", first->task.taskid, first->task.cgi);
            first->allow_sessiontimeout_retry = false;
            first->remain_retry_count++;
            //__DeleteShortLink(first->running_id);

            __OnDeleteOwlJob(first->owl_req_job);
            first->owl_req_job = 0;
            // Task task = first->task;
            // task.need_erase = false;
            OwlTask task;
            task.task = first->task;
            task.action = kRemoveTask;
            task.need_erase = false;
            owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
            owl_scope_->co_launch([=] {
                owl_channel_->send(task);
                xinfo2(TSF "OWL send task %_", task.task.taskid);
            });

            first->PushHistory();
            first->InitSendParam();
            first = next;
            continue;
        }

        if (_src_taskid == Task::kInvalidTaskID || _src_taskid == first->task.taskid)
            __SingleRespHandle(
                first,
                _err_type,
                _err_code,
                _fail_handle,
                0,
                first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile() : ConnectProfile());
        else
            __SingleRespHandle(
                first,
                _err_type,
                0,
                _fail_handle,
                0,
                first->running_id ? ((ShortLinkInterface*)first->running_id)->Profile() : ConnectProfile());

        first = next;
    }
}

bool ShortLinkTaskManager::__SingleRespHandle(std::list<TaskProfile>::iterator _it,
                                              ErrCmdType _err_type,
                                              int _err_code,
                                              int _fail_handle,
                                              size_t _resp_length,
                                              const ConnectProfile& _connect_profile) {
    xverbose_function();
    xassert2(kEctServer != _err_type);
    xassert2(_it != lst_cmd_.end());

    if (_it == lst_cmd_.end())
        return false;

    if (kEctOK == _err_type) {
        tasks_continuous_fail_count_ = 0;
        default_use_proxy_ = _it->use_proxy;
    } else {
        ++tasks_continuous_fail_count_;
    }

    uint64_t curtime = gettickcount();
    _it->transfer_profile.connect_profile = _connect_profile;

    xassert2((kEctOK == _err_type) == (kTaskFailHandleNoError == _fail_handle),
             TSF "type:%_, handle:%_",
             _err_type,
             _fail_handle);

    if (_it->force_no_retry || 0 >= _it->remain_retry_count || kEctOK == _err_type
        || kTaskFailHandleTaskEnd == _fail_handle || kTaskFailHandleTaskTimeout == _fail_handle) {
        xlog2(kEctOK == _err_type ? kLevelInfo : kLevelWarn,
              TSF "task end callback short cmdid:%_, err(%_, %_, %_), ",
              _it->task.cmdid,
              _err_type,
              _err_code,
              _fail_handle)(TSF "svr(%_:%_, %_, %_), ",
                            _connect_profile.ip,
                            _connect_profile.port,
                            IPSourceTypeString[_connect_profile.ip_type],
                            _connect_profile.host)(TSF "cli(%_, %_, %_, n:%_, sig:%_), ",
                                                   _it->transfer_profile.external_ip,
                                                   _connect_profile.local_ip,
                                                   _connect_profile.connection_identify,
                                                   _connect_profile.net_type,
                                                   _connect_profile.disconn_signal)(
            TSF "cost(s:%_, r:%_%_%_, c:%_, rw:%_), all:%_, retry:%_, ",
            _it->transfer_profile.send_data_size,
            0 != _resp_length ? _resp_length : _it->transfer_profile.receive_data_size,
            0 != _resp_length ? "" : "/",
            0 != _resp_length ? "" : string_cast(_it->transfer_profile.received_size).str(),
            _connect_profile.conn_rtt,
            (_it->transfer_profile.start_send_time == 0 ? 0 : curtime - _it->transfer_profile.start_send_time),
            (curtime - _it->start_task_time),
            _it->remain_retry_count)(TSF "cgi:%_, taskid:%_, worker:%_, context id:%_",
                                     _it->task.cgi,
                                     _it->task.taskid,
                                     (ShortLinkInterface*)_it->running_id,
                                     _it->task.user_id);

        if (_err_type != kEctOK && _err_type != kEctServer) {
            xinfo_trace(TSF "cgi trace error: (%_, %_), cost:%_, rtt:%_, svr:(%_, %_, %_)",
                        _err_type,
                        _err_code,
                        (curtime - _it->start_task_time),
                        _connect_profile.conn_rtt,
                        _connect_profile.ip,
                        _connect_profile.port,
                        IPSourceTypeString[_connect_profile.ip_type]);
        }

        if (task_connection_detail_) {
            task_connection_detail_(_err_type, _err_code, _connect_profile.ip_index);
        }

        int cgi_retcode = fun_callback_(_err_type,
                                        _err_code,
                                        _fail_handle,
                                        _it->task,
                                        (unsigned int)(curtime - _it->start_task_time));
        int errcode = _err_code;

        if (_it->running_id) {
            if (kEctOK == _err_type) {
                errcode = cgi_retcode;
            }
        }

        _it->end_task_time = ::gettickcount();
        _it->err_type = _err_type;
        _it->transfer_profile.error_type = _err_type;
        _it->err_code = errcode;
        _it->transfer_profile.error_code = _err_code;

        _it->PushHistory();
        if (on_timeout_or_remote_shutdown_) {
            on_timeout_or_remote_shutdown_(*_it);
        }
        _it->is_weak_network = net_source_->GetWeakNetworkLogic()->IsCurrentNetworkWeak();
        int64_t span = 0;
        _it->is_last_valid_connect_fail = net_source_->GetWeakNetworkLogic()->IsLastValidConnectFail(span);
        /* mars2
        ReportTaskProfile(*_it);
        */
        context_->GetManager<StnManager>()->ReportTaskProfile(*_it);
        // WeakNetworkLogic::Singleton::Instance()->OnTaskEvent(*_it);
        net_source_->GetWeakNetworkLogic()->OnTaskEvent(*_it);

        // TODO cpan begin
        /*
        __DeleteShortLink(_it->running_id);
        lst_cmd_.erase(_it);
         */
        // Task task = _it->task;
        // task.need_erase = true;
        OwlTask task;
        task.task = _it->task;
        task.action = kRemoveTask;
        task.need_erase = true;
        owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
        owl_scope_->co_launch([=] {
            owl_channel_->send(task);
            xinfo2(TSF "OWL send task %_", task.task.taskid);
        });

        return true;
    }

    xlog2(kEctOK == _err_type ? kLevelInfo : kLevelWarn,
          TSF "task end retry short cmdid:%_, err(%_, %_, %_), ",
          _it->task.cmdid,
          _err_type,
          _err_code,
          _fail_handle)(TSF "svr(%_:%_, %_, %_), ",
                        _connect_profile.ip,
                        _connect_profile.port,
                        IPSourceTypeString[_connect_profile.ip_type],
                        _connect_profile.host)(TSF "cli(%_, %_, %_, n:%_, sig:%_), ",
                                               _it->transfer_profile.external_ip,
                                               _connect_profile.local_ip,
                                               _connect_profile.connection_identify,
                                               _connect_profile.net_type,
                                               _connect_profile.disconn_signal)(
        TSF "cost(s:%_, r:%_%_%_, c:%_, rw:%_), all:%_, retry:%_, ",
        _it->transfer_profile.send_data_size,
        0 != _resp_length ? _resp_length : _it->transfer_profile.received_size,
        0 != _resp_length ? "" : "/",
        0 != _resp_length ? "" : string_cast(_it->transfer_profile.receive_data_size).str(),
        _connect_profile.conn_rtt,
        (_it->transfer_profile.start_send_time == 0 ? 0 : curtime - _it->transfer_profile.start_send_time),
        (curtime - _it->start_task_time),
        _it->remain_retry_count)(TSF "cgi:%_, taskid:%_, worker:%_",
                                 _it->task.cgi,
                                 _it->task.taskid,
                                 (void*)_it->running_id);

    _it->remain_retry_count--;
    _it->transfer_profile.error_type = _err_type;
    _it->transfer_profile.error_code = _err_code;
    _it->err_type = _err_type;
    _it->err_code = _err_code;
    _it->PushHistory();

    //__DeleteShortLink(_it->running_id);

    // Task task = _it->task;
    // task.need_erase = false;
    OwlTask task;
    task.task = _it->task;
    task.action = kRemoveTask;
    task.need_erase = false;
    owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
    owl_scope_->co_launch([=] {
        owl_channel_->send(task);
        xinfo2(TSF "OWL send task %_", task.task.taskid);
    });
    if (on_timeout_or_remote_shutdown_) {
        on_timeout_or_remote_shutdown_(*_it);
    }
    _it->InitSendParam();

    _it->retry_start_time = ::gettickcount();
    // session timeout 应该立刻重试
    if (kTaskFailHandleSessionTimeout == _fail_handle) {
        _it->retry_start_time = 0;
    }
    // .quic失败立刻换tcp重试.
    if (_connect_profile.transport_protocol == Task::kTransportProtocolQUIC) {
        xwarn2(TSF "taskid:%_ quic failed, retry with tcp immediately.", _it->task.taskid);
        _it->retry_start_time = 0;
    }

    _it->retry_time_interval = DEF_TASK_RETRY_INTERNAL;

    return false;
}

std::list<TaskProfile>::iterator ShortLinkTaskManager::__LocateBySeq(intptr_t _running_id) {
    if (!_running_id)
        return lst_cmd_.end();

    find_seq find_functor;
    find_functor.p_worker = (ShortLinkInterface*)_running_id;
    std::list<TaskProfile>::iterator it = std::find_if(lst_cmd_.begin(), lst_cmd_.end(), find_functor);

    return it;
}

void ShortLinkTaskManager::__DeleteShortLink(intptr_t& _running_id) {
    if (!_running_id)
        return;
    ShortLinkInterface* p_shortlink = (ShortLinkInterface*)_running_id;
    // p_shortlink->func_add_weak_net_info = NULL;
    // p_shortlink->func_weak_net_report = NULL;
    ShortLinkChannelFactory::Destory(p_shortlink);
    MessageQueue::CancelMessage(asyncreg_.Get(), p_shortlink);
    p_shortlink = NULL;
}

ConnectProfile ShortLinkTaskManager::GetConnectProfile(uint32_t _taskid) const {
    std::list<TaskProfile>::const_iterator first = lst_cmd_.begin();
    std::list<TaskProfile>::const_iterator last = lst_cmd_.end();

    while (first != last) {
        if ((first->running_id) && _taskid == first->task.taskid) {
            return ((ShortLinkInterface*)(first->running_id))->Profile();
        }
        ++first;
    }
    return ConnectProfile();
}

SOCKET ShortLinkTaskManager::__OnGetCacheSocket(const IPPortItem& _address) {
    return socket_pool_.GetSocket(_address);
}

void ShortLinkTaskManager::__OnHandshakeCompleted(uint32_t _version, mars::stn::TlsHandshakeFrom _from) {
    xinfo2(TSF "receive tls version: %_", _version);
    if (already_release_manager_) {
        xinfo2(TSF "mars2 shortlink_task_manager had released. ignore handshake event.");
        return;
    }
    if (on_handshake_ready_) {
        on_handshake_ready_(_version, _from);
    }
}

void ShortLinkTaskManager::__OnRequestTimeout(ShortLinkInterface* _worker, int _errorcode) {
    if (kEctLocalTaskTimeout != _errorcode && _worker) {
        _worker->OnNetTimeout();
    }
}

void ShortLinkTaskManager::__OnAddWeakNetInfo(bool _connect_timeout, struct tcp_info& _info) {
    if (already_release_manager_) {
        xinfo2(TSF "mars2 longlink_task_manager had released. ignore add weaknet info.");
        return;
    }
    if (add_weaknet_info_) {
        add_weaknet_info_(_connect_timeout, _info);
    }
}

void ShortLinkTaskManager::__OnOwlInit() {
    xinfo_function("OWL ");
    owl_looper_ = std::shared_ptr<owl::looper>(
        owl::create_looper("shortlink_task_manager_looper",
                           MAIN_THREAD_STACK_SIZE,
                           [] {
                               xinfo2(TSF "OWL aff conetxt looper start hhhhhh!");
                               auto co_tid = std::this_thread::get_id();
// calling java from coroutine function will crash,
// we can't use heap memory as coroutine's stack
#ifdef __ANDROID__
                               char stack[COROUTINE_TOTAL_STACK_SIZE];
                               owl::co_set_stack(stack, COROUTINE_TOTAL_STACK_SIZE, COROUTINE_STACK_SIZE);
                               xinfo2(TSF "OWL before enable jni");
                               owl::co_enable_jni();
                               xinfo2(TSF "OWL after enable jni");
#else
                                   owl::co_set_stack(nullptr, 0, COROUTINE_STACK_SIZE);
                                   owl::co_set_max_concurrent_count(MAX_CONCURRENT_COUNT);
#endif
                               owl::this_looper()->run_until_quit();
                               xinfo2(TSF "caff conetxt looper end hhh!");
                           }),
        [](owl::looper* ptr) {
            xinfo2(TSF "context looper exiting");
            ptr->quit();
            ptr->join();
            delete ptr;
        });

    owl_scope_ = std::make_shared<owl::co_scope>("shortlink_task_manager_scope");
    owl_scope_.get()->options().exec = owl_looper_.get();
    owl_channel_ = std::make_shared<owl::co_channel<OwlTask>>();
    __OnOwlReceive();
}

void ShortLinkTaskManager::__OnOwlUnInit() {
    xinfo_function("OWL ");

    if (owl_channel_) {
        xinfo2(TSF "OWL __OnChanReceive close");
        co_with_context(owl_looper_.get()) {
            owl_channel_->close();
        };
    }

    if (owl_scope_) {
        xinfo2(TSF "OWL Scope cancle");
        owl_scope_->cancel();
        owl_scope_->join();
        owl_scope_ = nullptr;
    }

    if (owl_looper_) {
        xinfo2(TSF "OWL Looper quit");
        owl_looper_->quit();
        owl_looper_->join();
        owl_looper_ = nullptr;
    }

    owl_channel_ = nullptr;
}

void ShortLinkTaskManager::__OnOwlReceive() {
    xinfo_function("OWL ");
    owl::co_options options = owl::co_options::with_priority(owl::kCoPriorityDefault);
    owl_scope_->co_launch([=] {
        while (owl_channel_ != nullptr && !owl_channel_->is_closed()) {
            auto x = owl_channel_->receive();

            switch (x.action) {
                case kAddTask: {
                    TaskProfile task(x.task, x.prepare_profile);
                    task.link_type = Task::kChannelShort;
                    lst_cmd_.push_back(task);
                    lst_cmd_.sort(__CompareTask);
                    __RunLoop();
                } break;
                case kRemoveTask: {
                    xinfo2(TSF "OWL receive taskid %_", x.task.taskid);
                    auto first = lst_cmd_.begin();
                    auto last = lst_cmd_.end();
                    while (lst_cmd_.size() > 0 && first != last) {
                        if (x.task.taskid == first->task.taskid) {
                            xinfo2(TSF "OWL __OnOwlReceive lst_cmd before %_ taskid %_ job count %_",
                                   lst_cmd_.size(),
                                   x.task.taskid,
                                   owl_scope_->job_count());
                            co_non_cancellable() {
                                __DeleteShortLink(first->running_id);
                                if (x.need_erase) {
                                    lst_cmd_.erase(first);
                                }
                            };
                            xinfo2(TSF "OWL __OnOwlReceive lst_cmd after%_ job count %_",
                                   lst_cmd_.size(),
                                   owl_scope_->job_count());
                            break;
                        }
                        ++first;
                    }
                } break;
                case kRunLoop: {
                    __RunLoop();
                } break;
                default:
                    break;
            }
        }
        xinfo2(TSF "OWL __OnChanReceive channel is nullptr or closed.");
    });
}

void ShortLinkTaskManager::__OnDeleteOwlJob(intptr_t& _owl_job) {
    if (!_owl_job)
        return;
    xinfo2(TSF "OWL __OnDeleteOwlJob owl job %_", _owl_job);
    owl::co_job_base* job = (owl::co_job_base*)_owl_job;
    if (job != nullptr && !job->is_completed() && !job->is_cancelled()) {
        job->cancel();
        job->join();
    }
}
