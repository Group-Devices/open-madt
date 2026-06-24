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
	bool NewWebTab(const std::string& url,
	               const std::string& iconUrl,
	               int                preferredPos,
	               int                flags,
	               const std::string& uuid,
	               CmdResponse*       resp);
	bool KillTab(const std::string& uuid, CmdResponse* resp, bool forceDestroy);
	bool ActivateTab(const std::string& uuid, CmdResponse* resp);
	bool NavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
	bool GetCharacteristics(CmdResponse* resp);
	bool GetTabMap(CmdResponse* resp);
	bool BlinkTab(const std::string& uuid, CmdResponse* resp);
	bool PlaySound(const std::string& soundId,
	               unsigned int       soundFlags,
	               const std::string& soundFile,
	               const std::string& soundPlayerCommand,
	               CmdResponse*       resp);
	bool NewShortcut(const std::string& url,
	                 const std::string& iconUrl,
	                 int                preferredPos,
	                 int                flags,
	                 const std::string& shortcutId,
	                 CmdResponse*       resp);
	bool KillShortcut(const std::string& shortcutId, CmdResponse* resp);
	bool GetShortcuts(CmdResponse* resp);
}

// Q_DECLARE_METATYPE(Secretary::Madt::Gui::CmdResponse);

#endif
