#include <cerrno>
#include <cstring>
#include <fstream>
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
#include <QtGui/QScreen>
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

		QRect currentScreenGeometry(QWidget* widget)
		{
			if (widget != nullptr && widget->screen() != nullptr) {
				return widget->screen()->availableGeometry();
			}

			if (QGuiApplication::primaryScreen() != nullptr) {
				return QGuiApplication::primaryScreen()->availableGeometry();
			}

			return QRect();
		}

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

		bool readBacklightMaxValue(const RuntimeConfig& config, int& maxValue)
		{
			if (config.backlight.maxValue > 0) {
				maxValue = config.backlight.maxValue;
			}
			if (config.backlight.maxValuePath.empty()) {
				return maxValue > 0;
			}

			std::ifstream ifs(config.backlight.maxValuePath);
			if (!ifs.is_open()) {
				ELOG("Failed to open MADT backlight maxValuePath %s: %s",
				     config.backlight.maxValuePath.c_str(),
				     strerror(errno));
				return false;
			}

			if (!(ifs >> maxValue) || maxValue <= 0) {
				ELOG("Failed to parse MADT backlight maxValuePath %s",
				     config.backlight.maxValuePath.c_str());
				return false;
			}
			return true;
		}

		bool applyBacklightBrightness(const RuntimeConfig& config, int brightness)
		{
			if (config.backlight.path.empty()) {
				return true;
			}

			int maxValue = 0;
			if (!readBacklightMaxValue(config, maxValue)) {
				return false;
			}

			const int rawValue = (brightness * maxValue + 127) / 255;
			std::ofstream ofs(config.backlight.path);
			if (!ofs.is_open()) {
				ELOG("Failed to open MADT backlight path %s: %s",
				     config.backlight.path.c_str(),
				     strerror(errno));
				return false;
			}

			ofs << rawValue;
			if (!ofs.good()) {
				ELOG("Failed to write MADT backlight path %s",
				     config.backlight.path.c_str());
				return false;
			}

			return true;
		}

		bool applyAudioVolume(const RuntimeConfig& config, const SettingsState& settings)
		{
			if (config.audioVolume.controlName.empty()) {
				return true;
			}

			const int percent = settingsVolumeToPercent(settings.volume);
			const QString percentArg = QString::number(percent) + QStringLiteral("%");

			QProcess process;
			process.start(QString::fromStdString(config.audioVolume.command),
			              { QStringLiteral("-q"),
			                QStringLiteral("sset"),
			                QString::fromStdString(config.audioVolume.controlName),
			                percentArg });
			if (!process.waitForStarted() || !process.waitForFinished()) {
				ELOG("Failed to start or wait for %s to apply MADT audio volume %s %s",
				     config.audioVolume.command.c_str(),
				     config.audioVolume.controlName.c_str(),
				     percentArg.toStdString().c_str());
				return false;
			}
			if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
				ELOG("Failed to apply MADT audio volume via %s %s %s (exit=%d, stderr=%s)",
				     config.audioVolume.command.c_str(),
				     config.audioVolume.controlName.c_str(),
				     percentArg.toStdString().c_str(),
				     process.exitCode(),
				     process.readAllStandardError().toStdString().c_str());
				return false;
			}

			ILOG("Applied MADT audio volume via %s %s %s",
			     config.audioVolume.command.c_str(),
			     config.audioVolume.controlName.c_str(),
			     percentArg.toStdString().c_str());
			return true;
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
		const QRect screenGeometry = currentScreenGeometry(mainWindow);
		if (screenGeometry.isValid()) {
			mainWindow->setGeometry(screenGeometry);
		}
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
			const QRect browserGeometry = currentScreenGeometry(mainWindow);
			if (browserGeometry.isValid()) {
				browser->resize(browserGeometry.size());
			}
			browser->show();
			browser->ApplySettings(runtimeConfig.settings);
			if (!applyAudioVolume(runtimeConfig, runtimeConfig.settings)) {
				WLOG("Failed to apply initial MADT audio volume");
			}
			if (!applyBacklightBrightness(runtimeConfig, runtimeConfig.settings.brightness)) {
				WLOG("Failed to apply initial MADT backlight brightness");
			}
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

	bool KillTab(const std::string& uuid, CmdResponse* resp, bool forceDestroy)
	{
		if (browser)
			browser->KillTab(uuid, resp, forceDestroy);
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
			QSize viewportSize = browser->contentsRect().size();
			if (QWidget* currentPage = browser->currentWidget(); currentPage != nullptr) {
				viewportSize = currentPage->contentsRect().size();
				if (!viewportSize.isValid() || viewportSize.isEmpty()) {
					viewportSize = currentPage->size();
				}
			}
			if (!viewportSize.isValid() || viewportSize.isEmpty()) {
				viewportSize = browser->size();
			}

			resp->payload = {
				{ "winWidth", viewportSize.width() },
				{ "winHeight", viewportSize.height() },
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

	bool CaptureScreenshot(const std::string& fileName, CmdResponse* resp)
	{
		if (resp == nullptr || appInstance == nullptr) {
			return false;
		}

		QMetaObject::invokeMethod(
		  appInstance,
		  [resp, fileName]() {
			  CmdResponse::ResultCode ret = CmdResponse::ResultCode::EXEC_ERROR;
			  try {
				  if (browser != nullptr) {
					  if (QWidget* currentPage = browser->currentWidget(); currentPage != nullptr) {
						  const QString path = QString::fromStdString(fileName);
						  QPixmap       pixmap = currentPage->grab();
						  if (pixmap.isNull()) {
							  ELOG("MADT screenshot capture returned a null pixmap for %s",
							       fileName.c_str());
						  } else if (pixmap.save(path)) {
							  resp->payload = {
								  { "fileName", QFileInfo(path).absoluteFilePath().toStdString() },
								  { "width", pixmap.width() },
								  { "height", pixmap.height() },
							  };
							  ret = CmdResponse::ResultCode::OK;
						  } else {
							  ELOG("MADT failed to save screenshot to %s", fileName.c_str());
						  }
					  } else {
						  ELOG("MADT screenshot capture failed because there is no active page");
					  }
				  } else {
					  ELOG("MADT screenshot capture failed because the browser is not available");
				  }
			  } catch (const std::exception& e) {
				  ELOG("MADT screenshot capture threw an exception for %s: %s",
				       fileName.c_str(),
				       e.what());
				  ret = CmdResponse::ResultCode::EXEC_ERROR;
			  } catch (...) {
				  ELOG("MADT screenshot capture threw an unknown exception for %s",
				       fileName.c_str());
				  ret = CmdResponse::ResultCode::EXEC_ERROR;
			  }

			  std::lock_guard<std::mutex> lock(resp->mtx);
			  resp->result = ret;
			  resp->ready  = true;
			  resp->cv.notify_one();
		  },
		  Qt::QueuedConnection);
		return true;
	}

	bool ApplySettings(const SettingsState& settings, CmdResponse* resp)
	{
		if (resp == nullptr || appInstance == nullptr) {
			return false;
		}

		QMetaObject::invokeMethod(
		  appInstance,
		  [resp, settings]() {
			  CmdResponse::ResultCode ret = CmdResponse::ResultCode::EXEC_ERROR;
			  try {
				  bool applied = true;
				  if (browser != nullptr) {
					  browser->ApplySettings(settings);
				  }
				  applied = applyAudioVolume(runtimeConfig, settings) && applied;
				  applied = applyBacklightBrightness(runtimeConfig, settings.brightness) && applied;
				  ret     = applied ? CmdResponse::ResultCode::OK : CmdResponse::ResultCode::EXEC_ERROR;
			  } catch (...) {
				  ret = CmdResponse::ResultCode::EXEC_ERROR;
			  }

			  std::lock_guard<std::mutex> lock(resp->mtx);
			  resp->result = ret;
			  resp->ready  = true;
			  resp->cv.notify_one();
		  },
		  Qt::QueuedConnection);
		return true;
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
