#include "ui/MainWindow.h"

#include "core/AccountProfile.h"
#include "core/CodexClient.h"
#include "core/CodexPaths.h"
#include "core/JobEngine.h"
#include "core/RolloutScanner.h"
#include "core/SettingsStore.h"
#include "core/ThreadCatalog.h"
#include "ui/PillProgress.h"
#include "ui/QuotaWidget.h"
#include "ui/AvatarUtils.h"
#include "ui/Theme.h"
#include "ui/SettingsDialog.h"
#include "ui/ThreadSettingsDialog.h"
#include "ui/ThreadTableModel.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace arc {
namespace {

QColor progressColor(int percent)
{
    return theme::quotaColor(percent);
}

QString shorten(const QString &text, int maxLength = 48)
{
    QString oneLine = text.simplified();
    if (oneLine.size() <= maxLength)
        return oneLine;
    return oneLine.left(maxLength - 1) + QStringLiteral("…");
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_settings(SettingsStore::load())
{
    setWindowTitle(QStringLiteral("AutoRun Codex · 额度看守"));
    setWindowIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    resize(1180, 780);
    setMinimumSize(980, 620);

    m_client = new CodexClient(this);
    m_catalog = new ThreadCatalog(m_client, this);
    m_jobs = new JobEngine(m_client, this);

    setupUi();
    setupMenuAndTray();

    connect(m_client, &CodexClient::ready, this, [this]() {
        m_catalog->refresh();
        m_client->readRateLimits(nullptr);
    });
    connect(m_client, &CodexClient::connectionStateChanged, this, &MainWindow::onClientStateChanged);
    connect(m_client, &CodexClient::rateLimitsUpdated, this, &MainWindow::onRateLimitsUpdated);
    connect(m_client, &CodexClient::logMessage, this, &MainWindow::showLog);
    connect(m_client, &CodexClient::threadStatusChanged, this, &MainWindow::onThreadStatusChanged);
    connect(m_catalog, &ThreadCatalog::threadsUpdated, this, &MainWindow::onThreadsUpdated);
    connect(m_catalog, &ThreadCatalog::errorOccurred, this, &MainWindow::showLog);
    connect(m_jobs, &JobEngine::jobsChanged, this, &MainWindow::onJobsChanged);
    connect(m_jobs, &JobEngine::logMessage, this, &MainWindow::showLog);
    connect(m_jobs, &JobEngine::notifyRequested, this,
            [this](const QString &title, const QString &message, bool warning) {
                if (!m_settings.trayNotifications || !m_tray)
                    return;
                m_tray->showMessage(title, message,
                                    warning ? QSystemTrayIcon::Warning
                                            : QSystemTrayIcon::Information,
                                    6000);
            });
    connect(m_jobs, &JobEngine::jobStateChanged, this, [this](const QString &) { updateSelectionButtons(); });

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(5000);
    connect(m_tickTimer, &QTimer::timeout, this, &MainWindow::onTick);
    m_tickTimer->start();

    // 开发/验收用：AR_UI_THEME=light|dark 可强制指定主题（只影响本次进程，不写回 settings.json）。
    const QString themeOverride = qEnvironmentVariable("AR_UI_THEME").trimmed();
    if (!themeOverride.isEmpty())
        m_settings.uiTheme = themeOverride;

    applySettingsToUi();
    m_client->start(m_settings.codexCliPath);

    // 账号资料：本地 auth.json 立即读出昵称；头像缓存缺失时做一次只读 HTTP 获取。
    m_profileLoader = new AccountProfileLoader(this);
    connect(m_profileLoader, &AccountProfileLoader::profileUpdated,
            this, &MainWindow::onAccountProfileUpdated);
    connect(m_profileLoader, &AccountProfileLoader::logMessage, this, &MainWindow::showLog);
    m_profileLoader->load();

    QTimer::singleShot(1500, this, [this]() {
        if (m_client->isReady()) {
            m_catalog->refresh();
            m_client->readRateLimits(nullptr);
        }
    });

    // 开发/验收用：AR_SCREENSHOT_DIR 非空时，4 秒后把窗口渲染成 PNG 并退出。
    // 可选参数：
    //   AR_SCREENSHOT_FORCE_PULSE=1            强制按“有进行中对话”渲染（检查弹卡进度条呼吸光晕）
    //   AR_SCREENSHOT_SWITCH_THEME=light|dark  弹卡可见时切换主题（检查文字颜色是否即时刷新）
    //   AR_SCREENSHOT_CLOSE=1                  触发收回动画，并抓取动画中间帧
    const QString shotDir = qEnvironmentVariable("AR_SCREENSHOT_DIR");
    if (!shotDir.isEmpty()) {
        QTimer::singleShot(4000, this, [this, shotDir]() {
            QDir().mkpath(shotDir);
            // AR_SCREENSHOT_FAKE_REMAINING="82,62"：用假的“剩余百分比”渲染，检查颜色分档。
            const QString fakeRemaining = qEnvironmentVariable("AR_SCREENSHOT_FAKE_REMAINING").trimmed();
            if (!fakeRemaining.isEmpty()) {
                const QStringList parts = fakeRemaining.split(QLatin1Char(','), Qt::SkipEmptyParts);
                RateLimitSnapshot fake;
                fake.valid = true;
                fake.planType = QStringLiteral("plus");
                fake.resetCreditsAvailable = 2;
                fake.readAtMs = QDateTime::currentMSecsSinceEpoch();
                // AR_SCREENSHOT_FAKE_CREDIT_DAYS="1.5"：假的“最近一张重置卡剩余天数”，
                // 用来抓 <2 天（红 + 橙点）/ <7 天（黄）/ >=7 天（绿）三档。
                const QString fakeCreditDays = qEnvironmentVariable("AR_SCREENSHOT_FAKE_CREDIT_DAYS").trimmed();
                if (!fakeCreditDays.isEmpty()) {
                    ResetCredit credit;
                    credit.id = QStringLiteral("fake-reset-credit");
                    credit.title = QStringLiteral("Full reset (Weekly + 5 hr)");
                    credit.status = QStringLiteral("available");
                    credit.grantedAt = nowSec() - 86400;
                    credit.expiresAt = nowSec()
                            + static_cast<qint64>(fakeCreditDays.toDouble() * 86400.0);
                    fake.resetCredits.append(credit);
                }
                fake.primary = RateLimitWindow{true, qBound(0, 100 - parts.value(0).trimmed().toInt(), 100),
                                               300, nowSec() + 3 * 3600};
                fake.secondary = RateLimitWindow{true, qBound(0, 100 - parts.value(1).trimmed().toInt(), 100),
                                                 10080, nowSec() + 5 * 86400};
                m_limits = fake;
                updateQuotaHeader();
                m_floating->setSnapshot(fake);
                // 让刚改过文字的布局先完成一次激活，否则截图可能抓到被裁掉的旧宽度。
                QApplication::processEvents();
            }

            grab().save(shotDir + QStringLiteral("/main-window.png"));
            if (!m_floating) {
                QCoreApplication::exit(0);
                return;
            }
            if (qEnvironmentVariableIsSet("AR_SCREENSHOT_FORCE_PULSE"))
                m_floating->setWatchedCount(1, 1);

            // AR_SCREENSHOT_DIALOGS=1：顺带抓取设置对话框（检查浅色主题下文字是否可读）。
            if (qEnvironmentVariableIsSet("AR_SCREENSHOT_DIALOGS")) {
                SettingsDialog settingsDialog(m_settings, this);
                settingsDialog.show();
                QApplication::processEvents();
                settingsDialog.grab().save(shotDir + QStringLiteral("/settings-dialog.png"));
                settingsDialog.close();

                ThreadInfo demoThread;
                demoThread.id = QStringLiteral("demo-thread-id");
                demoThread.name = QStringLiteral("示例对话：完善结算界面");
                demoThread.cwd = QStringLiteral("D:/MyWorkSpace/Demo");
                ResumeJob demoJob;
                demoJob.threadId = demoThread.id;
                demoJob.threadName = demoThread.name;
                demoJob.prompt = m_settings.defaultPrompt;
                demoJob.enabled = true;
                demoJob.state = JobState::Waiting;
                demoJob.nextTriggerAt = nowSec() + 1800;
                ThreadSettingsDialog threadDialog(demoThread, demoJob, this);
                threadDialog.show();
                QApplication::processEvents();
                threadDialog.grab().save(shotDir + QStringLiteral("/thread-settings-dialog.png"));
                threadDialog.close();
            }

            m_floating->showCard();
            QTimer::singleShot(70, this, [this, shotDir]() {
                if (m_floating && m_floating->cardWidget())
                    m_floating->cardWidget()->grab().save(shotDir + QStringLiteral("/quota-card-opening.png"));
            });

            auto finish = [this, shotDir]() {
                if (!qEnvironmentVariableIsSet("AR_SCREENSHOT_CLOSE")) {
                    QCoreApplication::exit(0);
                    return;
                }
                m_floating->hideCard();
                QTimer::singleShot(70, this, [this, shotDir]() {
                    if (m_floating && m_floating->cardWidget())
                        m_floating->cardWidget()->grab().save(shotDir + QStringLiteral("/quota-card-closing.png"));
                    QTimer::singleShot(400, this, [this, shotDir]() {
                        if (!qEnvironmentVariableIsSet("AR_SCREENSHOT_REOPEN")) {
                            QCoreApplication::exit(0);
                            return;
                        }
                        // 收回后再打开一次，验证第二次弹出仍然能正确计算尺寸。
                        m_floating->showCard();
                        QTimer::singleShot(650, this, [this, shotDir]() {
                            if (QWidget *card = m_floating->cardWidget())
                                card->grab().save(shotDir + QStringLiteral("/quota-card-reopen.png"));
                            QCoreApplication::exit(0);
                        });
                    });
                });
            };

            QTimer::singleShot(650, this, [this, shotDir, finish]() {
                m_floating->grab().save(shotDir + QStringLiteral("/quota-widget.png"));
                if (QWidget *card = m_floating->cardWidget())
                    card->grab().save(shotDir + QStringLiteral("/quota-card.png"));

                const QString switchTheme = qEnvironmentVariable("AR_SCREENSHOT_SWITCH_THEME").trimmed();
                if (switchTheme.isEmpty()) {
                    finish();
                    return;
                }
                m_settings.uiTheme = switchTheme;
                applySettingsToUi();
                QTimer::singleShot(450, this, [this, shotDir, finish]() {
                    if (QWidget *card = m_floating->cardWidget())
                        card->grab().save(shotDir + QStringLiteral("/quota-card-switched.png"));
                    grab().save(shotDir + QStringLiteral("/main-window-switched.png"));
                    if (m_floating)
                        m_floating->grab().save(shotDir + QStringLiteral("/quota-widget-switched.png"));
                    finish();
                });
            });
        });
    }

    showLog(QStringLiteral("CODEX_HOME: ") + codexHome());
    showLog(QStringLiteral("等待连接到 Codex app-server…"));
}

MainWindow::~MainWindow()
{
    if (m_client)
        m_client->stop();
    if (m_floating) {
        m_floating->hide();
        delete m_floating;
        m_floating = nullptr;
    }
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // ---- header -------------------------------------------------------
    auto *header = new QFrame(central);
    header->setObjectName(QStringLiteral("headerFrame"));
    header->setStyleSheet(QStringLiteral("QFrame#headerFrame{background:%1;border:1px solid %2;border-radius:8px;}")
                                  .arg(theme::surface().name(), theme::border().name()));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(12);

    // 当前用户头像 + 昵称（取不到资料时回退“Codex 用量看守”）
    m_avatarLabel = new QLabel(header);
    m_avatarLabel->setFixedSize(22, 22);
    m_avatarLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_avatarLabel->hide();
    headerLayout->addWidget(m_avatarLabel);

    m_userLabel = new QLabel(QStringLiteral("◉ Codex 用量看守"), header);
    QFont logoFont = m_userLabel->font();
    logoFont.setPointSize(12);
    logoFont.setBold(true);
    m_userLabel->setFont(logoFont);
    headerLayout->addWidget(m_userLabel);

    m_accountLabel = new QLabel(QStringLiteral("—"), header);
    m_accountLabel->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));
    // 重置卡数量单独一个 label，颜色按“最近一张卡的剩余时间”分档。
    m_creditLabel = new QLabel(header);
    m_creditLabel->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));
    headerLayout->addWidget(m_accountLabel);
    headerLayout->addWidget(m_creditLabel);
    headerLayout->addStretch(1);

    m_connectionLabel = new QLabel(QStringLiteral("连接中…"), header);
    m_connectionLabel->setStyleSheet(QStringLiteral("color:%1;").arg(theme::yellow().name()));
    headerLayout->addWidget(m_connectionLabel);

    m_refreshButton = new QPushButton(QStringLiteral("刷新"), header);
    headerLayout->addWidget(m_refreshButton);
    auto *settingsButton = new QPushButton(QStringLiteral("设置"), header);
    headerLayout->addWidget(settingsButton);
    root->addWidget(header);

    // ---- quota bars ---------------------------------------------------
    auto *quotaFrame = new QFrame(central);
    quotaFrame->setObjectName(QStringLiteral("quotaFrame"));
    quotaFrame->setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;border-radius:8px;}")
                                      .arg(theme::surface().name(), theme::border().name()));
    auto *quotaLayout = new QGridLayout(quotaFrame);
    quotaLayout->setContentsMargins(12, 8, 12, 8);
    quotaLayout->setHorizontalSpacing(10);
    quotaLayout->setVerticalSpacing(6);

    auto *fiveHourTitle = new QLabel(QStringLiteral("5 小时额度"), quotaFrame);
    fiveHourTitle->setObjectName(QStringLiteral("fiveHourTitle"));
    fiveHourTitle->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));
    m_fiveHourBar = new PillProgress(quotaFrame);
    m_quotaFiveHourLabel = new QLabel(QStringLiteral("等待数据…"), quotaFrame);
    m_quotaFiveHourLabel->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));

    auto *weeklyTitle = new QLabel(QStringLiteral("每周额度"), quotaFrame);
    weeklyTitle->setObjectName(QStringLiteral("weeklyTitle"));
    weeklyTitle->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));
    m_weeklyBar = new PillProgress(quotaFrame);
    m_quotaWeeklyLabel = new QLabel(QStringLiteral("等待数据…"), quotaFrame);
    m_quotaWeeklyLabel->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));

    quotaLayout->addWidget(fiveHourTitle, 0, 0);
    quotaLayout->addWidget(m_fiveHourBar, 0, 1);
    quotaLayout->addWidget(m_quotaFiveHourLabel, 0, 2);
    quotaLayout->addWidget(weeklyTitle, 1, 0);
    quotaLayout->addWidget(m_weeklyBar, 1, 1);
    quotaLayout->addWidget(m_quotaWeeklyLabel, 1, 2);
    m_watchSummaryLabel = new QLabel(QStringLiteral("看守队列：0"), quotaFrame);
    m_watchSummaryLabel->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));
    quotaLayout->addWidget(m_watchSummaryLabel, 2, 0, 1, 3);
    quotaLayout->setColumnStretch(1, 1);
    root->addWidget(quotaFrame);

    // ---- main splitter ------------------------------------------------
    auto *splitter = new QSplitter(Qt::Horizontal, central);

    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto *filterRow = new QHBoxLayout();
    m_statusFilter = new QComboBox(left);
    m_statusFilter->addItem(QStringLiteral("全部对话"), QStringLiteral("all"));
    m_statusFilter->addItem(QStringLiteral("进行中"), QStringLiteral("running"));
    m_statusFilter->addItem(QStringLiteral("限额中断"), QStringLiteral("pending"));
    m_statusFilter->addItem(QStringLiteral("历史"), QStringLiteral("history"));
    filterRow->addWidget(m_statusFilter);
    m_searchEdit = new QLineEdit(left);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索标题 / 目录 / 线程 ID"));
    filterRow->addWidget(m_searchEdit, 1);
    leftLayout->addLayout(filterRow);

    m_threadTable = new QTableView(left);
    m_model = new ThreadTableModel(this);
    m_proxy = new ThreadFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_threadTable->setModel(m_proxy);
    m_threadTable->setSortingEnabled(true);
    m_threadTable->sortByColumn(ThreadTableModel::ColumnUpdated, Qt::DescendingOrder);
    m_threadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_threadTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_threadTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_threadTable->setAlternatingRowColors(true);
    m_threadTable->setShowGrid(false);
    m_threadTable->verticalHeader()->setVisible(false);
    m_threadTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_threadTable->horizontalHeader()->setSectionResizeMode(ThreadTableModel::ColumnTitle,
                                                            QHeaderView::Stretch);
    m_threadTable->setColumnWidth(ThreadTableModel::ColumnStatus, 80);
    m_threadTable->setColumnWidth(ThreadTableModel::ColumnUpdated, 120);
    m_threadTable->setColumnWidth(ThreadTableModel::ColumnWatch, 80);
    m_threadTable->setColumnWidth(ThreadTableModel::ColumnNextTrigger, 160);
    m_threadTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_threadTable, &QTableView::customContextMenuRequested, this,
            &MainWindow::onThreadContextMenu);
    leftLayout->addWidget(m_threadTable, 1);

    auto *right = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    m_detailLabel = new QLabel(QStringLiteral("选择一个对话查看详情"), right);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailLabel->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:8px;"
                                                "padding:10px;color:%3;")
                                         .arg(theme::surface().name(), theme::border().name(),
                                              theme::text().name()));
    m_detailLabel->setMinimumHeight(150);
    m_detailLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    rightLayout->addWidget(m_detailLabel);

    auto *buttonGrid = new QGridLayout();
    buttonGrid->setHorizontalSpacing(8);
    buttonGrid->setVerticalSpacing(8);
    m_addWatchButton = new QPushButton(QStringLiteral("加入看守"), right);
    m_runNowButton = new QPushButton(QStringLiteral("立即续跑"), right);
    m_removeWatchButton = new QPushButton(QStringLiteral("取消看守"), right);
    m_threadSettingsButton = new QPushButton(QStringLiteral("单独设置…"), right);
    buttonGrid->addWidget(m_addWatchButton, 0, 0);
    buttonGrid->addWidget(m_runNowButton, 0, 1);
    buttonGrid->addWidget(m_removeWatchButton, 1, 0);
    buttonGrid->addWidget(m_threadSettingsButton, 1, 1);
    rightLayout->addLayout(buttonGrid);

    auto *logTitle = new QLabel(QStringLiteral("运行日志"), right);
    logTitle->setObjectName(QStringLiteral("logTitle"));
    logTitle->setStyleSheet(QStringLiteral("color:%1;").arg(theme::textMuted().name()));
    rightLayout->addWidget(logTitle);
    m_logView = new QPlainTextEdit(right);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(600);
    rightLayout->addWidget(m_logView, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({760, 400});
    root->addWidget(splitter, 1);

    setCentralWidget(central);

    m_removeWatchButton->setToolTip(QStringLiteral("仅对已加入看守队列的对话可用；未加入时没有可取消的续跑"));
    m_addWatchButton->setToolTip(QStringLiteral("加入看守队列；被限额打断的对话会在额度恢复后自动续跑"));
    m_runNowButton->setToolTip(QStringLiteral("立即向选中对话发送续跑 prompt"));
    connect(m_threadTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &MainWindow::onThreadsSelectionChanged);
    connect(m_threadTable, &QTableView::doubleClicked, this, &MainWindow::onThreadSettingsClicked);
    connect(m_statusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onFilterChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::onOpenSettings);
    connect(m_addWatchButton, &QPushButton::clicked, this, &MainWindow::onAddWatchClicked);
    connect(m_runNowButton, &QPushButton::clicked, this, &MainWindow::onRunNowClicked);
    connect(m_removeWatchButton, &QPushButton::clicked, this, &MainWindow::onRemoveWatchClicked);
    connect(m_threadSettingsButton, &QPushButton::clicked, this, &MainWindow::onThreadSettingsClicked);

    updateThemeStyles();
    updateSelectionButtons();
}

