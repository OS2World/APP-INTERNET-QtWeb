/*
 * Copyright (C) 2008-2009 Alexei Chaloupov <alexei.chaloupov@gmail.com>
 * Copyright (C) 2007-2008 Benjamin C. Meyer <ben@meyerhome.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */
/****************************************************************************
**
** Copyright (C) 2007-2008 Trolltech ASA. All rights reserved.
**
** This file is part of the demonstration applications of the Qt Toolkit.
**
** Licensees holding a valid Qt License Agreement may use this file in
** accordance with the rights, responsibilities and obligations
** contained therein.  Please consult your licensing agreement or
** contact sales@trolltech.com if any conditions of this licensing
** agreement are not clear to you.
**
** Further information about Qt licensing is available at:
** http://www.trolltech.com/products/qt/licensing.html or by
** contacting info@trolltech.com.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

#include "browserapplication.h"

#include "bookmarks.h"
#include "browsermainwindow.h"
#include "cookiejar.h"
#include "downloadmanager.h"
#include "history.h"
#include "networkaccessmanager.h"
#include "tabwidget.h"
#include "webview.h"
#include "webpage.h"
#include "toolbarsearch.h"

#include <QtCore/QBuffer>
#include <QtCore/QDir>
#include <QtCore/QLibraryInfo>
#include <QtCore/QSettings>
#include <QtCore/QTextStream>
#include <QtCore/QTranslator>
#include <QtCore/QReadWriteLock>

#include <QtGui/QDesktopServices>
#include <QtGui/QFileOpenEvent>

#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtNetwork/QNetworkProxy>

#include <QtWebKit/QWebSettings>
#include <QStyleFactory>
#include <QtCore/QDebug>
#include <QMessageBox>

#ifdef Q_WS_WIN
    #include "shlwapi.h"
    #include "shellapi.h"
#endif

#include "autocomplete.h"
#include "torrent/torrentwindow.h"


DownloadManager *BrowserApplication::s_downloadManager = 0;
TorrentWindow *BrowserApplication::s_torrents = 0;
HistoryManager *BrowserApplication::s_historyManager = 0;
NetworkAccessManager *BrowserApplication::s_networkAccessManager = 0;
BookmarksManager *BrowserApplication::s_bookmarksManager = 0;
QMap<QString, QIcon> BrowserApplication::s_hostIcons;
bool BrowserApplication::s_resetOnQuit = false;
AutoComplete* BrowserApplication::s_autoCompleter = 0;
bool BrowserApplication::s_portableRunMode = false;
QString BrowserApplication::s_exeLocation;
bool BrowserApplication::s_startResizeOnMouseweelClick = true;
QReadWriteLock lockIcons;

int BrowserApplication::getApplicationBuild()
{
	return 63; // Current Build 
}

BrowserApplication::BrowserApplication(int &argc, char **argv)
    : QApplication(argc, argv)
    , m_localServer(0)
    , quiting(false)
{
    QCoreApplication::setOrganizationName(QLatin1String("QtWeb.NET"));
    QCoreApplication::setApplicationName(QLatin1String("QtWeb Internet Browser"));
    QCoreApplication::setApplicationVersion(QLatin1String("3.7"));
    QString serverName = QCoreApplication::applicationName();
	
	// Define the base location - Should be ended with a slash!
	s_exeLocation = QCoreApplication::arguments()[0];
	int i = s_exeLocation.lastIndexOf( QDir::separator() );
	if (i == -1)
		s_exeLocation = "";
	else
		s_exeLocation = s_exeLocation.left(i+1);

	definePortableRunMode();

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(500)) {
        QTextStream stream(&socket);
        QStringList args = QCoreApplication::arguments();
        if (args.count() > 1)
            stream << args.last();
        else
            stream << QString();
        stream.flush();
        socket.waitForBytesWritten();
        return;
    }

    QApplication::setQuitOnLastWindowClosed(true);

    m_localServer = new QLocalServer(this);
    connect(m_localServer, SIGNAL(newConnection()),
            this, SLOT(newLocalSocketConnection()));
    if (!m_localServer->listen(serverName)) {
        if (m_localServer->serverError() == QAbstractSocket::AddressInUseError
            && QFile::exists(m_localServer->serverName())) {
            QFile::remove(m_localServer->serverName());
            m_localServer->listen(serverName);
        }
    }

    QDesktopServices::setUrlHandler(QLatin1String("http"), this, "openUrl");

	CheckSetTranslator();

    QSettings settings;
    settings.beginGroup(QLatin1String("sessions"));
    m_lastSession = settings.value(QLatin1String("lastSession")).toByteArray();
    settings.endGroup();

    connect(&m_iconManager, SIGNAL(finished(QNetworkReply*)),SLOT(iconDownloadFinished(QNetworkReply*)));

    QTimer::singleShot(0, this, SLOT(postLaunch()));

}

void BrowserApplication::CheckSetTranslator()
{
	QStringList args = QCoreApplication::arguments();
	if (args.count() > 2)
	{
		QString command = args[1].toLower();
		if (command == "-lang" || command == "-language")
		{
			QString lang_file = args[2].toLower();
			QTranslator translator;

			if (translator.load(QLatin1String(":/tr/qtweb_") + lang_file + ".qm"))
			{
				installTranslator(QLatin1String(":/tr/qtweb_") + lang_file + ".qm"); 
				installTranslator(QLatin1String(":/tr/qt_") + lang_file + ".qm"); 
				return;
			}
			if (translator.load(lang_file))
			{	
				installTranslator(lang_file);
				return;
			}
		}
	}

	QSettings settings;
    settings.beginGroup(QLatin1String("general"));
	QString language = settings.value("Language", "").toString();

	if (language.isEmpty())
	{
		language = QLocale::system().name().toLower();
		QTranslator translator;
		if (translator.load(QLatin1String(":/tr/qtweb_") + language + ".qm"))
		{
			settings.setValue("Language", language);
		}
	}

	//installTranslator(QLatin1String("C:\\QtWeb\\qtweb_32.qm")); 
	installTranslator(QLatin1String(":/tr/qtweb_") + language + ".qm"); 
	// install default translator (for buttons, hints, etc..)
	installTranslator(QLatin1String(":/tr/qt_") + language + ".qm"); 
}

void RecurseDelete(QDir d)
{
	QStringList list = d.entryList();
	for (int i = 0; i < list.size(); ++i) 
	{
		 if (list.at(i) == "." || list.at(i) == "..")
			continue;

         QString fileInfo = d. absolutePath() +  "/" + list.at(i);
		 RecurseDelete( fileInfo );
		 d.rmdir( fileInfo );
		 d.remove(fileInfo);
     }
}

#define QTWEB_SETTINGS  (s_exeLocation + "QtWebSettings")

void BrowserApplication::definePortableRunMode()
{
	s_portableRunMode = !QFile::exists(s_exeLocation + "unins000.exe");
	if (s_portableRunMode)
	{
		QSettings::setDefaultFormat(QSettings::IniFormat);
                QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QTWEB_SETTINGS);
		bool is_writable = true;
		{
			QSettings settings;
			settings.beginGroup(QLatin1String("general"));
			is_writable = settings.isWritable();
			settings.setValue(QLatin1String("settings_writable"), true);
		}
		{
			QSettings settings;
			settings.beginGroup(QLatin1String("general"));
			bool tested_ok = settings.value(QLatin1String("settings_writable"), false).toBool();
			// just removing this test value
			if (is_writable)
				settings.remove("settings_writable");
			if (!tested_ok || !is_writable)
			{
				// Copy settings from base template	to temporary storage
				QDir temp_dir(QDir::temp());
				bool res = temp_dir.mkdir(settings.organizationName());
				res = temp_dir.cd(settings.organizationName());
				res = QFile::copy(  settings.fileName(), temp_dir.absolutePath() + QDir::separator() + settings.applicationName() + ".ini" );
				// Change path to settings to the temp storage
                                QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::temp().tempPath());
			}

		}
	}
}

void RemoveEmptyDir(const QString& path)
{
    QDir d(path);
    QString name = d.dirName();
    d.cdUp();
    d.rmdir(name);

}

BrowserApplication::~BrowserApplication()
{
	QReadLocker locker(&lockIcons);
	s_hostIcons.clear();

    quiting = true;

	if (resetOnQuit())
	{
		BrowserApplication::clearDownloads();
		BrowserApplication::emptyCaches();
	}

	if (s_downloadManager)
		delete s_downloadManager;

	if (s_torrents)
	{	
		delete s_torrents;
	}

    qDeleteAll(m_mainWindows);

	if (resetOnQuit())
	{
		BrowserApplication::clearCookies();
	}

	if (s_networkAccessManager)
		delete s_networkAccessManager;

	if (s_bookmarksManager)
		delete s_bookmarksManager;

	if (resetOnQuit())
	{
		BrowserApplication::historyClear();
		BrowserApplication::clearIcons();
		BrowserApplication::clearPasswords();
		BrowserApplication::resetSettings( false );

		QString base = BrowserApplication::dataLocation();
		QFile::remove(  base + "/bookmarks.xbel" );
		QFile::remove( base  + "/cookies" );
		QFile::remove( base  + "/history" );

                // TO DO: !!! Check on Windows cleanup ???
		RecurseDelete( QDir(base  + "/cache") );
                RemoveEmptyDir( QString(base  + "/cache") );
		RecurseDelete( QDir(base) );
                RemoveEmptyDir( base );

		{
			QSettings settings;
			settings.clear();
			QString settingsFileName = settings.fileName();
			QFile::remove(settingsFileName);
			int index = settingsFileName.indexOf(organizationName());
			if (index >=0)
			{
				settingsFileName = settingsFileName.left(index + organizationName().length());
                                RemoveEmptyDir( settingsFileName );
			}
		}
		RecurseDelete( QDir( QTWEB_SETTINGS ) );
                RemoveEmptyDir( QString(QTWEB_SETTINGS) );

		int index = base.indexOf(organizationName());
		if (index >=0)
		{
			base = base.left(index + organizationName().length());
                        RemoveEmptyDir( base );
		}

#ifdef Q_WS_WIN
		HKEY hKey;

		wchar_t key[256];
		wcscpy(key, L"Software");
		if (ERROR_SUCCESS == RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, DELETE, &hKey))
		{
			RegDeleteKey(hKey, organizationName().utf16());
			RegCloseKey(hKey);
		}
#endif

	}
}


BrowserApplication *BrowserApplication::instance()
{
    return (static_cast<BrowserApplication *>(QCoreApplication::instance()));
}

/*!
    Any actions that can be delayed until the window is visible
 */
