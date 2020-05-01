#pragma once

#include <sds_logging/logging.h>
#include <spdlog/fmt/fmt.h>
#include <metrics/metrics.hpp>

// clang-format off
/***** HomeStore Logging Macro facility: Goal is to provide consistent logging capability
 * 
 * HS_LOG: Use this log macro to simply log the message for a given logmod (without any request or other details)
 * Parameters are
 * 1) level: log level to which this message is logged, Possible values are TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL
 * 2) logmod: Log module name. This parameter can be empty (upon which it uses base log module), which is on by default
 * 3) msg: The actual message in fmt style where parameters are mentioned as {}
 * 4) msg_params [optional]: Paramters for the above message if any.
 *
 *
 * HS_REQ_LOG: Use this log macro to log the message along with the request id. It will log of the format:
 * <Timestamp etc..>  [req_id=1234] <Actual message>
 * Parameters are
 * 1) level: log level to which this message is logged, Possible values are TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL
 * 2) logmod: Log module name. This parameter can be empty (upon which it uses base log module), which is on by default
 * 3) req: Request id value to log. It can be empty in which case this macro is exactly same as HS_LOG()
 * 4) msg: The actual message in fmt style where parameters are mentioned as {}
 * 5) msg_params [optional]: Paramters for the above message if any.
 *
 *
 * HS_SUBMOD_LOG: Use this macro to log the message with both request_id and submodule name and value. Log format is:
 * <Timestamp etc..>  [volume=<vol_name>] [req_id=1234] <Actual message>
 * Parameters are
 * 1) level: log level to which this message is logged, Possible values are TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL
 * 2) logmod: Log module name. This parameter can be empty (upon which it uses base log module), which is on by default
 * 3) req: Request id value to log. It can be empty in which it will not print req_id portion of the log
 * 4) submod_name: Submodule name (for example volume or blkalloc or btree etc...)
 * 5) submod_val: Submodule value (for example vol1 or chunk1 or mem_btree_1 etc...)
 * 6) msg: The actual message in fmt style where parameters are mentioned as {}
 * 7) msg_params [optional]: Paramters for the above message if any.
 *
 * HS_DETAILED_LOG: Use this macro to log the message with request_id, submodule name/value and any additional info.
 * Log format is:
 * <Timestamp etc..>  [btree=<btree_name>] [req_id=1234] [node=<node_contents>] <Actual message>
 * Parameters are
 * 1) level: log level to which this message is logged, Possible values are TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL
 * 2) logmod: Log module name. This parameter can be empty (upon which it uses base log module), which is on by default
 * 3) req: Request id value to log. It can be empty in which it will not print req_id portion of the log
 * 4) submod_name: Submodule name (for example volume or btree etc...). It can be empty in which case no modname/value
 *                 is added.
 * 5) submod_val: Submodule value (for example vol1 or mem_btree_1 etc...). It can be empty in which case no
 *                modname/value is added.
 * 6) detail_name: Name of the additional details, (example: node)
 * 7) detail_value: Additional value (example: node contents in string)
 * 8) msg: The actual message in fmt style where parameters are mentioned as {}
 * 9) msg_params [optional]: Paramters for the above message if any.
 */
// clang-format on
#define HS_DETAILED_LOG(level, mod, req, submod_name, submod_val, detail_name, detail_val, msg, ...)                   \
    {                                                                                                                  \
        LOG##level##MOD_FMT(BOOST_PP_IF(BOOST_PP_IS_EMPTY(mod), base, mod),                                            \
                            ([&](fmt::memory_buffer& buf, const char* __m, auto&&... args) {                           \
                                fmt::format_to(buf, "[{}:{}] ", file_name(__FILE__), __LINE__);                        \
                                BOOST_PP_IF(BOOST_PP_IS_EMPTY(submod_name), ,                                          \
                                            fmt::format_to(buf, "[{}={}] ", submod_name, submod_val));                 \
                                BOOST_PP_IF(BOOST_PP_IS_EMPTY(req), ,                                                  \
                                            fmt::format_to(buf, "[req_id={}] ", req->request_id));                     \
                                BOOST_PP_IF(BOOST_PP_IS_EMPTY(detail_name), ,                                          \
                                            fmt::format_to(buf, "[{}={}] ", detail_name, detail_val));                 \
                                fmt::format_to(buf, __m, args...);                                                     \
                            }),                                                                                        \
                            msg, ##__VA_ARGS__);                                                                       \
    }
