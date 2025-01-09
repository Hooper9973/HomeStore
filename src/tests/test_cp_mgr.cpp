/*********************************************************************************
 * Modifications Copyright 2017-2019 eBay Inc.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 *
 *********************************************************************************/

#include <iomgr/io_environment.hpp>
#include <iomgr/iomgr.hpp>
#include <sisl/logging/logging.h>
#include <sisl/options/options.h>
#include <gtest/gtest.h>

#include <homestore/homestore.hpp>
#include <homestore/meta_service.hpp>
#include <homestore/checkpoint/cp_mgr.hpp>
#include <homestore/checkpoint/cp.hpp>
#include "test_common/homestore_test_common.hpp"

using namespace homestore;

SISL_LOGGING_INIT(HOMESTORE_LOG_MODS)

SISL_OPTIONS_ENABLE(logging, test_cp_mgr, iomgr, test_common_setup)
SISL_LOGGING_DECL(test_cp_mgr)

SISL_OPTION_GROUP(test_cp_mgr,
                  (num_records, "", "num_records", "number of record to test",
                   ::cxxopts::value< uint32_t >()->default_value("1000"), "number"),
                  (iterations, "", "iterations", "Iterations", ::cxxopts::value< uint32_t >()->default_value("1"),
                   "the number of iterations to run each test"));

class TestCPContext : public CPContext {
public:
    TestCPContext(CP* cp) : CPContext{cp} {}
    virtual ~TestCPContext() = default;

    void add() {
        auto val = m_next_val.fetch_add(1);
        if (val < max_values) { m_cur_values[val] = std::make_pair(id(), val); }
    }

    void validate(uint64_t cp_id) {
        for (uint64_t i{0}; i < m_next_val.load(); ++i) {
            auto [session, val] = m_cur_values[i];
            ASSERT_EQ(session, cp_id) << "CP Context has data with mismatched cp_id";
            ASSERT_EQ(val, i);
        }
        LOGINFO("CP={}, CPContext has {} values to be flushed/validated", cp_id, m_next_val.load());
    }

private:
    static constexpr size_t max_values = 10000;

    std::array< std::pair< uint64_t, uint64_t >, max_values > m_cur_values;
    std::atomic< uint64_t > m_next_val{0};
    folly::Promise< bool > m_comp_promise;
};

class TestCPCallbacks : public CPCallbacks {
public:
    std::unique_ptr< CPContext > on_switchover_cp(CP*, CP* new_cp) override {
        return std::make_unique< TestCPContext >(new_cp);
    }

    folly::Future< bool > cp_flush(CP* cp) override {
        auto ctx = s_cast< TestCPContext* >(cp->context(cp_consumer_t::HS_CLIENT));
        ctx->validate(cp->id());
        return folly::makeFuture< bool >(true);
    }

    void cp_cleanup(CP* cp) override {}

    int cp_progress_percent() override { return 100; }
};

class TestCPMgr : public ::testing::Test {
public:
    void SetUp() override {
        m_helper.start_homestore("test_cp", {{HS_SERVICE::META, {.size_pct = 85.0}}});
        hs()->cp_mgr().register_consumer(cp_consumer_t::HS_CLIENT, std::move(std::make_unique< TestCPCallbacks >()));
    }

    void TearDown() override { m_helper.shutdown_homestore(); }

    void simulate_io() {
        iomanager.run_on_forget(iomgr::reactor_regex::least_busy_worker, [this]() {
            auto cur_cp = homestore::hs()->cp_mgr().cp_guard();
            r_cast< TestCPContext* >(cur_cp->context(cp_consumer_t::HS_CLIENT))->add();
        });
    }

    void rescheduled_io() {
        iomanager.run_on_forget(iomgr::reactor_regex::least_busy_worker, [this]() {
            auto cur_cp = homestore::hs()->cp_mgr().cp_guard();
            iomanager.run_on_forget(iomgr::reactor_regex::least_busy_worker, [moved_cp = std::move(cur_cp)]() mutable {
                r_cast< TestCPContext* >(moved_cp->context(cp_consumer_t::HS_CLIENT))->add();
            });
        });
    }