void BrowserApplication::postLaunch()
{
    loadSettings();

    // newMainWindow() needs to be called in main() for this to happen
    if (m_mainWindows.count() > 0) 
	{
        QSettings settings;
        settings.beginGroup(QLatin1String("MainWindow"));
        int startup = settings.value(QLatin1String("onStartup")).toInt();
        bool first_start = settings.value(QLatin1String("first_start"), true).toBool();
		if (first_start)
		{
			settings.setValue(QLatin1String("first_start"), false);
		}

		QStringList args = QCoreApplication::arguments();

        if (args.count() > 1) 
		{
            switch (startup) 
			{
				case 2: 
				{
					restoreLastSession();
					WebView *webView = mainWindow()->tabWidget()->newTab(true);
					webView->loadUrl(args.last());
					break;
				}
				default:
				{
					QString command = args[1].toLower();
					if (command == "-dump" || command == "-dump_and_quit")
					{
						if (args.count() >= 4)
						{
							mainWindow()->loadPage( args[2] );
							mainWindow()->setDumpFile(args[3] , (command == "-dump_and_quit"));
						}
					}
					else
					{
						mainWindow()->loadPage(args.last());
					}
					break;
				}
            }
        } 
		else 
		{
            switch (startup) 
			{
				case 0:
					mainWindow()->slotHome();
					if (first_start)
					{
						mainWindow()->tabWidget()->newTab(false);
					}
					break;
				case 1:
					break;
				case 2:
					restoreLastSession();
                break;
            }
        }

    }
    BrowserApplication::historyManager();
}

