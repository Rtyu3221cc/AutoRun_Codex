#include "SettingsDialog.h"

#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace arc {

namespace {

const QString kDefaultPrompt = QStringLiteral("继续执行未完成的任务");

} // namespace

SettingsDialog::SettingsDialog(const AppSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumWidth(480);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("AutoRun Codex 设置"), this);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: 15px; font-weight: bold;")
                                 .arg(theme::text().name()));
    root->addWidget(title);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);

    m_cliPathEdit = new QLineEdit(m_settings.codexCliPath, this);
    m_cliPathEdit->setPlaceholderText(QStringLiteral("留空则自动探测 codex.exe"));
    m_cliPathEdit->setClearButtonEnabled(true);
    m_cliPathEdit->setToolTip(QStringLiteral("Codex CLI（codex.exe）的完整路径；留空会根据环境变量与安装目录自动探测。"));
    form->addRow(QStringLiteral("Codex CLI 路径"), m_cliPathEdit);

    m_promptEdit = new QLineEdit(m_settings.defaultPrompt, this);
    m_promptEdit->setPlaceholderText(kDefaultPrompt);
    m_promptEdit->setClearButtonEnabled(true);
    m_promptEdit->setToolTip(QStringLiteral("新增看守时默认使用的续跑 prompt。"));
    form->addRow(QStringLiteral("默认续跑 prompt"), m_promptEdit);

    m_jitterSpin = new QSpinBox(this);
    m_jitterSpin->setRange(120, 600);
    m_jitterSpin->setSingleStep(30);
    m_jitterSpin->setSuffix(QStringLiteral(" 秒"));
    m_jitterSpin->setValue(qBound(120, m_settings.jitterSeconds, 600));
    m_jitterSpin->setToolTip(QStringLiteral("额度重置后先等待多久再发送续跑指令（默认 150 秒 ≈ 2.5 分钟），"
                                            "避免额度重置本身有延迟导致一上来就失败。"));
    form->addRow(QStringLiteral("重置后续跑等待"), m_jitterSpin);

    m_attemptsSpin = new QSpinBox(this);
    m_attemptsSpin->setRange(1, 5);
    m_attemptsSpin->setValue(qBound(1, m_settings.retryAttempts, 5));
    m_attemptsSpin->setToolTip(QStringLiteral("每轮触发最多尝试几次（默认 3：首次 + 2 次自动重试，每次失败间隔 2 分钟）。"
                                              "次数用尽仍失败时停止自动重试，悬浮窗“看守中”灯由绿变红。"));
    form->addRow(QStringLiteral("每轮最多尝试次数"), m_attemptsSpin);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->addItem(QStringLiteral("深色主题"), QStringLiteral("dark"));
    m_themeCombo->addItem(QStringLiteral("浅色主题"), QStringLiteral("light"));
    m_themeCombo->setCurrentIndex(m_settings.uiTheme == QStringLiteral("light") ? 1 : 0);
    m_themeCombo->setToolTip(QStringLiteral("主窗口、悬浮条、弹卡的深浅色主题。"));
    form->addRow(QStringLiteral("界面主题"), m_themeCombo);

    m_opacitySpin = new QSpinBox(this);
    m_opacitySpin->setRange(30, 100);
    m_opacitySpin->setSingleStep(5);
    m_opacitySpin->setSuffix(QStringLiteral(" %"));
    m_opacitySpin->setValue(qBound(30, m_settings.widgetOpacity, 100));
    m_opacitySpin->setToolTip(QStringLiteral("悬浮窗背景的不透明度，越低越透。"));
    form->addRow(QStringLiteral("悬浮窗不透明度"), m_opacitySpin);

    m_displayCombo = new QComboBox(this);
    m_displayCombo->addItem(QStringLiteral("已用百分比（用量）"), false);
    m_displayCombo->addItem(QStringLiteral("剩余百分比（余量）"), true);
    m_displayCombo->setCurrentIndex(m_settings.displayRemaining ? 1 : 0);
    m_displayCombo->setToolTip(QStringLiteral("主窗口进度条与悬浮环按“已用”还是“剩余”显示。"));
    form->addRow(QStringLiteral("额度显示方式"), m_displayCombo);

    m_styleCombo = new QComboBox(this);
    m_styleCombo->addItem(QStringLiteral("参考样式（数字在环下）"), QStringLiteral("reference"));
    m_styleCombo->addItem(QStringLiteral("经典样式（数字在环内）"), QStringLiteral("classic"));
    m_styleCombo->addItem(QStringLiteral("极简模式（Plus 双环合并）"), QStringLiteral("minimal"));
    m_styleCombo->setCurrentIndex(m_settings.floatingStyle == QStringLiteral("classic") ? 1
                                  : m_settings.floatingStyle == QStringLiteral("minimal") ? 2 : 0);
    m_styleCombo->setToolTip(QStringLiteral("悬浮条数字排布：参考样式更窄（数字在环下方），经典样式数字在环内。"));
    form->addRow(QStringLiteral("悬浮窗样式"), m_styleCombo);

    m_edgeCombo = new QComboBox(this);
    m_edgeCombo->addItem(QStringLiteral("左侧"), QStringLiteral("left"));
    m_edgeCombo->addItem(QStringLiteral("右侧"), QStringLiteral("right"));
    m_edgeCombo->addItem(QStringLiteral("顶部"), QStringLiteral("top"));
    m_edgeCombo->addItem(QStringLiteral("底部"), QStringLiteral("bottom"));
    m_edgeCombo->addItem(QStringLiteral("自动"), QStringLiteral("auto"));
    const int edgeIndex = m_edgeCombo->findData(m_settings.dockEdge);
    m_edgeCombo->setCurrentIndex(edgeIndex >= 0 ? edgeIndex : 1);
    m_edgeCombo->setToolTip(QStringLiteral("悬浮条停靠的屏幕边缘；自动表示吸附到最近的边缘。"));
    form->addRow(QStringLiteral("吸附屏幕边缘"), m_edgeCombo);

    m_scaleSpin = new QSpinBox(this);
    m_scaleSpin->setRange(50, 150);
    m_scaleSpin->setSingleStep(5);
    m_scaleSpin->setSuffix(QStringLiteral(" %"));
    m_scaleSpin->setValue(qBound(50, m_settings.widgetScale, 150));
    m_scaleSpin->setToolTip(QStringLiteral("悬浮条整体尺寸百分比（50% - 150%）。"));
    form->addRow(QStringLiteral("悬浮条缩放"), m_scaleSpin);

    m_topCheck = new QCheckBox(QStringLiteral("悬浮条置顶显示"), this);
    m_topCheck->setChecked(m_settings.alwaysOnTop);
    form->addRow(m_topCheck);

    m_autostartCheck = new QCheckBox(QStringLiteral("登录 Windows 时自动启动"), this);
    m_autostartCheck->setChecked(m_settings.autoStart);
    form->addRow(m_autostartCheck);

    m_floatingCheck = new QCheckBox(QStringLiteral("显示悬浮额度条"), this);
    m_floatingCheck->setChecked(m_settings.showFloatingWidget);
    form->addRow(m_floatingCheck);

    m_continuousCheck = new QCheckBox(QStringLiteral("新增看守默认使用持续看守模式"), this);
    m_continuousCheck->setChecked(m_settings.continuousByDefault);
    form->addRow(m_continuousCheck);

    m_writerLockCheck = new QCheckBox(
            QStringLiteral("写锁被 Codex 桌面版占用时自动关闭桌面版（推荐夜间无人值守）"), this);
    m_writerLockCheck->setChecked(m_settings.autoReleaseWriterLock);
    m_writerLockCheck->setToolTip(QStringLiteral(
            "Codex 桌面版打开着某个对话时会独占该对话的写锁，本程序无法续跑。\n"
            "勾选后：检测到写锁冲突且你已离开电脑（系统闲置 ≥5 分钟）时，自动关闭 Codex 桌面版释放写锁，再继续续跑；\n"
            "不勾选则只等待你自己关闭桌面版/该对话。"));
    form->addRow(m_writerLockCheck);

    m_trayNotifyCheck = new QCheckBox(QStringLiteral("续跑/写锁状态变化时显示 Windows 托盘通知"), this);
    m_trayNotifyCheck->setChecked(m_settings.trayNotifications);
    m_trayNotifyCheck->setToolTip(QStringLiteral(
            "续跑完成、失败重试、写锁等待/释放、自动关闭桌面版等状态变化时，在系统托盘弹出气泡提醒。"));
    form->addRow(m_trayNotifyCheck);

    root->addLayout(form);

    auto *hint = new QLabel(QStringLiteral("设置会保存到本程序配置目录，下次启动自动生效。"), this);
    hint->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(theme::textDim().name()));
    hint->setWordWrap(true);
    root->addWidget(hint);

    root->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto *okButton = buttons->button(QDialogButtonBox::Ok))
        okButton->setText(QStringLiteral("确定"));
    if (auto *cancelButton = buttons->button(QDialogButtonBox::Cancel))
        cancelButton->setText(QStringLiteral("取消"));
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_settings.codexCliPath = m_cliPathEdit->text().trimmed();
        const QString prompt = m_promptEdit->text().trimmed();
        m_settings.defaultPrompt = prompt.isEmpty() ? kDefaultPrompt : prompt;
        m_settings.jitterSeconds = m_jitterSpin->value();
        m_settings.retryAttempts = m_attemptsSpin->value();
        m_settings.dockEdge = m_edgeCombo->currentData().toString();
        m_settings.floatingStyle = m_styleCombo->currentData().toString();
        m_settings.widgetScale = m_scaleSpin->value();
        m_settings.displayRemaining = m_displayCombo->currentData().toBool();
        m_settings.uiTheme = m_themeCombo->currentData().toString();
        m_settings.widgetOpacity = m_opacitySpin->value();
        m_settings.alwaysOnTop = m_topCheck->isChecked();
        m_settings.autoStart = m_autostartCheck->isChecked();
        m_settings.showFloatingWidget = m_floatingCheck->isChecked();
        m_settings.continuousByDefault = m_continuousCheck->isChecked();
        m_settings.autoReleaseWriterLock = m_writerLockCheck->isChecked();
        m_settings.trayNotifications = m_trayNotifyCheck->isChecked();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

AppSettings SettingsDialog::settings() const
{
    return m_settings;
}

} // namespace arc