    void nested_io() {
        [[maybe_unused]] auto cur_cp = homestore::hs()->cp_mgr().cp_guard();
        rescheduled_io();
    }

    void trigger_cp(bool wait) {
        static std::mutex mtx;
        static std::condition_variable cv;
        static uint64_t this_flush_cp{0};
        static uint64_t last_flushed_cp{0};

        {
            std::unique_lock lg(mtx);
            ++this_flush_cp;
        }

        auto fut = homestore::hs()->cp_mgr().trigger_cp_flush(true /* force */);

        auto on_complete = [&](auto success) {
            ASSERT_EQ(success, true) << "CP Flush failed";
            {
                std::unique_lock lg(mtx);
                ASSERT_LT(last_flushed_cp, this_flush_cp) << "CP out_of_order completion";
                ++last_flushed_cp;
            }
        };

        if (wait) {
            on_complete(std::move(fut).get());
        } else {
            std::move(fut).thenValue(on_complete);
        }
    }

    void print_hi_with_mtx(int i) {
        LOGINFO("access print_hi: {}", i);

        std::lock_guard lg{m_mtx};
        std::this_thread::sleep_for(std::chrono::milliseconds{500});

        if (i == 2) { boost::this_fiber::yield(); }
        LOGINFO("Hi: {}", i);
    }

    void print_hi_with_fiber_mtx(int i) {
        LOGINFO("access print_hi: {}", i);

        std::lock_guard lg{m_fiber_mtx};
        std::this_thread::sleep_for(std::chrono::milliseconds{500});

        if (i == 2) { boost::this_fiber::yield(); }
        LOGINFO("Hi: {}", i);
    }



	void test_promise(int i) {
        LOGINFO("access print_hi: {}", i);
        std::lock_guard lg{m_fiber_mtx};
        static bool is_retrived = false;
        if (!is_retrived) {
            auto v = m_promise.getFuture().get();
            is_retrived = true;
            LOGINFO("Hi: {}, promise: {}", i, v);
        } else {
            LOGINFO("Hi: {}", i);
        }
    }

    void test_promise_with_mutex(int i) {
        LOGINFO("access print_hi: {}", i);
        std::lock_guard lg{m_mtx};
        static bool is_retrived = false;
        if (!is_retrived) {
            auto v = m_promise.getFuture().get();
            is_retrived = true;
            LOGINFO("Hi: {}, promise: {}", i, v);
        } else {
            LOGINFO("Hi: {}", i);
        }
    }

private:
    test_common::HSTestHelper m_helper;
    std::mutex m_mtx;
    iomgr::FiberManagerLib::mutex m_fiber_mtx;

public:
    iomgr::FiberManagerLib::Promise< bool > m_promise;
};

TEST_F(TestCPMgr, cp_start_and_flush) {
    auto nrecords = SISL_OPTIONS["num_records"].as< uint32_t >();
    LOGINFO("Step 1: Simulate IO on cp session for {} records", nrecords);
    for (uint32_t i{0}; i < nrecords; ++i) {
        this->simulate_io();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1000});

    LOGINFO("Step 2: Trigger a new cp without waiting for it to complete");
    this->trigger_cp(false /* wait */);

    LOGINFO("Step 3: Simulate IO parallel to CP for {} records", nrecords);
    for (uint32_t i{0}; i < nrecords; ++i) {
        this->simulate_io();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1000});

    LOGINFO("Step 4: Trigger a back-to-back cp");
    this->trigger_cp(false /* wait */);
    this->trigger_cp(true /* wait */);

    LOGINFO("Step 5: Simulate rescheduled IO for {} records", nrecords);
    for (uint32_t i{0}; i < nrecords; ++i) {
        this->nested_io();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1000});

    LOGINFO("Step 6: Trigger a cp to validate");
    this->trigger_cp(true /* wait */);
}

// mock and find deadlock, thread 25 try to lock mtx, and mtx's owner is itself