extern QString DefaultAppStyle();

void BrowserApplication::loadSettings()
{
    QSettings settings;

	settings.beginGroup(QLatin1String("MainWindow"));
	QString style = settings.value(QLatin1String("style"), DefaultAppStyle()).toString();
	QApplication::setStyle(QStyleFactory::create(style));
	s_startResizeOnMouseweelClick = settings.value(QLatin1String("mouseweelClickAction"), 1).toInt() == 1;
	settings.endGroup();

    settings.beginGroup(QLatin1String("websettings"));

	QWebSettings *defaultSettings = QWebSettings::globalSettings();
    QString standardFontFamily = defaultSettings->fontFamily(QWebSettings::StandardFont);
    int standardFontSize = defaultSettings->fontSize(QWebSettings::DefaultFontSize) + 2; // Fake to fix WebKit bug !!!
	if (standardFontSize > 16)
		standardFontSize = 16;
    QFont standardFont = QFont(standardFontFamily, standardFontSize);
    standardFont = qVariantValue<QFont>(settings.value(QLatin1String("standardFont"), standardFont));
    defaultSettings->setFontFamily(QWebSettings::StandardFont, standardFont.family());
    defaultSettings->setFontSize(QWebSettings::DefaultFontSize, standardFont.pointSize());

    QString fixedFontFamily = defaultSettings->fontFamily(QWebSettings::FixedFont);
    int fixedFontSize = defaultSettings->fontSize(QWebSettings::DefaultFixedFontSize);
    QFont fixedFont = QFont(fixedFontFamily, fixedFontSize);
    fixedFont = qVariantValue<QFont>(settings.value(QLatin1String("fixedFont"), fixedFont));
    defaultSettings->setFontFamily(QWebSettings::FixedFont, fixedFont.family());
	defaultSettings->setFontSize(QWebSettings::DefaultFixedFontSize, fixedFont.pointSize());

    bool zoom_text_only = settings.value(QLatin1String("zoom_text_only"), false).toBool();
	defaultSettings->setAttribute(QWebSettings::ZoomTextOnly, zoom_text_only);

    defaultSettings->setAttribute(QWebSettings::JavascriptEnabled, settings.value(QLatin1String("enableJavascript"), true).toBool());
    defaultSettings->setAttribute(QWebSettings::PluginsEnabled, settings.value(QLatin1String("enablePlugins"), true).toBool());
	defaultSettings->setAttribute(QWebSettings::AutoLoadImages, settings.value(QLatin1String("autoLoadImages"), true).toBool());
	defaultSettings->setAttribute(QWebSettings::JavascriptCanOpenWindows, ! (settings.value(QLatin1String("blockPopups"), true).toBool()));

	if (settings.value(QLatin1String("customUserStyleSheet"), false).toBool())
	{
		QUrl url = settings.value(QLatin1String("userStyleSheet")).toUrl();
		defaultSettings->setUserStyleSheetUrl(url);
	}
	else
		defaultSettings->setUserStyleSheetUrl(QUrl());

	settings.endGroup();

	settings.beginGroup(QLatin1String("privacy"));
    bool private_browsing = settings.value(QLatin1String("private_browsing"), false).toBool();
	if (private_browsing)
	{
		QWebSettings::globalSettings()->setAttribute(QWebSettings::PrivateBrowsingEnabled, true);
		WebPage::setUserAgent( QLatin1String("") );
	}
	settings.endGroup();

    settings.beginGroup(QLatin1String("proxy"));
    if (settings.value(QLatin1String("enabled"), false).toBool()) 
	{
		QNetworkProxy pxy;
        if (settings.value(QLatin1String("type"), 0).toInt() == 0)
            pxy = QNetworkProxy::Socks5Proxy;
        else
            pxy = QNetworkProxy::HttpProxy;
        pxy.setHostName(settings.value(QLatin1String("hostName")).toString());
        pxy.setPort(settings.value(QLatin1String("port"), 1080).toInt());
        pxy.setUser(settings.value(QLatin1String("userName")).toString());
        pxy.setPassword(settings.value(QLatin1String("password")).toString());
	    m_iconManager.setProxy(pxy);
    }
	else
    if (settings.value(QLatin1String("autoProxy"), false).toBool()) 
	{
		QNetworkProxy pxy;
#ifdef Q_WS_WIN
		HKEY hKey;
		wchar_t key[256];
		wcscpy(key, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings\\");
		if (ERROR_SUCCESS == RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey))
		{
			DWORD dwEnabled = FALSE; DWORD dwBufSize = sizeof(dwEnabled);
			if (RegQueryValueExW(hKey, L"ProxyEnable", NULL, NULL, (LPBYTE)&dwEnabled, &dwBufSize ) == ERROR_SUCCESS
				&& dwEnabled)
			{
				memset(key,0,sizeof(key)); dwBufSize = sizeof(key) / sizeof(TCHAR);
				if (RegQueryValueExW(hKey, L"ProxyServer", NULL, NULL, (LPBYTE)&key, &dwBufSize ) == ERROR_SUCCESS)
				{
					pxy = QNetworkProxy::HttpProxy;
					QStringList lst = QString::fromWCharArray(key).split(':');
					if (lst.size() > 0)
					{
						pxy.setHostName(lst.at(0));
						pxy.setPort( lst.size() > 1 ? lst.at(1).toInt() : 1080 );
						m_iconManager.setProxy(pxy);
					}
				}
			}
			RegCloseKey(hKey);
		}
#endif
	}

	settings.endGroup();

	mainWindow()->loadSettings();
}

