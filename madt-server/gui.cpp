#include <memory>
#include <string>

#include "loghelper/log.h"
#include "utils/wdog.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QMetaObject>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include "Browser.h"
#include "gui.h"
#include "runtime-config.h"

namespace Secretary::Madt::Gui {
	namespace {
		constexpr int STARTUP_MIN_VISIBLE_MS = 2000;
		constexpr int WATCHDOG_KICK_MS       = 18 * 1000;

		Browser*      browser         = nullptr;
		QApplication* appInstance     = nullptr;
		QWidget*      mainWindow      = nullptr;
		QWidget*      startupContent  = nullptr;
		QVBoxLayout*  mainLayout      = nullptr;
		std::mutex    startupMutex;
		std::condition_variable startupCv;
		bool startupReady     = false;
		bool startupCancelled = false;
		int requestedExitCode = 0;
		RuntimeConfig runtimeConfig;

		QWidget* createStartupContent(QWidget* parent)
		{
			auto* content = new QWidget(parent);
			content->setObjectName(QStringLiteral("startupContent"));
			content->setStyleSheet(QStringLiteral(
			  "#startupContent { background-color: #ffffff; }"
			  "QLabel { background-color: transparent; }"));

			auto* layout = new QVBoxLayout(content);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->addStretch();

			auto* label = new QLabel(content);
			label->setAlignment(Qt::AlignCenter);

			const QString runtimeLogoPath =
			  QDir::current().absoluteFilePath(QStringLiteral("qt-logo-black-and-white.png"));
			QPixmap startupLogoPixmap;
			if (QFileInfo::exists(runtimeLogoPath)) {
				startupLogoPixmap.load(runtimeLogoPath);
			} else {
				startupLogoPixmap.load(":/madt/qt-logo-black-and-white.png");
			}
			label->setPixmap(startupLogoPixmap);
			layout->addWidget(label, 0, Qt::AlignCenter);
			layout->addStretch();

			return content;
		}

		void setStartupState(bool ready, bool cancelled)
		{
			{
				std::lock_guard<std::mutex> lock(startupMutex);
				startupReady = ready;
				startupCancelled = cancelled;
			}
			startupCv.notify_all();
		}
	} // namespace

	int run(int argc, char* argv[])
	{
		QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);
		QApplication app(argc, argv);
		appInstance = &app;
		requestedExitCode = 0;
		runtimeConfig = loadRuntimeConfig();
		setStartupState(false, false);

		qRegisterMetaType<std::string>("std::string");
		mainWindow = new QWidget();
		mainWindow->setObjectName(QStringLiteral("madtRootWindow"));
		mainWindow->setStyleSheet(QStringLiteral("#madtRootWindow { background-color: #ffffff; }"));
		mainLayout = new QVBoxLayout(mainWindow);
		mainLayout->setContentsMargins(0, 0, 0, 0);
		startupContent = createStartupContent(mainWindow);
		mainLayout->addWidget(startupContent);
		mainWindow->showFullScreen();
		mainWindow->raise();
		mainWindow->activateWindow();
		app.processEvents();
		mainWindow->repaint();
		app.processEvents();

		QTimer watchdogTimer;
		QObject::connect(&watchdogTimer, &QTimer::timeout, &app, []() { WDOGKICK(); });
		watchdogTimer.start(WATCHDOG_KICK_MS);

		QTimer::singleShot(STARTUP_MIN_VISIBLE_MS, &app, [&app]() {
			if (startupCancelled) {
				return;
			}

			browser = new Browser(runtimeConfig);
			if (mainLayout != nullptr) {
				if (startupContent != nullptr) {
					mainLayout->removeWidget(startupContent);
					startupContent->deleteLater();
					startupContent = nullptr;
				}
				mainLayout->addWidget(browser);
			}
			browser->show();
			browser->raise();
			mainWindow->raise();
			mainWindow->activateWindow();
			app.processEvents();
			mainWindow->repaint();
			browser->repaint();
			app.processEvents();
			app.processEvents();
			setStartupState(true, false);
		});