// (gdb) f 6
// #6  0x00005610bf3e22ae in std::lock_guard<std::mutex>::lock_guard (this=0x7f325c0b4f28, __m=...)
//    at /usr/include/c++/11/bits/std_mutex.h:229
// 229	      { _M_device.lock(); }
//(gdb) info args
// this = 0x7f325c0b4f28
//__m = @0x5610c250e3a0: {<std::__mutex_base> = {_M_mutex = {__data = {__lock = 2, __count = 0,
//        __owner = 1815663, __nusers = 1, __kind = 0, __spins = 0, __elision = 0, __list = {
//          __prev = 0x0, __next = 0x0}},
//      __size = "\002\000\000\000\000\000\000\000o\264\033\000\001", '\000' <repeats 26 times>,
//      __align = 2}}, <No data fields>}
//(gdb) info threads
//  Id   Target Id                                             Frame
//* 25   Thread 0x7f32775fe600 (LWP 1815663) "test_fiber"      futex_wait (private=0, expected=2,
//    futex_word=0x5610c250e3a0) at ../sysdeps/nptl/futex-internal.h:146

TEST_F(TestCPMgr, test_fiber_deadlock) {

    std::vector< iomgr::io_fiber_t > fibers;
    // Start WBCache flush threads
    struct Context {
        std::condition_variable cv;
        std::mutex mtx;
        int32_t thread_cnt{0};
    };
    auto ctx = std::make_shared< Context >();

    // create fibers
    iomanager.create_reactor("test_fiber", iomgr::INTERRUPT_LOOP, 8u, [&fibers, ctx](bool is_started) {
        if (is_started) {
            {
                std::unique_lock< std::mutex > lk{ctx->mtx};
                std::vector< iomgr::io_fiber_t > v = iomanager.sync_io_capable_fibers();
                fibers.insert(fibers.end(), v.begin(), v.end());
                LOGINFO("print fibers size: {}", fibers.size());
                ++(ctx->thread_cnt);
            }
            ctx->cv.notify_one();
        }
    });

    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 1); });
    }

    // use fibers print hi
    for (size_t i = 0; i < fibers.size(); ++i) {
        iomanager.run_on_forget(fibers[i], [this, i]() { this->print_hi_with_mtx(i); });
    }

    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 2); });
    }
}

// fiber 2 get fiber_mtx, and call yield(), other yields are scheduled
// but they cannot get fiber mtx, and then they are suspended.
// until fiber2 is scheduled, and finish its opts, released fiber_mtx.
// other fibers can get fiber_mtx, and finish their opts.

//[01/08/25 20:54:01-07:00] [info] [1816378] [test_cp_mgr.cpp:290:operator()] print fibers size: 7
//[01/08/25 20:54:01-07:00] [info] [1816378] [test_cp_mgr.cpp:178:print_hi_with_fiber_mtx] access print_hi: 0
//[01/08/25 20:54:02-07:00] [info] [1816378] [test_cp_mgr.cpp:186:print_hi_with_fiber_mtx] Hi: 0
//[01/08/25 20:54:02-07:00] [info] [1816378] [test_cp_mgr.cpp:178:print_hi_with_fiber_mtx] access print_hi: 1
//[01/08/25 20:54:02-07:00] [info] [1816378] [test_cp_mgr.cpp:186:print_hi_with_fiber_mtx] Hi: 1
//[01/08/25 20:54:02-07:00] [info] [1816378] [test_cp_mgr.cpp:178:print_hi_with_fiber_mtx] access print_hi: 2
//[01/08/25 20:54:03-07:00] [info] [1816378] [test_cp_mgr.cpp:178:print_hi_with_fiber_mtx] access print_hi: 3
//[01/08/25 20:54:03-07:00] [info] [1816378] [test_cp_mgr.cpp:178:print_hi_with_fiber_mtx] access print_hi: 4
//[01/08/25 20:54:03-07:00] [info] [1816378] [test_cp_mgr.cpp:178:print_hi_with_fiber_mtx] access print_hi: 5
//[01/08/25 20:54:03-07:00] [info] [1816378] [test_cp_mgr.cpp:178:print_hi_with_fiber_mtx] access print_hi: 6
//[01/08/25 20:54:03-07:00] [info] [1816378] [test_cp_mgr.cpp:186:print_hi_with_fiber_mtx] Hi: 2
//[01/08/25 20:54:03-07:00] [info] [1816378] [test_cp_mgr.cpp:186:print_hi_with_fiber_mtx] Hi: 3
//[01/08/25 20:54:04-07:00] [info] [1816378] [test_cp_mgr.cpp:186:print_hi_with_fiber_mtx] Hi: 4
//[01/08/25 20:54:04-07:00] [info] [1816378] [test_cp_mgr.cpp:186:print_hi_with_fiber_mtx] Hi: 5
//[01/08/25 20:54:05-07:00] [info] [1816378] [test_cp_mgr.cpp:186:print_hi_with_fiber_mtx] Hi: 6

