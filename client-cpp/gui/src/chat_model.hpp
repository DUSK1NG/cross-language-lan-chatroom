#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <QStringList>

class ChatListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    explicit ChatListModel(QStringList roleNames, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int roleForName(const QByteArray& name) const;

    void append(const QVariantMap& row);
    int findRow(const QByteArray& roleName, const QVariant& value) const;
    QVariant valueAt(int row, const QByteArray& roleName) const;
    void updateRow(int row, const QVariantMap& values);
    void clear();

private:
    QHash<int, QByteArray> roles_;
    QList<QVariantMap> rows_;
};