		app.exec();
		WDOGDONE();
		if (mainWindow != nullptr) {
			mainWindow->close();
			mainWindow = nullptr;
		}
		startupContent = nullptr;
		mainLayout = nullptr;
		setStartupState(false, true);
		appInstance = nullptr;
		browser = nullptr;
		return requestedExitCode;
	}

	void stop()
	{
		setStartupState(false, true);
		if (appInstance != nullptr) {
			QMetaObject::invokeMethod(appInstance, "quit", Qt::QueuedConnection);
		}
	}

	void requestExit(int exitCode)
	{
		requestedExitCode = exitCode;
		stop();
	}

	bool waitForStartupReady()
	{
		std::unique_lock<std::mutex> lock(startupMutex);
		startupCv.wait(lock, []() { return startupReady || startupCancelled; });
		return startupReady;
	}

	bool NewWebTab(const std::string& url,
	               const std::string& iconUrl,
	               int                preferredPos,
	               int                flags,
	               const std::string& uuid,
	               CmdResponse*       resp)
	{
		bool ret = false;
		if (browser) {
			browser->NewWebTab(url, iconUrl, preferredPos, flags, uuid, resp);
			ret = true;
		}
		return ret;
	}

	bool KillTab(const std::string& uuid, CmdResponse* resp)
	{
		if (browser)
			browser->KillTab(uuid, resp);
		return (browser != nullptr);
	}

	bool ActivateTab(const std::string& uuid, CmdResponse* resp)
	{
		if (browser)
			browser->ActivateTab(uuid, resp);
		return (browser != nullptr);
	}

	bool NavigateTo(const std::string& uuid, const std::string& url, CmdResponse* resp)
	{
		if (browser)
			browser->NavigateTo(uuid, url, resp);
		return (browser != nullptr);
	}

	bool GetCharacteristics(CmdResponse* resp)
	{
		if (resp == nullptr) {
			return false;
		}

		std::lock_guard<std::mutex> lock(resp->mtx);
		if (browser != nullptr) {
			resp->payload = {
				{ "winWidth", browser->width() },
				{ "winHeight", browser->height() },
			};
			resp->result = CmdResponse::ResultCode::OK;
		} else {
			resp->result = CmdResponse::ResultCode::EXEC_ERROR;
		}
		resp->ready = true;
		resp->cv.notify_one();
		return true;
	}

	bool GetTabMap(CmdResponse* resp)
	{
		if (browser)
			browser->GetTabMap(resp);
		return (browser != nullptr);
	}

	bool BlinkTab(const std::string& uuid, CmdResponse* resp)
	{
		if (browser)
			browser->BlinkTab(uuid, resp);
		return (browser != nullptr);
	}

	bool PlaySound(const std::string& soundId,
	               unsigned int       soundFlags,
	               const std::string& soundFile,
	               const std::string& soundPlayerCommand,
	               CmdResponse*       resp)
	{
		(void)soundId;
		(void)soundFlags;
		if (resp == nullptr || appInstance == nullptr) {
			return false;
		}

		QMetaObject::invokeMethod(
		  appInstance,
		  [resp, soundFile, soundPlayerCommand]() {
			  bool played = false;
			  if (!soundFile.empty() && QFileInfo::exists(QString::fromStdString(soundFile)) &&
			      !soundPlayerCommand.empty()) {
				  played = QProcess::startDetached(QString::fromStdString(soundPlayerCommand),
				                                   { QString::fromStdString(soundFile) });
			  }
			  if (!played) {
				  QApplication::beep();
			  }
			  std::lock_guard<std::mutex> lock(resp->mtx);
			  resp->result = CmdResponse::ResultCode::OK;
			  resp->ready  = true;
			  resp->cv.notify_one();
		  },
		  Qt::QueuedConnection);
		return true;
	}

	bool NewShortcut(const std::string& url,
	                 const std::string& iconUrl,
	                 int                preferredPos,
	                 int                flags,
	                 const std::string& shortcutId,
	                 CmdResponse*       resp)
	{
		if (browser) {
			browser->NewShortcut(url, iconUrl, preferredPos, flags, shortcutId, resp);
		}
		return (browser != nullptr);
	}

	bool KillShortcut(const std::string& shortcutId, CmdResponse* resp)
	{
		if (browser) {
			browser->KillShortcut(shortcutId, resp);
		}
		return (browser != nullptr);
	}

	bool GetShortcuts(CmdResponse* resp)
	{
		if (browser) {
			browser->GetShortcuts(resp);
		}
		return (browser != nullptr);
	}
}