QList<BrowserMainWindow*> BrowserApplication::mainWindows()
{
    clean();
    QList<BrowserMainWindow*> list;
    for (int i = 0; i < m_mainWindows.count(); ++i)
        list.append(m_mainWindows.at(i));
    return list;
}

void BrowserApplication::clean()
{
    // cleanup any deleted main windows first
    for (int i = m_mainWindows.count() - 1; i >= 0; --i)
        if (m_mainWindows.at(i).isNull())
            m_mainWindows.removeAt(i);
}

static const qint32 BrowserApplicationMagic = 0xec;

void BrowserApplication::saveSession()
{
	if (quiting)
        return;

    QSettings settings;
    settings.beginGroup(QLatin1String("MainWindow"));
    settings.setValue(QLatin1String("restoring"), false);
    settings.endGroup();

    QWebSettings *globalSettings = QWebSettings::globalSettings();
    if (globalSettings->testAttribute(QWebSettings::PrivateBrowsingEnabled))
        return;

    clean();

    settings.beginGroup(QLatin1String("sessions"));

    int version = 2;

    QByteArray data;
    QBuffer buffer(&data);
    QDataStream stream(&buffer);
    buffer.open(QIODevice::WriteOnly);

    stream << qint32(BrowserApplicationMagic);
    stream << qint32(version);

    stream << qint32(m_mainWindows.count());
    for (int i = 0; i < m_mainWindows.count(); ++i)
        stream << m_mainWindows.at(i)->saveState();
    settings.setValue(QLatin1String("lastSession"), data);
    settings.endGroup();

}

