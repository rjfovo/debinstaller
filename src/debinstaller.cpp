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
#include "debinstaller.h"
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QDebug>
#include <QThread>
#include <QRegularExpression>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>

DebInstaller::DebInstaller(QObject *parent)
    : QObject(parent)
    , m_cacheFile(nullptr)
    , m_installProcess(nullptr)
    , m_dependencyWatcher(nullptr)
    , m_isValid(false)
    , m_canInstall(false)
    , m_aptInitialized(false)
    , m_isInstalled(false)
    , m_status(DebInstaller::Begin)
{
    m_aptInitialized = initializeApt();
    
    m_installProcess = new QProcess(this);
    connect(m_installProcess, &QProcess::readyReadStandardOutput, this, &DebInstaller::onInstallOutput);
    connect(m_installProcess, &QProcess::readyReadStandardError, this, &DebInstaller::onInstallOutput);
    connect(m_installProcess, &QProcess::finished, this, &DebInstaller::onInstallFinished);
}

DebInstaller::~DebInstaller()
{
    if (m_cacheFile) {
        delete m_cacheFile;
        m_cacheFile = nullptr;
    }
    if (m_installProcess) {
        m_installProcess->deleteLater();
    }
    if (m_dependencyWatcher) {
        m_dependencyWatcher->deleteLater();
    }
}

bool DebInstaller::initializeApt()
{
    if (m_aptInitialized) {
        return true;
    }
    
    // 使用现代的 APT 初始化方法
    // 不再使用 _system 全局变量
    if (!pkgInitConfig(*_config)) {
        m_statusDetails = tr("Failed to initialize APT configuration");
        emit statusDetailsTextChanged();
        return false;
    }
    
    // 创建缓存文件，它会自动处理系统初始化
    m_cacheFile = new pkgCacheFile();
    if (!m_cacheFile->Open(nullptr, true)) {
        m_statusDetails = tr("Failed to open APT cache");
        emit statusDetailsTextChanged();
        return false;
    }
    
    return true;
}

QString DebInstaller::extractControlField(const QString &fieldName) const
{
    // 使用 dpkg 命令提取控制字段
    QString output;
    if (runDpkgCommand(QStringList() << "-I" << m_fileName << fieldName, output)) {
        // 解析输出，获取字段值
        QRegularExpression regex(fieldName + ":\\s*(.*)");
        QRegularExpressionMatch match = regex.match(output);
        if (match.hasMatch()) {
            return match.captured(1).trimmed();
        }
    }
    return QString();
}

bool DebInstaller::runDpkgCommand(const QStringList &arguments, QString &output) const
{
    QProcess process;
    process.start("dpkg", arguments);
    if (process.waitForFinished(5000)) {
        output = QString::fromLocal8Bit(process.readAllStandardOutput());
        if (process.exitCode() == 0) {
            return true;
        }
    }
    return false;
}

QString DebInstaller::fileName() const
{
    return m_fileName;
}

void DebInstaller::setFileName(const QString &fileName)
{
    if (fileName.isEmpty() || m_fileName == fileName)
        return;

    QString newPath = fileName;
    newPath = newPath.remove("file://");

    QFileInfo info(newPath);
    QString mimeType = QMimeDatabase().mimeTypeForFile(info.absoluteFilePath()).name();

    if (mimeType != "application/vnd.debian.binary-package") {
        m_preInstallMessage = tr("Error: Not a valid Debian package");
        emit preInstallMessageChanged();
        return;
    }

    m_fileName = info.absoluteFilePath();
    
    // 重置状态
    m_isValid = false;
    m_canInstall = false;
    m_preInstallMessage.clear();
    
    // 解析 deb 文件
    m_isValid = parseDebFile();
    emit isValidChanged();
    
    if (m_isValid) {
        updatePackageInfo();
        
        // 异步检查依赖
        if (!m_dependencyWatcher) {
            m_dependencyWatcher = new QFutureWatcher<bool>(this);
            connect(m_dependencyWatcher, &QFutureWatcher<bool>::finished, [this]() {
                m_canInstall = m_dependencyWatcher->result();
                if (!m_canInstall && m_preInstallMessage.isEmpty()) {
                    m_preInstallMessage = tr("Error: Cannot satisfy dependencies");
                }
                emit canInstallChanged();
                emit preInstallMessageChanged();
            });
        }
        
        // 使用 QFuture 进行异步操作
        QFuture<bool> future = QtConcurrent::run([this]() {
            return checkDependencies() && !checkConflicts() && !checkBreaksSystem();
        });
        m_dependencyWatcher->setFuture(future);
    } else {
        m_preInstallMessage = tr("Error: Invalid or corrupted package");
        emit preInstallMessageChanged();
    }
    
    emit fileNameChanged();
}

