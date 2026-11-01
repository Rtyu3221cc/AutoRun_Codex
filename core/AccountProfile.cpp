#include "core/AccountProfile.h"

#include "core/CodexPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

namespace arc {
namespace {

constexpr int kAvatarRefreshDays = 7;
constexpr int kNetworkTimeoutMs = 8000;

QString avatarCachePath()
{
    return appDataDir() + QStringLiteral("/account-avatar.png");
}

QByteArray base64UrlDecode(const QString &part)
{
    QByteArray data = part.toLatin1();
    data.replace('-', '+').replace('_', '/');
    while (data.size() % 4 != 0)
        data.append('=');
    return QByteArray::fromBase64(data);
}

// 在 JSON 里按 key 优先级找第一个非空字符串（先当前层，再递归子对象）。
QString firstStringField(const QJsonValue &value, const QStringList &keys)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (const QString &key : keys) {
            const QJsonValue candidate = object.value(key);
            if (candidate.isString() && !candidate.toString().trimmed().isEmpty())
                return candidate.toString().trimmed();
        }
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString found = firstStringField(it.value(), keys);
            if (!found.isEmpty())
                return found;
        }
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            const QString found = firstStringField(item, keys);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

bool networkAllowed()
{
    return qEnvironmentVariableIsEmpty("AR_PROFILE_OFFLINE");
}

} // namespace

AccountProfileLoader::AccountProfileLoader(QObject *parent)
    : QObject(parent)
{
}

void AccountProfileLoader::load()
{
    m_profile = AccountProfile{};
    m_remoteAttempted = false;
    m_remoteFailed = false;
    readLocalAuth();
    loadCachedAvatar();
    m_profile.valid = !m_profile.name.trimmed().isEmpty()
            || !m_profile.email.trimmed().isEmpty()
            || !m_profile.avatar.isNull();
    emitUpdated();

    if (!networkAllowed() || m_accessToken.isEmpty())
        return;

    const QFileInfo cached(avatarCachePath());
    const bool needsFetch = !cached.exists()
            || cached.lastModified().daysTo(QDateTime::currentDateTime()) >= kAvatarRefreshDays;
    if (!needsFetch)
        return;

    m_remoteAttempted = true;
    fetchRemoteProfile();
}

void AccountProfileLoader::readLocalAuth()
{
    const QString path = codexHome() + QStringLiteral("/auth.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage(QStringLiteral("账号资料：未找到 auth.json（使用默认标题）"));
        return;
    }
    const QByteArray payload = file.readAll();
    file.close();
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    const QJsonObject tokens = root.value(QStringLiteral("tokens")).toObject();
    m_accessToken = tokens.value(QStringLiteral("access_token")).toString();
    const QString idToken = tokens.value(QStringLiteral("id_token")).toString();
    const QStringList parts = idToken.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return;
    const QJsonObject claims = QJsonDocument::fromJson(base64UrlDecode(parts.at(1))).object();
    m_profile.name = claims.value(QStringLiteral("name")).toString().trimmed();
    m_profile.email = claims.value(QStringLiteral("email")).toString().trimmed();
    if (!m_profile.name.isEmpty())
        emit logMessage(QStringLiteral("账号资料：本地昵称「%1」").arg(m_profile.name));
}

void AccountProfileLoader::loadCachedAvatar()
{
    if (!QFileInfo::exists(avatarCachePath()))
        return;
    QPixmap pixmap(avatarCachePath());
    if (!pixmap.isNull())
        m_profile.avatar = pixmap;
}

void AccountProfileLoader::fetchRemoteProfile()
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request(QUrl(QStringLiteral("https://chatgpt.com/backend-api/me")));
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "AutoRunCodex/0.1.6");
    request.setTransferTimeout(kNetworkTimeoutMs);
    emit logMessage(QStringLiteral("账号资料：读取头像/昵称为只读请求（不消耗额度）"));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_remoteFailed = true;
            emit logMessage(QStringLiteral("账号资料：联网读取失败（%1），继续用本地昵称/字母头像")
                                    .arg(reply->errorString()));
            return;
        }
        const QJsonObject profile = QJsonDocument::fromJson(reply->readAll()).object();
        const QString name = firstStringField(profile, {QStringLiteral("name"),
                                                        QStringLiteral("display_name"),
                                                        QStringLiteral("given_name")});
        if (!name.isEmpty() && name != m_profile.name) {
            m_profile.name = name;
            m_profile.valid = true;
            emitUpdated();
            emit logMessage(QStringLiteral("账号资料：昵称「%1」").arg(name));
        }
        const QString picture = firstStringField(profile, {QStringLiteral("picture"),
                                                           QStringLiteral("avatar_url"),
                                                           QStringLiteral("avatar"),
                                                           QStringLiteral("profile_picture"),
                                                           QStringLiteral("profile_image"),
                                                           QStringLiteral("photo"),
                                                           QStringLiteral("image")});
        if (picture.startsWith(QStringLiteral("http"), Qt::CaseInsensitive))
            downloadAvatar(picture);
        else
            emit logMessage(QStringLiteral("账号资料：服务端未提供头像地址（使用字母头像）"));
    });
}

void AccountProfileLoader::downloadAvatar(const QString &url)
{
    if (!m_network)
        return;
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "AutoRunCodex/0.1.6");
    request.setTransferTimeout(kNetworkTimeoutMs);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit logMessage(QStringLiteral("账号资料：头像下载失败（%1）").arg(reply->errorString()));
            return;
        }
        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll()) || pixmap.isNull()) {
            emit logMessage(QStringLiteral("账号资料：头像数据无法解析"));
            return;
        }
        QDir().mkpath(appDataDir());
        pixmap.save(avatarCachePath(), "PNG");
        m_profile.avatar = pixmap;
        m_profile.valid = true;
        emitUpdated();
        emit logMessage(QStringLiteral("账号资料：头像已缓存"));
    });
}

void AccountProfileLoader::emitUpdated()
{
    emit profileUpdated(m_profile);
}

} // namespace arc