void MainWindow::setupMenuAndTray()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    auto *settingsAction = fileMenu->addAction(QStringLiteral("设置…"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(QStringLiteral("退出"));
    connect(quitAction, &QAction::triggered, this, [this]() {
        m_quitting = true;
        saveSettings();
        m_client->stop();
        QCoreApplication::exit(0);
    });

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("查看"));
    auto *refreshAction = viewMenu->addAction(QStringLiteral("刷新"));
    connect(refreshAction, &QAction::triggered, this, &MainWindow::onRefreshClicked);
    auto *ignoreAction = viewMenu->addAction(QStringLiteral("忽略/恢复选中对话的限额提示"));
    connect(ignoreAction, &QAction::triggered, this, &MainWindow::onIgnoreInterruptedClicked);
    m_showFloatingAction = viewMenu->addAction(QStringLiteral("显示悬浮条"));
    m_showFloatingAction->setCheckable(true);
    connect(m_showFloatingAction, &QAction::toggled, this, [this](bool checked) {
        m_settings.showFloatingWidget = checked;
        if (m_floating)
            m_floating->setVisible(checked);
        saveSettings();
    });

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    auto *aboutAction = helpMenu->addAction(QStringLiteral("关于"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(windowIcon());
    m_tray->setToolTip(QStringLiteral("AutoRun Codex · 额度看守"));
    auto *trayMenu = new QMenu(this);
    auto *openAction = trayMenu->addAction(QStringLiteral("打开主窗口"));
    connect(openAction, &QAction::triggered, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
    auto *trayRefreshAction = trayMenu->addAction(QStringLiteral("刷新"));
    connect(trayRefreshAction, &QAction::triggered, this, &MainWindow::onRefreshClicked);
    auto *traySettingsAction = trayMenu->addAction(QStringLiteral("设置…"));
    connect(traySettingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
    trayMenu->addSeparator();
    auto *trayQuitAction = trayMenu->addAction(QStringLiteral("退出"));
    connect(trayQuitAction, &QAction::triggered, this, [this]() {
        m_quitting = true;
        saveSettings();
        m_client->stop();
        QCoreApplication::exit(0);
    });
    m_tray->setContextMenu(trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();

    m_floating = new QuotaWidget(nullptr);
    connect(m_floating, &QuotaWidget::openMainWindowRequested, this, [this]() {
        showNormal();
        raise();
        activateWindow();
    });
    connect(m_floating, &QuotaWidget::settingsRequested, this, &MainWindow::onOpenSettings);
    connect(m_floating, &QuotaWidget::quitRequested, this, [this]() {
        m_quitting = true;
        saveSettings();
        m_client->stop();
        QCoreApplication::exit(0);
    });
}

void MainWindow::applySettingsToUi()
{
    if (m_jobs) {
        m_jobs->setResetWaitSeconds(m_settings.jitterSeconds);
        m_jobs->setRetryAttempts(m_settings.retryAttempts);
        m_jobs->setAutoReleaseWriterLock(m_settings.autoReleaseWriterLock);
    }
    theme::setDark(m_settings.uiTheme != QStringLiteral("light"));
    theme::applyApplicationTheme(*qApp);
    updateThemeStyles();

    if (m_floating) {
        m_floating->setBackgroundOpacity(m_settings.widgetOpacity);
        m_floating->applyThemeStyles();
        m_floating->setDisplayRemaining(m_settings.displayRemaining);
        m_floating->setFloatingStyle(m_settings.floatingStyle);
        m_floating->setScalePercent(m_settings.widgetScale);
        m_floating->applyDockEdge(m_settings.dockEdge);
        m_floating->setAlwaysOnTopHintEnabled(m_settings.alwaysOnTop);
        m_floating->setAccountProfile(m_accountProfile);
        m_floating->setVisible(m_settings.showFloatingWidget);
    }
    if (m_showFloatingAction)
        m_showFloatingAction->setChecked(m_settings.showFloatingWidget);
    if (m_limits.valid)
        updateQuotaHeader();
}

void MainWindow::updateThemeStyles()
{
    const QString surface = theme::surface().name();
    const QString border = theme::border().name();
    const QString text = theme::text().name();
    const QString muted = theme::textMuted().name();
    const QString dim = theme::textDim().name();

    if (auto *frame = findChild<QFrame *>(QStringLiteral("headerFrame"))) {
        frame->setStyleSheet(QStringLiteral("QFrame#headerFrame{background:%1;border:1px solid %2;"
                                            "border-radius:8px;}")
                                     .arg(surface, border));
    }
    if (auto *frame = findChild<QFrame *>(QStringLiteral("quotaFrame"))) {
        frame->setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;border-radius:8px;}")
                                     .arg(surface, border));
    }
    for (const char *name : {"fiveHourTitle", "weeklyTitle"}) {
        if (auto *label = findChild<QLabel *>(QString::fromLatin1(name)))
            label->setStyleSheet(QStringLiteral("color:%1;").arg(muted));
    }
    if (m_accountLabel)
        m_accountLabel->setStyleSheet(QStringLiteral("color:%1;").arg(muted));
    refreshAccountHeader();
    if (m_watchSummaryLabel)
        m_watchSummaryLabel->setStyleSheet(QStringLiteral("color:%1;").arg(muted));
    if (m_quotaFiveHourLabel)
        m_quotaFiveHourLabel->setStyleSheet(QStringLiteral("color:%1;").arg(muted));
    if (m_quotaWeeklyLabel)
        m_quotaWeeklyLabel->setStyleSheet(QStringLiteral("color:%1;").arg(muted));
    if (m_detailLabel) {
        m_detailLabel->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:8px;"
                                                    "padding:10px;color:%3;")
                                             .arg(surface, border, text));
    }
    if (auto *label = findChild<QLabel *>(QStringLiteral("logTitle")))
        label->setStyleSheet(QStringLiteral("color:%1;").arg(dim));

    if (m_connectionLabel) {
        const QColor color = m_clientStateKnown
                ? (m_clientReady ? theme::green() : theme::red())
                : theme::yellow();
        m_connectionLabel->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
    }

    for (PillProgress *bar : {m_fiveHourBar, m_weeklyBar}) {
        if (bar)
            bar->setTrackColor(theme::track());
    }

    // 详情富文本里的强调色是拼 HTML 时写死的，主题切换后按当前主题重建一次。
    onThreadsSelectionChanged();
    if (m_threadTable)
        m_threadTable->viewport()->update();
}

void MainWindow::saveSettings()
{
    SettingsStore::save(m_settings);
}

void MainWindow::onClientStateChanged(bool ready, const QString &detail)
{
    m_clientStateKnown = true;
    m_clientReady = ready;
    m_connectionLabel->setText(ready ? QStringLiteral("● 已连接") : QStringLiteral("● ") + detail);
    m_connectionLabel->setStyleSheet(QStringLiteral("color:%1;")
                                             .arg((ready ? theme::green() : theme::red()).name()));
    if (ready) {
        m_catalog->refresh();
        m_client->readRateLimits(nullptr);
    }
    updateSelectionButtons();
}

void MainWindow::onAccountProfileUpdated(const arc::AccountProfile &profile)
{
    m_accountProfile = profile;
    refreshAccountHeader();
    if (m_floating)
        m_floating->setAccountProfile(profile);
}

void MainWindow::refreshAccountHeader()
{
    if (!m_avatarLabel || !m_userLabel)
        return;
    if (m_accountProfile.valid) {
        const int avatarSize = 22;
        m_avatarLabel->setPixmap(
                arc::avatar::pixmapFor(m_accountProfile, avatarSize, devicePixelRatioF()));
        m_avatarLabel->show();
        const QString name = m_accountProfile.name.trimmed();
        m_userLabel->setText(name.isEmpty() ? QStringLiteral("Codex 用量看守") : name);
        if (!m_accountProfile.email.isEmpty())
            m_userLabel->setToolTip(QStringLiteral("账号：%1").arg(m_accountProfile.email));
    } else {
        m_avatarLabel->hide();
        m_userLabel->setText(QStringLiteral("◉ Codex 用量看守"));
    }
}

void MainWindow::onRateLimitsUpdated(const RateLimitSnapshot &snapshot)
{
    // 截图/调色验收时用 AR_SCREENSHOT_FAKE_REMAINING 固定假数据，避免 4~5 秒时又收到真实额度把它覆盖。
    if (qEnvironmentVariableIsSet("AR_SCREENSHOT_FAKE_REMAINING")
            || qEnvironmentVariableIsSet("AR_SCREENSHOT_FAKE_CREDIT_DAYS"))
        return;
    m_limits = snapshot;
    m_jobs->updateRateLimits(snapshot);
    updateQuotaHeader();
    if (m_floating) {
        m_floating->setSnapshot(snapshot);
        updateFloatingWidget();
    }
}

void MainWindow::onThreadsUpdated(const QList<ThreadInfo> &threads)
{
    const QString selectedId = currentThread().id;
    QList<ThreadInfo> display;
    display.reserve(threads.size());
    for (ThreadInfo info : threads) {
        if (m_settings.ignoredThreadIds.contains(info.id)) {
            info.interrupted = false;
            info.resetAt = 0;
        }
        display.append(info);
    }
    m_model->setThreads(display);
    m_model->setJobs(m_jobs->jobs());
    onFilterChanged();
    if (!selectedId.isEmpty()) {
        const int sourceRow = m_model->rowForThread(selectedId);
        if (sourceRow >= 0) {
            const QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(sourceRow, 0));
            if (proxyIndex.isValid())
                m_threadTable->selectRow(proxyIndex.row());
        }
    }
    for (const ThreadInfo &info : threads) {
        if (info.interrupted)
            m_jobs->notifyThreadState(info);
    }
    updateSelectionButtons();
    updateFloatingWidget();
    updateQuotaHeader();
}