#define HS_SUBMOD_LOG(level, mod, req, submod_name, submod_val, msg, ...)                                              \
    HS_DETAILED_LOG(level, mod, req, submod_name, submod_val, , , msg, ##__VA_ARGS__)
#define HS_REQ_LOG(level, mod, req, msg, ...) HS_SUBMOD_LOG(level, mod, req, , , msg, ##__VA_ARGS__)
#define HS_LOG(level, mod, msg, ...) HS_REQ_LOG(level, mod, , msg, ##__VA_ARGS__)

// clang-format off
/***** HomeStore Assert Macro facility: Goal is to provide consistent assert and gather crucial information
 *
 * HS_DETAILED_ASSERT: Use this macro to assert and also print the request_id, submodule name/value and any additional
 * info.
 * Example Assertlog format:
 * [btree=<btree_name>] [req_id=1234] [node=<node_contents>] [Metrics=<Metrics to diagnose>] <Actual message>
 *
 * Parameters are
 * 1) assert_type: Behavior in case asserting condition is not met. One of the following 3 types
 *   a) DEBUG - Prints the log and crashes the application (with stack trace) in debug build. In release build it is compiled out. 
 *   b) LOGMSG - Same behavior as DEBUG in debug build. In release build, it logs the message along with stack trace and moves on (no crashing) 
 *   c) RELEASE - Prints the log and crashes the application (with stack trace) on all build
 * 2) cond: Condition to validate. If result in false, program will behave as per the assert_type
 * 3) req: Request string for this assert. It can be empty in which it will not print req_id portion of the log
 * 4) submod_name: Submodule name (for example volume or btree etc...). It can be empty in which case no modname/value
 *                 is added.
 * 5) submod_val: Submodule value (for example vol1 or mem_btree_1 etc...). It can be empty in which case no
 *                modname/value is added.
 * 6) detail_name: Name of the additional details, (example: node)
 * 7) detail_value: Additional value (example: node contents in string)
 * 8) msg: The actual message in fmt style where parameters are mentioned as {}
 * 9) msg_params [optional]: Paramters for the above message if any.
 * 
 * HS_SUBMOD_ASSERT is similar to HS_DETAILED_ASSERT, except that detail_name and detail_value is not present.
 * HS_REQ_ASSERT is similar to HS_DETAILED_ASSERT, except that both detail name/value and submodule name/value is not present.
 * HS_ASSERT is barebone version of HS_DETAILED_ASSERT, where no request, submodule and details name/value is present. 
 */
// clang-format on
#define HS_DETAILED_ASSERT(assert_type, cond, req, submod_name, submod_val, detail_name, detail_val, msg, ...)         \
    {                                                                                                                  \
        assert_type##_ASSERT_FMT(                                                                                      \
            cond,                                                                                                      \
            [&](fmt::memory_buffer& buf, const char* __m, auto&&... args) {                                            \
                BOOST_PP_IF(BOOST_PP_IS_EMPTY(submod_name), ,                                                          \
                            fmt::format_to(buf, "\n[{}={}] ", submod_name, submod_val));                               \
                BOOST_PP_IF(BOOST_PP_IS_EMPTY(req), , fmt::format_to(buf, "\n[request={}] ", req->to_string()));       \
                BOOST_PP_IF(BOOST_PP_IS_EMPTY(detail_name), ,                                                          \
                            fmt::format_to(buf, "\n[{}={}] ", detail_name, detail_val));                               \
                fmt::format_to(buf, "\n[Metrics = {}]\n",                                                              \
                               sisl::MetricsFarm::getInstance().get_result_in_json().dump(4));                         \
                fmt::format_to(buf, __m, args...);                                                                     \
            },                                                                                                         \
            msg, ##__VA_ARGS__);                                                                                       \
    }
#define HS_SUBMOD_ASSERT(assert_type, cond, req, submod_name, submod_val, msg, ...)                                    \
    HS_DETAILED_ASSERT(assert_type, cond, req, submod_name, submod_val, , , msg, ##__VA_ARGS__)
#define HS_REQ_ASSERT(assert_type, cond, req, msg, ...) HS_SUBMOD_ASSERT(assert_type, cond, req, , , msg, ##__VA_ARGS__)
#define HS_ASSERT(assert_type, cond, msg, ...) HS_REQ_ASSERT(assert_type, cond, , msg, ##__VA_ARGS__)

#define HS_DETAILED_ASSERT_CMP(assert_type, val1, cmp, val2, req, submod_name, submod_val, detail_name, detail_val,    \
                               ...)                                                                                    \
    {                                                                                                                  \
        assert_type##_ASSERT_CMP(                                                                                      \
            val1, cmp, val2,                                                                                           \
            [&](fmt::memory_buffer& buf, const char* __m, auto&&... args) {                                            \
                fmt::format_to(buf, "[{}:{}] ", file_name(__FILE__), __LINE__);                                        \
                sds_logging::default_cmp_assert_formatter(buf, __m, args...);                                          \
                BOOST_PP_IF(BOOST_PP_IS_EMPTY(submod_name), ,                                                          \
                            fmt::format_to(buf, " \n[{}={}] ", submod_name, submod_val));                              \
                BOOST_PP_IF(BOOST_PP_IS_EMPTY(req), , fmt::format_to(buf, "\n[request={}] ", req->to_string()));       \
                BOOST_PP_IF(BOOST_PP_IS_EMPTY(detail_name), ,                                                          \
                            fmt::format_to(buf, "\n[{}={}] ", detail_name, detail_val));                               \
                fmt::format_to(buf, "\n[Metrics = {}]\n",                                                              \
                               sisl::MetricsFarm::getInstance().get_result_in_json().dump(4));                         \
            },                                                                                                         \
            ##__VA_ARGS__);                                                                                            \
    }

#define HS_SUBMOD_ASSERT_CMP(assert_type, val1, cmp, val2, req, submod_name, submod_val, ...)                          \
    HS_DETAILED_ASSERT_CMP(assert_type, val1, cmp, val2, req, submod_name, submod_val, , , ##__VA_ARGS__)
#define HS_REQ_ASSERT_CMP(assert_type, val1, cmp, val2, req, ...)                                                      \
    HS_SUBMOD_ASSERT_CMP(assert_type, val1, cmp, val2, req, , , ##__VA_ARGS__)
#define HS_ASSERT_CMP(assert_type, val1, cmp, val2, ...)                                                               \
    HS_REQ_ASSERT_CMP(assert_type, val1, cmp, val2, , ##__VA_ARGS__)

/* Not null assert */
#define HS_REQ_ASSERT_NOTNULL(assert_type, val1, req, ...)                                                             \
    HS_REQ_ASSERT_CMP(assert_type, (void*)val1, !=, (void*)nullptr, req, ##__VA_ARGS__)
#define HS_ASSERT_NOTNULL(assert_type, val1, ...) HS_REQ_ASSERT_NOTNULL(assert_type, val1, , ##__VA_ARGS__)
#define HS_SUBMOD_ASSERT_NOTNULL(assert_type, val1, req, submod_name, submod_val, ...)                                 \
    HS_SUBMOD_ASSERT_CMP(assert_type, (void*)val1, !=, (void*)nullptr, req, submod_name, submod_val, ##__VA_ARGS__)

/* Null assert */
#define HS_REQ_ASSERT_NULL(assert_type, val1, req, ...)                                                                \
    HS_REQ_ASSERT_CMP(assert_type, (void*)val1, ==, (void*)nullptr, req, ##__VA_ARGS__)
#define HS_ASSERT_NULL(assert_type, val1, ...) HS_REQ_ASSERT_NULL(assert_type, val1, , ##__VA_ARGS__)
#define HS_SUBMOD_ASSERT_NULL(assert_type, val1, req, submod_name, submod_val, ...)                                    \
    HS_SUBMOD_ASSERT_CMP(assert_type, (void*)val1, ==, (void*)nullptr, req, submod_name, submod_val, ##__VA_ARGS__)