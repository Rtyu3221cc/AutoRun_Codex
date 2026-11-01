#pragma once

#include "core/Types.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace arc {

class ThreadSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    ThreadSettingsDialog(const ThreadInfo &thread, const ResumeJob &job, QWidget *parent = nullptr);

    ResumeJob job() const;

private:
    ResumeJob m_job;
    QLineEdit *m_promptEdit = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
};

} // namespace arc
