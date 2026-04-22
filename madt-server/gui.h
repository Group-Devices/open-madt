#ifndef __GUI_H
#define __GUI_H

#include <condition_variable>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace Secretary::Madt::Gui {

	class CmdResponse
	{
	  public:
		enum class ResultCode
		{
			OK,
			TAB_NOT_FOUND,
			EXEC_ERROR
		};
		CmdResponse()
		  : ready(false)
		{
		}
		std::mutex              mtx;
		std::condition_variable cv;
		bool                    ready;
		ResultCode              result;
		nlohmann::json          payload;
	};

	int  run(int argc, char* argv[]);
	void stop();
	void requestExit(int exitCode);
	bool waitForStartupReady();
	bool NewWebTab(const std::string& url, const std::string& uuid);
	bool KillTab(const std::string& uuid, CmdResponse* resp);
	bool ActivateTab(const std::string& uuid, CmdResponse* resp);
	bool NavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
	bool GetCharacteristics(CmdResponse* resp);
	bool GetTabMap(CmdResponse* resp);
}

// Q_DECLARE_METATYPE(Secretary::Madt::Gui::CmdResponse);

#endif
