#include "PresetLibraryModel.h"

#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace {
constexpr int kNameColumn = 0;
constexpr int kRatingColumn = 1;
constexpr int kFavoriteColumn = 2;
constexpr int kTagsColumn = 3;

QStringList parseTags(const QString &raw) {
  QStringList tags;
  for (const QString &piece : raw.split(',', Qt::SkipEmptyParts)) {
    const QString cleaned = piece.trimmed();
    if (!cleaned.isEmpty()) {
      tags.push_back(cleaned);
    }
  }
  tags.removeDuplicates();
  return tags;
}

} // namespace

PresetLibraryModel::PresetLibraryModel(QObject *parent) : QAbstractTableModel(parent) {}

int PresetLibraryModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_presets.size();
}

int PresetLibraryModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return 4;
}

QVariant PresetLibraryModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_presets.size()) {
    return {};
  }

  const PresetEntry &entry = m_presets.at(index.row());
  const PresetMetadata &metadata = entry.metadata;

  if (role == Qt::DisplayRole || role == Qt::EditRole) {
    if (index.column() == kNameColumn) {
      return entry.name;
    }
    if (index.column() == kRatingColumn) {
      return metadata.rating;
    }
    if (index.column() == kFavoriteColumn) {
      return metadata.favorite;
    }
    if (index.column() == kTagsColumn) {
      return metadata.tags.join(QStringLiteral(", "));
    }
  }

  if (role == Qt::CheckStateRole && index.column() == kFavoriteColumn) {
    return metadata.favorite ? Qt::Checked : Qt::Unchecked;
  }

  if (role == Qt::ToolTipRole) {
    return entry.path;
  }

  return {};
}

QVariant PresetLibraryModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
    return QAbstractTableModel::headerData(section, orientation, role);
  }

  if (section == kNameColumn) {
    return QStringLiteral("Preset");
  }
  if (section == kRatingColumn) {
    return QStringLiteral("Rating");
  }
  if (section == kFavoriteColumn) {
    return QStringLiteral("Favorite");
  }
  if (section == kTagsColumn) {
    return QStringLiteral("Tags");
  }

  return {};
}

Qt::ItemFlags PresetLibraryModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }

  Qt::ItemFlags baseFlags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
  if (index.column() == kRatingColumn || index.column() == kTagsColumn) {
    baseFlags |= Qt::ItemIsEditable;
  }
  if (index.column() == kFavoriteColumn) {
    baseFlags |= Qt::ItemIsEditable | Qt::ItemIsUserCheckable;
  }
  return baseFlags;
}

bool PresetLibraryModel::setData(const QModelIndex &index, const QVariant &value, int role) {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_presets.size()) {
    return false;
  }

  PresetEntry &entry = m_presets[index.row()];
  bool changed = false;

  if (index.column() == kRatingColumn && role == Qt::EditRole) {
    bool ok = false;
    const int newRating = value.toInt(&ok);
    if (!ok) {
      return false;
    }
    const int bounded = std::clamp(newRating, 1, 5);
    if (entry.metadata.rating != bounded) {
      entry.metadata.rating = bounded;
      changed = true;
    }
  } else if (index.column() == kFavoriteColumn &&
             (role == Qt::CheckStateRole || role == Qt::EditRole)) {
    const bool favorite = role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool();
    if (entry.metadata.favorite != favorite) {
      entry.metadata.favorite = favorite;
      changed = true;
    }
  } else if (index.column() == kTagsColumn && role == Qt::EditRole) {
    const QStringList tags = parseTags(value.toString());
    if (entry.metadata.tags != tags) {
      entry.metadata.tags = tags;
      changed = true;
    }
  }

  if (!changed) {
    return false;
  }

  Q_EMIT dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
  Q_EMIT metadataChanged(entry.path,
                         entry.metadata.rating,
                         entry.metadata.favorite,
                         entry.metadata.tags);
  return true;
}

void PresetLibraryModel::setPresetDirectory(const QString &directoryPath) {
  if (m_directoryPath == directoryPath) {
    return;
  }

  m_directoryPath = directoryPath;
  reloadPresets();
}

void PresetLibraryModel::applyMetadata(const QHash<QString, PresetMetadata> &metadata) {
  for (PresetEntry &entry : m_presets) {
    if (metadata.contains(entry.path)) {
      const PresetMetadata &info = metadata.value(entry.path);
      entry.metadata.rating = std::clamp(info.rating, 1, 5);
      entry.metadata.favorite = info.favorite;
      entry.metadata.tags = info.tags;
    }
  }

  if (!m_presets.isEmpty()) {
    Q_EMIT dataChanged(index(0, 0), index(m_presets.size() - 1, kTagsColumn),
                       {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
  }
}

QString PresetLibraryModel::presetPathForRow(int row) const {
  if (row < 0 || row >= m_presets.size()) {
    return {};
  }
  return m_presets.at(row).path;
}

QString PresetLibraryModel::presetNameForRow(int row) const {
  if (row < 0 || row >= m_presets.size()) {
    return {};
  }
  return m_presets.at(row).name;
}

PresetMetadata PresetLibraryModel::presetMetadataForRow(int row) const {
  if (row < 0 || row >= m_presets.size()) {
    return {};
  }
  return m_presets.at(row).metadata;
}

int PresetLibraryModel::rowForPresetPath(const QString &presetPath) const {
  for (int i = 0; i < m_presets.size(); ++i) {
    if (m_presets.at(i).path == presetPath) {
      return i;
    }
  }
  return -1;
}

bool PresetLibraryModel::updateMetadataForPath(const QString &presetPath, const PresetMetadata &metadata) {
  const int row = rowForPresetPath(presetPath);
  if (row < 0) {
    return false;
  }

  PresetEntry &entry = m_presets[row];
  const PresetMetadata normalized = {
      .rating = std::clamp(metadata.rating, 1, 5),
      .favorite = metadata.favorite,
      .tags = metadata.tags,
  };

  if (entry.metadata.rating == normalized.rating && entry.metadata.favorite == normalized.favorite &&
      entry.metadata.tags == normalized.tags) {
    return true;
  }

  entry.metadata = normalized;
  const QModelIndex left = index(row, kRatingColumn);
  const QModelIndex right = index(row, kTagsColumn);
  Q_EMIT dataChanged(left, right, {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
  Q_EMIT metadataChanged(entry.path,
                         entry.metadata.rating,
                         entry.metadata.favorite,
                         entry.metadata.tags);
  return true;
}

QHash<QString, PresetMetadata> PresetLibraryModel::metadataMap() const {
  QHash<QString, PresetMetadata> map;
  map.reserve(m_presets.size());
  for (const PresetEntry &entry : m_presets) {
    map.insert(entry.path, entry.metadata);
  }
  return map;
}

const QVector<PresetEntry> &PresetLibraryModel::presets() const { return m_presets; }

void PresetLibraryModel::reloadPresets() {
  beginResetModel();
  m_presets.clear();

  if (!m_directoryPath.isEmpty()) {
    QDirIterator it(m_directoryPath,
                    QStringList{QStringLiteral("*.milk"), QStringLiteral("*.prjm")},
                    QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
      const QString path = it.next();
      QFileInfo info(path);
      PresetEntry entry;
      entry.name = info.completeBaseName();
      entry.path = info.absoluteFilePath();
      m_presets.push_back(entry);
    }

    std::sort(m_presets.begin(), m_presets.end(),
              [](const PresetEntry &a, const PresetEntry &b) {
                return a.name.localeAwareCompare(b.name) < 0;
              });
  }

  endResetModel();
}
