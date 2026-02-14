#pragma once

#include "PlaylistModel.h"
#include "PresetMetadata.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

class SettingsManager : public QObject {
  Q_OBJECT

public:
  explicit SettingsManager(QObject *parent = nullptr);

  QHash<QString, PresetMetadata> loadPresetMetadata() const;
  bool savePresetMetadata(const QString &presetPath, const PresetMetadata &metadata);
  bool savePresetMetadataMap(const QHash<QString, PresetMetadata> &metadataMap);

  bool exportPresetMetadata(const QString &filePath,
                            const QHash<QString, PresetMetadata> &metadataMap) const;
  QHash<QString, PresetMetadata> importPresetMetadata(const QString &filePath,
                                                      bool *ok = nullptr,
                                                      QString *error = nullptr) const;

  QStringList listPlaylists() const;
  bool savePlaylist(const QString &name, const QVector<PlaylistItem> &items);
  QVector<PlaylistItem> loadPlaylist(const QString &name) const;

  bool exportPlaylistToFile(const QString &filePath,
                            const QString &playlistName,
                            const QVector<PlaylistItem> &items) const;
  QVector<PlaylistItem> importPlaylistFromFile(const QString &filePath,
                                               QString *playlistName = nullptr,
                                               bool *ok = nullptr,
                                               QString *error = nullptr) const;

  QVariantMap loadProjectMSettings() const;
  void saveProjectMSettings(const QVariantMap &settings);

private:
  QHash<QString, PresetMetadata> readMetadataFromPath(const QString &path,
                                                      bool *ok,
                                                      QString *error) const;
  bool writeMetadataToPath(const QString &path,
                           const QHash<QString, PresetMetadata> &metadataMap,
                           QString *error) const;

  bool writePlaylistFile(const QString &path,
                         const QString &playlistName,
                         const QVector<PlaylistItem> &items,
                         QString *error) const;
  QVector<PlaylistItem> readPlaylistFile(const QString &path,
                                         QString *playlistName,
                                         bool *ok,
                                         QString *error) const;

  QString appDataDir() const;
  QString metadataPath() const;
  QString playlistsDir() const;
  QString playlistPath(const QString &name) const;
};
