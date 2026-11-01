#include "ThreadTableModel.h"

#include "ui/Theme.h"

#include <QColor>
#include <QDateTime>
#include <QStringList>
#include <QVariant>

#include <limits>

namespace arc {

namespace {

// 状态色随深浅色主题走：深色主题用亮色，浅色主题压暗以保证白底可读性。
QColor statusRunningColor()
{
    return theme::isDark() ? QColor(88, 166, 255) : QColor(9, 105, 218);
}

QColor statusPendingColor()
{
    return theme::isDark() ? QColor(245, 185, 66) : QColor(154, 103, 0);
}

QString threadTitle(const ThreadInfo &info)
{
    const QString name = info.name.trimmed();
    if (!name.isEmpty())
        return name;
    QString preview = info.preview.simplified();
    if (preview.length() > 80)
        preview = preview.left(80) + QStringLiteral("…");
    if (!preview.isEmpty())
        return preview;
    if (!info.id.isEmpty())
        return info.id;
    return QStringLiteral("(未知对话)");
}

QString relativeActivityText(qint64 epoch)
{
    if (epoch <= 0)
        return QStringLiteral("-");
    const qint64 now = nowSec();
    const qint64 diff = now > epoch ? now - epoch : 0;
    if (diff < 60)
        return QStringLiteral("刚刚");
    if (diff < 3600)
        return QStringLiteral("%1 分钟前").arg(diff / 60);
    if (diff < 86400)
        return QStringLiteral("%1 小时前").arg(diff / 3600);
    if (diff < 7 * 86400)
        return QStringLiteral("%1 天前").arg(diff / 86400);
    return formatEpochShort(epoch);
}

QString threadActivityText(const ThreadInfo &info)
{
    qint64 epoch = info.lastEventAt;
    if (epoch <= 0)
        epoch = info.updatedAt;
    if (epoch <= 0)
        epoch = info.recencyAt;
    return relativeActivityText(epoch);
}

QString threadWatchText(const ThreadInfo &info, bool hasJob, const ResumeJob &job)
{
    if (!hasJob) {
        if (info.hasJob && !info.jobSummary.trimmed().isEmpty())
            return info.jobSummary.trimmed();
        return QStringLiteral("-");
    }
    if (!job.enabled)
        return QStringLiteral("已停用");
    QString text = QStringLiteral("%1 · %2").arg(resumeModeText(job.mode), jobStateText(job.state));
    if (job.attempts > 0)
        text += QStringLiteral(" · 尝试 %1 次").arg(job.attempts);
    return text;
}

QString threadNextTriggerText(const ThreadInfo &info, bool hasJob, const ResumeJob &job)
{
    if (hasJob && job.enabled && job.nextTriggerAt > 0) {
        return QStringLiteral("%1（%2）").arg(formatEpochShort(job.nextTriggerAt),
                                             formatCountdown(job.nextTriggerAt));
    }
    return QStringLiteral("-");
}

QString threadTooltip(const ThreadInfo &info, bool hasJob, const ResumeJob &job)
{
    QStringList lines;
    lines << threadTitle(info);
    if (!info.id.isEmpty())
        lines << QStringLiteral("ID：%1").arg(info.id);
    if (!info.cwd.isEmpty())
        lines << QStringLiteral("目录：%1").arg(info.cwd);
    if (!info.preview.trimmed().isEmpty())
        lines << QStringLiteral("预览：%1").arg(info.preview.simplified());
    if (info.statusType == QStringLiteral("systemError"))
        lines << QStringLiteral("状态：系统错误");
    if (info.interrupted) {
        QString line = QStringLiteral("额度中断");
        if (info.resetAt > 0)
            line += QStringLiteral("，预计 %1 恢复").arg(formatEpoch(info.resetAt));
        if (!hasJob)
            line += QStringLiteral("（未加入看守，不会自动续跑）");
        lines << line;
        if (!info.lastLimitMessage.trimmed().isEmpty())
            lines << info.lastLimitMessage.trimmed();
    }
    if (hasJob) {
        lines << QStringLiteral("看守：%1 · %2 · 尝试 %3 次")
                         .arg(resumeModeText(job.mode), jobStateText(job.state))
                         .arg(job.attempts);
        if (job.nextTriggerAt > 0)
            lines << QStringLiteral("下次触发：%1").arg(formatEpoch(job.nextTriggerAt));
        if (!job.lastError.trimmed().isEmpty())
            lines << QStringLiteral("最近错误：%1").arg(job.lastError.trimmed());
    }
    return lines.join(QStringLiteral("\n"));
}

} // namespace

ThreadTableModel::ThreadTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ThreadTableModel::setThreads(const QList<ThreadInfo> &threads)
{
    beginResetModel();
    m_threads = threads;
    endResetModel();
}

void ThreadTableModel::setJobs(const QList<ResumeJob> &jobs)
{
    m_jobs.clear();
    for (const ResumeJob &job : jobs) {
        if (!job.threadId.isEmpty())
            m_jobs.insert(job.threadId, job);
    }
    if (!m_threads.isEmpty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_threads.size()) - 1, ColumnCount - 1));
    }
}