void MainWindow::onJobsChanged()
{
    if (m_model)
        m_model->setJobs(m_jobs->jobs());
    updateSelectionButtons();
    updateFloatingWidget();
    updateQuotaHeader();
}

void MainWindow::onThreadsSelectionChanged()
{
    const ThreadInfo info = currentThread();
    if (info.id.isEmpty()) {
        m_detailLabel->setText(QStringLiteral("选择一个对话查看详情"));
        updateSelectionButtons();
        return;
    }
    const ResumeJob job = m_jobs->jobFor(info.id);
    QStringList lines;
    lines << QStringLiteral("<b>%1</b>").arg(info.name.isEmpty() ? info.id : info.name);
    lines << QStringLiteral("线程: %1").arg(info.id);
    lines << QStringLiteral("目录: %1").arg(info.cwd);
    lines << QStringLiteral("状态: %1%2")
                 .arg(info.interrupted ? QStringLiteral("待续跑（限额中断）")
                                       : (info.running ? QStringLiteral("进行中") : QStringLiteral("历史/空闲")),
                      info.updatedAt > 0 ? QStringLiteral(" · 最近活动 %1").arg(formatEpoch(info.updatedAt))
                                         : QString());
    if (info.interrupted && info.resetAt > 0)
        lines << QStringLiteral("预计额度重置: %1（%2）").arg(formatEpoch(info.resetAt), formatCountdown(info.resetAt));
    if (!job.threadId.isEmpty()) {
        QString attemptsText = QStringLiteral("累计 %1 次").arg(job.attempts);
        if (job.cycleAttempts > 0)
            attemptsText += QStringLiteral("，本轮第 %1 次").arg(job.cycleAttempts);
        const QString stateText = job.writerBlocked ? QStringLiteral("等待写锁释放")
                                                    : jobStateText(job.state);
        lines << QStringLiteral("看守: %1 · %2 · %3")
                     .arg(resumeModeText(job.mode), stateText, attemptsText);
        if (job.writerBlocked) {
            lines << QStringLiteral("<span style='color:%1;'><b>等待写锁释放</b>：Codex 桌面版正打开着这个对话，"
                                    "AutoRun 无法同时续跑（Codex 同一个对话只允许一个写入者）。</span>")
                         .arg(theme::yellow().name());
            lines << QStringLiteral("<b>你现在可以</b>：在 Codex 桌面版里<b>关闭这个对话</b>或切换到其他对话；"
                                    "也可以直接退出桌面版。写锁释放后 AutoRun 会自动继续，不需要再点按钮。");
            lines << QStringLiteral("如果你离开电脑 ≥5 分钟且该对话空闲，程序会自动关闭 Codex 桌面版来释放写锁%1。")
                         .arg(m_settings.autoReleaseWriterLock
                                      ? QString()
                                      : QStringLiteral("（设置里已关闭“自动关闭桌面版”）"));
        }
        if (job.nextTriggerAt > 0)
            lines << QStringLiteral("下次触发: %1（%2）").arg(formatEpoch(job.nextTriggerAt),
                                                              formatCountdown(job.nextTriggerAt));
        if (!job.lastError.isEmpty()) {
            lines << QStringLiteral("最近错误: %1").arg(job.lastError);
            if (job.state == JobState::Failed
                && job.lastError.contains(QStringLiteral("active writer"), Qt::CaseInsensitive)) {
                lines << QStringLiteral("<span style='color:%1;'>这是旧版本把“桌面版占用写锁”当成失败留下的红灯；"
                                        "点“立即续跑”后，v0.1.9 会进入等待写锁释放（不消耗重试次数），锁释放后自动继续。</span>")
                             .arg(theme::yellow().name());
            }
        }
    } else if (info.interrupted) {
        lines << QStringLiteral("<span style='color:%1;'>该对话未加入看守，不会自动续跑；"
                                "点“加入看守”或“立即续跑”。</span>")
                     .arg(theme::yellow().name());
    }
    if (!info.preview.isEmpty())
        lines << QString();
    lines << QStringLiteral("<span style='color:%1;'>%2</span>")
                 .arg(theme::textDim().name(), shorten(info.preview, 600).toHtmlEscaped());
    m_detailLabel->setText(lines.join(QStringLiteral("<br>")));
    updateSelectionButtons();
}

