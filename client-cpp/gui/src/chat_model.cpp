#include "chat_model.hpp"

#include <utility>

namespace {
constexpr int kMaxRowsPerModel = 1000;
}

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
    appendRows({row});
}

void ChatListModel::appendRows(const QList<QVariantMap>& rows) {
    if (rows.isEmpty()) return;

    QList<QVariantMap> incoming = rows;
    if (roleForName("messageId") != Qt::DisplayRole) {
        QList<QVariantMap> uniqueIncoming;
        QHash<QString, int> incomingRowsByMessageId;
        for (const QVariantMap& row : std::as_const(incoming)) {
            const QString messageId = row.value("messageId").toString();
            if (messageId.isEmpty()) {
                uniqueIncoming.append(row);
                continue;
            }
            const int existingRow = findRow("messageId", messageId);
            if (existingRow >= 0) {
                updateRow(existingRow, row);
                continue;
            }
            if (incomingRowsByMessageId.contains(messageId)) {
                uniqueIncoming[incomingRowsByMessageId.value(messageId)].insert(row);
                continue;
            }
            incomingRowsByMessageId.insert(messageId, uniqueIncoming.size());
            uniqueIncoming.append(row);
        }
        incoming = std::move(uniqueIncoming);
        if (incoming.isEmpty()) return;
    }
    if (incoming.size() > kMaxRowsPerModel) {
        incoming = incoming.mid(incoming.size() - kMaxRowsPerModel);
    }

    const int overflow = qMax(0, rows_.size() + incoming.size() - kMaxRowsPerModel);
    if (overflow >= rows_.size() && !rows_.isEmpty()) {
        beginResetModel();
        rows_ = incoming;
        endResetModel();
        return;
    }

    if (overflow > 0) {
        beginRemoveRows({}, 0, overflow - 1);
        rows_.remove(0, overflow);
        endRemoveRows();
    }

    const int firstNewRow = rows_.size();
    beginInsertRows({}, firstNewRow, firstNewRow + incoming.size() - 1);
    rows_.append(incoming);
    endInsertRows();
}

void ChatListModel::prependRows(const QList<QVariantMap>& rows) {
    if (rows.isEmpty()) return;

    QList<QVariantMap> incoming = rows;
    if (incoming.size() > kMaxRowsPerModel) {
        incoming = incoming.mid(incoming.size() - kMaxRowsPerModel);
    }

    const int overflow = qMax(0, rows_.size() + incoming.size() - kMaxRowsPerModel);
    if (overflow >= rows_.size() && !rows_.isEmpty()) {
        beginResetModel();
        rows_ = incoming;
        endResetModel();
        return;
    }

    if (overflow > 0) {
        const int firstRemovedRow = rows_.size() - overflow;
        beginRemoveRows({}, firstRemovedRow, rows_.size() - 1);
        rows_.remove(firstRemovedRow, overflow);
        endRemoveRows();
    }

    beginInsertRows({}, 0, incoming.size() - 1);
    rows_ = incoming + rows_;
    endInsertRows();
}

void ChatListModel::replaceRows(const QList<QVariantMap>& rows) {
    QList<QVariantMap> replacement = rows;
    if (replacement.size() > kMaxRowsPerModel) {
        replacement = replacement.mid(replacement.size() - kMaxRowsPerModel);
    }
    if (rows_ == replacement) return;

    beginResetModel();
    rows_ = std::move(replacement);
    endResetModel();
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

void ChatListModel::updateRows(const QList<QVariantMap>& valuesByRow) {
    const int lastRow = qMin(rows_.size(), valuesByRow.size()) - 1;
    if (lastRow < 0) return;

    bool changed = false;
    for (int row = 0; row <= lastRow; ++row) {
        if (valuesByRow.at(row).isEmpty()) continue;
        const QVariantMap& values = valuesByRow.at(row);
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (rows_.at(row).value(it.key()) != it.value()) {
                changed = true;
                break;
            }
        }
        rows_[row].insert(values);
    }
    if (changed) {
        emit dataChanged(index(0, 0), index(lastRow, 0));
    }
}

void ChatListModel::removeRow(int row) {
    if (row < 0 || row >= rows_.size()) return;
    beginRemoveRows({}, row, row);
    rows_.removeAt(row);
    endRemoveRows();
}

void ChatListModel::removeRowsByValue(const QByteArray& roleName, const QVariant& value) {
    for (int row = rows_.size() - 1; row >= 0; --row) {
        if (rows_.at(row).value(QString::fromUtf8(roleName)) == value) removeRow(row);
    }
}

void ChatListModel::clear() {
    replaceRows({});
}
