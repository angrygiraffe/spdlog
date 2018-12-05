#define SPDLOG_NO_NAME
#define SPDLOG_ACTIVE_LEVEL 0
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"
#include "spdlog/sinks/scheduled_file_sink.h"
#include "spdlog/async.h"

#include <iostream>
#include <memory>
#include <utility>

int main(int, char *[])
{
	bool enable_debug = true;
	try
	{
		// This other example use a single logger with multiple sinks.
		// This means that the same log_msg is forwarded to multiple sinks;
		// Each sink can have it's own log level and a message will be logged.
		std::vector<spdlog::sink_ptr> sinks;
		//sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_mt>());
		sinks.push_back(std::make_shared<spdlog::sinks::scheduled_file_sink_mt>("./logs/info.log", spdlog::sinks::scheduled_type::DAILY));
		sinks.push_back(std::make_shared<spdlog::sinks::scheduled_file_sink_mt>("./logs/warn.log", spdlog::sinks::scheduled_type::DAILY));
		sinks.push_back(std::make_shared<spdlog::sinks::scheduled_file_sink_mt>("./logs/error.log", spdlog::sinks::scheduled_type::DAILY));
		sinks[0]->set_level(spdlog::level::debug); // console. Allow everything.  Default value
		sinks[1]->set_level(spdlog::level::warn); //  regular file. Allow everything.  Default value
		sinks[2]->set_level(spdlog::level::err); //  regular file. Allow everything.  Default value
		{
			auto &registry_inst = spdlog::details::registry::instance();
			spdlog::init_thread_pool(spdlog::details::default_async_q_size, 1);
			auto tp = spdlog::thread_pool();
			//spdlog::async_logger::async_logger async_common_logger("async_common", sinks.begin(), sinks.end(), tp); 
			auto async_common_logger = std::make_shared<spdlog::async_logger>("async_common",sinks.begin(), sinks.end(), tp);
			//async_common_logger->set_level(spdlog::level::debug);
			async_common_logger->flush_on(spdlog::level::err); //  regular file. Allow everything.  Default value
			spdlog::details::registry::instance().set_default_logger(async_common_logger);

			spdlog::details::registry::instance().set_default_logger(async_common_logger);
			auto heartb_logger = spdlog::scheduled_rolling_logger_mt("heartb", "./logs/heartb.log", spdlog::sinks::scheduled_type::DAILY);
			heartb_logger->set_level(spdlog::level::trace);
			//spdlog::details::registry::instance().register_logger(heartb_logger);
		}
		int i_count = 0;
		float f_count = 0;

		auto heartb = spdlog::get("heartb");
		for(auto i = 0; i < 102400; i ++) {
			//async_common_logger->warn("warn: will print only on console and regular file");
			//async_common_logger->debug("Debug: you should see this on console and both files");
			//async_common_logger->error("Error: you should see this on console and both files");
			SPDLOG_TRACE("Trace: you should see this on console");	
			SPDLOG_DEBUG("Debug: you should see this on console and both files");	
			SPDLOG_INFO("Info: you should see this on console and both files: {} - {}", i_count, f_count);	
			SPDLOG_WARN("warn: will print only on console and regular file");	
			SPDLOG_ERROR("Error: you should see this on console and both files");	
			if(i % 5 == 0){
				SPDLOG_LOGGER_INFO(heartb, "Heartb: you should only see this in heartb.log");
				heartb->flush();
			}
			i_count = i;
			f_count = i;
			sleep(1);
		}

		// Release and close all loggers
		spdlog::drop_all();
	}
	// Exceptions will only be thrown upon failed logger or sink construction (not during logging)
	catch (const spdlog::spdlog_ex &ex)
	{
		std::cout << "Log init failed: " << ex.what() << std::endl;
		return 1;
	}
}
