#pragma once

#include "core/Types.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QSortFilterProxyModel>

namespace arc {

class ThreadTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { ColumnTitle = 0, ColumnStatus, ColumnUpdated, ColumnWatch, ColumnNextTrigger, ColumnCount };
    static constexpr int SortRole = Qt::UserRole + 1;

    explicit ThreadTableModel(QObject *parent = nullptr);

    void setThreads(const QList<ThreadInfo> &threads);
    void setJobs(const QList<ResumeJob> &jobs);
    ThreadInfo threadAt(int row) const;
    int rowForThread(const QString &threadId) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QString statusTextFor(const ThreadInfo &info) const;
    QColor statusColorFor(const ThreadInfo &info) const;

    QList<ThreadInfo> m_threads;
    QHash<QString, ResumeJob> m_jobs;
};

// 状态/关键字过滤。
class ThreadFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ThreadFilterProxy(QObject *parent = nullptr);

    void setStatusFilter(const QString &filter); // all/running/pending/history
    void setSearchText(const QString &text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_statusFilter = QStringLiteral("all");
    QString m_search;
};

} // namespace arc
