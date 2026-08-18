// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Updates.hpp"

#include "common/Literals.hpp"
#include "common/Modes.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"
#include "util/PostToThread.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStringBuilder>
#include <QtConcurrent>
#include <semver/semver.hpp>

namespace {

using namespace chatterino;
using namespace literals;

QString currentBranch()
{
    return getSettings()->betaUpdates ? "beta" : "stable";
}

#if defined(Q_OS_WIN)
const QString CHATTERINO_OS = u"win"_s;
#elif defined(Q_OS_MACOS)
const QString CHATTERINO_OS = u"macos"_s;
#elif defined(Q_OS_LINUX)
const QString CHATTERINO_OS = u"linux"_s;
#elif defined(Q_OS_FREEBSD)
const QString CHATTERINO_OS = u"freebsd"_s;
#else
const QString CHATTERINO_OS = u"unknown"_s;
#endif

QJsonValue getForArchitecture(const QJsonObject &obj, const QString &key)
{
    auto val = obj[key];

#ifdef Q_PROCESSOR_ARM
    QString armKey = key % u"_arm";
    if (obj[armKey].isString())
    {
        val = obj[armKey];
    }
#elifdef Q_PROCESSOR_X86
    QString x86Key = key % u"_x86";
    if (obj[x86Key].isString())
    {
        val = obj[x86Key];
    }
#endif

    return val;
}

}  // namespace

