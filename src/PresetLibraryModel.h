#pragma once

#include "PresetMetadata.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QString>
#include <QVector>

struct PresetEntry {
  QString name;
  QString path;
  PresetMetadata metadata;
};

class PresetLibraryModel : public QAbstractTableModel {
  Q_OBJECT

public:
  explicit PresetLibraryModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  bool setData(const QModelIndex &index, const QVariant &value, int role) override;

  void setPresetDirectory(const QString &directoryPath);
  void applyMetadata(const QHash<QString, PresetMetadata> &metadata);

  QString presetPathForRow(int row) const;
  QString presetNameForRow(int row) const;
  PresetMetadata presetMetadataForRow(int row) const;
  int rowForPresetPath(const QString &presetPath) const;
  bool updateMetadataForPath(const QString &presetPath, const PresetMetadata &metadata);
  QHash<QString, PresetMetadata> metadataMap() const;
  const QVector<PresetEntry> &presets() const;

Q_SIGNALS:
  void metadataChanged(const QString &presetPath, int rating, bool favorite, const QStringList &tags);

private:
  void reloadPresets();

  QString m_directoryPath;
  QVector<PresetEntry> m_presets;
};
