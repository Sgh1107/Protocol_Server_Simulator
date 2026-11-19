// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/configbundle.h"

#include <QDialog>

class QDialogButtonBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace protosim::ui {

/// 虚拟设备管理: 新增 / 删除 / 启用 / 改端口。
///
/// 对话框内部维护一份 ConfigBundle 副本，所有写操作都走 DeviceManager，
/// 只有点击「确定」才把整份配置交给 MainWindow 保存并热生效。
class DeviceManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DeviceManagerDialog(const ConfigBundle &bundle, QWidget *parent = nullptr);

    ConfigBundle bundle() const { return m_bundle; }

private Q_SLOTS:
    void refresh();
    void addDevice();
    void editEndpoint();
    void removeDevice();
    void toggleDevice();
    void updateButtons();

private:
    QString selectedDeviceId() const;
    int nextFreePort() const;
    void showError(const Result<void> &result);

    ConfigBundle m_bundle;
    QTableWidget *m_table = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_toggleButton = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
};

} // namespace protosim::ui
