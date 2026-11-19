// SPDX-License-Identifier: GPL-3.0-or-later
#include "protosim/ui/devicemanagerdialog.h"

#include "protosim/devicemanager.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace protosim::ui {
namespace {

QLineEdit *makeField(QFormLayout *form, const QString &label, const QString &value)
{
    auto *field = new QLineEdit(value);
    field->setObjectName(QStringLiteral("deviceField"));
    form->addRow(label, field);
    return field;
}

QDialogButtonBox *makeButtons(QDialog *dialog)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

} // namespace

DeviceManagerDialog::DeviceManagerDialog(const ConfigBundle &bundle, QWidget *parent)
    : QDialog(parent)
    , m_bundle(bundle)
{
    setWindowTitle(QStringLiteral("虚拟设备管理"));
    setObjectName(QStringLiteral("deviceDialog"));
    resize(760, 440);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto *hint = new QLabel(
        QStringLiteral("每个虚拟设备对应一个独立 Modbus TCP 端点；停止后该端口不再监听。"),
        this);
    hint->setObjectName(QStringLiteral("deviceHint"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("deviceTable"));
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({QStringLiteral("设备"), QStringLiteral("ID"),
                                        QStringLiteral("状态"), QStringLiteral("监听"),
                                        QStringLiteral("unit"), QStringLiteral("点位")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    root->addWidget(m_table, 1);

    auto *actions = new QHBoxLayout;
    auto *addButton = new QPushButton(QStringLiteral("新增设备"), this);
    m_editButton = new QPushButton(QStringLiteral("编辑端点"), this);
    m_toggleButton = new QPushButton(QStringLiteral("启用 / 停用"), this);
    m_removeButton = new QPushButton(QStringLiteral("删除"), this);
    for (QPushButton *button : {addButton, m_editButton, m_toggleButton, m_removeButton}) {
        button->setObjectName(QStringLiteral("ghostButton"));
        button->setCursor(Qt::PointingHandCursor);
        actions->addWidget(button);
    }
    actions->addStretch(1);
    root->addLayout(actions);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_buttonBox);

    connect(addButton, &QPushButton::clicked, this, &DeviceManagerDialog::addDevice);
    connect(m_editButton, &QPushButton::clicked, this, &DeviceManagerDialog::editEndpoint);
    connect(m_toggleButton, &QPushButton::clicked, this, &DeviceManagerDialog::toggleDevice);
    connect(m_removeButton, &QPushButton::clicked, this, &DeviceManagerDialog::removeDevice);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &DeviceManagerDialog::updateButtons);

    refresh();
}

QString DeviceManagerDialog::selectedDeviceId() const
{
    if (m_table == nullptr || m_table->currentRow() < 0)
        return QString();
    QTableWidgetItem *item = m_table->item(m_table->currentRow(), 0);
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

int DeviceManagerDialog::nextFreePort() const
{
    for (int port = 1502; port < 1702; ++port) {
        bool used = false;
        for (const EndpointInfo &endpoint : m_bundle.endpoints) {
            if (endpoint.port == port) {
                used = true;
                break;
            }
        }
        if (!used)
            return port;
    }
    return 1502;
}

void DeviceManagerDialog::showError(const Result<void> &result)
{
    QMessageBox::warning(this, QStringLiteral("操作未完成"), result.error().toString());
}

void DeviceManagerDialog::refresh()
{
    m_table->setRowCount(m_bundle.devices.size());

    for (int row = 0; row < m_bundle.devices.size(); ++row) {
        const Device &device = m_bundle.devices.at(row);
        const EndpointInfo *endpoint = m_bundle.endpointById(device.endpointId);
        const QString listen = endpoint == nullptr
                                   ? QStringLiteral("—")
                                   : QStringLiteral("%1:%2")
                                         .arg(endpoint->bindAddress)
                                         .arg(endpoint->port);
        const QString unit = endpoint == nullptr || endpoint->unitIds.isEmpty()
                                 ? QStringLiteral("—")
                                 : QString::number(endpoint->unitIds.first());
        const int pointCount = m_bundle.signalsOfDevice(device.id).size();

        const QStringList cells = {
            device.name,
            device.id,
            device.enabled && (endpoint == nullptr || endpoint->enabled)
                ? QStringLiteral("启用")
                : QStringLiteral("停用"),
            listen,
            unit,
            QString::number(pointCount),
        };

        for (int column = 0; column < cells.size(); ++column) {
            auto *item = new QTableWidgetItem(cells.at(column));
            item->setData(Qt::UserRole, device.id);
            m_table->setItem(row, column, item);
        }
    }

    if (m_table->rowCount() > 0 && m_table->currentRow() < 0)
        m_table->selectRow(0);
    updateButtons();
}

void DeviceManagerDialog::updateButtons()
{
    const bool hasSelection = !selectedDeviceId().isEmpty();
    m_editButton->setEnabled(hasSelection);
    m_toggleButton->setEnabled(hasSelection);
    m_removeButton->setEnabled(hasSelection && m_bundle.devices.size() > 1);
}

void DeviceManagerDialog::addDevice()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("新增虚拟设备"));
    dialog.setObjectName(QStringLiteral("deviceDialog"));

    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = makeField(form, QStringLiteral("设备名称"), QStringLiteral("新设备"));
    auto *idEdit = makeField(form, QStringLiteral("设备 ID"),
                             DeviceManager::makeDeviceId(nameEdit->text()));
    auto *addressEdit = makeField(form, QStringLiteral("监听地址"), QStringLiteral("127.0.0.1"));
    auto *portEdit = makeField(form, QStringLiteral("端口"), QString::number(nextFreePort()));
    auto *unitEdit = makeField(form, QStringLiteral("unitId"),
                               QString::number(DeviceManager::nextFreeUnitId(m_bundle)));
    auto *descriptionEdit = makeField(form, QStringLiteral("描述"), QString());

    portEdit->setValidator(new QIntValidator(1, 65535, &dialog));
    unitEdit->setValidator(new QIntValidator(1, 247, &dialog));

    // 用户没手改 ID 时，ID 跟着名称自动派生；一旦手改就尊重用户输入。
    idEdit->setProperty("autoId", true);
    connect(idEdit, &QLineEdit::textEdited, &dialog,
            [idEdit] { idEdit->setProperty("autoId", false); });
    connect(nameEdit, &QLineEdit::textChanged, &dialog, [idEdit](const QString &text) {
        if (idEdit->property("autoId").toBool())
            idEdit->setText(DeviceManager::makeDeviceId(text));
    });

    auto *buttons = makeButtons(&dialog);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    VirtualDeviceSpec spec;
    spec.id = idEdit->text().trimmed();
    spec.name = nameEdit->text().trimmed();
    spec.description = descriptionEdit->text().trimmed();
    spec.type = QStringLiteral("PLC");
    spec.bindAddress = addressEdit->text().trimmed();
    spec.port = portEdit->text().toInt();
    spec.unitId = unitEdit->text().toInt();
    spec.enabled = true;

    const Result<void> added = DeviceManager::addVirtualDevice(&m_bundle, spec);
    if (!added.ok()) {
        showError(added);
        return;
    }
    refresh();
}

