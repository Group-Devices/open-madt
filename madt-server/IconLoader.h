#ifndef MADT_SERVER_ICON_LOADER_H
#define MADT_SERVER_ICON_LOADER_H

#include <functional>
#include <map>
#include <vector>

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtGui/QIcon>
#include <QtNetwork/QNetworkAccessManager>

namespace Secretary::Madt::Gui {

	class IconLoader : public QObject
	{
		Q_OBJECT

	  public:
		explicit IconLoader(QObject* parent = nullptr);
		void loadIcon(const QUrl& url, std::function<void(const QIcon&)> onLoaded);

	  private:
		std::map<QString, QIcon>                                   cache;
		std::map<QString, std::vector<std::function<void(const QIcon&)>>> pending;
		QNetworkAccessManager* manager;
	};

} // namespace Secretary::Madt::Gui

#endif