bool DebInstaller::parseDebFile()
{
    // 使用 dpkg 命令检查包是否有效
    QString output;
    if (!runDpkgCommand(QStringList() << "-I" << m_fileName, output)) {
        return false;
    }
    
    // 提取基本包信息
    m_packageName = extractControlField("Package");
    m_version = extractControlField("Version");
    m_maintainer = extractControlField("Maintainer");
    m_description = extractControlField("Description");
    m_homePage = extractControlField("Homepage");
    
    // 处理描述字段（只取第一行）
    int newlinePos = m_description.indexOf('\n');
    if (newlinePos > 0) {
        m_description = m_description.left(newlinePos);
    }
    
    // 获取安装大小
    QString installedSizeStr = extractControlField("Installed-Size");
    if (!installedSizeStr.isEmpty()) {
        bool ok;
        double size = installedSizeStr.toDouble(&ok);
        if (ok) {
            m_installedSize = formatByteSize(size * 1024.0, 1);
        }
    }
    
    return !m_packageName.isEmpty();
}

void DebInstaller::updatePackageInfo()
{
    if (!m_aptInitialized || m_packageName.isEmpty()) {
        m_isInstalled = false;
        m_installedVersion.clear();
        return;
    }
    
    pkgCache *cache = m_cacheFile->GetPkgCache();
    if (!cache) {
        m_isInstalled = false;
        m_installedVersion.clear();
        return;
    }
    
    pkgCache::PkgIterator pkg = cache->FindPkg(m_packageName.toStdString());
    if (pkg.end()) {
        m_isInstalled = false;
        m_installedVersion.clear();
    } else {
        m_isInstalled = (pkg->CurrentState == pkgCache::State::Installed);
        if (pkg.CurrentVer()) {
            m_installedVersion = QString::fromStdString(pkg.CurrentVer().VerStr());
        } else {
            m_installedVersion.clear();
        }
    }
    
    emit isInstalledChanged();
    emit installedVersionChanged();
    emit packageNameChanged();
    emit versionChanged();
    emit maintainerChanged();
    emit descriptionChanged();
    emit homePageChanged();
    emit installedSizeChanged();
}

bool DebInstaller::checkDependencies()
{
    // 使用 dpkg 检查依赖
    QString output;
    if (runDpkgCommand(QStringList() << "--dry-run" << "-i" << m_fileName, output)) {
        // 如果 dry-run 成功，说明依赖满足
        return true;
    } else {
        // 检查输出中是否包含依赖错误
        if (output.contains("depends", Qt::CaseInsensitive) || 
            output.contains("dependency", Qt::CaseInsensitive)) {
            m_preInstallMessage = tr("Error: Unmet dependencies");
            return false;
        }
        return true;
    }
}

bool DebInstaller::checkConflicts()
{
    // 简化的冲突检查
    // 在实际应用中，这里应该进行更详细的检查
    QString output;
    if (runDpkgCommand(QStringList() << "--dry-run" << "-i" << m_fileName, output)) {
        return false; // 没有冲突
    } else {
        // 检查输出中是否包含冲突信息
        if (output.contains("conflict", Qt::CaseInsensitive)) {
            m_preInstallMessage = tr("Error: Package conflicts");
            return true;
        }
        return false;
    }
}