bool BrowserApplication::canRestoreSession() const
{
    return !m_lastSession.isEmpty();
}

void BrowserApplication::restoreLastSession()
{
    {
        QSettings settings;
        settings.beginGroup(QLatin1String("MainWindow"));
        if (settings.value(QLatin1String("restoring"), false).toBool()) 
		{
            QMessageBox::information(0, tr("Session restore failed"),
                tr("The saved session will not be restored because QtWeb crashed before while trying to restore this session."));
            return;
        }
        // saveSession will be called by an AutoSaver timer from the set tabs
        // and in saveSession we will reset this flag back to false
        settings.setValue(QLatin1String("restoring"), true);
    }

    int version = 2;
    QList<QByteArray> windows;
    QBuffer buffer(&m_lastSession);
    QDataStream stream(&buffer);
    buffer.open(QIODevice::ReadOnly);

    qint32 marker;
    qint32 v;
    stream >> marker;
    stream >> v;
    if (marker != BrowserApplicationMagic || v != version)
        return;

    qint32 windowCount;
    stream >> windowCount;
    for (qint32 i = 0; i < windowCount; ++i) {
        QByteArray windowState;
        stream >> windowState;
        windows.append(windowState);
    }
    for (int i = 0; i < windows.count(); ++i) {
        BrowserMainWindow *newWindow = 0;
        if (i == 0 && m_mainWindows.count() >= 1) {
            newWindow = mainWindow();
        } else {
            newWindow = newMainWindow();
        }
        newWindow->restoreState(windows.at(i));
    }
}

bool BrowserApplication::isTheOnlyBrowser() const
{
    return (m_localServer != 0);
}

