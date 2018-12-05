/**
 * Implementation of non-public facing functions in spdlog_setup.
 * @author Chen Weiguang
 * @version 0.3.0-alpha.1
 */

#pragma once

#include "spdlog/third_party/cpptoml.h"
#include "spdlog/fmt/fmt.h"

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/scheduled_file_sink.h"
#include "spdlog/sinks/null_sink.h"
#include "spdlog/sinks/sink.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/sinks/syslog_sink.h"

#ifdef _WIN32
#include "spdlog/sinks/wincolor_sink.h"
#else
#include "spdlog/sinks/ansicolor_sink.h"
#endif

#include "spdlog/spdlog.h"
#include "spdlog/async.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <io.h>

#ifndef F_OK
#define F_OK 0
#endif
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace spdlog {
namespace details {
// declaration section

/**
 * Describes the sink types in enumeration form.
 */
enum class sink_type {
    /** Represents stdout_sink_st */
    StdoutSinkSt,

    /** Represents stdout_sink_mt */
    StdoutSinkMt,

    /**
     * Represents either wincolor_stdout_sink_st (Windows) or
     * ansicolor_stdout_sink_st (Linux)
     */
    ColorStdoutSinkSt,

    /**
     * Represents either wincolor_stdout_sink_mt (Windows) or
     * ansicolor_stdout_sink_mt (Linux)
     */
    ColorStdoutSinkMt,

    /** Represents basic_file_sink_st */
    BasicFileSinkSt,

    /** Represents basic_file_sink_mt */
    BasicFileSinkMt,

    /** Represents rotating_file_sink_st */
    RotatingFileSinkSt,

    /** Represents rotating_file_sink_mt */
    RotatingFileSinkMt,

    /** Represents scheduled_file_sink_st */
    ScheduledFileSinkSt,

    /** Represents scheduled_file_sink_mt */
    ScheduledFileSinkMt,

    /** Represents null_sink_st */
    NullSinkSt,

    /** Represents null_sink_mt */
    NullSinkMt,

    /** Represents syslog_sink */
    SyslogSink,
};

namespace names {
// table names
static constexpr auto LOGGER_TABLE = "logger";
static constexpr auto PATTERN_TABLE = "pattern";
static constexpr auto SINK_TABLE = "sink";

// field names
static constexpr auto BASE_FILENAME = "base_filename";
static constexpr auto CREATE_PARENT_DIR = "create_parent_dir";
static constexpr auto FILENAME = "filename";
static constexpr auto GLOBAL_PATTERN = "global_pattern";
static constexpr auto IDENT = "ident";
static constexpr auto LEVEL = "level";
static constexpr auto MAX_FILES = "max_files";
static constexpr auto MAX_SIZE = "max_size";
static constexpr auto NAME = "name";
static constexpr auto PATTERN = "pattern";
static constexpr auto SINKS = "sinks";
static constexpr auto SYSLOG_FACILITY = "syslog_facility";
static constexpr auto SYSLOG_OPTION = "syslog_option";
static constexpr auto TRUNCATE = "truncate";
static constexpr auto TYPE = "type";
static constexpr auto VALUE = "value";

static constexpr auto IS_ASYNC = "is_async";
static constexpr auto ASYNC_IS_BLOCK = "async.block";
static constexpr auto FLUSH_LEVEL = "flush_level";
static constexpr auto IS_DEFAULT = "is_default";
static constexpr auto SCHEDULED_TYPE = "scheduled_type";
	
} // namespace names

inline auto get_parent_path(const std::string &file_path) -> std::string {
    // string
    using std::string;

#ifdef _WIN32
    static constexpr auto DIR_SLASHES = "\\/";
#else
    static constexpr auto DIR_SLASHES = '/';
#endif

    const auto last_slash_index = file_path.find_last_of(DIR_SLASHES);

    if (last_slash_index == string::npos) {
        return "";
    }

    return file_path.substr(0, last_slash_index);
}

inline bool native_create_dir(const std::string &dir_path) noexcept {
#ifdef _WIN32
    // non-zero for success in Windows
    return CreateDirectoryA(dir_path.c_str(), nullptr) != 0;
#else
    // zero for success for GNU
    return mkdir(dir_path.c_str(), 0775) == 0;
#endif
}

inline auto file_exists(const std::string &file_path) noexcept -> bool {
    static constexpr auto FILE_NOT_FOUND = -1;

#ifdef _WIN32
    return _access(file_path.c_str(), F_OK) != FILE_NOT_FOUND;
#else
    return access(file_path.c_str(), F_OK) != FILE_NOT_FOUND;
#endif
}

inline void create_dirs_impl(const std::string &dir_path) {
    // fmt
    using fmt::format;

#ifdef _WIN32
    // check for both empty and drive letter
    if (dir_path.empty() || (dir_path.length() == 2 && dir_path[1] == ':')) {
        return;
    }
#else
    if (dir_path.empty()) {
        return;
    }
#endif

    if (!file_exists(dir_path)) {
        create_dirs_impl(get_parent_path(dir_path));

        if (!native_create_dir(dir_path)) {
            throw spdlog_ex(
                format("Unable to create directory at '{}'", dir_path));
        }
    }
}

inline void create_directories(const std::string &dir_path) {
    create_dirs_impl(dir_path);
}

inline auto
find_item_iter_by_name(cpptoml::table_array &items, const std::string &name)
    -> cpptoml::table_array::iterator {

    using names::NAME;

    // std
    using std::find_if;
    using std::shared_ptr;
    using std::string;

    return find_if(
        items.begin(),
        items.end(),
        [&name](const shared_ptr<cpptoml::table> item) {
            const auto item_name_opt = item->get_as<string>(NAME);
            return item_name_opt && *item_name_opt == name;
        });
}

inline auto
find_item_by_name(cpptoml::table_array &items, const std::string &name)
    -> std::shared_ptr<cpptoml::table> {

    const auto item_it = find_item_iter_by_name(items, name);

    if (item_it == items.end()) {
        return nullptr;
    }

    return *item_it;
}

inline void write_to_config_file(
    const cpptoml::table &config, const std::string &toml_path) {

    // fmt
    using fmt::format;

    // std
    using std::ofstream;

    ofstream override_str(toml_path);

    if (!override_str) {
        throw spdlog_ex(format("Unable to open '{}' for writing", toml_path));
    }

    auto writer = cpptoml::toml_writer(override_str);
    writer.visit(config);
}

template <class... Ps>
void read_template_file_into_stringstream(
    std::stringstream &toml_ss, const std::string &file_path, Ps &&... ps) {

    // fmt
    using fmt::format;

    // std
    using std::exception;
    using std::forward;
    using std::ifstream;
    using std::move;
    using std::string;
    using std::stringstream;

    try {
        ifstream file_stream(file_path);

        if (!file_stream) {
            throw spdlog_ex(format("Error reading file at '{}'", file_path));
        }

        stringstream pre_toml_ss;
        pre_toml_ss << file_stream.rdbuf();

        const auto pre_toml_content = pre_toml_ss.str();

        const auto toml_content =
            format(pre_toml_content, std::forward<Ps>(ps)...);

        toml_ss << toml_content;
    } catch (const exception &e) {
        throw spdlog_ex(e.what());
    }
}

inline void merge_config_items(
    cpptoml::table &base_ref,
    const std::shared_ptr<cpptoml::table_array> &base_items,
    const std::shared_ptr<cpptoml::table_array> &ovr_items) {

    using names::NAME;

    // std
    using std::string;

    if (base_items && ovr_items) {
        auto &base_items_ref = *base_items;
        const auto &ovr_items_ref = *ovr_items;

        for (const auto ovr_item : ovr_items_ref) {
            const auto &ovr_item_ref = *ovr_item;
            const auto ovr_name_opt = ovr_item->get_as<string>(NAME);

            if (!ovr_name_opt) {
                throw spdlog_ex(
                    "One of the items in override does not have a name");
            }

            const auto &ovr_name = *ovr_name_opt;

            const auto found_base_item =
                find_item_by_name(base_items_ref, ovr_name);

            if (found_base_item) {
                // merge from override to base
                auto &found_base_item_ref = *found_base_item;

                for (const auto ovr_item_kv : ovr_item_ref) {
                    found_base_item_ref.insert(
                        ovr_item_kv.first, ovr_item_kv.second);
                }
            } else {
                // insert new override item entry
                base_items_ref.push_back(ovr_item);
            }
        }
    } else if (!base_items && ovr_items) {
        base_ref.insert(names::LOGGER_TABLE, ovr_items);
    }
}

inline void merge_config_root(
    const std::shared_ptr<cpptoml::table> &base,
    const std::shared_ptr<cpptoml::table> &ovr) {

    using names::LOGGER_TABLE;
    using names::PATTERN_TABLE;
    using names::SINK_TABLE;

    if (!base) {
        throw spdlog_ex("Base config cannot be null for merging");
    }

    if (!ovr) {
        throw spdlog_ex("Override config cannot be null for merging");
    }

    auto &base_ref = *base;
    const auto &ovr_ref = *ovr;

    // directly target items to merge

    // sinks
    const auto base_sinks = base_ref.get_table_array(names::SINK_TABLE);
    const auto ovr_sinks = ovr_ref.get_table_array(names::SINK_TABLE);
    merge_config_items(base_ref, base_sinks, ovr_sinks);

    // patterns
    const auto base_patterns = base_ref.get_table_array(names::PATTERN_TABLE);
    const auto ovr_patterns = ovr_ref.get_table_array(names::PATTERN_TABLE);
    merge_config_items(base_ref, base_patterns, ovr_patterns);

    // loggers
    const auto base_loggers = base_ref.get_table_array(names::LOGGER_TABLE);
    const auto ovr_loggers = ovr_ref.get_table_array(names::LOGGER_TABLE);
    merge_config_items(base_ref, base_loggers, ovr_loggers);
} // namespace details

template <class T, class Fn>
void if_value_from_table(
    const std::shared_ptr<cpptoml::table> &table,
    const char field[],
    Fn &&if_value_fn) {

    const auto value_opt = table->get_as<T>(field);

    if (value_opt) {
        if_value_fn(*value_opt);
    }
}

template <class T>
auto value_from_table_opt(
    const std::shared_ptr<cpptoml::table> &table, const char field[])
    -> cpptoml::option<T> {

    // std
    using std::string;

    return table->get_as<string>(field);
}

template <class T>
auto value_from_table_or(
    const std::shared_ptr<cpptoml::table> &table,
    const char field[],
    const T &alt_val) -> T {

    const auto value_opt = table->get_as<T>(field);

    if (value_opt) {
        return *value_opt;
    } else {
        return alt_val;
    }
}

template <class T>
auto value_from_table_qualified_or(
    const std::shared_ptr<cpptoml::table> &table,
    const char field[],
    const T &alt_val) -> T {

    const auto value_opt = table->get_qualified_as<T>(field);

    if (value_opt) {
        return *value_opt;
    } else {
        return alt_val;
    }
}

template <class T>
auto value_from_table(
    const std::shared_ptr<cpptoml::table> &table,
    const char field[],
    const std::string &err_msg) -> T {

    const auto value_opt = table->get_as<T>(field);

    if (!value_opt) {
        throw spdlog_ex(err_msg);
    }

    return *value_opt;
}

template <class T>
auto value_from_table_qualified(
    const std::shared_ptr<cpptoml::table> &table,
    const char field[],
    const std::string &err_msg) -> T {

    const auto value_opt = table->get_qualified_as<T>(field);

    if (!value_opt) {
        throw spdlog_ex(err_msg);
    }

    return *value_opt;
}

template <class T>
auto array_from_table(
    const std::shared_ptr<cpptoml::table> &table,
    const char field[],
    const std::string &err_msg) -> std::vector<T> {

    const auto array_opt = table->get_array_of<T>(field);

    if (!array_opt) {
        throw spdlog_ex(err_msg);
    }

    return *array_opt;
}

template <class Map, class Key>
auto find_value_from_map(
    const Map &m, const Key &key, const std::string &err_msg) ->
    typename Map::mapped_type {

    const auto iter = m.find(key);

    if (iter == m.cend()) {
        throw spdlog_ex(err_msg);
    }

    return iter->second;
}

template <class Fn, class ErrFn>
auto add_msg_on_err(Fn &&fn, ErrFn &&add_msg_on_err_fn) ->
    typename std::result_of<Fn()>::type {

    // std
    using std::exception;
    using std::move;
    using std::string;

    try {
        return fn();
    } catch (const exception &e) {
        throw spdlog_ex(add_msg_on_err_fn(e.what()));
    }
}

inline std::string trim(const std::string &str, const std::string &cutset) {
		std::string::size_type start = str.find_first_not_of( cutset );
		std::string::size_type end = str.find_last_not_of( cutset );

		return start != std::string::npos ? str.substr( start, 1+end-start ) : "";
}

inline std::string trim_space(const std::string & str ) {
		static char const* whitespace_chars = "\n\r\t ";
	  return trim(str, whitespace_chars);
}

inline std::string trim_r(const std::string &str, const std::string &cutset) {
	std::string::size_type end = str.find_last_not_of(cutset);
	return end != std::string::npos ? str.substr(0, end + 1) : "";
}

inline std::string trim_l(const std::string &str, const std::string &cutset) {
	std::string::size_type start = str.find_first_not_of(cutset);
	return start != std::string::npos ? str.substr(start, str.size() - start) : "";
}

inline auto parse_max_size(const std::string &max_size_str) -> uint64_t {
    // fmt
    using fmt::format;

    // std
    using std::exception;
    using std::string;

	try {
		std::size_t pos;
		uint64_t base_val = std::stoul(max_size_str, &pos);
		auto suffix = trim_space(max_size_str.substr(pos));
		if(suffix == "k" || suffix == "K") {
			return base_val * 1024;
		}else if(suffix == "m" || suffix == "M") {
			return base_val * 1024 * 1024;
		}else if(suffix == "g" || suffix == "G") {
			return base_val * 1024 * 1024 * 1024;
		}else if(suffix == "t" || suffix == "T") {
			return base_val * 1024 * 1024 * 1024 * 1024;
		}else {
			throw spdlog_ex(format(
                    "Unexpected unit suffix '{}' for max size parsing", suffix));
		}
		return base_val;
    } catch (const exception &e) {
        throw spdlog_ex(format(
            "Unexpected exception for max size parsing on string '{}': {}",
            max_size_str,
            e.what()));
    }
}

inline auto sink_type_from_str(const std::string &type) -> sink_type {
    // fmt
    using fmt::format;

    // std
    using std::string;
    using std::unordered_map;

    static const unordered_map<string, sink_type> MAPPING{
        {"stdout_sink_st", sink_type::StdoutSinkSt},
        {"stdout_sink_mt", sink_type::StdoutSinkMt},
        {"color_stdout_sink_st", sink_type::ColorStdoutSinkSt},
        {"color_stdout_sink_mt", sink_type::ColorStdoutSinkMt},
        {"basic_file_sink_st", sink_type::BasicFileSinkSt},
        {"basic_file_sink_mt", sink_type::BasicFileSinkMt},
        {"rotating_file_sink_st", sink_type::RotatingFileSinkSt},
        {"rotating_file_sink_mt", sink_type::RotatingFileSinkMt},
        {"scheduled_file_sink_st", sink_type::ScheduledFileSinkSt},
        {"scheduled_file_sink_mt", sink_type::ScheduledFileSinkMt},
        {"null_sink_st", sink_type::NullSinkSt},
        {"null_sink_mt", sink_type::NullSinkMt},

#ifdef SPDLOG_ENABLE_SYSLOG
        {"syslog_sink", sink_type::SyslogSink},
#endif
    };

    return find_value_from_map(
        MAPPING, type, format("Invalid sink type '{}' found", type));
}

inline void create_parent_dir_if_present(
    const std::shared_ptr<cpptoml::table> &sink_table,
    const std::string &filename) {

    using names::CREATE_PARENT_DIR;

    // std
    using std::string;

    if_value_from_table<bool>(
        sink_table, CREATE_PARENT_DIR, [&filename](const bool flag) {
            if (flag) {
                create_directories(get_parent_path(filename));
            }
        });
}

inline auto level_from_str(const std::string &level)
    -> spdlog::level::level_enum {

    // fmt
    using fmt::format;

    // spdlog
    namespace lv = spdlog::level;

    if (level == "trace") {
        return lv::trace;
    } else if (level == "debug") {
        return lv::debug;
    } else if (level == "info") {
        return lv::info;
    } else if (level == "warn") {
        return lv::warn;
    } else if (level == "error") {
        return lv::err;
    } else if (level == "critical") {
        return lv::critical;
    } else if (level == "off") {
        return lv::off;
    } else {
        throw spdlog_ex(format("Invalid level string '{}' provided", level));
    }
}

inline auto scheduled_from_str(const std::string &scheduled_type_str)
    -> spdlog::sinks::scheduled_type {

    // fmt
    using fmt::format;

    // spdlog
    namespace ss = spdlog::sinks;

    if (scheduled_type_str == "daily") {
        return ss::scheduled_type::DAILY;
    } else if (scheduled_type_str == "hourly") {
        return ss::scheduled_type::HOURLY;
    } else if (scheduled_type_str == "minutely") {
        return ss::scheduled_type::MINUTELY;
	} else {
        throw spdlog_ex(format("Invalid scheduled_type string '{}' provided", scheduled_type_str));
    }
}

inline auto level_to_str(const spdlog::level::level_enum level) -> std::string {
    // fmt
    using fmt::format;

    // spdlog
    namespace lv = spdlog::level;

    if (level == lv::trace) {
        return "trace";
    } else if (level == lv::debug) {
        return "debug";
    } else if (level == lv::info) {
        return "info";
    } else if (level == lv::warn) {
        return "warn";
    } else if (level == lv::err) {
        return "error";
    } else if (level == lv::critical) {
        return "critical";
    } else if (level == lv::off) {
        return "off";
    } else {
        throw spdlog_ex(format("Invalid level enum '{}' provided", level));
    }
}

inline void set_sink_level_if_present(
    const std::shared_ptr<cpptoml::table> &sink_table,
    const std::shared_ptr<spdlog::sinks::sink> &sink) {

    using names::LEVEL;

    // std
    using std::string;

    if_value_from_table<string>(
        sink_table, LEVEL, [&sink](const string &level) {
            const auto level_enum = level_from_str(level);
            sink->set_level(level_enum);
        });
}

template <class BasicFileSink>
auto basic_file_sink_from_table(
    const std::shared_ptr<cpptoml::table> &sink_table)
    -> std::shared_ptr<spdlog::sinks::sink> {

    using names::FILENAME;
    using names::LEVEL;
    using names::TRUNCATE;

    // fmt
    using fmt::format;

    // std
    using std::make_shared;
    using std::string;

    static constexpr auto DEFAULT_TRUNCATE = false;

    const auto filename = value_from_table<string>(
        sink_table,
        FILENAME,
        format(
            "Missing '{}' field of string value for simple_file_sink",
            FILENAME));

    // must create the directory before creating the sink
    create_parent_dir_if_present(sink_table, filename);

    const auto truncate =
        value_from_table_or<bool>(sink_table, TRUNCATE, DEFAULT_TRUNCATE);

    return make_shared<BasicFileSink>(filename, truncate);
}

template <class RotatingFileSink>
auto rotating_file_sink_from_table(
    const std::shared_ptr<cpptoml::table> &sink_table)
    -> std::shared_ptr<spdlog::sinks::sink> {

    using names::BASE_FILENAME;
    using names::MAX_FILES;
    using names::MAX_SIZE;

    // fmt
    using fmt::format;

    // std
    using std::make_shared;
    using std::string;

    const auto base_filename = value_from_table<string>(
        sink_table,
        BASE_FILENAME,
        format(
            "Missing '{}' field of string value for rotating_file_sink",
            BASE_FILENAME));

    // must create the directory before creating the sink
    create_parent_dir_if_present(sink_table, base_filename);

    const auto max_filesize_str = value_from_table<string>(
        sink_table,
        MAX_SIZE,
        format(
            "Missing '{}' field of string value for rotating_file_sink",
            MAX_SIZE));

    const auto max_filesize = parse_max_size(max_filesize_str);

    const auto max_files = value_from_table<uint64_t>(
        sink_table,
        MAX_FILES,
        format(
            "Missing '{}' field of u64 value for rotating_file_sink",
            MAX_FILES));

    return make_shared<RotatingFileSink>(
        base_filename, max_filesize, max_files);
}

template <class ScheduledFileSink>
auto scheduled_file_sink_from_table(
    const std::shared_ptr<cpptoml::table> &sink_table)
    -> std::shared_ptr<spdlog::sinks::sink> {

    using names::BASE_FILENAME;

    using names::SCHEDULED_TYPE;

    // fmt
    using fmt::format;

    // std
    using std::make_shared;
    using std::string;

    const auto base_filename = value_from_table<string>(
        sink_table,
        BASE_FILENAME,
        format(
            "Missing '{}' field of string value for scheduled_file_sink",
            BASE_FILENAME));

    // must create the directory before creating the sink
    create_parent_dir_if_present(sink_table, base_filename);

    const auto scheduled_type = value_from_table<string>(
        sink_table,
        SCHEDULED_TYPE,
        format(
            "Missing '{}' field of string value for scheudled_file_sink",
            SCHEDULED_TYPE));

    const auto type = scheduled_from_str(scheduled_type);

    return make_shared<ScheduledFileSink>(
        base_filename, type);
}

#ifdef SPDLOG_ENABLE_SYSLOG

inline auto
syslog_sink_from_table(const std::shared_ptr<cpptoml::table> &sink_table)
    -> std::shared_ptr<spdlog::sinks::sink> {

    using names::IDENT;
    using names::SYSLOG_FACILITY;
    using names::SYSLOG_OPTION;

    // spdlog
    using spdlog::sinks::syslog_sink;

    // fmt
    using fmt::format;

    // std
    using std::make_shared;
    using std::string;

    // all are optional fields
    static constexpr auto DEFAULT_IDENT = "";
    static constexpr auto DEFAULT_SYSLOG_OPTION = 0;
    static constexpr auto DEFAULT_SYSLOG_FACILITY = LOG_USER;

    const auto ident =
        value_from_table_or<string>(sink_table, IDENT, DEFAULT_IDENT);

    const auto syslog_option = value_from_table_or<int32_t>(
        sink_table, SYSLOG_OPTION, DEFAULT_SYSLOG_OPTION);

    const auto syslog_facility = value_from_table_or<int32_t>(
        sink_table, SYSLOG_FACILITY, DEFAULT_SYSLOG_FACILITY);

    return make_shared<syslog_sink>(ident, syslog_option, syslog_facility);
}

#endif

inline auto sink_from_sink_type(
    const sink_type sink_val, const std::shared_ptr<cpptoml::table> &sink_table)
    -> std::shared_ptr<spdlog::sinks::sink> {

    // fmt
    using fmt::format;

    // spdlog
    using spdlog::sinks::scheduled_file_sink_mt;
    using spdlog::sinks::scheduled_file_sink_st;
    using spdlog::sinks::null_sink_mt;
    using spdlog::sinks::null_sink_st;
    using spdlog::sinks::rotating_file_sink_mt;
    using spdlog::sinks::rotating_file_sink_st;
    using spdlog::sinks::basic_file_sink_mt;
    using spdlog::sinks::basic_file_sink_st;
    using spdlog::sinks::sink;
    using spdlog::sinks::stdout_sink_mt;
    using spdlog::sinks::stdout_sink_st;

    // std
    using std::make_shared;

#ifdef _WIN32
    using color_stdout_sink_st = spdlog::sinks::wincolor_stdout_sink_st;
    using color_stdout_sink_mt = spdlog::sinks::wincolor_stdout_sink_mt;
#else
    using color_stdout_sink_st = spdlog::sinks::ansicolor_stdout_sink_st;
    using color_stdout_sink_mt = spdlog::sinks::ansicolor_stdout_sink_mt;
#endif

    switch (sink_val) {
    case sink_type::StdoutSinkSt:
        return make_shared<stdout_sink_st>();

    case sink_type::StdoutSinkMt:
        return make_shared<stdout_sink_mt>();

    case sink_type::ColorStdoutSinkSt:
        return make_shared<color_stdout_sink_st>();

    case sink_type::ColorStdoutSinkMt:
        return make_shared<color_stdout_sink_mt>();

    case sink_type::BasicFileSinkSt:
        return basic_file_sink_from_table<basic_file_sink_st>(sink_table);

    case sink_type::BasicFileSinkMt:
        return basic_file_sink_from_table<basic_file_sink_mt>(sink_table);

    case sink_type::RotatingFileSinkSt:
        return rotating_file_sink_from_table<rotating_file_sink_st>(sink_table);

    case sink_type::RotatingFileSinkMt:
        return rotating_file_sink_from_table<rotating_file_sink_mt>(sink_table);

    case sink_type::ScheduledFileSinkSt:
        return scheduled_file_sink_from_table<scheduled_file_sink_st>(sink_table);

    case sink_type::ScheduledFileSinkMt:
        return scheduled_file_sink_from_table<scheduled_file_sink_mt>(sink_table);

    case sink_type::NullSinkSt:
        return make_shared<null_sink_st>();

    case sink_type::NullSinkMt:
        return make_shared<null_sink_mt>();

#ifdef SPDLOG_ENABLE_SYSLOG
    case sink_type::SyslogSink:
        return syslog_sink_from_table(sink_table);
#endif

    default:
        throw spdlog_ex(format(
            "Unexpected sink error with sink enum value '{}'",
            static_cast<int>(sink_val)));
    }
}

inline auto sink_from_table(const std::shared_ptr<cpptoml::table> &sink_table)
    -> std::shared_ptr<spdlog::sinks::sink> {

    using names::TYPE;

    // fmt
    using fmt::format;

    // std
    using std::move;
    using std::string;

    const auto type_val = value_from_table<string>(
        sink_table, TYPE, format("Sink missing '{}' field", TYPE));

    const auto sink_val = sink_type_from_str(type_val);
    auto sink = sink_from_sink_type(sink_val, sink_table);

    // set optional parts and return back the same sink
    set_sink_level_if_present(sink_table, sink);

    return move(sink);
}

inline void set_logger_level_if_present(
    const std::shared_ptr<cpptoml::table> &logger_table,
    const std::shared_ptr<spdlog::logger> &logger) {

    using names::LEVEL;

    // std
    using std::string;

    if_value_from_table<string>(
        logger_table, LEVEL, [&logger](const string &level) {
            const auto level_enum = level_from_str(level);
            logger->set_level(level_enum);
        });
}

inline auto setup_sinks_impl(const std::shared_ptr<cpptoml::table> &config)
    -> std::unordered_map<std::string, std::shared_ptr<spdlog::sinks::sink>> {

    using names::NAME;
    using names::SINK_TABLE;

    // fmt
    using fmt::format;

    // std
    using std::move;
    using std::shared_ptr;
    using std::string;
    using std::unordered_map;

    const auto sinks = config->get_table_array(SINK_TABLE);

    if (!sinks) {
        throw spdlog_ex("No sinks configured for set-up");
    }

    unordered_map<string, shared_ptr<spdlog::sinks::sink>> sinks_map;

    for (const auto &sink_table : *sinks) {
        auto name = value_from_table<string>(
            sink_table,
            NAME,
            format("One of the sinks does not have a '{}' field", NAME));

        auto sink = add_msg_on_err(
            [&sink_table] { return sink_from_table(sink_table); },
            [&name](const string &err_msg) {
                return format("Sink '{}' error:\n > {}", name, err_msg);
            });

        sinks_map.emplace(move(name), move(sink));
    }

    return move(sinks_map);
}

inline auto setup_formats_impl(const std::shared_ptr<cpptoml::table> &config)
    -> std::unordered_map<std::string, std::string> {

    using names::NAME;
    using names::PATTERN_TABLE;
    using names::VALUE;

    // fmt
    using fmt::format;

    // std
    using std::move;
    using std::string;
    using std::unordered_map;

    // possible to return an entire empty pattern map
    unordered_map<string, string> patterns_map;

    const auto formats = config->get_table_array(PATTERN_TABLE);

    if (formats) {
        for (const auto &format_table : *formats) {
            auto name = value_from_table<string>(
                format_table,
                NAME,
                format("One of the formats does not have a '{}' field", NAME));

            auto value = value_from_table<string>(
                format_table,
                VALUE,
                format("Format '{}' does not have '{}' field", name, VALUE));

            patterns_map.emplace(move(name), move(value));
        }
    }

    return move(patterns_map);
}

inline void setup_loggers_impl(
    const std::shared_ptr<cpptoml::table> &config,
    const std::unordered_map<std::string, std::shared_ptr<spdlog::sinks::sink>>
        &sinks_map,
    const std::unordered_map<std::string, std::string> &patterns_map) {

    using names::GLOBAL_PATTERN;
    using names::LOGGER_TABLE;
    using names::NAME;
    using names::PATTERN;
    using names::SINKS;

    using names::IS_ASYNC;
    using names::ASYNC_IS_BLOCK;
	using names::IS_DEFAULT;
	using names::FLUSH_LEVEL;

    // fmt
    using fmt::format;

    // std
    using std::exception;
    using std::make_shared;
    using std::move;
    using std::shared_ptr;
    using std::string;
    using std::vector;

    const auto loggers = config->get_table_array(LOGGER_TABLE);

    if (!loggers) {
        throw spdlog_ex("No loggers configured for set-up");
    }

    // set up possibly the global pattern if present
    auto global_pattern_opt =
        value_from_table_opt<string>(config, GLOBAL_PATTERN);

	bool default_set = false;

    for (const auto &logger_table : *loggers) {
        const auto name = value_from_table<string>(
            logger_table,
            NAME,
            format("One of the loggers does not have a '{}' field", NAME));

        const auto sinks = array_from_table<string>(
            logger_table,
            SINKS,
            format(
                "Logger '{}' does not have a '{}' field of sink names",
                name,
                SINKS));

        vector<shared_ptr<spdlog::sinks::sink>> logger_sinks;
        logger_sinks.reserve(sinks.size());

        for (const auto &sink_name : sinks) {
            auto sink = find_value_from_map(
                sinks_map,
                sink_name,
                format(
                    "Unable to find sink '{}' for logger '{}'",
                    sink_name,
                    name));

            logger_sinks.push_back(move(sink));
        }

				const auto is_async = value_from_table_or<bool>(logger_table, IS_ASYNC, false);
				shared_ptr<logger> logger;
				if(is_async) {
					const auto is_overflow_block = value_from_table_qualified_or<bool>(logger_table, ASYNC_IS_BLOCK, true);
					auto &registry_inst = details::registry::instance();
					std::lock_guard<std::recursive_mutex> tp_lock(registry_inst.tp_mutex());
					auto tp = registry_inst.get_tp();
					if(tp == nullptr) {
						tp = std::make_shared<details::thread_pool>(details::default_async_q_size, 1);
						registry_inst.set_tp(tp);
					}
					logger = make_shared<spdlog::async_logger>(name, logger_sinks.cbegin(), logger_sinks.cend(), std::move(tp), is_overflow_block? async_overflow_policy::block : async_overflow_policy::overrun_oldest);
				}else{
					logger = make_shared<spdlog::logger>(name, logger_sinks.cbegin(), logger_sinks.cend());
				}

        // optional fields
        add_msg_on_err(
            [&logger_table, &logger] {
                set_logger_level_if_present(logger_table, logger);
            },
            [&name](const string &err_msg) {
                return format(
                    "Logger '{}' set level error:\n > {}", name, err_msg);
            });

        const auto pattern_name_opt =
            value_from_table_opt<string>(logger_table, PATTERN);

        using pattern_option_t = cpptoml::option<string>;

        auto pattern_value_opt =
            pattern_name_opt ? [&name,
                                &patterns_map,
                                &pattern_name_opt]() {
            const auto &pattern_name = *pattern_name_opt;

            const auto pattern_value = find_value_from_map(
                patterns_map,
                pattern_name,
                format(
                    "Pattern name '{}' cannot be found for logger '{}'",
                    pattern_name,
                    name));

            return pattern_option_t(pattern_value);
        }()

            : pattern_option_t();

        const auto selected_pattern_opt = pattern_value_opt
                                              ? move(pattern_value_opt)
                                              : global_pattern_opt;

        try {
            if (selected_pattern_opt) {
                logger->set_pattern(*selected_pattern_opt);
            }
        } catch (const exception &e) {
            throw spdlog_ex(format(
                "Error setting pattern to logger '{}': {}", name, e.what()));
        }

		logger->flush_on(level_from_str(value_from_table_or<string>(logger_table, FLUSH_LEVEL, "warn")));

		auto is_default = value_from_table_or<bool>(logger_table, IS_DEFAULT, false);
		if(is_default) {
			if(default_set) {
				throw spdlog_ex("Default logger mustn't set more than once");
			}
			//if is default logger, don't need to call register_logger
			set_default_logger(logger);
			default_set = true;
		}else {
			register_logger(logger);
		}
    }
}

inline void setup_impl(const std::shared_ptr<cpptoml::table> &config) {
    // set up sinks
    const auto sinks_map = setup_sinks_impl(config);

    // set up patterns
    const auto patterns_map = setup_formats_impl(config);

    // set up loggers, setting the respective sinks and patterns
    setup_loggers_impl(config, sinks_map, patterns_map);
}

} // namespace details
} // namespace spdlog