ThreadInfo ThreadTableModel::threadAt(int row) const
{
    if (row < 0 || row >= m_threads.size())
        return ThreadInfo();
    return m_threads.at(row);
}

int ThreadTableModel::rowForThread(const QString &threadId) const
{
    for (int row = 0; row < m_threads.size(); ++row) {
        if (m_threads.at(row).id == threadId)
            return row;
    }
    return -1;
}

int ThreadTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_threads.size());
}

int ThreadTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant ThreadTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_threads.size())
        return QVariant();

    const ThreadInfo &info = m_threads.at(index.row());
    const bool hasJob = m_jobs.contains(info.id);
    const ResumeJob job = hasJob ? m_jobs.value(info.id) : ResumeJob();

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColumnTitle:
            return threadTitle(info);
        case ColumnStatus:
            return statusTextFor(info);
        case ColumnUpdated:
            return threadActivityText(info);
        case ColumnWatch:
            return threadWatchText(info, hasJob, job);
        case ColumnNextTrigger:
            return threadNextTriggerText(info, hasJob, job);
        default:
            break;
        }
        break;
    case Qt::ForegroundRole:
        switch (index.column()) {
        case ColumnStatus:
            return QVariant::fromValue(statusColorFor(info));
        case ColumnWatch:
            if (hasJob && job.state == JobState::Failed)
                return QVariant::fromValue(theme::red());
            if (hasJob && !job.enabled)
                return QVariant::fromValue(theme::textMuted());
            break;
        case ColumnNextTrigger:
            if (info.interrupted)
                return QVariant::fromValue(statusPendingColor());
            break;
        default:
            break;
        }
        break;
    case Qt::ToolTipRole:
        return threadTooltip(info, hasJob, job);
    case Qt::TextAlignmentRole:
        if (index.column() == ColumnStatus)
            return static_cast<int>(Qt::AlignCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    case SortRole:
        switch (index.column()) {
        case ColumnTitle:
            return threadTitle(info);
        case ColumnStatus: {
            const QString status = statusTextFor(info);
            if (status == QStringLiteral("进行中") || status == QStringLiteral("续跑中"))
                return 0;
            if (status == QStringLiteral("限额中断"))
                return 1;
            if (status == QStringLiteral("排队等待"))
                return 2;
            if (status == QStringLiteral("错误"))
                return 3;
            return 4;
        }
        case ColumnUpdated: {
            qint64 epoch = info.lastEventAt;
            if (epoch <= 0)
                epoch = info.updatedAt;
            if (epoch <= 0)
                epoch = info.recencyAt;
            return static_cast<qlonglong>(epoch);
        }
        case ColumnWatch:
            return hasJob ? static_cast<qlonglong>(job.enabled ? (10 + static_cast<int>(job.state)) : 1)
                          : static_cast<qlonglong>(0);
        case ColumnNextTrigger:
            return hasJob && job.enabled && job.nextTriggerAt > 0
                    ? static_cast<qlonglong>(job.nextTriggerAt)
                    : static_cast<qlonglong>(std::numeric_limits<qint64>::max());
        default:
            break;
        }
        break;
    case Qt::UserRole:
        return info.id;
    default:
        break;
    }
    return QVariant();
}

QVariant ThreadTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole) {
        if (orientation == Qt::Horizontal) {
            switch (section) {
            case ColumnTitle:
                return QStringLiteral("标题");
            case ColumnStatus:
                return QStringLiteral("状态");
            case ColumnUpdated:
                return QStringLiteral("最近活动");
            case ColumnWatch:
                return QStringLiteral("看守");
            case ColumnNextTrigger:
                return QStringLiteral("看守计划");
            default:
                break;
            }
        } else if (section >= 0) {
            return QString::number(section + 1);
        }
    }
    return QVariant();
}

