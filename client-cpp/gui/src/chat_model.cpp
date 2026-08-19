#include "chat_model.hpp"

ChatListModel::ChatListModel(QStringList roleNames, QObject* parent)
    : QAbstractListModel(parent) {
    int role = Qt::UserRole + 1;
    for (const QString& name : roleNames) {
        roles_.insert(role++, name.toUtf8());
    }
}

int ChatListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QVariant ChatListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
        return {};
    }

    const QByteArray roleName = roles_.value(role);
    if (roleName.isEmpty()) {
        return {};
    }
    return rows_.at(index.row()).value(QString::fromUtf8(roleName));
}

QHash<int, QByteArray> ChatListModel::roleNames() const {
    return roles_;
}

int ChatListModel::roleForName(const QByteArray& name) const {
    for (auto it = roles_.cbegin(); it != roles_.cend(); ++it) {
        if (it.value() == name) {
            return it.key();
        }
    }
    return Qt::DisplayRole;
}

void ChatListModel::append(const QVariantMap& row) {
    const int newRow = rows_.size();
    beginInsertRows({}, newRow, newRow);
    rows_.append(row);
    endInsertRows();
}

int ChatListModel::findRow(const QByteArray& roleName, const QVariant& value) const {
    const QString key = QString::fromUtf8(roleName);
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_.at(row).value(key) == value) {
            return row;
        }
    }
    return -1;
}

QVariant ChatListModel::valueAt(int row, const QByteArray& roleName) const {
    if (row < 0 || row >= rows_.size()) return {};
    return rows_.at(row).value(QString::fromUtf8(roleName));
}

void ChatListModel::updateRow(int row, const QVariantMap& values) {
    if (row < 0 || row >= rows_.size() || values.isEmpty()) return;
    rows_[row].insert(values);
    emit dataChanged(index(row, 0), index(row, 0));
}

void ChatListModel::clear() {
    if (rows_.isEmpty()) {
        return;
    }
    beginResetModel();
    rows_.clear();
    endResetModel();
}
