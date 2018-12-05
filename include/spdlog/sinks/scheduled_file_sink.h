//
// Copyright(c) 2015 Gabi Melman.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
//

#pragma once

#ifndef SPDLOG_H
#error "spdlog.h must be included before this file."
#endif

#include "spdlog/details/file_helper.h"
#include "spdlog/details/os.h"
#include "spdlog/details/null_mutex.h"
#include "spdlog/fmt/fmt.h"
#include "spdlog/sinks/base_sink.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace spdlog {
namespace sinks {

	enum class scheduled_type{
		DAILY,
		HOURLY,
		MINUTELY,
	};

/*
 * Generator of hourly log file names in format basename.YYYY-MM-DD-HH.ext
 */
struct scheduled_filename_calculator
{
    // Create filename for the form basename.YYYY-MM-DD-HH
    static filename_t calc_filename(const filename_t &filename, const tm &now_tm, scheduled_type type)
    {
        filename_t basename, ext;
        std::tie(basename, ext) = details::file_helper::split_by_extenstion(filename);
        std::conditional<std::is_same<filename_t::value_type, char>::value, fmt::memory_buffer, fmt::wmemory_buffer>::type w;
				switch (type) {
					case scheduled_type::DAILY:
						fmt::format_to(
								w, SPDLOG_FILENAME_T("{}_{:04d}-{:02d}-{:02d}{}"), basename, now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, ext);
						break;
					case scheduled_type::HOURLY:
						fmt::format_to(
								w, SPDLOG_FILENAME_T("{}_{:04d}-{:02d}-{:02d}-{:02d}{}"), basename, now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, ext);
						break;
					case scheduled_type::MINUTELY:
						fmt::format_to(
								w, SPDLOG_FILENAME_T("{}_{:04d}-{:02d}-{:02d}-{:02d}-{:02d}{}"), basename, now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, now_tm.tm_hour, now_tm.tm_min, ext);
						break;
					default:
						throw spdlog_ex(
								"scheduled_file_sink: failed calc_filename " + details::os::filename_to_str(filename) + " invalid scheduled type " + fmt::format("{}", static_cast<int>(type)));
				}
        return fmt::to_string(w);
    }
};

/*
 * Rotating file sink based on date. rotates at midnight
 */
template<typename Mutex, typename FileNameCalc = scheduled_filename_calculator>
class scheduled_rolling_file_sink final : public base_sink<Mutex>
{
public:
    // create scheduled rolling file sink which rotates on given time
    scheduled_rolling_file_sink(filename_t base_filename, scheduled_type sch, bool truncate = false)
        : base_filename_(std::move(base_filename))
        , scheduled_type_(sch)
        , truncate_(truncate)
    {
        auto now = log_clock::now();
        file_helper_.open(FileNameCalc::calc_filename(base_filename_, now_tm(now), scheduled_type_), truncate_);
        rotation_tp_ = next_rotation_tp_();
    }

protected:
    void sink_it_(const details::log_msg &msg) override
    {

        if (msg.time >= rotation_tp_)
        {
            file_helper_.open(FileNameCalc::calc_filename(base_filename_, now_tm(msg.time), scheduled_type_), truncate_);
            rotation_tp_ = next_rotation_tp_();
        }
        fmt::memory_buffer formatted;
        sink::formatter_->format(msg, formatted);
        file_helper_.write(formatted);
    }

    void flush_() override
    {
        file_helper_.flush();
    }

private:
    tm now_tm(log_clock::time_point tp)
    {
        time_t tnow = log_clock::to_time_t(tp);
        return spdlog::details::os::localtime(tnow);
    }

    log_clock::time_point next_rotation_tp_()
    {
				auto align_tm = now_tm(log_clock::now());
				switch (scheduled_type_) {
					case scheduled_type::DAILY:
						{
							align_tm.tm_hour = 0;
							align_tm.tm_min = 0;
							align_tm.tm_sec = 0;
							auto rotation_time = log_clock::from_time_t(std::mktime(&align_tm));
							return {rotation_time + std::chrono::hours(24)};
						}
					case scheduled_type::HOURLY:
						{
							align_tm.tm_min = 0;
							align_tm.tm_sec = 0;
							auto rotation_time = log_clock::from_time_t(std::mktime(&align_tm));
							return {rotation_time + std::chrono::hours(1)};
						}
					case scheduled_type::MINUTELY:
						{
							align_tm.tm_sec = 0;
							auto rotation_time = log_clock::from_time_t(std::mktime(&align_tm));
							return {rotation_time + std::chrono::minutes(1)};
						}
					default:
						throw spdlog_ex(
								"scheduled_file_sink: failed in next_rotation_tp_, invalid scheduled type " + fmt::format("{}", static_cast<int>(scheduled_type_)));
				}
    }

    filename_t base_filename_;
    log_clock::time_point rotation_tp_;
    details::file_helper file_helper_;
		scheduled_type scheduled_type_;
    bool truncate_;
};

using scheduled_file_sink_mt = scheduled_rolling_file_sink<std::mutex>;
using scheduled_file_sink_st = scheduled_rolling_file_sink<details::null_mutex>;

} // namespace sinks

//
// factory functions
//
template<typename Factory = default_factory>
inline std::shared_ptr<logger> scheduled_rolling_logger_mt(
    const std::string &logger_name, const filename_t &filename, spdlog::sinks::scheduled_type t, bool truncate = false)
{
    return Factory::template create<sinks::scheduled_file_sink_mt>(logger_name, filename, t, truncate);
}

template<typename Factory = default_factory>
inline std::shared_ptr<logger> scheduled_rolling_logger_st(
    const std::string &logger_name, const filename_t &filename, spdlog::sinks::scheduled_type t, bool truncate = false)
{
    return Factory::template create<sinks::scheduled_file_sink_st>(logger_name, filename, t, truncate);
}
} // namespace spdlog