void BrowserApplication::installTranslator(const QString &name)
{
    QTranslator *translator = new QTranslator(this);
    translator->load(name, QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    QApplication::installTranslator(translator);
}

void BrowserApplication::openUrl(const QUrl &url)
{
    mainWindow()->loadPage(url.toString());
}

BrowserMainWindow *BrowserApplication::newMainWindow()
{
	BrowserMainWindow *browser = new BrowserMainWindow();
	m_mainWindows.prepend(browser);
	browser->show();
	return browser;
}

BrowserMainWindow *BrowserApplication::mainWindow()
{
    clean();
    if (m_mainWindows.isEmpty())
        newMainWindow();
    return m_mainWindows[0];
}

void BrowserApplication::newLocalSocketConnection()
{
    QLocalSocket *socket = m_localServer->nextPendingConnection();
    if (!socket)
        return;
    socket->waitForReadyRead(1000);
    QTextStream stream(socket);
    QString url;
    stream >> url;
    if (!url.isEmpty()) {
        QSettings settings;
        settings.beginGroup(QLatin1String("general"));
        int openLinksIn = settings.value(QLatin1String("openLinksIn"), 0).toInt();
        settings.endGroup();
        if (openLinksIn == 1 || m_mainWindows.isEmpty())
            newMainWindow();
        else
            mainWindow()->tabWidget()->newTab();
        openUrl(url);
    }
    delete socket;
    mainWindow()->raise();
    mainWindow()->activateWindow();
}

CookieJar *BrowserApplication::cookieJar()
{
    return (CookieJar*)networkAccessManager()->cookieJar();
}

DownloadManager *BrowserApplication::downloadManager()
{
    if (!s_downloadManager) {
        s_downloadManager = new DownloadManager();
    }
    return s_downloadManager;
}

TorrentWindow *BrowserApplication::torrents()
{
    if (!s_torrents) {
	    qsrand(QTime(0,0,0).secsTo(QTime::currentTime()));
        s_torrents = new TorrentWindow();
    }
    return s_torrents;
}

NetworkAccessManager *BrowserApplication::networkAccessManager()
{
    if (!s_networkAccessManager) {
        s_networkAccessManager = new NetworkAccessManager();
        s_networkAccessManager->setCookieJar(new CookieJar);
    }
    return s_networkAccessManager;
}

HistoryManager *BrowserApplication::historyManager()
{
    if (!s_historyManager) {
        s_historyManager = new HistoryManager();
        QWebHistoryInterface::setDefaultInterface(s_historyManager);
    }
    return s_historyManager;
}

BookmarksManager *BrowserApplication::bookmarksManager()
{
    if (!s_bookmarksManager) {
        s_bookmarksManager = new BookmarksManager;
    }
    return s_bookmarksManager;
}

AutoComplete *BrowserApplication::autoCompleter()
{
    if (!s_autoCompleter) {
		s_autoCompleter = new AutoComplete( BrowserApplication::instance() );
    }
    return s_autoCompleter;
}

#ifdef Q_WS_WIN
	#include "windows.h"
#endif

void BrowserApplication::iconDownloadFinished(QNetworkReply* reply)
{
    if (!reply->error()) 
	{
	    QUrl url = reply->url();
		QString filename = QDir::toNativeSeparators(QDir::tempPath()) + QDir::separator() + url.host() + ".ico" ;
		QFile file(filename);
	    if (file.open(QIODevice::WriteOnly)) 
		{
			file.write( reply->readAll() );
			file.close();

#ifdef Q_WS_WIN
			HICON i = (HICON)LoadImage( NULL, filename.utf16(), IMAGE_ICON, 16 , 16 , LR_LOADFROMFILE);
			QPixmap pix = QPixmap::fromWinHICON( i ) ;
#else
			QIcon icon(filename);
			QPixmap pix = icon.pixmap(16,16);
			
#endif
			if (!pix.isNull())
			{
				QSettings settings(BrowserApplication::dataLocation() + "/Icons", QSettings::IniFormat);
				settings.beginGroup(QLatin1String("Icons"));
				QBuffer imageBuffer;
				imageBuffer.open(QBuffer::ReadWrite);
				if (pix.save(&imageBuffer, "PNG") && !pix.isNull() )
				{
					settings.setValue(url.host(), imageBuffer.buffer());
					setHostIcon(url.host(), pix);
				}
				settings.endGroup();
			}
			file.remove();
	    }
    }
    reply->deleteLater();
}


QIcon BrowserApplication::getHostIcon(const QString &host)
{
	QReadLocker locker(&lockIcons);
	
	return s_hostIcons[host];
}

void BrowserApplication::setHostIcon(const QString &host, const QIcon& icon)
{
	QWriteLocker locker(&lockIcons);
	s_hostIcons[host] = icon;
}

void BrowserApplication::CheckIcon(const QUrl &url)
{
	if (url.isEmpty())
		return;

	QString host = url.host();
	QIcon icon = getHostIcon( host );
	if ( icon.isNull() && !host.isEmpty())
	{
		QSettings settings(BrowserApplication::dataLocation() + "/Icons", QSettings::IniFormat);
		settings.beginGroup(QLatin1String("Icons"));
		QPixmap pix;
		QByteArray image = settings.value( host ).toByteArray();
		settings.endGroup();
		pix.loadFromData( image, "PNG" );
		if (!pix.isNull())
			return;

		QUrl iconUrl = url.scheme() + "://" + host + "/favicon.ico";
	    QNetworkRequest request(iconUrl);
	    m_iconManager.get(request);
	}
}

QIcon BrowserApplication::icon(const QUrl &url) const
{
	 if (url.scheme() == QLatin1String("https"))
	 {
	    if (m_defaultSecureIcon.isNull())
		    m_defaultSecureIcon = QIcon(QLatin1String(":secureicon.png"));

		 return m_defaultSecureIcon.pixmap(16,16);
	 }

	if (!url.host().isEmpty())
	{
		QString host = url.host();
		QIcon icon = getHostIcon(host);
		if (!icon.isNull())
			return icon;
		
		{
			QSettings settings(BrowserApplication::dataLocation() + "/Icons", QSettings::IniFormat);
			settings.beginGroup(QLatin1String("Icons"));
			QPixmap pix;
			QByteArray image = settings.value( host ).toByteArray();
			settings.endGroup();
			if (pix.loadFromData(image, "PNG"))
				if (!pix.isNull())
					return QIcon(pix).pixmap(16,16);
		}

	}

    if (m_defaultIcon .isNull())
        m_defaultIcon = QIcon(QLatin1String(":defaulticon.png"));

    return m_defaultIcon.pixmap(16, 16);
}

void BrowserApplication::closeMainWindows()
{
    
    foreach (BrowserMainWindow* win, mainWindows())
	{
		if (win != mainWindow())
		{
			win->close();
			mainWindows().removeOne(win);
		}
	}

}

void BrowserApplication::closeTabs()
{
	if (mainWindow()->tabWidget()->count() > 1)
		mainWindow()->tabWidget()->closeOtherTabs( mainWindow()->tabWidget()->currentIndex() );
}


bool BrowserApplication::handleMIME(QString content, const QUrl& url)
{

	int ind = content.indexOf(QChar(';'));
	if ( ind != -1)
		content = content.left(ind );

	// just skip pixel.gif and spacer.gif
	QString path = url.path().toLower();
	if (path.endsWith(QLatin1String("pixel.gif")) || path.endsWith(QLatin1String("spacer.gif"))  )
		return true;

	if (content.indexOf(QLatin1String("video")) == -1 && content.indexOf(QLatin1String("audio")) == -1)
		return false;

    QSettings settings;
    settings.beginGroup(QLatin1String("MainWindow"));
	bool bDownloadAudioVideo = settings.value(QLatin1String("downloadAudioVideo"), false ).toBool();
	settings.endGroup();
	if (bDownloadAudioVideo)
		return false;

#ifdef Q_WS_WIN
	HKEY hKey;

	wchar_t key[256];
	wcscpy(key, L"Mime\\Database\\Content Type\\");
	wcscat(key, content.utf16());
	if (ERROR_SUCCESS != RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &hKey))
		return false;

	wchar_t szTemp[256];
	DWORD dwBufSize = sizeof(szTemp) / sizeof(TCHAR);
	if (RegQueryValueExW(hKey, L"Extension", NULL, NULL, (LPBYTE)&szTemp, &dwBufSize ) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}
	RegCloseKey(hKey);

	//szTemp == ".asx"
	if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_CLASSES_ROOT, szTemp, 0, KEY_READ, &hKey))
		return false;

	dwBufSize = sizeof(szTemp) / sizeof(TCHAR);
	if (RegQueryValueExW(hKey, NULL, NULL, NULL, (LPBYTE)&szTemp, &dwBufSize ) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}

	RegCloseKey(hKey);
	//szTemp = "ASXFile"

	wchar_t szPath[512];
	wcscpy(szPath, szTemp);
	wcscat(szPath, L"\\shell\\open\\command");

	bool bOpen = true;
	if (ERROR_SUCCESS != RegOpenKeyExW(HKEY_CLASSES_ROOT, szPath, 0, KEY_READ, &hKey))
	{
		wcscpy(szPath, szTemp);
		wcscat(szPath, L"\\shell\\play\\command");
		bOpen = false;
		if (ERROR_SUCCESS != RegOpenKeyExW(HKEY_CLASSES_ROOT, szPath, 0, KEY_READ, &hKey))
			return false;
	}

	dwBufSize = sizeof(szPath) / sizeof(TCHAR);
	if (RegQueryValueExW(hKey, NULL, NULL, NULL, (LPBYTE)&szPath, &dwBufSize ) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return false;
	}
	RegCloseKey(hKey);

	wcslwr(szPath);
	wchar_t* pos = wcsstr(szPath, L"rundll32.exe");
	if (pos)
		return false;

	pos = wcsstr(szPath, L".exe");
	if (pos != NULL)
	{
		wchar_t szExe[256];
		memset(szExe,0,sizeof(szExe));
		wcsncpy(szExe, szPath,  pos - szPath + (pos[4] == '\"' ? 5 : 4) );

		wchar_t szCmd[256];
		memset(szCmd,0,sizeof(szCmd));
		pos+=5;
		
		wchar_t* arg = wcsstr(pos, L"%l");
		if ( arg != NULL)
			*++arg = 's';
		else
		{
			arg = wcsstr(pos, L"%1");
			if ( arg != NULL)
				*++arg = 's';
		}

		swprintf(szCmd, pos, url.toString().utf16());

		int res = (int)ShellExecute( NULL, (bOpen ? L"open" : L"play"), szExe, szCmd, NULL, SW_SHOWNORMAL);
		if (res <= 32 )
			res = (int)ShellExecute( NULL, (bOpen ? L"open" : L"play"), url.toString().utf16(), NULL, NULL, SW_SHOWNORMAL);

		return (res > 32);
	}
	else
		return ((int)ShellExecute( NULL, (bOpen ? L"open" : L"play"), url.toString().utf16(), NULL, NULL, SW_SHOWNORMAL) > 32);