void MainWindow::onRefreshClicked()
{
    if (m_client->isReady()) {
        m_catalog->refresh();
        m_client->readRateLimits(nullptr);
        showLog(QStringLiteral("手动刷新…"));
    } else {
        m_client->start(m_settings.codexCliPath);
    }
}

void MainWindow::onFilterChanged()
{
    if (!m_proxy)
        return;
    m_proxy->setStatusFilter(m_statusFilter->currentData().toString());
    m_proxy->setSearchText(m_searchEdit->text());
    updateSelectionButtons();
}

void MainWindow::onAddWatchClicked()
{
    ThreadInfo info = m_catalog->threadById(currentThread().id);
    if (info.id.isEmpty())
        info = currentThread();
    if (info.id.isEmpty())
        return;
    ResumeJob job;
    job.threadId = info.id;
    job.threadName = info.name.isEmpty() ? info.id : info.name;
    job.prompt = m_settings.defaultPrompt;
    job.mode = m_settings.continuousByDefault ? ResumeMode::Continuous : ResumeMode::Once;
    job.enabled = true;
    job.attempts = 0;
    job.state = JobState::Waiting;
    qint64 reset = info.resetAt > 0 ? info.resetAt : m_limits.primary.resetsAt;
    if (info.interrupted && reset > 0) {
        if (reset + 30 <= nowSec())
            job.state = JobState::Ready; // 已经过重置时间，立刻续跑
        else
            job.nextTriggerAt = reset + qMax(60, m_settings.jitterSeconds);
    } else {
        job.nextTriggerAt = 0; // 仅看守，等下次限额中断再触发
    }
    m_jobs->addOrUpdateJob(job);
    showLog(QStringLiteral("已加入看守：%1").arg(job.threadName));
    updateSelectionButtons();
}

