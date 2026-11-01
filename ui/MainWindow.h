#pragma once

#include "core/AccountProfile.h"
#include "core/SettingsStore.h"
#include "core/Types.h"

#include <QMainWindow>
#include <QStringList>

class QAction;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTableView;
class QPlainTextEdit;
class QSystemTrayIcon;
class QTimer;

namespace arc {

class AccountProfileLoader;
class CodexClient;
class ThreadCatalog;
class ThreadTableModel;
class ThreadFilterProxy;
class JobEngine;
class QuotaWidget;
class PillProgress;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onThreadsUpdated(const QList<arc::ThreadInfo> &threads);
    void onRateLimitsUpdated(const arc::RateLimitSnapshot &snapshot);
    void onThreadsSelectionChanged();
    void onRefreshClicked();
    void onFilterChanged();
    void onAddWatchClicked();
    void onRunNowClicked();
    void onRemoveWatchClicked();
    void onThreadSettingsClicked();
    void onOpenSettings();
    void onJobsChanged();
    void onClientStateChanged(bool ready, const QString &detail);
    void onAccountProfileUpdated(const arc::AccountProfile &profile);
    void onTrayActivated();
    void onThreadContextMenu(const QPoint &pos);
    void onIgnoreInterruptedClicked();
    void onThreadStatusChanged(const QString &threadId, const QString &statusType,
                               const QStringList &activeFlags);
    void onTick();
    void showLog(const QString &message);

private:
    void setupUi();
    void setupMenuAndTray();
    void updateQuotaHeader();
    void updateSelectionButtons();
    void updateFloatingWidget();
    void refreshAccountHeader();
    ThreadInfo currentThread() const;
    ResumeJob currentJobOrDefault() const;
    int failedJobCount() const;
    void applySettingsToUi();
    void updateThemeStyles();
    void saveSettings();
    void showAbout();

    CodexClient *m_client = nullptr;
    AccountProfileLoader *m_profileLoader = nullptr;
    AccountProfile m_accountProfile;
    ThreadCatalog *m_catalog = nullptr;
    JobEngine *m_jobs = nullptr;
    AppSettings m_settings;

    ThreadTableModel *m_model = nullptr;
    ThreadFilterProxy *m_proxy = nullptr;
    QTableView *m_threadTable = nullptr;
    QComboBox *m_statusFilter = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_addWatchButton = nullptr;
    QPushButton *m_runNowButton = nullptr;
    QPushButton *m_removeWatchButton = nullptr;
    QPushButton *m_threadSettingsButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLabel *m_connectionLabel = nullptr;
    QLabel *m_planLabel = nullptr;
    QLabel *m_quotaFiveHourLabel = nullptr;
    QLabel *m_quotaWeeklyLabel = nullptr;
    PillProgress *m_fiveHourBar = nullptr;
    PillProgress *m_weeklyBar = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QLabel *m_detailLabel = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QuotaWidget *m_floating = nullptr;
    RateLimitSnapshot m_limits;
    QTimer *m_tickTimer = nullptr;
    int m_tickCounter = 0;
    QLabel *m_avatarLabel = nullptr;
    QLabel *m_userLabel = nullptr;
    QLabel *m_accountLabel = nullptr;
    QLabel *m_creditLabel = nullptr;
    QLabel *m_watchSummaryLabel = nullptr;
    QAction *m_showFloatingAction = nullptr;
    bool m_quitting = false;
    bool m_clientReady = false;
    bool m_clientStateKnown = false;
};

} // namespace arc