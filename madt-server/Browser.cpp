#include <algorithm>
#include <memory>

#include <QtCore/QPoint>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtCore/QtGlobal>
#include <QtGui/QDesktopServices>
#include <QtGui/QColor>
#include <QtGui/QIcon>
#include <QtGui/QPalette>
#include <QtGui/QPixmap>
#include <QtCore/QSize>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QVBoxLayout>
#if !defined(USE_WEBENGINEVIEW)
#include <QtWebKit/QWebSettings>
#include <QtWebKitWidgets/QWebFrame>
#endif

#include "Browser.h"
#include <loghelper/log.h>

namespace Secretary::Madt::Gui {
	namespace {
		constexpr int BLINK_RATE_MS       = 500;
		constexpr int FLAG_APPEND_ID      = 1;
		constexpr int FLAG_NO_SCROLLBARS  = 8;
		constexpr int POS_ANY             = -1;
		constexpr int POS_BEGINNING       = -2;
		constexpr int POS_MIDDLE          = -3;
		constexpr int POS_END             = -4;
		constexpr int POS_EXTRA           = -10;
		QIcon makeBlankIcon(const QSize& size)
		{
			QPixmap pixmap(size);
			pixmap.fill(Qt::transparent);
			return QIcon(pixmap);
		}

		bool pageUrlLess(const BrowserPage* left, const BrowserPage* right)
		{
			if (left->url != right->url) {
				return left->url < right->url;
			}
			return left->uuid < right->uuid;
		}

		bool shortcutPosLess(const ShortcutEntry* left, const ShortcutEntry* right)
		{
			if (left->shortcutPos != right->shortcutPos) {
				return left->shortcutPos < right->shortcutPos;
			}
			return left->shortcutId < right->shortcutId;
		}

		int zoneStart(int preferredPos)
		{
			const int third = MAX_TABS / 3;
			switch (preferredPos) {
				case POS_BEGINNING:
					return 0;
				case POS_MIDDLE:
					return third;
				case POS_END:
					return third * 2;
				default:
					return -1;
			}
		}

		int zoneEnd(int preferredPos)
		{
			const int third = MAX_TABS / 3;
			switch (preferredPos) {
				case POS_BEGINNING:
					return third - 1;
				case POS_MIDDLE:
					return (third * 2) - 1;
				case POS_END:
					return MAX_TABS - 1;
				default:
					return -1;
			}
		}

		QString extraZoneBorderStyle(ExtraZonePlacement placement)
		{
			switch (placement) {
				case ExtraZonePlacement::Bottom:
					return QStringLiteral("border-top: 1px solid #808080;");
				case ExtraZonePlacement::Free:
					return QStringLiteral("border: 1px solid #808080;");
				case ExtraZonePlacement::Top:
					return QStringLiteral("border-bottom: 1px solid #808080;");
			}
			return QStringLiteral("border-bottom: 1px solid #808080;");
		}
	}

	CustomPage::CustomPage(QObject* parent)
	  : QPAGE(parent)
	{
	}

#if defined(USE_WEBENGINEVIEW)
	void CustomPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
	                                          const QString&                 message,
	                                          int                            lineNumber,
	                                          const QString&                 sourceID)
	{
		(void)level;
		TLOG("javascript %s:%d %s",
		     qPrintable(sourceID),
		     lineNumber,
		     qPrintable(message));
	}
#else
	void CustomPage::javaScriptConsoleMessage(const QString& message,
	                                          int            lineNumber,
	                                          const QString& sourceID)
	{
		TLOG("javascript %s:%d %s",
		     qPrintable(sourceID),
		     lineNumber,
		     qPrintable(message));
	}
