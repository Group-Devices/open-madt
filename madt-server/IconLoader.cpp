#include "IconLoader.h"

#include <functional>

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtGui/QPixmap>

namespace Secretary::Madt::Gui {

	IconLoader::IconLoader(QObject* parent)
	  : QObject(parent)
	  , manager(new QNetworkAccessManager(this))
	{
	}

	void IconLoader::loadIcon(const QUrl& url, std::function<void(const QIcon&)> onLoaded)
	{
		if (!url.isValid() || url.isEmpty()) {
			onLoaded(QIcon());
			return;
		}

		const QString key = url.toString();
		const auto    cacheIt = cache.find(key);
		if (cacheIt != cache.end()) {
			onLoaded(cacheIt->second);
			return;
		}

		auto& callbacks = pending[key];
		callbacks.push_back(std::move(onLoaded));
		if (callbacks.size() > 1) {
			return;
		}

		QNetworkReply* reply = manager->get(QNetworkRequest(url));
		connect(reply, &QNetworkReply::finished, this, [this, key, reply]() mutable {
			QIcon icon;
			if (reply->error() == QNetworkReply::NoError) {
				QPixmap pixmap;
				if (pixmap.loadFromData(reply->readAll())) {
					icon = QIcon(pixmap);
				}
			}

			cache[key] = icon;
			auto callbacksIt = pending.find(key);
			if (callbacksIt != pending.end()) {
				for (auto& callback : callbacksIt->second) {
					callback(icon);
				}
				pending.erase(callbacksIt);
			}
			reply->deleteLater();
		});
	}

} // namespace Secretary::Madt::Gui