void DeviceManagerDialog::editEndpoint()
{
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty())
        return;

    const Device *device = m_bundle.deviceById(deviceId);
    const EndpointInfo *endpoint =
        device == nullptr ? nullptr : m_bundle.endpointById(device->endpointId);
    if (endpoint == nullptr) {
        QMessageBox::warning(this, QStringLiteral("操作未完成"),
                             QStringLiteral("该设备没有关联的协议端点"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑端点 — %1").arg(device->name));
    dialog.setObjectName(QStringLiteral("deviceDialog"));

    auto *form = new QFormLayout(&dialog);
    auto *addressEdit = makeField(form, QStringLiteral("监听地址"), endpoint->bindAddress);
    auto *portEdit = makeField(form, QStringLiteral("端口"), QString::number(endpoint->port));
    auto *unitEdit = makeField(
        form, QStringLiteral("unitId"),
        QString::number(endpoint->unitIds.isEmpty() ? 1 : endpoint->unitIds.first()));

    portEdit->setValidator(new QIntValidator(1, 65535, &dialog));
    unitEdit->setValidator(new QIntValidator(1, 247, &dialog));
    form->addRow(makeButtons(&dialog));

    if (dialog.exec() != QDialog::Accepted)
        return;

    const Result<void> updated = DeviceManager::setEndpoint(
        &m_bundle, deviceId, addressEdit->text().trimmed(), portEdit->text().toInt(),
        unitEdit->text().toInt());
    if (!updated.ok()) {
        showError(updated);
        return;
    }
    refresh();
}

void DeviceManagerDialog::removeDevice()
{
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty())
        return;

    const Device *device = m_bundle.deviceById(deviceId);
    const QString name = device == nullptr ? deviceId : device->name;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除虚拟设备"),
        QStringLiteral("确定删除「%1」吗？该设备的点位和独立端点会一起移除。").arg(name));
    if (answer != QMessageBox::Yes)
        return;

    const Result<void> removed = DeviceManager::removeVirtualDevice(&m_bundle, deviceId);
    if (!removed.ok()) {
        showError(removed);
        return;
    }
    refresh();
}

void DeviceManagerDialog::toggleDevice()
{
    const QString deviceId = selectedDeviceId();
    if (deviceId.isEmpty())
        return;

    const Device *device = m_bundle.deviceById(deviceId);
    if (device == nullptr)
        return;

    const Result<void> toggled =
        DeviceManager::setDeviceEnabled(&m_bundle, deviceId, !device->enabled);
    if (!toggled.ok()) {
        showError(toggled);
        return;
    }
    refresh();
}

} // namespace protosim::ui