#endif

	return false;
}


void BrowserApplication::historyClear()
{  
	BrowserApplication::historyManager()->clear();
}

#include <qnetworkdiskcache.h>

void BrowserApplication::emptyCaches()
{
    foreach (BrowserMainWindow* win, instance()->mainWindows())
	{
		win->emptyCache();
	}

	if (BrowserApplication::networkAccessManager() && BrowserApplication::networkAccessManager()->cache())
	{
        BrowserApplication::networkAccessManager()->cache()->clear();
	}
}

void BrowserApplication::clearDownloads()
{
	QSettings settings;
	settings.beginGroup(QLatin1String("downloadmanager"));
	settings.setValue(QLatin1String("first_ask"), false);

	BrowserApplication::downloadManager()->cleanup_full();
	BrowserApplication::downloadManager()->hide();

	QString downloadDirectory = dirDownloads(false);

        RemoveEmptyDir( downloadDirectory );
}

void BrowserApplication::clearCookies()
{
	BrowserApplication::cookieJar()->clear();
}

void BrowserApplication::clearIcons()
{
	QFile::remove( BrowserApplication::dataLocation() + "/Icons" );
}

void BrowserApplication::clearPasswords()
{
    QSettings settings;
	settings.remove(QLatin1String("AutoComplete"));
}

