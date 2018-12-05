#define SPDLOG_NO_NAME
#define SPDLOG_ACTIVE_LEVEL 0
#include "spdlog/spdlog.h"
#include "spdlog/conf.h"

#include <iostream>
#include <memory>
#include <utility>

int main(int argc, const char * argv[])
{
    bool enable_debug = true;
    try
    {
		spdlog::from_file(argc > 1 ? argv[1] : "log_conf.toml");
		int i_count = 0;
		float f_count = 0;
		auto heartb = spdlog::get("heartb");
		if(!heartb){
			std::cout << "heartb not found" << std::endl;
			return 1;
		}
		for(;;) {
			SPDLOG_TRACE("Trace: you should see this on console");	
			SPDLOG_DEBUG("Debug: you should see this on console and both files");	
			SPDLOG_INFO("Info: you should see this on console and both files: {} - {}", i_count, f_count);	
			SPDLOG_WARN("warn: will print only on console and regular file");	
			SPDLOG_ERROR("Error: you should see this on console and both files");	
			if(i_count % 5 == 0){
				SPDLOG_LOGGER_INFO(heartb, "Heartb: you should only see this in heartb.log");
				//heartb->flush();
			}
			i_count += 1;
			f_count += 1;
			//sleep(1);
		}
    }
    // Exceptions will only be thrown upon failed logger or sink construction (not during logging)
    catch (const spdlog::spdlog_ex &ex)
    {
        std::cout << "Log init failed: " << ex.what() << std::endl;
        return 1;
    }
}
