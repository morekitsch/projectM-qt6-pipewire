#pragma once

#include <QAbstractTableModel>
#include <QVector>

struct PlaylistItem {
  QString presetPath;
  QString presetName;
};

class PlaylistModel : public QAbstractTableModel {
  Q_OBJECT

public:
  explicit PlaylistModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

  void addItem(const PlaylistItem &item);
  void removeAt(int row);
  bool moveUp(int row);
  bool moveDown(int row);
  void clearAll();

  QVector<PlaylistItem> items() const;
  void replaceItems(const QVector<PlaylistItem> &items);

private:
  QVector<PlaylistItem> m_items;
};
