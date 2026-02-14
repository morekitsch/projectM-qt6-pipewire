#pragma once

#include <QSortFilterProxyModel>

class PresetFilterProxyModel : public QSortFilterProxyModel {
  Q_OBJECT

public:
  explicit PresetFilterProxyModel(QObject *parent = nullptr);

  void setFavoritesOnly(bool enabled);

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
  bool lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const override;

private:
  bool m_favoritesOnly = false;
};
