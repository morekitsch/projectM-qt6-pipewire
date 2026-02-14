#include "PresetFilterProxyModel.h"

#include <QAbstractItemModel>

PresetFilterProxyModel::PresetFilterProxyModel(QObject *parent) : QSortFilterProxyModel(parent) {
  setFilterCaseSensitivity(Qt::CaseInsensitive);
  setDynamicSortFilter(false);
}

void PresetFilterProxyModel::setFavoritesOnly(bool enabled) {
  if (m_favoritesOnly == enabled) {
    return;
  }

  beginFilterChange();
  m_favoritesOnly = enabled;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool PresetFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
  const QAbstractItemModel *source = sourceModel();
  if (source == nullptr) {
    return false;
  }

  const QModelIndex nameIdx = source->index(sourceRow, 0, sourceParent);
  const QModelIndex favoriteIdx = source->index(sourceRow, 2, sourceParent);
  const QModelIndex tagsIdx = source->index(sourceRow, 3, sourceParent);

  if (m_favoritesOnly && source->data(favoriteIdx, Qt::CheckStateRole).toInt() != Qt::Checked) {
    return false;
  }

  const QString pattern = filterRegularExpression().pattern().trimmed();
  if (pattern.isEmpty()) {
    return true;
  }

  const QString haystack = source->data(nameIdx, Qt::DisplayRole).toString() + QStringLiteral(" ") +
                          source->data(tagsIdx, Qt::DisplayRole).toString();
  return haystack.contains(filterRegularExpression());
}

bool PresetFilterProxyModel::lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const {
  if (sourceLeft.column() == 1 && sourceRight.column() == 1) {
    return sourceModel()->data(sourceLeft, Qt::EditRole).toInt() <
           sourceModel()->data(sourceRight, Qt::EditRole).toInt();
  }
  return QSortFilterProxyModel::lessThan(sourceLeft, sourceRight);
}
