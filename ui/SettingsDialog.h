#pragma once

#include "core/SettingsStore.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace arc {

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const AppSettings &settings, QWidget *parent = nullptr);

    AppSettings settings() const;

private:
    AppSettings m_settings;
    QLineEdit *m_cliPathEdit = nullptr;
    QLineEdit *m_promptEdit = nullptr;
    QSpinBox *m_jitterSpin = nullptr;
    QSpinBox *m_attemptsSpin = nullptr;
    QComboBox *m_edgeCombo = nullptr;
    QSpinBox *m_scaleSpin = nullptr;
    QComboBox *m_displayCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QSpinBox *m_opacitySpin = nullptr;
    QComboBox *m_styleCombo = nullptr;
    QCheckBox *m_topCheck = nullptr;
    QCheckBox *m_autostartCheck = nullptr;
    QCheckBox *m_floatingCheck = nullptr;
    QCheckBox *m_continuousCheck = nullptr;
    QCheckBox *m_writerLockCheck = nullptr;
};

} // namespace arc
