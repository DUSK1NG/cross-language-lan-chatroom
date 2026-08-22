#include "conversation_filter_model.hpp"

ConversationFilterModel::ConversationFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void ConversationFilterModel::setQuery(const QString& query) {
    const QString normalized = query.trimmed();
    if (query_ == normalized) return;
    query_ = normalized;
    refilter();
    emit queryChanged();
}

void ConversationFilterModel::setSearchRoles(const QStringList& roles) {
    if (searchRoles_ == roles) return;
    searchRoles_ = roles;
    refilter();
    emit searchRolesChanged();
}

void ConversationFilterModel::refilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
}

bool ConversationFilterModel::filterAcceptsRow(int sourceRow,
                                                const QModelIndex& sourceParent) const {
    if (query_.isEmpty() || !sourceModel()) return true;

    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    const QHash<int, QByteArray> roles = sourceModel()->roleNames();
    for (const QString& roleName : searchRoles_) {
        int role = -1;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            if (it.value() == roleName.toUtf8()) {
                role = it.key();
                break;
            }
        }
        if (role >= 0 && sourceModel()->data(sourceIndex, role).toString()
                              .contains(query_, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}