namespace chatterino {

Updates::Updates(const Modes &modes_, const Paths &paths_, Settings &settings)
    : paths(paths_)
    , modes(modes_)
    , currentVersion_(CHATTERINO_VERSION)
    , updateGuideLink_("https://chatterino.com")
{
    qCDebug(chatterinoUpdate) << "init UpdateManager";

    settings.betaUpdates.connect(
        [this] {
            this->checkForUpdates();
        },
        this->managedConnections, false);
}

bool Updates::isDowngradeOf(const QString &online, const QString &current)
{
    semver::version onlineVersion;
    if (!onlineVersion.from_string_noexcept(online.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse online version"
                                    << online << "into a proper semver string";
        return false;
    }

    semver::version currentVersion;
    if (!currentVersion.from_string_noexcept(current.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse current version"
                                    << current << "into a proper semver string";
        return false;
    }

    if (onlineVersion.major == 7)
    {
        onlineVersion.major = 2;
    }
    else if (currentVersion.major == 7 && onlineVersion.major == 2)
    {
        currentVersion = {2, currentVersion.minor, currentVersion.patch,
                          currentVersion.prerelease_type,
                          currentVersion.prerelease_number};
    }

    return onlineVersion < currentVersion;
}

void Updates::deleteOldFiles()
{
    std::ignore = QtConcurrent::run([dir{this->paths.miscDirectory}] {
        {
            auto path = combinePath(dir, "Update.exe");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
        {
            auto path = combinePath(dir, "update.zip");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
    });
}

const QString &Updates::getCurrentVersion() const
{
    return this->currentVersion_;
}

const QString &Updates::getOnlineVersion() const
{
    return this->onlineVersion_;
}

void Updates::installUpdates()
{
    if (this->status_ != UpdateAvailable)
    {
        assert(false);
        return;
    }

#ifdef Q_OS_MACOS
    if (!this->updateExe_.isEmpty())
    {
        QDesktopServices::openUrl(QUrl(this->updateExe_));
    }
    else
    {
        QDesktopServices::openUrl(
            QUrl("https://github.com/w77hxhx/sloperino/releases"));
    }
#elif defined Q_OS_LINUX
    if (!this->updateExe_.isEmpty())
    {
        QDesktopServices::openUrl(QUrl(this->updateExe_));
    }
    else
    {
        QDesktopServices::openUrl(
            QUrl("https://github.com/w77hxhx/sloperino/releases"));
    }
#elif defined Q_OS_WIN
    if (this->modes.isPortable && this->updatePortable_.isEmpty() &&
        this->updateExe_.isEmpty())
    {
        QDesktopServices::openUrl(
            QUrl("https://github.com/w77hxhx/sloperino/releases"));
        return;
    }
    if (!this->modes.isPortable && this->updateExe_.isEmpty())
    {
        QDesktopServices::openUrl(
            QUrl("https://github.com/w77hxhx/sloperino/releases"));
        return;
    }

    if (this->modes.isPortable)
    {
        QMessageBox *box =
            new QMessageBox(QMessageBox::Information, "Chatterino Update",
                            "Chatterino is downloading the update "
                            "in the background and will run the "
                            "updater once it is finished.");
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->show();

        NetworkRequest(this->updatePortable_)
            .timeout(600000)
            .followRedirects(true)
            .onError([this](NetworkResult) {
                this->setStatus_(DownloadFailed);

                postToThread([] {
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed while trying to download the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->show();
                    box->raise();
                });
            })
            .onSuccess([this](auto result) {
                if (result.status() != 200)
                {
                    auto *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        QStringLiteral("The update couldn't be downloaded "
                                       "(Error: %1).")
                            .arg(result.formatError()));
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();
                    return;
                }

                QByteArray object = result.getData();
                auto filename =
                    combinePath(this->paths.miscDirectory, "update.zip");

                QFile file(filename);
                if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly))
                {
                    qCWarning(chatterinoUpdate)
                        << "Failed to save update.zip" << file.errorString();
                    this->setStatus_(WriteFileFailed);
                    return;
                }

                if (file.write(object) == -1)
                {
                    this->setStatus_(WriteFileFailed);
                    return;
                }
                file.flush();
                file.close();

                auto updaterPath = Updates::portableUpdaterPath(this->paths);
                if (!QFile::exists(updaterPath))
                {
                    this->setStatus_(MissingPortableUpdater);
                    return;
                }
                bool ok =
                    QProcess::startDetached(updaterPath, {filename, "restart"});
                if (!ok)
                {
                    this->setStatus_(RunUpdaterFailed);
                    return;
                }

                QApplication::exit(0);
            })
            .execute();
        this->setStatus_(Downloading);
    }
    else
    {
        QMessageBox *box =
            new QMessageBox(QMessageBox::Information, "Chatterino Update",
                            "Chatterino is downloading the update "
                            "in the background and will run the "
                            "updater once it is finished.");
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->show();

        NetworkRequest(this->updateExe_)
            .timeout(600000)
            .followRedirects(true)
            .onError([this](NetworkResult) {
                this->setStatus_(DownloadFailed);

                QMessageBox *box = new QMessageBox(
                    QMessageBox::Information, "Chatterino Update",
                    "Failed to download the update. \n\nTry manually "
                    "downloading the update.");
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->exec();
            })
            .onSuccess([this](auto result) {
                if (result.status() != 200)
                {
                    auto *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        QStringLiteral("The update couldn't be downloaded "
                                       "(Error: %1).")
                            .arg(result.formatError()));
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();
                    return;
                }

                QByteArray object = result.getData();
                auto filePath =
                    combinePath(this->paths.miscDirectory, "Update.exe");

                QFile file(filePath);

                std::ignore =
                    file.open(QIODevice::Truncate | QIODevice::WriteOnly);

                if (file.write(object) == -1)
                {
                    this->setStatus_(WriteFileFailed);
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed to save the update file. This could be due to "
                        "window settings or antivirus software.\n\nTry "
                        "manually "
                        "downloading the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();

                    QDesktopServices::openUrl(this->updateExe_);
                    return;
                }
                file.flush();
                file.close();

                if (QProcess::startDetached(filePath, {}))
                {
                    QApplication::exit(0);
                }
                else
                {
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed to execute update binary. This could be due to "
                        "window "
                        "settings or antivirus software.\n\nTry manually "
                        "downloading "
                        "the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();

                    QDesktopServices::openUrl(this->updateExe_);
                }
            })
            .execute();
        this->setStatus_(Downloading);
    }
#endif
}

void Updates::checkForUpdates(bool promptUser)
{
    this->promptOnUpdate_ = promptUser;

#ifndef CHATTERINO_DISABLE_UPDATER
    auto version = Version::instance();

    if (!version.isSupportedOS())
    {
        qCDebug(chatterinoUpdate)
            << "Update checking disabled because OS doesn't appear to be one "
               "of Windows, GNU/Linux or macOS.";
        return;
    }

    if (version.isFlatpak())
    {
        return;
    }

    auto parseReleaseJson = [this](const QJsonObject &object) -> bool {
        if (object.isEmpty())
        {
            return false;
        }

        QString tagName = object["tag_name"].toString();
        QString publishedAt = object["published_at"].toString();
        QJsonArray assets = object["assets"].toArray();

        QString cleanVersion = tagName;
        if (cleanVersion.startsWith('v') || cleanVersion.startsWith('V'))
        {
            cleanVersion = cleanVersion.mid(1);
        }

        QString exeUrl;
        QString portableUrl;
        QString dmgUrl;
        QString appImageUrl;

        for (const auto &assetVal : assets)
        {
            auto assetObj = assetVal.toObject();
            QString name = assetObj["name"].toString();
            QString downloadUrl = assetObj["browser_download_url"].toString();

            if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            {
                if (name.contains(QStringLiteral("Installer"),
                                  Qt::CaseInsensitive) ||
                    exeUrl.isEmpty())
                {
                    exeUrl = downloadUrl;
                }
            }
            if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
            {
                if (name.contains(QStringLiteral("Portable"),
                                  Qt::CaseInsensitive) ||
                    portableUrl.isEmpty())
                {
                    portableUrl = downloadUrl;
                }
            }
            if (name.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive))
            {
                dmgUrl = downloadUrl;
            }
            if (name.endsWith(QStringLiteral(".AppImage"),
                              Qt::CaseInsensitive) ||
                name.endsWith(QStringLiteral(".deb"), Qt::CaseInsensitive))
            {
                appImageUrl = downloadUrl;
            }
        }

#    if defined(Q_OS_WIN)
        this->updateExe_ = exeUrl;
        this->updatePortable_ = portableUrl.isEmpty() ? exeUrl : portableUrl;
#    elif defined(Q_OS_MACOS)
        this->updateExe_ = dmgUrl;
#    elif defined(Q_OS_LINUX)
        this->updateExe_ = appImageUrl;
#    endif

        if (tagName == QStringLiteral("nightly-build"))
        {
            this->onlineVersion_ = QStringLiteral("nightly-build");
            this->setStatus_(UpdateAvailable);
            return true;
        }

        this->onlineVersion_ = cleanVersion;

        if (this->currentVersion_ != this->onlineVersion_ &&
            !Updates::isDowngradeOf(this->onlineVersion_,
                                    this->currentVersion_))
        {
            this->setStatus_(UpdateAvailable);
            return true;
        }
        else
        {
            this->setStatus_(NoUpdateAvailable);
            return true;
        }
    };

