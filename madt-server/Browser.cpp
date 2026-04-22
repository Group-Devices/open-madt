#include <memory>

#include <QtWidgets/QTabBar>

#include "Browser.h"
#include <loghelper/log.h>

namespace Secretary::Madt::Gui {
	namespace {
		constexpr int MAX_TABS = 64;
	}

	CustomPage::CustomPage(QObject* parent)
	  : QPAGE(parent)
	{
	}

	void CustomPage::javaScriptConsoleMessage(const QString& message,
	                                          int            lineNumber,
	                                          const QString& sourceID)
	{
		(void)lineNumber;
		(void)sourceID;
		TLOG("javascript %s", qPrintable(message));
	}

	WebView::WebView(QWidget* parent)
	  : QVIEW(parent)
	{
		CustomPage* webPage = new CustomPage(this);
		setContextMenuPolicy(Qt::NoContextMenu);
		setPage(webPage);
	}

	WebView::~WebView() {}

	Browser::Browser()
	  : QTabWidget()
	{
		QTabBar* bar = tabBar();
		bar->setVisible(false);
		connect(this,
		        SIGNAL(signalNewWebTab(const std::string&, const std::string&)),
		        this,
		        SLOT(onNewWebTab(const std::string&, const std::string&)));
		connect(this,
		        SIGNAL(signalActivateTab(const std::string&, CmdResponse*)),
		        this,
		        SLOT(onActivateTab(const std::string&, CmdResponse*)));
		connect(this,
		        SIGNAL(signalNavigateTo(const std::string&, const std::string&, CmdResponse*)),
		        this,
		        SLOT(onNavigateTo(const std::string&, const std::string&, CmdResponse*)));
		connect(this, SIGNAL(signalGetTabMap(CmdResponse*)), this, SLOT(onGetTabMap(CmdResponse*)));
		connect(this,
		        SIGNAL(signalKillTab(const std::string&, CmdResponse*)),
		        this,
		        SLOT(onKillTab(const std::string&, CmdResponse*)));
	}

	Browser::~Browser() {}

	void Browser::onNewWebTab(const std::string& url, const std::string& uuid)
	{
		WebView* view = new WebView(this);

		BrowserPage* page = new BrowserPage;
		page->url         = url;
		page->view        = view;

		list[uuid] = page;
		view->setUrl(QUrl(QString::fromStdString(url)));
		view->hide();
		page->index = addTab(view, "label");

		ILOG("Create tab %s url %s (%s)",
		     uuid.c_str(),
		     list[uuid]->view->url().toString().toStdString().c_str(),
		     url.c_str());
	}

	void Browser::onActivateTab(const std::string& uuid, CmdResponse* resp)
	{
		if (resp) {
			try {
				ILOG("Activate %s %d", uuid.c_str(), list.count(uuid));
				if (list.count(uuid) > 0) {
					WebView* view = list[uuid]->view;
					WebView* previousView = qobject_cast<WebView*>(currentWidget());
					const int targetIndex = indexOf(view);
					ILOG("url %s", view->url().toString().toStdString().c_str());
					if (previousView != nullptr && previousView != view)
						previousView->hide();
					if (targetIndex >= 0) {
						list[uuid]->index = targetIndex;
						setCurrentIndex(targetIndex);
					}
					view->show();
					view->raise();
					view->update();
					resp->result = CmdResponse::ResultCode::OK;
				} else {
					resp->result = CmdResponse::ResultCode::TAB_NOT_FOUND;
				}
			} catch (...) {
				resp->result = CmdResponse::ResultCode::EXEC_ERROR;
			}
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->ready = true;
			resp->cv.notify_one();
		}
	}

	void Browser::onNavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp)
	{
		if (resp) {
			try {
				if (list.count(uuid) > 0) {
					BrowserPage* page = list[uuid];
					page->url         = url;
					page->view->setUrl(QUrl(QString::fromStdString(url)));
					resp->result = CmdResponse::ResultCode::OK;
				} else {
					resp->result = CmdResponse::ResultCode::TAB_NOT_FOUND;
				}
			} catch (...) {
				resp->result = CmdResponse::ResultCode::EXEC_ERROR;
			}
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->ready = true;
			resp->cv.notify_one();
		}
	}

	void Browser::onGetTabMap(CmdResponse* resp)
	{
		if (resp) {
			try {
				nlohmann::json tabMap = nlohmann::json::array();
				for (int i = 0; i < MAX_TABS; ++i) {
					tabMap.push_back(-1);
				}
				for (const auto& entry : list) {
					BrowserPage* page      = entry.second;
					const int    pageIndex = indexOf(page->view);
					if (pageIndex >= 0) {
						page->index = pageIndex;
						if (pageIndex < MAX_TABS) {
							tabMap[pageIndex] = entry.first;
						}
					}
				}
				resp->payload = std::move(tabMap);
				resp->result  = CmdResponse::ResultCode::OK;
			} catch (...) {
				resp->result = CmdResponse::ResultCode::EXEC_ERROR;
			}
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->ready = true;
			resp->cv.notify_one();
		}
	}

	void Browser::onKillTab(const std::string& uuid, CmdResponse* resp)
	{
		CmdResponse::ResultCode ret;
		try {
			if (list.count(uuid) > 0) {
				BrowserPage* page  = list[uuid];
				const int    index = indexOf(page->view);
				if (index >= 0) {
					removeTab(index);
				}
				delete page->view;
				delete page;
				list.erase(uuid);
				ret = CmdResponse::ResultCode::OK;
			} else {
				ret = CmdResponse::ResultCode::TAB_NOT_FOUND;
			}
		} catch (...) {
			ret = CmdResponse::ResultCode::EXEC_ERROR;
		}
		if (resp) {
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->result = ret;
			resp->ready  = true;
			resp->cv.notify_one();
		}
	}

	void Browser::NewWebTab(const std::string& url, const std::string& uuid)
	{
		TLOG("Signal newWebTab");
		emit signalNewWebTab(url, uuid);
	}

	void Browser::ActivateTab(const std::string& uuid, CmdResponse* resp)
	{
		TLOG("Signal activateTab");
		emit signalActivateTab(uuid, resp);
	}

	void Browser::NavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp)
	{
		emit signalNavigateTo(uuid, url, resp);
	}

	void Browser::GetTabMap(CmdResponse* resp)
	{
		emit signalGetTabMap(resp);
	}

	void Browser::KillTab(const std::string& uuid, CmdResponse* resp)
	{
		emit signalKillTab(uuid, resp);
	}
}