bool DebInstaller::checkBreaksSystem()
{
    // 简化的系统破坏性检查
    // 在实际应用中，这里应该检查包是否会破坏关键系统组件
    return false;
}

void DebInstaller::install()
{
    if (!m_isValid || !m_canInstall) {
        return;
    }
    
    setStatus(Installing);
    m_statusMessage = tr("Starting installation");
    m_statusDetails.clear();
    emit statusMessageChanged();
    emit statusDetailsTextChanged();
    emit requestSwitchToInstallPage();
    
    // 使用 dpkg 安装 deb 包
    QStringList arguments;
    arguments << "-i" << m_fileName;
    
    m_installProcess->start("dpkg", arguments);
}

void DebInstaller::onInstallFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        setStatus(Succeeded);
        m_statusMessage = tr("Installation successful");
        m_isInstalled = true;
        emit isInstalledChanged();
    } else {
        setStatus(Error);
        m_statusMessage = tr("Installation failed");
        
        // 读取错误输出
        QString errorOutput = m_installProcess->readAllStandardError();
        if (errorOutput.isEmpty()) {
            errorOutput = m_installProcess->readAllStandardOutput();
        }
        
        if (!errorOutput.isEmpty()) {
            m_statusDetails += "\n" + tr("Error:") + "\n" + errorOutput;
            emit statusDetailsTextChanged();
        }
    }
    emit statusMessageChanged();
}

void DebInstaller::onInstallOutput()
{
    QString output = m_installProcess->readAllStandardOutput();
    if (!output.isEmpty()) {
        m_statusDetails += output;
        emit statusDetailsTextChanged();
    }
    
    output = m_installProcess->readAllStandardError();
    if (!output.isEmpty()) {
        m_statusDetails += output;
        emit statusDetailsTextChanged();
    }
}

// Getter 方法实现
QString DebInstaller::packageName() const { return m_packageName; }
QString DebInstaller::version() const { return m_version; }
QString DebInstaller::maintainer() const { return m_maintainer; }
QString DebInstaller::description() const { return m_description; }
bool DebInstaller::isValid() const { return m_isValid; }
bool DebInstaller::canInstall() const { return m_canInstall; }
QString DebInstaller::homePage() const { return m_homePage; }
QString DebInstaller::installedSize() const { return m_installedSize; }
QString DebInstaller::installedVersion() const { return m_installedVersion; }
bool DebInstaller::isInstalled() const { return m_isInstalled; }
QString DebInstaller::statusDetails() const { return m_statusDetails; }
QString DebInstaller::preInstallMessage() const { return m_preInstallMessage; }
DebInstaller::Status DebInstaller::status() const { return m_status; }
QString DebInstaller::statusMessage() const { return m_statusMessage; }

void DebInstaller::setStatus(DebInstaller::Status status)
{
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
}

QString DebInstaller::formatByteSize(double size, int precision) const
{
    int unit = 0;
    double multiplier = 1024.0;

    while (qAbs(size) >= multiplier && unit < 8) {
        size /= multiplier;
        ++unit;
    }

    if (unit == 0) {
        precision = 0;
    }

    QString numString = QString::number(size, 'f', precision);

    switch (unit) {
    case 0:
        return QString("%1 B").arg(numString);
    case 1:
        return QString("%1 KB").arg(numString);
    case 2:
        return QString("%1 MB").arg(numString);
    case 3:
        return QString("%1 GB").arg(numString);
    case 4:
        return QString("%1 TB").arg(numString);
    case 5:
        return QString("%1 PB").arg(numString);
    case 6:
        return QString("%1 EB").arg(numString);
    case 7:
        return QString("%1 ZB").arg(numString);
    case 8:
        return QString("%1 YB").arg(numString);
    default:
        return QString();
    }
}