void BrowserApplication::clearSSL()
{
    QSettings settings;
	settings.remove(QLatin1String("ApprovedSSL"));
}

void BrowserApplication::clearSearches()
{
    QList<BrowserMainWindow*> mainWindows = BrowserApplication::instance()->mainWindows();
    for (int i = 0; i < mainWindows.count(); ++i) 
		mainWindows.at(i)->m_toolbarSearch->clear();
}

void BrowserApplication::closeExtraWindows()
{
	BrowserApplication::instance()->closeMainWindows();
	BrowserApplication::instance()->closeTabs();
}


void BrowserApplication::resetSettings( bool reload)
{
    QSettings settings;

	// Save AutoComplete
	settings.beginGroup(QLatin1String("AutoComplete"));
	QStringList keys = settings.allKeys ();
	QMap<QString, QVariant> values;
	foreach(QString key, keys)
		values[key] = settings.value(key);
	settings.endGroup();

	// Clear everything else
	settings.clear();

	// Restore Auto-Complete
	settings.beginGroup(QLatin1String("AutoComplete"));
	foreach(QString key, values.keys())
	{
		settings.setValue(key, values[key]);
	}

	if (reload)
		BrowserApplication::instance()->loadSettings();
}

QString BrowserApplication::dataLocation()
{
	if (s_portableRunMode)
	{
		QDir dir( s_exeLocation );
		if (! dir.exists( "QtWebCache" ))
			if ( !dir.mkdir( "QtWebCache" ) )
				return "";

		return s_exeLocation + "QtWebCache";
	}
	else
		return QDesktopServices::storageLocation(QDesktopServices::DataLocation);
}

QString BrowserApplication::downloadsLocation(bool create_dir)
{
	QString base;

	if (s_portableRunMode)
		base = s_exeLocation;
	else
	{
		base = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation) ;
		if (base.at(base.length() - 1) != QDir::separator() )
			base += QDir::separator();
	}

	QString downs(tr("Downloads"));

	if (create_dir)
	{
		QDir dir( base );
		if (! dir.exists( downs ))
			if ( !dir.mkdir( downs ) )
				return "";
	}

	return base + downs;
}
