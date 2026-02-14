#include "PlaylistModel.h"

namespace {
constexpr int kOrderColumn = 0;
constexpr int kPresetColumn = 1;
} // namespace

PlaylistModel::PlaylistModel(QObject *parent) : QAbstractTableModel(parent) {}

int PlaylistModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_items.size();
}

int PlaylistModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return 2;
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
    return {};
  }

  const PlaylistItem &item = m_items.at(index.row());

  if (role == Qt::DisplayRole) {
    if (index.column() == kOrderColumn) {
      return index.row() + 1;
    }
    if (index.column() == kPresetColumn) {
      return item.presetName;
    }
  }

  if (role == Qt::ToolTipRole) {
    return item.presetPath;
  }

  return {};
}

QVariant PlaylistModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }

  if (section == kOrderColumn) {
    return QStringLiteral("Order");
  }
  if (section == kPresetColumn) {
    return QStringLiteral("Preset");
  }

  return {};
}

Qt::ItemFlags PlaylistModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

void PlaylistModel::addItem(const PlaylistItem &item) {
  const int row = m_items.size();
  beginInsertRows({}, row, row);
  m_items.push_back(item);
  endInsertRows();
}

void PlaylistModel::removeAt(int row) {
  if (row < 0 || row >= m_items.size()) {
    return;
  }

  beginRemoveRows({}, row, row);
  m_items.removeAt(row);
  endRemoveRows();

  if (row < m_items.size()) {
    Q_EMIT dataChanged(index(row, kOrderColumn), index(m_items.size() - 1, kOrderColumn),
                       {Qt::DisplayRole});
  }
}

bool PlaylistModel::moveUp(int row) {
  if (row <= 0 || row >= m_items.size()) {
    return false;
  }

  beginMoveRows({}, row, row, {}, row - 1);
  m_items.swapItemsAt(row, row - 1);
  endMoveRows();
  return true;
}

bool PlaylistModel::moveDown(int row) {
  if (row < 0 || row + 1 >= m_items.size()) {
    return false;
  }

  beginMoveRows({}, row, row, {}, row + 2);
  m_items.swapItemsAt(row, row + 1);
  endMoveRows();
  return true;
}

void PlaylistModel::clearAll() {
  beginResetModel();
  m_items.clear();
  endResetModel();
}

QVector<PlaylistItem> PlaylistModel::items() const { return m_items; }

void PlaylistModel::replaceItems(const QVector<PlaylistItem> &items) {
  beginResetModel();
  m_items = items;
  endResetModel();
}