TEST_F(TestCPMgr, test_fiber_mtx) {

    std::vector< iomgr::io_fiber_t > fibers;
    // Start WBCache flush threads
    struct Context {
        std::condition_variable cv;
        std::mutex mtx;
        int32_t thread_cnt{0};
    };
    auto ctx = std::make_shared< Context >();

    // create fibers
    iomanager.create_reactor("test_fiber", iomgr::INTERRUPT_LOOP, 8u, [&fibers, ctx](bool is_started) {
        if (is_started) {
            {
                std::unique_lock< std::mutex > lk{ctx->mtx};
                std::vector< iomgr::io_fiber_t > v = iomanager.sync_io_capable_fibers();
                fibers.insert(fibers.end(), v.begin(), v.end());
                LOGINFO("print fibers size: {}", fibers.size());
                ++(ctx->thread_cnt);
            }
            ctx->cv.notify_one();
        }
    });

    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 1); });
    }

    // use fibers print hi
    for (size_t i = 0; i < fibers.size(); ++i) {
        iomanager.run_on_forget(fibers[i], [this, i]() { this->print_hi_with_fiber_mtx(i); });
    }

    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 2); });
    }
}

//[01/09/25 01:38:49-07:00] [info] [1818349] [test_cp_mgr.cpp:392:operator()] print fibers size: 2
//[01/09/25 01:38:49-07:00] [info] [1818349] [test_cp_mgr.cpp:173:test_promise] access print_hi: 0
//[01/09/25 01:38:49-07:00] [info] [1818349] [test_cp_mgr.cpp:173:test_promise] access print_hi: 1   !!!! here shows
//that fiber 1 is scheduled [01/09/25 01:38:51-07:00] [info] [1818325] [test_cp_mgr.cpp:417:TestBody] success set
//m_promise [01/09/25 01:38:51-07:00] [info] [1818349] [test_cp_mgr.cpp:179:test_promise] Hi: 0, promise: true [01/09/25
//01:38:51-07:00] [info] [1818349] [test_cp_mgr.cpp:181:test_promise] Hi: 1
//
TEST_F(TestCPMgr, test_fiber_promise) {

    std::vector< iomgr::io_fiber_t > fibers;
    // Start WBCache flush threads
    struct Context {
        std::condition_variable cv;
        std::mutex mtx;
        int32_t thread_cnt{0};
    };
    auto ctx = std::make_shared< Context >();

    // create fibers 3-1 = 2, caused by sync_io_capable_fibers will return size()-1.
    iomanager.create_reactor("test_fiber_promise", iomgr::INTERRUPT_LOOP, 3u, [&fibers, ctx](bool is_started) {
        if (is_started) {
            {
                std::unique_lock< std::mutex > lk{ctx->mtx};
                std::vector< iomgr::io_fiber_t > v = iomanager.sync_io_capable_fibers();
                fibers.insert(fibers.end(), v.begin(), v.end());
                LOGINFO("print fibers size: {}", fibers.size());
                ++(ctx->thread_cnt);
            }
            ctx->cv.notify_one();
        }
    });

    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 1); });
    }

    // use fibers print hi
    for (size_t i = 0; i < fibers.size(); ++i) {
        iomanager.run_on_forget(fibers[i], [this, i]() { this->test_promise(i); });
    }

    // sleep 2s, make sure test_promise() has been called lots of times.
    std::this_thread::sleep_for(std::chrono::seconds{2});

    this->m_promise.setValue(true);
    LOGINFO("success set m_promise");

    // hang here to test success.
    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 2); });
    }
}