    auto onGitHubLatestSuccess = [this, parseReleaseJson](
                                     const NetworkResult &result) {
        const auto object = result.parseJson();
        if (!parseReleaseJson(object))
        {
            // Fallback to checking nightly-build tag
            NetworkRequest(
                QStringLiteral("https://api.github.com/repos/w77hxhx/"
                               "sloperino/releases/tags/nightly-build"))
                .timeout(30000)
                .followRedirects(true)
                .header("User-Agent", "Sloperino-App")
                .onSuccess(
                    [this, parseReleaseJson](const NetworkResult &nightlyRes) {
                        const auto nightlyObj = nightlyRes.parseJson();
                        if (!parseReleaseJson(nightlyObj))
                        {
                            this->setStatus_(SearchFailed);
                        }
                    })
                .onError([this](NetworkResult) {
                    this->setStatus_(SearchFailed);
                })
                .execute();
        }
    };

    auto onGitHubLatestError = [this, parseReleaseJson](NetworkResult) {
        // Try nightly-build tag
        NetworkRequest(QStringLiteral("https://api.github.com/repos/w77hxhx/"
                                      "sloperino/releases/tags/nightly-build"))
            .timeout(30000)
            .followRedirects(true)
            .header("User-Agent", "Sloperino-App")
            .onSuccess(
                [this, parseReleaseJson](const NetworkResult &nightlyRes) {
                    const auto nightlyObj = nightlyRes.parseJson();
                    if (!parseReleaseJson(nightlyObj))
                    {
                        this->setStatus_(SearchFailed);
                    }
                })
            .onError([this](NetworkResult) {
                this->setStatus_(SearchFailed);
            })
            .execute();
    };

