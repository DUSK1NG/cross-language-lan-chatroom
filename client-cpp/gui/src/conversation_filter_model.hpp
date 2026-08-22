#pragma once

#include <QSortFilterProxyModel>
#include <QStringList>

class ConversationFilterModel final : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QStringList searchRoles READ searchRoles WRITE setSearchRoles NOTIFY searchRolesChanged)

public:
    explicit ConversationFilterModel(QObject* parent = nullptr);

    QString query() const { return query_; }
    QStringList searchRoles() const { return searchRoles_; }

public slots:
    void setQuery(const QString& query);
    void setSearchRoles(const QStringList& roles);

signals:
    void queryChanged();
    void searchRolesChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    void refilter();

    QString query_;
    QStringList searchRoles_;
};