//
//[01/09/25 01:39:48-07:00] [info] [1818396] [test_cp_mgr.cpp:445:operator()] print fibers size: 2
//[01/09/25 01:39:48-07:00] [info] [1818396] [test_cp_mgr.cpp:188:test_promise_with_mutex] access print_hi: 0
//[01/09/25 01:39:48-07:00] [info] [1818396] [test_cp_mgr.cpp:188:test_promise_with_mutex] access print_hi: 1
//[01/09/25 01:39:50-07:00] [info] [1818372] [test_cp_mgr.cpp:470:TestBody] success set m_promise
// .... long time

//
//(gdb) thread 25
//[Switching to thread 25 (Thread 0x7f8781dfb600 (LWP 1818396))]
// #0  futex_wait (private=0, expected=2, futex_word=0x5645533e7820) at ../sysdeps/nptl/futex-internal.h:146
// 146	../sysdeps/nptl/futex-internal.h: No such file or directory.
//(gdb) bt
// #0  futex_wait (private=0, expected=2, futex_word=0x5645533e7820) at ../sysdeps/nptl/futex-internal.h:146
// #1  __GI___lll_lock_wait (futex=futex@entry=0x5645533e7820, private=0) at ./nptl/lowlevellock.c:49
// #2  0x00007f87bb485002 in lll_mutex_lock_optimized (mutex=0x5645533e7820) at ./nptl/pthread_mutex_lock.c:48
// #3  ___pthread_mutex_lock (mutex=0x5645533e7820) at ./nptl/pthread_mutex_lock.c:93
// #4  0x000056454fcfc2ba in __gthread_mutex_lock (__mutex=0x5645533e7820)
//    at /usr/include/x86_64-linux-gnu/c++/11/bits/gthr-default.h:749
// #5  0x000056454fcfcce6 in std::mutex::lock (this=0x5645533e7820) at /usr/include/c++/11/bits/std_mutex.h:100
// #6  0x000056454fd25954 in std::lock_guard<std::mutex>::lock_guard (this=0x7f8758071130, __m=...)
//    at /usr/include/c++/11/bits/std_mutex.h:229
// #7  0x000056454fd247d0 in TestCPMgr::test_promise_with_mutex (this=0x5645533e7750, i=1)
//    at /home/ubuntu/WorkSpace/CLion/Hooper_HS/src/tests/test_cp_mgr.cpp:189

//
//(gdb) f 6
// #6  0x000056454fd25954 in std::lock_guard<std::mutex>::lock_guard (this=0x7f8758071130, __m=...)
//    at /usr/include/c++/11/bits/std_mutex.h:229
// 229	      { _M_device.lock(); }
//(gdb) info args
// this = 0x7f8758071130
//__m = @0x5645533e7820: {<std::__mutex_base> = {_M_mutex = {__data = {__lock = 2, __count = 0,
//        __owner = 1818396, __nusers = 1, __kind = 0, __spins = 0, __elision = 0, __list = {__prev = 0x0,
//          __next = 0x0}},
//      __size = "\002\000\000\000\000\000\000\000\034\277\033\000\001", '\000' <repeats 26 times>,
//      __align = 2}}, <No data fields>}

// reporduce deadlock, owner is itself.

