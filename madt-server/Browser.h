
#ifndef _GUI_SCREEN_DISPLAYMANAGER_H_
#define _GUI_SCREEN_DISPLAYMANAGER_H_

#include <map>
#include <string>
#include <vector>

#include <QtCore/QTimer>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>
#if defined(USE_WEBENGINEVIEW)
#include <QtWebEngineWidgets/QWebEngineView>
#define QPAGE QWebEnginePage
#define QVIEW QWebEngineView
#else
#include <QtWebKitWidgets/QWebView>
#define QPAGE QWebPage
#define QVIEW QWebView
#endif

#include "IconLoader.h"
#include "gui.h"
#include "runtime-config.h"

class QResizeEvent;
class QVBoxLayout;

namespace Secretary::Madt::Gui {
	class CustomPage : public QPAGE
	{
	  public:
		CustomPage(QObject* parent = 0);
#if defined(USE_WEBENGINEVIEW)
		virtual void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
		                                      const QString&                 message,
		                                      int                            lineNumber,
		                                      const QString&                 sourceID) override;
#else
		virtual void
		javaScriptConsoleMessage(const QString& message, int lineNumber, const QString& sourceID) override;
#endif
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
		std::string iconUrl;
		std::string uuid;
		int         index;
		int         logicalPos;
		int         preferredPos;
		int         flags;
		QIcon       icon;
		QTimer*     blinkTimer;
		bool        blinkVisible;
	};

	struct ShortcutEntry
	{
		std::string shortcutId;
		std::string iconUrl;
		std::string url;
		int         shortcutPos;
		int         preferredPos;
		int         flags;
		QIcon       icon;
	};

	class Browser : public QWidget
	{
		Q_OBJECT
	  public:
		explicit Browser(const RuntimeConfig& runtimeConfig, QWidget* parent = nullptr);
		~Browser();
		QWidget* currentWidget() const;
		void     ApplySettings(const SettingsState& settings);

	  signals:
		void signalNewWebTab(const std::string& url,
		                     const std::string& iconUrl,
		                     int                preferredPos,
		                     int                flags,
		                     const std::string& uuid,
		                     CmdResponse*       resp);
		void signalActivateTab(const std::string& uuid, CmdResponse* resp);
		void signalNavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
		void signalGetTabMap(CmdResponse* resp);
		void signalKillTab(const std::string& uuid, CmdResponse* resp, bool forceDestroy);
		void signalBlinkTab(const std::string& uuid, CmdResponse* resp);
		void signalNewShortcut(const std::string& url,
		                       const std::string& iconUrl,
		                       int                preferredPos,
		                       int                flags,
		                       const std::string& shortcutId,
		                       CmdResponse*       resp);
		void signalKillShortcut(const std::string& shortcutId, CmdResponse* resp);
		void signalGetShortcuts(CmdResponse* resp);

	  public slots:
		void onNewWebTab(const std::string& url,
		                 const std::string& iconUrl,
		                 int                preferredPos,
		                 int                flags,
		                 const std::string& uuid,
		                 CmdResponse*       resp);
		void onActivateTab(const std::string& uuid, CmdResponse* resp);
		void onNavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
		void onGetTabMap(CmdResponse* resp);
		void onKillTab(const std::string& uuid, CmdResponse* resp, bool forceDestroy);
		void onBlinkTab(const std::string& uuid, CmdResponse* resp);
		void onNewShortcut(const std::string& url,
		                   const std::string& iconUrl,
		                   int                preferredPos,
		                   int                flags,
		                   const std::string& shortcutId,
		                   CmdResponse*       resp);
		void onKillShortcut(const std::string& shortcutId, CmdResponse* resp);
		void onGetShortcuts(CmdResponse* resp);

	  public:
		void NewWebTab(const std::string& url,
		               const std::string& iconUrl,
		               int                preferredPos,
		               int                flags,
		               const std::string& uuid,
		               CmdResponse*       resp);
		void ActivateTab(const std::string& uuid, CmdResponse* resp);
		void NavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp);
		void GetTabMap(CmdResponse* resp);
		void KillTab(const std::string& uuid, CmdResponse* resp, bool forceDestroy);
		void BlinkTab(const std::string& uuid, CmdResponse* resp);
		void NewShortcut(const std::string& url,
		                 const std::string& iconUrl,
		                 int                preferredPos,
		                 int                flags,
		                 const std::string& shortcutId,
		                 CmdResponse*       resp);
		void KillShortcut(const std::string& shortcutId, CmdResponse* resp);
		void GetShortcuts(CmdResponse* resp);

	  private:
		void resizeEvent(QResizeEvent* event) override;
		void applyRuntimeConfig();
		void applyPageIcon(BrowserPage* page);
		void stopBlink(BrowserPage* page);
		void updatePageIndex(BrowserPage* page);
		void applyPageLabel(BrowserPage* page);
		void syncTabOrder();
		bool isExtraPage(const BrowserPage* page) const;
		bool hasExtraPage() const;
		int  normalPageCount() const;
		void positionExtraZoneOverlay();
		void showExtraZoneOverlay();
		void hideExtraZoneOverlay();
		void updateExtraZoneVisibility();
		bool allocateLogicalPosition(BrowserPage* page);
		bool allocateAbsolutePosition(BrowserPage* page);
		bool allocateZonePosition(BrowserPage* page, int preferredPos);
		bool configureView(BrowserPage* page);
		void applySettingsToView(WebView* view);
		std::vector<BrowserPage*> pagesInDisplayOrder() const;
		std::vector<int>          freePositionsInRange(int start, int end) const;
		std::vector<BrowserPage*> pagesInRange(int start, int end) const;
		void                      setupShortcutUi();
		void                      updateShortcutLauncherState();
		void                      rebuildShortcutPopup();
		void                      toggleShortcutPopup();
		void                      activateShortcut(const std::string& shortcutId);
		bool                      allocateShortcutPosition(ShortcutEntry* shortcut);
		std::vector<ShortcutEntry*> shortcutsInDisplayOrder() const;
		std::vector<int>            freeShortcutPositions() const;
		int                         maxShortcutCount() const;

		RuntimeConfig                    runtimeConfig;
		SettingsState                    currentSettings;
		IconLoader                       iconLoader;
		std::map<std::string, BrowserPage*> list;
		std::map<std::string, ShortcutEntry*> shortcuts;
		QTabWidget*                      tabs             = nullptr;
		QFrame*                          extraFrame       = nullptr;
		QVBoxLayout*                     extraLayout      = nullptr;
		QToolButton*                     shortcutLauncher = nullptr;
		QFrame*                          shortcutPopup    = nullptr;
		QGridLayout*                     shortcutLayout   = nullptr;
	};
}
#endif