void MainWindow::onRunNowClicked()
{
    const ThreadInfo info = currentThread();
    if (info.id.isEmpty())
        return;
    ResumeJob job = currentJobOrDefault();
    job.enabled = true;
    job.state = JobState::Ready;
    job.nextTriggerAt = 0;
    // 手动“立即续跑”应重新获得完整的重试机会（与右键菜单的 JobEngine::requestRunNow 一致），
    // 并清掉“等待写锁”状态，下一 tick 立刻重新探测。
    job.cycleAttempts = 0;
    job.retryPending = false;
    job.writerBlocked = false;
    job.writerReleaseAttempted = false;
    if (job.prompt.isEmpty())
        job.prompt = m_settings.defaultPrompt;
    m_jobs->addOrUpdateJob(job);
    showLog(QStringLiteral("立即续跑：%1").arg(job.threadName));
}

void MainWindow::onRemoveWatchClicked()
{
    const ThreadInfo info = currentThread();
    if (info.id.isEmpty())
        return;
    const ResumeJob job = m_jobs->jobFor(info.id);
    if (job.state == JobState::Running) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("停止续跑"),
            QStringLiteral("该对话正在续跑，取消看守会中断当前 turn。继续吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    m_jobs->cancelJob(info.id);
    showLog(QStringLiteral("已取消看守：%1").arg(info.name.isEmpty() ? info.id : info.name));
}

void MainWindow::onThreadSettingsClicked()
{
    const ThreadInfo info = currentThread();
    if (info.id.isEmpty())
        return;
    ResumeJob job = currentJobOrDefault();
    ThreadSettingsDialog dialog(info, job, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_jobs->addOrUpdateJob(dialog.job());
        showLog(QStringLiteral("已更新看守设置：%1").arg(job.threadName));
    }
}

void MainWindow::onOpenSettings()
{
    const QString oldCliPath = m_settings.codexCliPath;
    SettingsDialog dialog(m_settings, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_settings = dialog.settings();
    saveSettings();
    applySettingsToUi();
    showLog(QStringLiteral("设置已保存"));
    if (m_settings.codexCliPath != oldCliPath) {
        m_client->stop();
        m_client->start(m_settings.codexCliPath);
    }
}

void MainWindow::onThreadStatusChanged(const QString &threadId, const QString &statusType,
                                       const QStringList &activeFlags)
{
    Q_UNUSED(threadId);
    Q_UNUSED(statusType);
    Q_UNUSED(activeFlags);
    QTimer::singleShot(600, this, [this]() {
        if (m_client->isReady())
            m_catalog->refresh();
    });
}

void MainWindow::onTick()
{
    ++m_tickCounter;
    updateQuotaHeader();
    if (!m_client->isReady())
        return;
    if (m_tickCounter % 12 == 0)
        m_client->readRateLimits(nullptr);
    if (m_tickCounter % 6 == 0)
        m_catalog->refresh();
    if (m_tickCounter % 6 == 0)
        updateFloatingWidget();
}

void MainWindow::onTrayActivated()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::updateQuotaHeader()
{
    int enabledJobs = 0;
    for (const ResumeJob &job : m_jobs->jobs()) {
        if (job.enabled)
            ++enabledJobs;
    }
    int runningThreads = 0;
    for (const ThreadInfo &info : m_catalog->threads()) {
        if (info.running)
            ++runningThreads;
    }
    const bool pulse = runningThreads > 0;

    // 主窗口进度条与悬浮环/弹卡共用同一套 PillProgress 行为：
    // 有进行中对话时呼吸；额度耗尽时整条外圈红色呼吸灯。
    auto applyBar = [pulse](PillProgress *bar, bool valid, int usedPercent) {
        if (!bar)
            return;
        bar->setTrackColor(theme::track());
        bar->setPulseActive(valid && pulse);
        bar->setExhausted(valid && usedPercent >= 100);
        bar->setAccentColor(valid ? progressColor(usedPercent) : theme::textDim());
        if (!valid)
            bar->setValue(0);
    };

    if (!m_limits.valid) {
        applyBar(m_fiveHourBar, false, 0);
        applyBar(m_weeklyBar, false, 0);
        const int failedJobs = failedJobCount();
        QString summary = QStringLiteral("看守队列：%1 个已启用 · 检测到 %2 个进行中")
                                  .arg(enabledJobs)
                                  .arg(runningThreads);
        if (failedJobs > 0)
            summary += QStringLiteral(" · %1 个续跑失败（红灯）").arg(failedJobs);
        const int blockedJobs = blockedJobCount();
        if (blockedJobs > 0)
            summary += QStringLiteral(" · %1 个等待 Codex 桌面版释放写锁").arg(blockedJobs);
        int activityCount = 0;
        const ActivityState activity = computeActivityState(&activityCount);
        summary += QStringLiteral(" · 当前：%1").arg(activityStateText(activity, activityCount));
        m_watchSummaryLabel->setText(summary);
        return;
    }
    const RateLimitWindow &five = m_limits.primary;
    const RateLimitWindow &weekly = m_limits.secondary;
    auto displayValue = [this](int usedPercent) {
        return m_settings.displayRemaining ? qBound(0, 100 - usedPercent, 100)
                                           : qBound(0, usedPercent, 100);
    };
    if (five.valid) {
        const int value = displayValue(five.usedPercent);
        m_fiveHourBar->setValue(value);
        applyBar(m_fiveHourBar, true, five.usedPercent);
        m_quotaFiveHourLabel->setText(QStringLiteral("%1%2% · %3 重置（%4）")
                                          .arg(m_settings.displayRemaining ? QStringLiteral("剩余 ")
                                                                           : QString())
                                          .arg(value)
                                          .arg(formatEpochShort(five.resetsAt))
                                          .arg(formatCountdown(five.resetsAt)));
    } else {
        applyBar(m_fiveHourBar, false, 0);
    }
    if (weekly.valid) {
        const int value = displayValue(weekly.usedPercent);
        m_weeklyBar->setValue(value);
        applyBar(m_weeklyBar, true, weekly.usedPercent);
        m_quotaWeeklyLabel->setText(QStringLiteral("%1%2% · %3 重置（%4）")
                                        .arg(m_settings.displayRemaining ? QStringLiteral("剩余 ")
                                                                         : QString())
                                        .arg(value)
                                        .arg(formatEpochShort(weekly.resetsAt))
                                        .arg(formatCountdown(weekly.resetsAt)));
    } else {
        applyBar(m_weeklyBar, false, 0);
    }
    const QString plan = m_limits.planType.isEmpty() ? QStringLiteral("—") : m_limits.planType.toUpper();
    m_accountLabel->setText(plan);
    m_accountLabel->setToolTip(QStringLiteral("计划: %1\n数据时间: %2")
                                       .arg(plan, formatEpoch(m_limits.readAtMs / 1000)));
    if (m_creditLabel) {
        if (m_limits.resetCreditsAvailable >= 0) {
            // 与弹卡/悬浮窗同口径：最近一张重置卡 <2 天红、<7 天黄、>=7 天绿。
            const QColor creditColor = m_limits.resetCreditsAvailable > 0
                    ? theme::resetCreditColor(resetCreditUrgency(m_limits))
                    : theme::textMuted();
            m_creditLabel->setText(QStringLiteral("· 重置卡 %1 张").arg(m_limits.resetCreditsAvailable));
            m_creditLabel->setStyleSheet(QStringLiteral("color:%1;").arg(creditColor.name()));
            QString creditDetail = QString::number(m_limits.resetCreditsAvailable);
            const qint64 nearestCreditExpiry = nearestResetCreditExpiry(m_limits);
            if (nearestCreditExpiry > 0) {
                creditDetail += QStringLiteral("（最近一张 %1 到期，剩 %2）")
                                        .arg(formatEpoch(nearestCreditExpiry),
                                             formatCountdown(nearestCreditExpiry));
            }
            m_creditLabel->setToolTip(QStringLiteral("重置卡: %1").arg(creditDetail));
            m_creditLabel->show();
        } else {
            m_creditLabel->hide();
        }
    }

    const int failedJobs = failedJobCount();
    QString summary = QStringLiteral("看守队列：%1 个已启用 · 检测到 %2 个进行中")
                              .arg(enabledJobs)
                              .arg(runningThreads);
    if (failedJobs > 0)
        summary += QStringLiteral(" · %1 个续跑失败（红灯）").arg(failedJobs);
    const int blockedJobs = blockedJobCount();
    if (blockedJobs > 0)
        summary += QStringLiteral(" · %1 个等待 Codex 桌面版释放写锁").arg(blockedJobs);
    int activityCount = 0;
    const ActivityState activity = computeActivityState(&activityCount);
    summary += QStringLiteral(" · 当前：%1").arg(activityStateText(activity, activityCount));
    m_watchSummaryLabel->setText(summary);
}

void MainWindow::updateFloatingWidget()
{
    if (!m_floating)
        return;
    m_floating->setSnapshot(m_limits);
    int enabledJobs = 0;
    int runningThreads = 0;
    for (const ResumeJob &job : m_jobs->jobs()) {
        if (job.enabled)
            ++enabledJobs;
    }
    for (const ThreadInfo &info : m_catalog->threads()) {
        if (info.running)
            ++runningThreads;
    }
    m_floating->setWatchedCount(enabledJobs, runningThreads, failedJobCount());
    int activityCount = 0;
    const ActivityState activity = computeActivityState(&activityCount);
    if (activity != m_lastActivityState || activityCount != m_lastActivityCount) {
        m_lastActivityState = activity;
        m_lastActivityCount = activityCount;
        showLog(QStringLiteral("活动指示灯：%1").arg(activityStateText(activity, activityCount)));
    }
    m_floating->setActivityStatus(activity, activityCount);
    m_floating->setVisible(m_settings.showFloatingWidget);
}

ActivityState MainWindow::computeActivityState(int *count) const
{
    // 开发/截图用：AR_SCREENSHOT_FAKE_ACTIVITY=running|waiting|interrupted|completed|recent|idle[:N]
    const QString fake = qEnvironmentVariable("AR_SCREENSHOT_FAKE_ACTIVITY").trimmed().toLower();
    auto report = [count](ActivityState state, int n) {
        if (count)
            *count = qMax(1, n);
        return state;
    };
    if (!fake.isEmpty()) {
        const QStringList parts = fake.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        const QString name = parts.value(0);
        const int n = parts.size() > 1 ? qMax(1, parts.value(1).toInt()) : 1;
        if (name.startsWith(QStringLiteral("run")))
            return report(ActivityState::Running, n);
        if (name.startsWith(QStringLiteral("wait")))
            return report(ActivityState::WaitingReply, n);
        if (name.startsWith(QStringLiteral("interrupt")) || name.startsWith(QStringLiteral("abort")))
            return report(ActivityState::Interrupted, n);
        if (name.startsWith(QStringLiteral("complet")))
            return report(ActivityState::Completed, n);
        if (name.startsWith(QStringLiteral("recent")) || name.startsWith(QStringLiteral("end")))
            return report(ActivityState::RecentlyEnded, n);
        if (name.startsWith(QStringLiteral("idle")) || name.startsWith(QStringLiteral("none")))
            return report(ActivityState::Idle, 0);
    }

    int recentSeconds = 300; // “刚完成/最近结束”的展示窗口：5 分钟
    if (qEnvironmentVariableIsSet("AR_ACTIVITY_RECENT_SECONDS")) {
        bool ok = false;
        const int value = qEnvironmentVariableIntValue("AR_ACTIVITY_RECENT_SECONDS", &ok);
        if (ok && value > 0)
            recentSeconds = value;
    }
    const qint64 now = nowSec();
    int interrupted = 0;
    int running = 0;
    int waitingReply = 0;
    int completed = 0;
    int recentlyEnded = 0;
    for (const ThreadInfo &info : m_catalog->threads()) {
        if (info.archived || info.id.isEmpty())
            continue;
        if (m_settings.ignoredThreadIds.contains(info.id))
            continue;
        if (info.interrupted) {
            ++interrupted;
            continue;
        }
        if (info.running) {
            ++running;
            continue;
        }
        // turn 已经开始但 rollout 最近没有写入：大概率在等模型回复（长时间思考/排队）。
        if (info.statusType.compare(QStringLiteral("active"), Qt::CaseInsensitive) == 0) {
            ++waitingReply;
            continue;
        }
        if (info.completedAt > 0 && info.completedAt <= now
            && now - info.completedAt <= recentSeconds) {
            ++completed;
            continue;
        }
        // 兜底：最近活动停止但无法确认是否正常完成（例如尾部被截断）。
        if (info.completedAt <= 0 && info.lastEventAt > 0 && info.lastEventAt <= now
            && now - info.lastEventAt <= recentSeconds) {
            ++recentlyEnded;
            continue;
        }
    }
    if (interrupted > 0)
        return report(ActivityState::Interrupted, interrupted);
    if (running > 0)
        return report(ActivityState::Running, running);
    if (waitingReply > 0)
        return report(ActivityState::WaitingReply, waitingReply);
    if (completed > 0)
        return report(ActivityState::Completed, completed);
    if (recentlyEnded > 0)
        return report(ActivityState::RecentlyEnded, recentlyEnded);
    return report(ActivityState::Idle, 0);
}

void MainWindow::updateSelectionButtons()
{
    const ThreadInfo info = currentThread();
    const bool hasThread = !info.id.isEmpty();
    const bool hasJob = hasThread && m_jobs->hasJob(info.id);
    if (m_addWatchButton)
        m_addWatchButton->setEnabled(hasThread && !hasJob);
    if (m_runNowButton)
        m_runNowButton->setEnabled(hasThread);
    if (m_removeWatchButton)
        m_removeWatchButton->setEnabled(hasJob);
    if (m_threadSettingsButton)
        m_threadSettingsButton->setEnabled(hasThread);
}

ThreadInfo MainWindow::currentThread() const
{
    if (!m_threadTable || !m_model || !m_proxy)
        return ThreadInfo{};
    const QModelIndex index = m_threadTable->currentIndex();
    if (!index.isValid())
        return ThreadInfo{};
    const QModelIndex source = m_proxy->mapToSource(index);
    if (!source.isValid())
        return ThreadInfo{};
    return m_model->threadAt(source.row());
}

int MainWindow::failedJobCount() const
{
    if (!m_jobs)
        return 0;
    int failed = 0;
    for (const ResumeJob &job : m_jobs->jobs()) {
        if (job.enabled && job.state == JobState::Failed)
            ++failed;
    }
    return failed;
}

int MainWindow::blockedJobCount() const
{
    if (!m_jobs)
        return 0;
    int blocked = 0;
    for (const ResumeJob &job : m_jobs->jobs()) {
        if (job.enabled && job.writerBlocked)
            ++blocked;
    }
    return blocked;
}

ResumeJob MainWindow::currentJobOrDefault() const
{
    const ThreadInfo info = currentThread();
    ResumeJob job = m_jobs->jobFor(info.id);
    if (job.threadId.isEmpty()) {
        job.threadId = info.id;
        job.threadName = info.name.isEmpty() ? info.id : info.name;
        job.prompt = m_settings.defaultPrompt;
        job.mode = m_settings.continuousByDefault ? ResumeMode::Continuous : ResumeMode::Once;
    }
    return job;
}

void MainWindow::onThreadContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_threadTable->indexAt(pos);
    if (!index.isValid())
        return;
    m_threadTable->setCurrentIndex(index);
    const ThreadInfo info = currentThread();
    if (info.id.isEmpty())
        return;
    const bool hasJob = m_jobs->hasJob(info.id);
    const bool ignored = m_settings.ignoredThreadIds.contains(info.id);

    QMenu menu(this);
    auto *addAction = menu.addAction(QStringLiteral("加入看守"));
    addAction->setEnabled(!hasJob);
    auto *runAction = menu.addAction(QStringLiteral("立即续跑"));
    auto *cancelAction = menu.addAction(QStringLiteral("取消看守"));
    cancelAction->setEnabled(hasJob);
    menu.addSeparator();
    auto *ignoreAction = menu.addAction(ignored ? QStringLiteral("恢复限额提示")
                                                : QStringLiteral("忽略此对话的限额提示"));
    ignoreAction->setEnabled(info.interrupted || ignored);
    menu.addSeparator();
    auto *settingsAction = menu.addAction(QStringLiteral("单独设置…"));

    QAction *chosen = menu.exec(m_threadTable->viewport()->mapToGlobal(pos));
    if (chosen == addAction)
        onAddWatchClicked();
    else if (chosen == runAction)
        onRunNowClicked();
    else if (chosen == cancelAction)
        onRemoveWatchClicked();
    else if (chosen == ignoreAction)
        onIgnoreInterruptedClicked();
    else if (chosen == settingsAction)
        onThreadSettingsClicked();
}

void MainWindow::onIgnoreInterruptedClicked()
{
    const ThreadInfo info = currentThread();
    if (info.id.isEmpty())
        return;
    const bool ignored = m_settings.ignoredThreadIds.contains(info.id);
    if (ignored) {
        m_settings.ignoredThreadIds.removeAll(info.id);
        showLog(QStringLiteral("已恢复限额提示：%1").arg(info.name.isEmpty() ? info.id : info.name));
    } else {
        m_settings.ignoredThreadIds.append(info.id);
        showLog(QStringLiteral("已忽略限额提示：%1").arg(info.name.isEmpty() ? info.id : info.name));
    }
    saveSettings();
    onThreadsUpdated(m_catalog->threads());
    updateSelectionButtons();
}

void MainWindow::showLog(const QString &message)
{
    // 同时落盘到 auto-run-codex.log：否则界面上的“续跑失败/重试/等待写锁”等
    // 关键调度日志在重启后丢失，排查问题时只剩 app-server 的 stderr。
    qInfo().noquote() << QStringLiteral("[job] ") + message;
    if (!m_logView)
        return;
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("[HH:mm:ss] "));
    m_logView->appendPlainText(time + message);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, QStringLiteral("关于 AutoRun Codex"),
                       QStringLiteral("<b>AutoRun Codex</b><br>"
                                      "Codex 剩余额度显示 + 用量重置后自动续跑。<br><br>"
                                      "通过本机 Codex app-server JSON-RPC 工作，不上报任何数据；"
                                      "不会自动消耗重置卡。"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_quitting && m_settings.minimizeToTray && m_tray && m_tray->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    m_quitting = true;
    saveSettings();
    m_client->stop();
    event->accept();
    QCoreApplication::exit(0);
}

} // namespace arc