TEST_F(TestCPMgr, test_fiber_promise_deadlock) {

    std::vector< iomgr::io_fiber_t > fibers;
    // Start WBCache flush threads
    struct Context {
        std::condition_variable cv;
        std::mutex mtx;
        int32_t thread_cnt{0};
    };
    auto ctx = std::make_shared< Context >();

    // create fibers 3-1 = 2, caused by sync_io_capable_fibers will return size()-1.
    iomanager.create_reactor("test_fiber_promise", iomgr::INTERRUPT_LOOP, 3u, [&fibers, ctx](bool is_started) {
        if (is_started) {
            {
                std::unique_lock< std::mutex > lk{ctx->mtx};
                std::vector< iomgr::io_fiber_t > v = iomanager.sync_io_capable_fibers();
                fibers.insert(fibers.end(), v.begin(), v.end());
                LOGINFO("print fibers size: {}", fibers.size());
                ++(ctx->thread_cnt);
            }
            ctx->cv.notify_one();
        }
    });

    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 1); });
    }

    // use fibers print hi
    for (size_t i = 0; i < fibers.size(); ++i) {
        iomanager.run_on_forget(fibers[i], [this, i]() { this->test_promise_with_mutex(i); });
    }

    // sleep 2s, make sure test_promise() has been called lots of times.
    std::this_thread::sleep_for(std::chrono::seconds{2});

    this->m_promise.setValue(true);
    LOGINFO("success set m_promise");

    // hang here to test success.
    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 2); });
    }
}

TEST_F(TestCPMgr, test_fiber_promise_without_deadlock_with_1_sync_io_fiber) {

    std::vector< iomgr::io_fiber_t > fibers;
    // Start WBCache flush threads
    struct Context {
        std::condition_variable cv;
        std::mutex mtx;
        int32_t thread_cnt{0};
    };
    auto ctx = std::make_shared< Context >();

    // create fibers 2-1 = 1, caused by sync_io_capable_fibers will return size()-1.
    iomanager.create_reactor("test_fiber_promise", iomgr::INTERRUPT_LOOP, 2u, [&fibers, ctx](bool is_started) {
        if (is_started) {
            {
                std::unique_lock< std::mutex > lk{ctx->mtx};
                std::vector< iomgr::io_fiber_t > v = iomanager.sync_io_capable_fibers();
                fibers.insert(fibers.end(), v.begin(), v.end());
                LOGINFO("print fibers size: {}", fibers.size());
                ++(ctx->thread_cnt);
            }
            ctx->cv.notify_one();
        }
    });

    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 1); });
    }

    // use fibers print hi,  run two task on 1 fiber
    iomanager.run_on_forget(fibers[0], [this]() { this->test_promise_with_mutex(0); });
    iomanager.run_on_forget(fibers[0], [this]() { this->test_promise_with_mutex(0); });
    // sleep 2s, make sure test_promise() has been called lots of times.
    std::this_thread::sleep_for(std::chrono::seconds{2});

    this->m_promise.setValue(true);
    LOGINFO("success set m_promise");


    // hang here to test success. these means success, no deadlock
	//  [01/23/25 19:48:41-07:00] [info] [1824371] [test_cp_mgr.cpp:515:operator()] print fibers size: 1
	//	[01/23/25 19:48:41-07:00] [info] [1824371] [test_cp_mgr.cpp:181:test_promise_with_mutex] access print_hi: 0
	//	[01/23/25 19:48:43-07:00] [info] [1824347] [test_cp_mgr.cpp:534:TestBody] success set m_promise
	//	[01/23/25 19:48:43-07:00] [info] [1824371] [test_cp_mgr.cpp:187:test_promise_with_mutex] Hi: 0, promise: true
	//	[01/23/25 19:48:43-07:00] [info] [1824371] [test_cp_mgr.cpp:181:test_promise_with_mutex] access print_hi: 0
	//	[01/23/25 19:48:43-07:00] [info] [1824371] [test_cp_mgr.cpp:189:test_promise_with_mutex] Hi: 0
    {
        std::unique_lock< std::mutex > lk{ctx->mtx};
        ctx->cv.wait(lk, [ctx] { return (ctx->thread_cnt == 2); });
    }
}

int main(int argc, char* argv[]) {
    int parsed_argc = argc;
    ::testing::InitGoogleTest(&parsed_argc, argv);
    SISL_OPTIONS_LOAD(parsed_argc, argv, logging, test_cp_mgr, iomgr, test_common_setup);
    sisl::logging::SetLogger("test_home_local_journal");
    spdlog::set_pattern("[%D %T%z] [%^%l%$] [%t] %v");

    return RUN_ALL_TESTS();
}
