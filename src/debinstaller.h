/*
 * Copyright (C) 2021 Cutefish Technology Co., Ltd.
 *
 * Author:     Reion Wong <reion@cutefishos.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef DEBINSTALLER_H
#define DEBINSTALLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <QFutureWatcher>
#include <QHash>
#include <QFile>

// 只包含必要的 APT 头文件
#include <apt-pkg/cachefile.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/init.h>
#include <apt-pkg/error.h>

class DebInstaller : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(QString packageName READ packageName NOTIFY packageNameChanged)
    Q_PROPERTY(QString version READ version NOTIFY versionChanged)
    Q_PROPERTY(QString maintainer READ maintainer NOTIFY maintainerChanged)
    Q_PROPERTY(QString description READ description NOTIFY descriptionChanged)
    Q_PROPERTY(QString homePage READ homePage NOTIFY homePageChanged)
    Q_PROPERTY(QString installedSize READ installedSize NOTIFY installedSizeChanged)
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY installedVersionChanged)

    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString statusDetails READ statusDetails NOTIFY statusDetailsTextChanged)
    Q_PROPERTY(QString preInstallMessage READ preInstallMessage NOTIFY preInstallMessageChanged)

    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool isInstalled READ isInstalled NOTIFY isInstalledChanged)

    Q_PROPERTY(bool valid READ isValid NOTIFY isValidChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY canInstallChanged)

public:
    enum Status {
        Begin = 0,
        Installing,
        Error,
        Succeeded,
    };
    Q_ENUM(Status);

    explicit DebInstaller(QObject *parent = nullptr);
    ~DebInstaller();

    QString fileName() const;
    void setFileName(const QString &fileName);

    QString packageName() const;
    QString version() const;
    QString maintainer() const;
    QString description() const;

    bool isValid() const;
    bool canInstall() const;

    QString homePage() const;
    QString installedSize() const;
    QString installedVersion() const;

    bool isInstalled() const;

    Q_INVOKABLE void install();

    QString statusMessage() const;
    QString statusDetails() const;
    QString preInstallMessage() const;

    Status status() const;

signals:
    void fileNameChanged();
    void packageNameChanged();
    void versionChanged();
    void maintainerChanged();
    void descriptionChanged();
    void isValidChanged();
    void canInstallChanged();
    void homePageChanged();
    void installedSizeChanged();
    void installedVersionChanged();
    void statusMessageChanged();
    void statusDetailsTextChanged();
    void statusChanged();
    void isInstalledChanged();

    void requestSwitchToInstallPage();
    void preInstallMessageChanged();

private:
    bool initializeApt();
    bool parseDebFile();
    void setStatus(Status status);
    
    bool checkDependencies();
    bool checkConflicts();
    bool checkBreaksSystem();
    void updatePackageInfo();
    
    QString formatByteSize(double size, int precision) const;
    QString extractControlField(const QString &fieldName) const;
    bool runDpkgCommand(const QStringList &arguments, QString &output) const;

private slots:
    void onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onInstallOutput();

private:
    // APT 相关成员
    pkgCacheFile *m_cacheFile;
    
    QProcess *m_installProcess;
    QFutureWatcher<bool> *m_dependencyWatcher;
    
    bool m_isValid;
    bool m_canInstall;
    bool m_aptInitialized;

    QString m_fileName;
    QString m_packageName;
    QString m_version;
    QString m_maintainer;
    QString m_description;
    QString m_homePage;
    QString m_installedSize;
    QString m_installedVersion;
    bool m_isInstalled;

    QString m_statusMessage;
    QString m_statusDetails;
    QString m_preInstallMessage;

    Status m_status;
    
    QHash<QString, QString> m_controlFields;
};

#endif // DEBINSTALLER_H