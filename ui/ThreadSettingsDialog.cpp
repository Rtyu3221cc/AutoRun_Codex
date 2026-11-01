#include "ThreadSettingsDialog.h"

#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace arc {

namespace {

const QString kDefaultPrompt = QStringLiteral("继续执行未完成的任务");

QString threadDisplayName(const ThreadInfo &thread)
{
    const QString name = thread.name.trimmed();
    if (!name.isEmpty())
        return name;
    const QString preview = thread.preview.simplified();
    if (!preview.isEmpty())
        return preview.length() > 60 ? preview.left(60) + QStringLiteral("…") : preview;
    return thread.id;
}

} // namespace

ThreadSettingsDialog::ThreadSettingsDialog(const ThreadInfo &thread, const ResumeJob &job, QWidget *parent)
    : QDialog(parent)
    , m_job(job)
{
    m_job.threadId = thread.id;
    m_job.threadName = threadDisplayName(thread);
    if (m_job.threadName.trimmed().isEmpty())
        m_job.threadName = m_job.threadId;
    if (m_job.prompt.trimmed().isEmpty())
        m_job.prompt = kDefaultPrompt;

    setWindowTitle(QStringLiteral("线程看守设置"));
    setMinimumWidth(440);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("线程看守设置"), this);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: 15px; font-weight: bold;")
                                 .arg(theme::text().name()));
    root->addWidget(title);

    auto *threadLabel = new QLabel(this);
    threadLabel->setTextFormat(Qt::PlainText);
    threadLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    threadLabel->setWordWrap(true);
    threadLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                       .arg(theme::textMuted().name()));
    threadLabel->setText(QStringLiteral("线程：%1\nID：%2").arg(m_job.threadName, m_job.threadId));
    root->addWidget(threadLabel);

    m_enabledCheck = new QCheckBox(QStringLiteral("启用看守（额度重置后自动续跑）"), this);
    m_enabledCheck->setChecked(job.enabled);
    root->addWidget(m_enabledCheck);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_promptEdit = new QLineEdit(this);
    m_promptEdit->setText(m_job.prompt);
    m_promptEdit->setPlaceholderText(kDefaultPrompt);
    m_promptEdit->setClearButtonEnabled(true);
    m_promptEdit->setToolTip(QStringLiteral("额度重置后续跑时发送给 Codex 的内容。"));
    form->addRow(QStringLiteral("续跑 prompt"), m_promptEdit);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("单次"), static_cast<int>(ResumeMode::Once));
    m_modeCombo->addItem(QStringLiteral("持续看守"), static_cast<int>(ResumeMode::Continuous));
    m_modeCombo->setCurrentIndex(job.mode == ResumeMode::Continuous ? 1 : 0);
    m_modeCombo->setToolTip(QStringLiteral("单次：续跑成功一次后结束；持续看守：之后每个额度窗口继续自动续跑。"));
    form->addRow(QStringLiteral("看守模式"), m_modeCombo);
    root->addLayout(form);

    QString stateText = QStringLiteral("当前状态：%1").arg(jobStateText(job.state));
    if (job.attempts > 0)
        stateText += QStringLiteral("，已尝试 %1 次").arg(job.attempts);
    if (job.nextTriggerAt > 0)
        stateText += QStringLiteral("，下次触发 %1").arg(formatEpoch(job.nextTriggerAt));
    auto *stateLabel = new QLabel(stateText, this);
    stateLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
                                      .arg(theme::textDim().name()));
    stateLabel->setWordWrap(true);
    root->addWidget(stateLabel);

    root->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto *okButton = buttons->button(QDialogButtonBox::Ok))
        okButton->setText(QStringLiteral("确定"));
    if (auto *cancelButton = buttons->button(QDialogButtonBox::Cancel))
        cancelButton->setText(QStringLiteral("取消"));
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_job.enabled = m_enabledCheck->isChecked();
        const QString prompt = m_promptEdit->text().trimmed();
        m_job.prompt = prompt.isEmpty() ? kDefaultPrompt : prompt;
        const int modeValue = m_modeCombo->currentData().toInt();
        m_job.mode = (modeValue == static_cast<int>(ResumeMode::Continuous))
                ? ResumeMode::Continuous
                : ResumeMode::Once;
        if (!m_job.enabled)
            m_job.state = JobState::Disabled;
        else if (m_job.state == JobState::Disabled)
            m_job.state = JobState::Waiting;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ResumeJob ThreadSettingsDialog::job() const
{
    return m_job;
}

} // namespace arc