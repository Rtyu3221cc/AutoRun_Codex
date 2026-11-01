#pragma once

#include <QObject>
#include <QPixmap>
#include <QString>

class QNetworkAccessManager;

namespace arc {

// 当前 Codex 登录用户的资料（昵称 / 邮箱 / 头像）。
struct AccountProfile
{
    bool valid = false;
    QString name;
    QString email;
    QPixmap avatar; // 原始头像（未裁圆），可能为空
};

// 读取当前登录用户资料：
//   1. 本地 CODEX_HOME/auth.json 的 id_token（只读、不联网）→ 昵称 / 邮箱；
//   2. 只有本地没有缓存头像（或缓存超过 7 天）时，才做一次只读 HTTPS
//      GET https://chatgpt.com/backend-api/me 取头像 URL，再下载图片缓存到
//      %APPDATA%/AutoRun_Codex/account-avatar.png。
// 不写 auth.json、不刷新 token、不消耗额度或重置卡。
// 环境变量 AR_PROFILE_OFFLINE=1 可完全禁用联网（只用本地昵称/字母头像）。
class AccountProfileLoader : public QObject
{
    Q_OBJECT

public:
    explicit AccountProfileLoader(QObject *parent = nullptr);

    void load();
    const AccountProfile &profile() const { return m_profile; }

signals:
    void profileUpdated(const arc::AccountProfile &profile);
    void logMessage(const QString &message);

private:
    void readLocalAuth();
    void loadCachedAvatar();
    void fetchRemoteProfile();
    void downloadAvatar(const QString &url);
    void emitUpdated();

    QNetworkAccessManager *m_network = nullptr;
    AccountProfile m_profile;
    QString m_accessToken;
    bool m_remoteAttempted = false;
    bool m_remoteFailed = false;
};

} // namespace arc
