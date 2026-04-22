
#ifndef _GUI_SCREEN_DISPLAYMANAGER_H_
#define _GUI_SCREEN_DISPLAYMANAGER_H_

#include <map>
#include <string>

#include <QtWidgets/QTabWidget>
#if defined(USE_WEBENGINEVIEW)
#include <QtWebEngineWidgets/QWebEngineView>
#define QPAGE QWebEnginePage
#define QVIEW QWebEngineView
#else
#include <QtWebKitWidgets/QWebView>
#define QPAGE QWebPage
#define QVIEW QWebView
#endif

#include "gui.h"

namespace Secretary::Madt::Gui {
	class CustomPage : public QPAGE
	{
	  public:
		CustomPage(QObject* parent = 0);
		virtual void
		javaScriptConsoleMessage(const QString& message, int lineNumber, const QString& sourceID);
	};

	class WebView : public QVIEW
	{
		Q_OBJECT
	  public:
		WebView(QWidget* parent);
		~WebView();
	};

	struct BrowserPage
	{
		WebView*    view;
		std::string url;
		int         index;
	};

	class Browser : public QTabWidget
	{
		Q_OBJECT
	  public:
		Browser();
		~Browser();

	  signals:
		void signalNewWebTab(const std::string& url, const std::string& uuid);
		void signalActivateTab(const std::string& uuid, CmdResponse* resp);
		void signalNavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
		void signalGetTabMap(CmdResponse* resp);
		void signalKillTab(const std::string& uuid, CmdResponse* resp);

	  public slots:
		void onNewWebTab(const std::string& url, const std::string& uuid);
		void onActivateTab(const std::string& uuid, CmdResponse* resp);
		void onNavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
		void onGetTabMap(CmdResponse* resp);
		void onKillTab(const std::string& uuid, CmdResponse* resp);

	  public:
		void NewWebTab(const std::string& url, const std::string& uuid);
		void ActivateTab(const std::string& uuid, CmdResponse* resp);
		void NavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
		void GetTabMap(CmdResponse* resp);
		void KillTab(const std::string& uuid, CmdResponse* resp);

	  private:
		std::map<std::string, BrowserPage*> list;
	};
}
#endif