QString ThreadTableModel::statusTextFor(const ThreadInfo &info) const
{
    const bool hasJob = m_jobs.contains(info.id);
    const ResumeJob job = hasJob ? m_jobs.value(info.id) : ResumeJob();

    if (info.running || info.statusType == QStringLiteral("active"))
        return QStringLiteral("进行中");
    if (hasJob && job.enabled && job.state == JobState::Running)
        return QStringLiteral("续跑中");
    if (info.archived)
        return QStringLiteral("历史");
    if (info.interrupted)
        return QStringLiteral("限额中断");
    if (hasJob && job.enabled
        && (job.state == JobState::Waiting || job.state == JobState::Ready
            || job.state == JobState::Paused)) {
        return QStringLiteral("排队等待");
    }
    if (info.statusType == QStringLiteral("systemError"))
        return QStringLiteral("错误");
    if (hasJob && job.state == JobState::Failed)
        return QStringLiteral("错误");
    return QStringLiteral("历史");
}

QColor ThreadTableModel::statusColorFor(const ThreadInfo &info) const
{
    const bool hasJob = m_jobs.contains(info.id);
    const ResumeJob job = hasJob ? m_jobs.value(info.id) : ResumeJob();

    if (info.running || info.statusType == QStringLiteral("active")
        || (hasJob && job.enabled && job.state == JobState::Running)) {
        return statusRunningColor();
    }
    if (info.archived)
        return theme::textMuted();
    if (info.interrupted
        || (hasJob && job.enabled
            && (job.state == JobState::Waiting || job.state == JobState::Ready
                || job.state == JobState::Paused))) {
        return statusPendingColor();
    }
    if (info.statusType == QStringLiteral("systemError")
        || (hasJob && job.state == JobState::Failed)) {
        return theme::red();
    }
    return theme::textMuted();
}

ThreadFilterProxy::ThreadFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void ThreadFilterProxy::setStatusFilter(const QString &filter)
{
    QString normalized = filter.trimmed().toLower();
    if (normalized.isEmpty())
        normalized = QStringLiteral("all");
    if (m_statusFilter == normalized)
        return;
    m_statusFilter = normalized;
    invalidateFilter();
}

void ThreadFilterProxy::setSearchText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (m_search == trimmed)
        return;
    m_search = trimmed;
    invalidateFilter();
}

bool ThreadFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QVariant leftValue = sourceModel()->data(left, ThreadTableModel::SortRole);
    const QVariant rightValue = sourceModel()->data(right, ThreadTableModel::SortRole);
    if (leftValue.typeId() == QMetaType::LongLong || leftValue.typeId() == QMetaType::Int)
        return leftValue.toLongLong() < rightValue.toLongLong();
    if (leftValue.typeId() == QMetaType::QString)
        return QString::localeAwareCompare(leftValue.toString(), rightValue.toString()) < 0;
    return QSortFilterProxyModel::lessThan(left, right);
}

bool ThreadFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    auto *model = qobject_cast<ThreadTableModel *>(sourceModel());
    if (!model)
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);

    if (!m_search.isEmpty()) {
        const ThreadInfo info = model->threadAt(sourceRow);
        const bool matched = info.name.contains(m_search, Qt::CaseInsensitive)
                || info.preview.contains(m_search, Qt::CaseInsensitive)
                || info.cwd.contains(m_search, Qt::CaseInsensitive)
                || info.id.contains(m_search, Qt::CaseInsensitive);
        if (!matched)
            return false;
    }

    if (m_statusFilter == QStringLiteral("all"))
        return true;

    const QVariant statusValue =
            model->index(sourceRow, ThreadTableModel::ColumnStatus, sourceParent).data(Qt::DisplayRole);
    const QString status = statusValue.toString();
    const bool running = status == QStringLiteral("进行中") || status == QStringLiteral("续跑中");
    const bool pending = status == QStringLiteral("限额中断") || status == QStringLiteral("排队等待");

    if (m_statusFilter == QStringLiteral("running"))
        return running;
    if (m_statusFilter == QStringLiteral("pending"))
        return pending;
    if (m_statusFilter == QStringLiteral("history"))
        return !running && !pending;
    return true;
}

} // namespace arc