#endif

	WebView::WebView(QWidget* parent)
	  : QVIEW(parent)
	{
		CustomPage* webPage = new CustomPage(this);
		setContextMenuPolicy(Qt::NoContextMenu);
		setPage(webPage);
#if !defined(USE_WEBENGINEVIEW)
		if (page() != nullptr && page()->settings() != nullptr) {
			page()->settings()->setAttribute(QWebSettings::JavascriptCanOpenWindows, true);
		}
#endif
	}

	WebView::~WebView() {}

	Browser::Browser(const RuntimeConfig& runtimeConfig, QWidget* parent)
	  : QWidget(parent)
	  , runtimeConfig(runtimeConfig)
	  , currentSettings(runtimeConfig.settings)
	  , iconLoader(this)
	{
		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);

		extraFrame = new QFrame(this);
		extraFrame->setObjectName(QStringLiteral("extraZone"));
		extraFrame->setStyleSheet(QStringLiteral("#extraZone { background-color: #ffffff; %1 }")
		                            .arg(extraZoneBorderStyle(runtimeConfig.extraZonePlacement)));

		extraLayout = new QVBoxLayout(extraFrame);
		extraLayout->setContentsMargins(0, 0, 0, 0);
		extraLayout->setSpacing(0);

		tabs = new QTabWidget(this);
		layout->addWidget(tabs, 1);
		extraFrame->hide();
		positionExtraZoneOverlay();

		applyRuntimeConfig();
		setupShortcutUi();
		updateExtraZoneVisibility();
		connect(this,
		        SIGNAL(signalNewWebTab(const std::string&,
		                               const std::string&,
		                               int,
		                               int,
		                               const std::string&,
		                               CmdResponse*)),
		        this,
		        SLOT(onNewWebTab(const std::string&,
		                         const std::string&,
		                         int,
		                         int,
		                         const std::string&,
		                         CmdResponse*)));
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
		        SIGNAL(signalKillTab(const std::string&, CmdResponse*, bool)),
		        this,
		        SLOT(onKillTab(const std::string&, CmdResponse*, bool)));
		connect(this,
		        SIGNAL(signalBlinkTab(const std::string&, CmdResponse*)),
		        this,
		        SLOT(onBlinkTab(const std::string&, CmdResponse*)));
		connect(this,
		        SIGNAL(signalNewShortcut(const std::string&,
		                                 const std::string&,
		                                 int,
		                                 int,
		                                 const std::string&,
		                                 CmdResponse*)),
		        this,
		        SLOT(onNewShortcut(const std::string&,
		                           const std::string&,
		                           int,
		                           int,
		                           const std::string&,
		                           CmdResponse*)));
		connect(this,
		        SIGNAL(signalKillShortcut(const std::string&, CmdResponse*)),
		        this,
		        SLOT(onKillShortcut(const std::string&, CmdResponse*)));
		connect(this,
		        SIGNAL(signalGetShortcuts(CmdResponse*)),
		        this,
		        SLOT(onGetShortcuts(CmdResponse*)));
	}

	Browser::~Browser()
	{
		for (auto& entry : list) {
			delete entry.second;
		}
		for (auto& entry : shortcuts) {
			delete entry.second;
		}
	}

	QWidget* Browser::currentWidget() const
	{
		return tabs != nullptr ? tabs->currentWidget() : nullptr;
	}

	void Browser::ApplySettings(const SettingsState& settings)
	{
		currentSettings = settings;
		for (const auto& entry : list) {
			applySettingsToView(entry.second->view);
		}
	}

	void Browser::resizeEvent(QResizeEvent* event)
	{
		QWidget::resizeEvent(event);
		positionExtraZoneOverlay();
	}

	void Browser::applyRuntimeConfig()
	{
		if (tabs == nullptr) {
			return;
		}

		QTabBar* bar = tabs->tabBar();
		bar->setVisible(runtimeConfig.tabBarVisible);
		bar->setIconSize(QSize(runtimeConfig.tabBarWidth, runtimeConfig.tabBarHeight));
		bar->setExpanding(false);
		bar->setUsesScrollButtons(runtimeConfig.tabBarUseScrollButtons);

		switch (runtimeConfig.tabBarEdge) {
			case TabBarEdge::Top:
				tabs->setTabPosition(QTabWidget::North);
				break;
			case TabBarEdge::Bottom:
				tabs->setTabPosition(QTabWidget::South);
				break;
			case TabBarEdge::Left:
				tabs->setTabPosition(QTabWidget::West);
				break;
			case TabBarEdge::Right:
				tabs->setTabPosition(QTabWidget::East);
				break;
		}

		tabs->setStyleSheet(QStringLiteral("QTabBar::tab { width: %1px; height: %2px; }")
		                      .arg(runtimeConfig.tabBarWidth)
		                      .arg(runtimeConfig.tabBarHeight));
	}

	void Browser::setupShortcutUi()
	{
		shortcutLauncher = new QToolButton(tabs);
		shortcutLauncher->setText(QString::fromStdString(runtimeConfig.shortcutLauncherLabel));
		shortcutLauncher->setEnabled(false);
		shortcutLauncher->setToolButtonStyle(Qt::ToolButtonTextOnly);
		shortcutLauncher->setAutoRaise(false);
		if (tabs != nullptr) {
			tabs->setCornerWidget(shortcutLauncher,
			                      runtimeConfig.shortcutLauncherCorner == ShortcutLauncherCorner::TopLeft
			                        ? Qt::TopLeftCorner
			                        : Qt::TopRightCorner);
		}
		shortcutLauncher->setVisible(runtimeConfig.shortcutLauncherVisible &&
		                             runtimeConfig.shortcutsEnabled);

		shortcutPopup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
		shortcutPopup->setObjectName(QStringLiteral("shortcutPopup"));
		shortcutPopup->setStyleSheet(QStringLiteral(
		  "#shortcutPopup { background-color: #ffffff; border: 1px solid #808080; border-radius: 6px; }"
		  "#shortcutPopup QToolButton { min-width: 96px; min-height: 96px; padding: 6px; }"
		  "#shortcutPopup QLabel { color: #202020; font-weight: 600; }"));

		auto* popupLayout = new QVBoxLayout(shortcutPopup);
		popupLayout->setContentsMargins(10, 10, 10, 10);
		popupLayout->setSpacing(8);

		auto* title = new QLabel(QString::fromStdString(runtimeConfig.shortcutPopupTitle), shortcutPopup);
		popupLayout->addWidget(title);

		shortcutLayout = new QGridLayout();
		shortcutLayout->setContentsMargins(0, 0, 0, 0);
		shortcutLayout->setHorizontalSpacing(8);
		shortcutLayout->setVerticalSpacing(8);
		popupLayout->addLayout(shortcutLayout);

		connect(shortcutLauncher, &QToolButton::clicked, this, [this]() { toggleShortcutPopup(); });
	}

	std::vector<BrowserPage*> Browser::pagesInDisplayOrder() const
	{
		std::vector<BrowserPage*> pages;
		pages.reserve(list.size());
		for (const auto& entry : list) {
			if (!isExtraPage(entry.second)) {
				pages.push_back(entry.second);
			}
		}
		std::sort(pages.begin(), pages.end(), [](const BrowserPage* left, const BrowserPage* right) {
			if (left->logicalPos != right->logicalPos) {
				return left->logicalPos < right->logicalPos;
			}
			return left->uuid < right->uuid;
		});
		return pages;
	}

	std::vector<int> Browser::freePositionsInRange(int start, int end) const
	{
		std::vector<bool> occupied(MAX_TABS, false);
		for (const auto& entry : list) {
			if (entry.second->logicalPos >= 0 && entry.second->logicalPos < MAX_TABS) {
				occupied[entry.second->logicalPos] = true;
			}
		}

		std::vector<int> freePositions;
		for (int pos = start; pos <= end; ++pos) {
			if (!occupied[pos]) {
				freePositions.push_back(pos);
			}
		}
		return freePositions;
	}

	std::vector<BrowserPage*> Browser::pagesInRange(int start, int end) const
	{
		std::vector<BrowserPage*> pages;
		for (const auto& entry : list) {
			BrowserPage* page = entry.second;
			if (page->logicalPos >= start && page->logicalPos <= end) {
				pages.push_back(page);
			}
		}
		return pages;
	}

	std::vector<ShortcutEntry*> Browser::shortcutsInDisplayOrder() const
	{
		std::vector<ShortcutEntry*> ordered;
		ordered.reserve(shortcuts.size());
		for (const auto& entry : shortcuts) {
			ordered.push_back(entry.second);
		}
		std::sort(ordered.begin(), ordered.end(), shortcutPosLess);
		return ordered;
	}

	std::vector<int> Browser::freeShortcutPositions() const
	{
		std::vector<bool> occupied(static_cast<std::size_t>(maxShortcutCount()), false);
		for (const auto& entry : shortcuts) {
			const int pos = entry.second->shortcutPos;
			if (pos >= 0 && pos < maxShortcutCount()) {
				occupied[static_cast<std::size_t>(pos)] = true;
			}
		}

		std::vector<int> freePositions;
		for (int pos = 0; pos < maxShortcutCount(); ++pos) {
			if (!occupied[static_cast<std::size_t>(pos)]) {
				freePositions.push_back(pos);
			}
		}
		return freePositions;
	}

	int Browser::maxShortcutCount() const
	{
		return std::max(1, runtimeConfig.shortcutMaxCount);
	}

	bool Browser::allocateShortcutPosition(ShortcutEntry* shortcut)
	{
		if (shortcut == nullptr) {
			return false;
		}

		auto freePositions = freeShortcutPositions();
		if (freePositions.empty()) {
			return false;
		}

		if (shortcut->preferredPos >= 0 && shortcut->preferredPos < maxShortcutCount()) {
			for (int pos : freePositions) {
				if (pos == shortcut->preferredPos) {
					shortcut->shortcutPos = pos;
					return true;
				}
			}
			return false;
		}

		shortcut->shortcutPos = freePositions.front();
		return true;
	}

	void Browser::updateShortcutLauncherState()
	{
		if (shortcutLauncher == nullptr) {
			return;
		}

		const bool hasShortcuts = !shortcuts.empty();
		shortcutLauncher->setEnabled(hasShortcuts);
		shortcutLauncher->setVisible(runtimeConfig.shortcutLauncherVisible &&
		                             runtimeConfig.shortcutsEnabled);
		const QString baseLabel = QString::fromStdString(runtimeConfig.shortcutLauncherLabel);
		shortcutLauncher->setText(hasShortcuts ? QStringLiteral("%1 (%2)").arg(baseLabel).arg(shortcuts.size())
		                                       : baseLabel);
		if (!hasShortcuts && shortcutPopup != nullptr) {
			shortcutPopup->hide();
		}
	}

	void Browser::rebuildShortcutPopup()
	{
		if (shortcutLayout == nullptr || shortcutPopup == nullptr) {
			return;
		}

		while (QLayoutItem* item = shortcutLayout->takeAt(0)) {
			if (item->widget() != nullptr) {
				item->widget()->deleteLater();
			}
			delete item;
		}

		auto ordered = shortcutsInDisplayOrder();
		for (std::size_t i = 0; i < ordered.size(); ++i) {
			ShortcutEntry* shortcut = ordered[i];
			auto* button = new QToolButton(shortcutPopup);
			button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
			button->setIcon(shortcut->icon);
			button->setIconSize(QSize(runtimeConfig.shortcutIconWidth, runtimeConfig.shortcutIconHeight));
			button->setText(runtimeConfig.tabBarShowLabels
			                  ? QStringLiteral("S%1").arg(shortcut->shortcutPos + 1)
			                  : QString());
			button->setToolTip(runtimeConfig.tabBarShowTooltips
			                     ? QString::fromStdString(shortcut->url)
			                     : QString());
			connect(button, &QToolButton::clicked, this, [this, shortcutId = shortcut->shortcutId]() {
				activateShortcut(shortcutId);
			});
			const int columns = std::max(1, runtimeConfig.shortcutPopupColumns);
			shortcutLayout->addWidget(button,
			                          static_cast<int>(i / static_cast<std::size_t>(columns)),
			                          static_cast<int>(i % static_cast<std::size_t>(columns)));
		}

		shortcutPopup->adjustSize();
	}

	void Browser::toggleShortcutPopup()
	{
		if (shortcutLauncher == nullptr || shortcutPopup == nullptr || shortcuts.empty() ||
		    !runtimeConfig.shortcutsEnabled || !runtimeConfig.shortcutLauncherVisible) {
			return;
		}

		if (shortcutPopup->isVisible()) {
			shortcutPopup->hide();
			return;
		}

		rebuildShortcutPopup();
		const QPoint globalAnchor =
		  shortcutLauncher->mapToGlobal(QPoint(0, shortcutLauncher->height()));
		shortcutPopup->move(globalAnchor);
		shortcutPopup->show();
		shortcutPopup->raise();
	}

	void Browser::activateShortcut(const std::string& shortcutId)
	{
		const auto it = shortcuts.find(shortcutId);
		if (it == shortcuts.end()) {
			return;
		}

		const QUrl url(QString::fromStdString(it->second->url));
		if (!QDesktopServices::openUrl(url)) {
			ELOG("Failed to open shortcut URL %s", it->second->url.c_str());
		}
		if (shortcutPopup != nullptr && runtimeConfig.shortcutAutoClose) {
			shortcutPopup->hide();
		}
	}

	void Browser::updatePageIndex(BrowserPage* page)
	{
		if (page != nullptr) {
			page->index = isExtraPage(page) || tabs == nullptr ? -1 : tabs->indexOf(page->view);
		}
	}

	void Browser::applyPageLabel(BrowserPage* page)
	{
		if (page == nullptr) {
			return;
		}

		updatePageIndex(page);
		if (page->index < 0) {
			return;
		}

		tabs->setTabText(page->index,
		                 runtimeConfig.tabBarShowLabels ? QString::number(page->logicalPos + 1)
		                                                : QString());
		tabs->setTabToolTip(page->index,
		                    runtimeConfig.tabBarShowTooltips ? QString::fromStdString(page->url)
		                                                     : QString());
	}

	void Browser::applyPageIcon(BrowserPage* page)
	{
		if (page == nullptr) {
			return;
		}

		updatePageIndex(page);
		if (page->index < 0) {
			return;
		}

		tabs->setTabIcon(page->index, page->icon);
		tabs->tabBar()->setTabTextColor(page->index, palette().color(QPalette::WindowText));
	}

	void Browser::stopBlink(BrowserPage* page)
	{
		if (page == nullptr || page->blinkTimer == nullptr) {
			return;
		}

		page->blinkTimer->stop();
		page->blinkVisible = true;
		applyPageIcon(page);
	}

	bool Browser::isExtraPage(const BrowserPage* page) const
	{
		return page != nullptr && page->preferredPos == POS_EXTRA;
	}

	bool Browser::hasExtraPage() const
	{
		for (const auto& entry : list) {
			if (isExtraPage(entry.second)) {
				return true;
			}
		}
		return false;
	}

	int Browser::normalPageCount() const
	{
		int count = 0;
		for (const auto& entry : list) {
			if (!isExtraPage(entry.second)) {
				++count;
			}
		}
		return count;
	}

	void Browser::positionExtraZoneOverlay()
	{
		if (extraFrame == nullptr) {
			return;
		}

		switch (runtimeConfig.extraZonePlacement) {
			case ExtraZonePlacement::Bottom: {
				const int overlayHeight = std::max(1, runtimeConfig.extraZoneHeight);
				extraFrame->setGeometry(0,
				                        std::max(0, height() - overlayHeight),
				                        width(),
				                        overlayHeight);
				break;
			}
			case ExtraZonePlacement::Free:
				extraFrame->setGeometry(runtimeConfig.extraZoneRect.x,
				                        runtimeConfig.extraZoneRect.y,
				                        std::max(1, runtimeConfig.extraZoneRect.width),
				                        std::max(1, runtimeConfig.extraZoneRect.height));
				break;
			case ExtraZonePlacement::Top: {
				const int overlayHeight = std::max(1, runtimeConfig.extraZoneHeight);
				extraFrame->setGeometry(0, 0, width(), overlayHeight);
				break;
			}
		}
	}

	void Browser::showExtraZoneOverlay()
	{
		if (extraFrame == nullptr || !hasExtraPage()) {
			return;
		}

		positionExtraZoneOverlay();
		extraFrame->show();
		extraFrame->raise();
		extraFrame->update();
	}

	void Browser::hideExtraZoneOverlay()
	{
		if (extraFrame != nullptr) {
			extraFrame->hide();
		}
	}

	void Browser::updateExtraZoneVisibility()
	{
		if (extraFrame == nullptr) {
			return;
		}

		if (!hasExtraPage()) {
			hideExtraZoneOverlay();
		} else if (extraFrame->isVisible()) {
			showExtraZoneOverlay();
		} else {
			positionExtraZoneOverlay();
		}
	}

	void Browser::syncTabOrder()
	{
		auto pages = pagesInDisplayOrder();
		for (std::size_t i = 0; i < pages.size(); ++i) {
			BrowserPage* page = pages[i];
			const int currentIndex = tabs->indexOf(page->view);
			const int targetIndex  = static_cast<int>(i);
			if (currentIndex >= 0 && currentIndex != targetIndex) {
				tabs->tabBar()->moveTab(currentIndex, targetIndex);
			}
		}
		for (BrowserPage* page : pages) {
			updatePageIndex(page);
			applyPageLabel(page);
			if (page->blinkVisible) {
				applyPageIcon(page);
			}
		}
	}

	bool Browser::allocateAbsolutePosition(BrowserPage* page)
	{
		if (page->preferredPos < 0 || page->preferredPos >= MAX_TABS) {
			return false;
		}
		for (const auto& entry : list) {
			if (entry.second != page && entry.second->logicalPos == page->preferredPos) {
				return false;
			}
		}
		page->logicalPos = page->preferredPos;
		return true;
	}

	bool Browser::allocateZonePosition(BrowserPage* page, int preferredPos)
	{
		const int start = zoneStart(preferredPos);
		const int end   = zoneEnd(preferredPos);
		if (start < 0 || end < start) {
			return false;
		}

		std::vector<BrowserPage*> pages;
		std::vector<int>          candidatePositions;
		for (const auto& entry : list) {
			BrowserPage* current = entry.second;
			if (current == page) {
				continue;
			}
			if (current->preferredPos == preferredPos && current->logicalPos >= start &&
			    current->logicalPos <= end) {
				pages.push_back(current);
				candidatePositions.push_back(current->logicalPos);
			}
		}
		auto freePositions = freePositionsInRange(start, end);
		if (page->logicalPos >= start && page->logicalPos <= end) {
			candidatePositions.push_back(page->logicalPos);
		}
		candidatePositions.insert(candidatePositions.end(), freePositions.begin(), freePositions.end());
		if (candidatePositions.empty()) {
			return false;
		}
		std::sort(candidatePositions.begin(), candidatePositions.end());

		pages.push_back(page);
		std::sort(pages.begin(), pages.end(), pageUrlLess);

		if (preferredPos == POS_END) {
			std::vector<int> allocatedPositions(candidatePositions.end() - pages.size(),
			                                   candidatePositions.end());
			std::sort(allocatedPositions.begin(), allocatedPositions.end());
			for (std::size_t i = 0; i < pages.size(); ++i) {
				pages[i]->logicalPos = allocatedPositions[i];
			}
			return true;
		}

		for (std::size_t i = 0; i < pages.size(); ++i) {
			pages[i]->logicalPos = candidatePositions[i];
		}
		return true;
	}

	bool Browser::allocateLogicalPosition(BrowserPage* page)
	{
		if (!isExtraPage(page) && normalPageCount() > MAX_TABS) {
			return false;
		}

		switch (page->preferredPos) {
			case POS_ANY: {
				auto freePositions = freePositionsInRange(0, MAX_TABS - 1);
				if (freePositions.empty()) {
					return false;
				}
				page->logicalPos = freePositions.front();
				return true;
			}
			case POS_BEGINNING:
			case POS_MIDDLE:
			case POS_END:
				return allocateZonePosition(page, page->preferredPos);
			case POS_EXTRA:
				page->logicalPos = POS_EXTRA;
				return true;
			default:
				return allocateAbsolutePosition(page);
		}
	}

	bool Browser::configureView(BrowserPage* page)
	{
		if (page == nullptr) {
			return false;
		}

#if !defined(USE_WEBENGINEVIEW)
		if ((page->flags & FLAG_NO_SCROLLBARS) != 0) {
			if (page->view->page() != nullptr && page->view->page()->mainFrame() != nullptr) {
				page->view->page()->mainFrame()->setScrollBarPolicy(Qt::Horizontal,
				                                                    Qt::ScrollBarAlwaysOff);
				page->view->page()->mainFrame()->setScrollBarPolicy(Qt::Vertical,
				                                                    Qt::ScrollBarAlwaysOff);
			}
		}
#else
		(void)FLAG_NO_SCROLLBARS;
#endif

		connect(page->view, &QVIEW::loadFinished, this, [this, view = page->view](bool ok) {
			if (ok) {
				applySettingsToView(view);
			}
		});

		return true;
	}

	void Browser::applySettingsToView(WebView* view)
	{
		if (view == nullptr || view->page() == nullptr) {
			return;
		}

		nlohmann::json settings = settingsToJson(currentSettings);
		settings["volumePercent"] = settingsVolumeToPercent(currentSettings.volume);
		settings["volumeScalar"]  = settingsVolumeToScalar(currentSettings.volume);

		const QString script = QString::fromStdString(
		  "(function(){"
		  "window.madtSettings=" + settings.dump() + ";"
		  "var madtEvent;"
		  "try {"
		  "  madtEvent = new CustomEvent('madt-settings-changed', {detail: window.madtSettings});"
		  "} catch (e) {"
		  "  madtEvent = document.createEvent('CustomEvent');"
		  "  madtEvent.initCustomEvent('madt-settings-changed', false, false, window.madtSettings);"
		  "}"
		  "window.dispatchEvent(madtEvent);"
		  "})();");

#if defined(USE_WEBENGINEVIEW)
		view->page()->runJavaScript(script);
#else
		if (view->page()->mainFrame() != nullptr) {
			view->page()->mainFrame()->evaluateJavaScript(script);
		}
#endif
	}

	void Browser::onNewWebTab(const std::string& url,
	                          const std::string& iconUrl,
	                          int                preferredPos,
	                          int                flags,
	                          const std::string& uuid,
	                          CmdResponse*       resp)
	{
		CmdResponse::ResultCode result = CmdResponse::ResultCode::EXEC_ERROR;
		const bool              extraPage = preferredPos == POS_EXTRA;
		if (!extraPage && normalPageCount() >= MAX_TABS) {
			ELOG("No free MADT slot for tab %s", uuid.c_str());
		} else if (extraPage && hasExtraPage()) {
			ELOG("MADT extra zone is already occupied for tab %s", uuid.c_str());
		} else {
			QWidget* viewParent = extraPage ? static_cast<QWidget*>(extraFrame)
			                                : static_cast<QWidget*>(tabs);
			WebView* view = new WebView(viewParent);

			BrowserPage* page  = new BrowserPage;
			page->url          = url;
			page->iconUrl      = iconUrl;
			page->uuid         = uuid;
			page->view         = view;
			page->preferredPos = preferredPos;
			page->flags        = flags;
			page->index        = -1;
			page->logicalPos   = -1;
			page->blinkTimer   = new QTimer(this);
			page->blinkVisible = true;

			list[uuid] = page;
			if (!allocateLogicalPosition(page) || !configureView(page)) {
				delete page->blinkTimer;
				delete page->view;
				delete page;
				list.erase(uuid);
			} else {
				view->setUrl(QUrl(QString::fromStdString(url)));
				page->blinkTimer->setInterval(BLINK_RATE_MS);
				connect(page->blinkTimer, &QTimer::timeout, this, [this, uuid]() {
					const auto it = list.find(uuid);
					if (it == list.end()) {
						return;
					}

					BrowserPage* page = it->second;
					if (isExtraPage(page)) {
						page->blinkTimer->stop();
						return;
					}

					updatePageIndex(page);
					if (page->index < 0) {
						page->blinkTimer->stop();
						return;
					}

					page->blinkVisible = !page->blinkVisible;
					if (page->blinkVisible) {
						tabs->setTabIcon(page->index, page->icon);
						tabs->tabBar()->setTabTextColor(page->index, palette().color(QPalette::WindowText));
					} else {
						tabs->setTabIcon(page->index, makeBlankIcon(tabs->tabBar()->iconSize()));
						tabs->tabBar()->setTabTextColor(page->index, QColor(Qt::red));
					}
				});

				if (extraPage) {
					extraLayout->addWidget(view);
					updateExtraZoneVisibility();
				} else {
					page->index = tabs->addTab(view, QString());
					syncTabOrder();
					const bool firstTab = (normalPageCount() == 1);
					if (firstTab) {
						const int targetIndex = tabs->indexOf(view);
						if (targetIndex >= 0) {
							page->index = targetIndex;
							tabs->setCurrentIndex(targetIndex);
						}
						view->show();
						view->raise();
						view->update();
					} else {
						view->hide();
					}
				}

				if (!iconUrl.empty()) {
					iconLoader.loadIcon(QUrl(QString::fromStdString(iconUrl)),
					                    [this, uuid](const QIcon& icon) {
						                    const auto it = list.find(uuid);
						                    if (it != list.end()) {
							                    BrowserPage* page = it->second;
							                    page->icon        = icon;
							                    if (page->blinkVisible) {
								                    applyPageIcon(page);
							                    }
						                    }
					                    });
				}

				ILOG("Create tab %s url %s (%s) pos=%d flags=%d",
				     uuid.c_str(),
				     list[uuid]->view->url().toString().toStdString().c_str(),
				     url.c_str(),
				     page->logicalPos,
				     flags);
				result = CmdResponse::ResultCode::OK;
			}
		}

		if (resp != nullptr) {
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->result = result;
			resp->ready  = true;
			resp->cv.notify_one();
		}
	}

	void Browser::onActivateTab(const std::string& uuid, CmdResponse* resp)
	{
		if (resp) {
			try {
				ILOG("Activate %s %d", uuid.c_str(), list.count(uuid));
				if (list.count(uuid) > 0) {
					BrowserPage* page         = list[uuid];
					WebView*      view         = page->view;
					ILOG("url %s", view->url().toString().toStdString().c_str());
					if (isExtraPage(page)) {
						showExtraZoneOverlay();
					} else {
						WebView*  previousView = qobject_cast<WebView*>(currentWidget());
						const int targetIndex  = tabs->indexOf(view);
						if (previousView != nullptr && previousView != view)
							previousView->hide();
						if (targetIndex >= 0) {
							page->index = targetIndex;
							tabs->setCurrentIndex(targetIndex);
						}
						view->show();
						view->raise();
						view->update();
					}
					stopBlink(page);
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
					applyPageLabel(page);
					if (isExtraPage(page)) {
						updateExtraZoneVisibility();
					} else if ((page->preferredPos == POS_BEGINNING || page->preferredPos == POS_MIDDLE ||
					            page->preferredPos == POS_END) &&
					           allocateZonePosition(page, page->preferredPos)) {
						syncTabOrder();
					}
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
					BrowserPage* page = entry.second;
					if (page->logicalPos >= 0 && page->logicalPos < MAX_TABS) {
						tabMap[page->logicalPos] = entry.first;
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

	void Browser::onKillTab(const std::string& uuid, CmdResponse* resp, bool forceDestroy)
	{
		CmdResponse::ResultCode ret;
		try {
			if (list.count(uuid) > 0) {
				BrowserPage* page  = list[uuid];
				stopBlink(page);
				const bool extraPage = isExtraPage(page);
				if (extraPage && !forceDestroy) {
					hideExtraZoneOverlay();
					ret = CmdResponse::ResultCode::OK;
				} else if (extraPage) {
					if (extraLayout != nullptr) {
						extraLayout->removeWidget(page->view);
					}
					delete page->blinkTimer;
					delete page->view;
					delete page;
					list.erase(uuid);
					updateExtraZoneVisibility();
					ret = CmdResponse::ResultCode::OK;
				} else {
					const int index = tabs->indexOf(page->view);
					if (index >= 0) {
						tabs->removeTab(index);
					}
					delete page->blinkTimer;
					delete page->view;
					delete page;
					list.erase(uuid);
					syncTabOrder();
					ret = CmdResponse::ResultCode::OK;
				}
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

	void Browser::onBlinkTab(const std::string& uuid, CmdResponse* resp)
	{
		CmdResponse::ResultCode ret = CmdResponse::ResultCode::EXEC_ERROR;
		try {
			if (list.count(uuid) > 0) {
				BrowserPage* page = list[uuid];
				if (isExtraPage(page)) {
					showExtraZoneOverlay();
					ret = CmdResponse::ResultCode::OK;
				} else {
					updatePageIndex(page);
					if (page->index >= 0) {
						if (currentWidget() != page->view) {
							page->blinkVisible = true;
							page->blinkTimer->start();
						}
						ret = CmdResponse::ResultCode::OK;
					} else {
						ret = CmdResponse::ResultCode::TAB_NOT_FOUND;
					}
				}
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

	void Browser::onNewShortcut(const std::string& url,
	                            const std::string& iconUrl,
	                            int                preferredPos,
	                            int                flags,
	                            const std::string& shortcutId,
	                            CmdResponse*       resp)
	{
		CmdResponse::ResultCode result = CmdResponse::ResultCode::EXEC_ERROR;
		if (!runtimeConfig.shortcutsEnabled) {
			if (resp != nullptr) {
				std::lock_guard<std::mutex> lock(resp->mtx);
				resp->result = CmdResponse::ResultCode::EXEC_ERROR;
				resp->ready  = true;
				resp->cv.notify_one();
			}
			return;
		}

		if (static_cast<int>(shortcuts.size()) < maxShortcutCount()) {
			auto* shortcut        = new ShortcutEntry;
			shortcut->shortcutId  = shortcutId;
			shortcut->iconUrl     = iconUrl;
			shortcut->url         = url;
			shortcut->preferredPos = preferredPos;
			shortcut->shortcutPos = -1;
			shortcut->flags       = flags;

			shortcuts[shortcutId] = shortcut;
			if (!allocateShortcutPosition(shortcut)) {
				shortcuts.erase(shortcutId);
				delete shortcut;
			} else {
				if (!iconUrl.empty()) {
					iconLoader.loadIcon(QUrl(QString::fromStdString(iconUrl)),
					                    [this, shortcutId](const QIcon& icon) {
						                    const auto it = shortcuts.find(shortcutId);
						                    if (it != shortcuts.end()) {
							                    it->second->icon = icon;
							                    rebuildShortcutPopup();
						                    }
					                    });
				}
				updateShortcutLauncherState();
				rebuildShortcutPopup();
				result = CmdResponse::ResultCode::OK;
			}
		}

		if (resp != nullptr) {
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->result = result;
			resp->ready  = true;
			resp->cv.notify_one();
		}
	}

	void Browser::onKillShortcut(const std::string& shortcutId, CmdResponse* resp)
	{
		CmdResponse::ResultCode result = CmdResponse::ResultCode::TAB_NOT_FOUND;
		const auto             it     = shortcuts.find(shortcutId);
		if (it != shortcuts.end()) {
			delete it->second;
			shortcuts.erase(it);
			updateShortcutLauncherState();
			rebuildShortcutPopup();
			result = CmdResponse::ResultCode::OK;
		}

		if (resp != nullptr) {
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->result = result;
			resp->ready  = true;
			resp->cv.notify_one();
		}
	}

	void Browser::onGetShortcuts(CmdResponse* resp)
	{
		if (resp == nullptr) {
			return;
		}

		try {
			nlohmann::json payload = nlohmann::json::array();
			for (ShortcutEntry* shortcut : shortcutsInDisplayOrder()) {
				payload.push_back({
				  { "shortcutId", shortcut->shortcutId },
				  { "shortcutPos", shortcut->shortcutPos },
				  { "flags", shortcut->flags },
				  { "iconUrl", shortcut->iconUrl },
				  { "url", shortcut->url },
				});
			}
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->payload = std::move(payload);
			resp->result  = CmdResponse::ResultCode::OK;
			resp->ready   = true;
			resp->cv.notify_one();
		} catch (...) {
			std::lock_guard<std::mutex> lock(resp->mtx);
			resp->result = CmdResponse::ResultCode::EXEC_ERROR;
			resp->ready  = true;
			resp->cv.notify_one();
		}
	}

	void Browser::NewWebTab(const std::string& url,
	                        const std::string& iconUrl,
	                        int                preferredPos,
	                        int                flags,
	                        const std::string& uuid,
	                        CmdResponse*       resp)
	{
		TLOG("Signal newWebTab");
		emit signalNewWebTab(url, iconUrl, preferredPos, flags, uuid, resp);
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

	void Browser::KillTab(const std::string& uuid, CmdResponse* resp, bool forceDestroy)
	{
		emit signalKillTab(uuid, resp, forceDestroy);
	}

	void Browser::BlinkTab(const std::string& uuid, CmdResponse* resp)
	{
		emit signalBlinkTab(uuid, resp);
	}

	void Browser::NewShortcut(const std::string& url,
	                          const std::string& iconUrl,
	                          int                preferredPos,
	                          int                flags,
	                          const std::string& shortcutId,
	                          CmdResponse*       resp)
	{
		emit signalNewShortcut(url, iconUrl, preferredPos, flags, shortcutId, resp);
	}

	void Browser::KillShortcut(const std::string& shortcutId, CmdResponse* resp)
	{
		emit signalKillShortcut(shortcutId, resp);
	}

	void Browser::GetShortcuts(CmdResponse* resp)
	{
		emit signalGetShortcuts(resp);
	}
}