    this->setStatus_(Searching);

    NetworkRequest(
        QStringLiteral(
            "https://api.github.com/repos/w77hxhx/sloperino/releases/latest"))
        .timeout(30000)
        .followRedirects(true)
        .header("User-Agent", "Sloperino-App")
        .onSuccess(onGitHubLatestSuccess)
        .onError(onGitHubLatestError)
        .execute();
#endif
}

Updates::Status Updates::getStatus() const
{
    return this->status_;
}

QString Updates::portableUpdaterPath(const Paths &paths)
{
    return combinePath(paths.rootAppDataDirectory,
                       "updater.1/ChatterinoUpdater.exe");
}

bool Updates::shouldShowUpdateButton() const
{
    switch (this->getStatus())
    {
        case UpdateAvailable:
        case SearchFailed:
        case Downloading:
        case DownloadFailed:
        case WriteFileFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isError() const
{
    switch (this->getStatus())
    {
        case SearchFailed:
        case DownloadFailed:
        case WriteFileFailed:
        case MissingPortableUpdater:
        case RunUpdaterFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isDowngrade() const
{
    return this->isDowngrade_;
}

QString Updates::buildUpdateAvailableText() const
{
    const auto &version = Version::instance();

    if (version.isNightly())
    {
        // Since Nightly builds can be installed in many different ways, we ask the user to download the update manually.
        if (this->isDowngrade())
        {
            return QString(
                       "The version online (%1) seems to be lower than the "
                       "current (%2).\nEither a version was reverted or "
                       "you are running a newer build.\n\nDo you want to "
                       "head to github.com/w77hxhx/sloperino to download it?")
                .arg(this->getOnlineVersion(), this->getCurrentVersion());
        }

        return QString(
                   "An update (%1) is available.\n\nDo you want to head to "
                   "github.com/w77hxhx/sloperino to download the new update?")
            .arg(this->getOnlineVersion());
    }

    if (this->isDowngrade())
    {
        return QString("The version online (%1) seems to be lower than the "
                       "current (%2).\nEither a version was reverted or "
                       "you are running a newer build.\n\nDo you want to "
                       "download and install it?")
            .arg(this->getOnlineVersion(), this->getCurrentVersion());
    }

    return QString("An update (%1) is available.\n\nDo you want to "
                   "download and install it?")
        .arg(this->getOnlineVersion());
}

void Updates::setStatus_(Status status)
{
    if (this->status_ != status)
    {
        this->status_ = status;
        postToThread([this, status] {
            this->statusUpdated.invoke(status);

            if (status == UpdateAvailable && this->promptOnUpdate_)
            {
                this->promptOnUpdate_ = false;
                QMessageBox *box = new QMessageBox(
                    QMessageBox::Question, "Sloperino Update Available",
                    QStringLiteral(
                        "A new version of Sloperino (%1) is available on "
                        "GitHub!\n\n"
                        "Would you like to download and install the update "
                        "now?")
                        .arg(this->onlineVersion_),
                    QMessageBox::Yes | QMessageBox::No,
                    QApplication::activeWindow());
                box->setAttribute(Qt::WA_DeleteOnClose);
                if (box->exec() == QMessageBox::Yes)
                {
                    this->installUpdates();
                }
            }
        });
    }
}

}  // namespace chatterino
