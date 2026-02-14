#include "SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {
QString sanitizePlaylistName(const QString &name) {
  QString clean = name.trimmed();
  clean.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
  if (clean.isEmpty()) {
    clean = QStringLiteral("playlist");
  }
  return clean;
}

QJsonObject metadataToJson(const PresetMetadata &metadata) {
  QJsonObject obj;
  obj.insert(QStringLiteral("rating"), qBound(1, metadata.rating, 5));
  obj.insert(QStringLiteral("favorite"), metadata.favorite);
  QJsonArray tags;
  for (const QString &tag : metadata.tags) {
    tags.append(tag);
  }
  obj.insert(QStringLiteral("tags"), tags);
  return obj;
}

PresetMetadata metadataFromJson(const QJsonValue &value) {
  PresetMetadata metadata;
  if (value.isDouble()) {
    metadata.rating = qBound(1, value.toInt(3), 5);
    return metadata;
  }

  const QJsonObject obj = value.toObject();
  metadata.rating = qBound(1, obj.value(QStringLiteral("rating")).toInt(3), 5);
  metadata.favorite = obj.value(QStringLiteral("favorite")).toBool(false);

  const QJsonArray tags = obj.value(QStringLiteral("tags")).toArray();
  for (const QJsonValue &tag : tags) {
    const QString text = tag.toString().trimmed();
    if (!text.isEmpty()) {
      metadata.tags.push_back(text);
    }
  }
  metadata.tags.removeDuplicates();
  return metadata;
}
} // namespace

SettingsManager::SettingsManager(QObject *parent) : QObject(parent) {}

QHash<QString, PresetMetadata> SettingsManager::loadPresetMetadata() const {
  bool ok = false;
  const QHash<QString, PresetMetadata> map = readMetadataFromPath(metadataPath(), &ok, nullptr);
  return ok ? map : QHash<QString, PresetMetadata>{};
}

bool SettingsManager::savePresetMetadata(const QString &presetPath, const PresetMetadata &metadata) {
  bool ok = false;
  QHash<QString, PresetMetadata> map = readMetadataFromPath(metadataPath(), &ok, nullptr);
  if (!ok) {
    map.clear();
  }

  map.insert(presetPath, metadata);
  return writeMetadataToPath(metadataPath(), map, nullptr);
}

bool SettingsManager::savePresetMetadataMap(const QHash<QString, PresetMetadata> &metadataMap) {
  return writeMetadataToPath(metadataPath(), metadataMap, nullptr);
}

bool SettingsManager::exportPresetMetadata(const QString &filePath,
                                           const QHash<QString, PresetMetadata> &metadataMap) const {
  return writeMetadataToPath(filePath, metadataMap, nullptr);
}

QHash<QString, PresetMetadata> SettingsManager::importPresetMetadata(const QString &filePath,
                                                                     bool *ok,
                                                                     QString *error) const {
  return readMetadataFromPath(filePath, ok, error);
}

QStringList SettingsManager::listPlaylists() const {
  QDir dir(playlistsDir());
  if (!dir.exists()) {
    return {};
  }

  QStringList names;
  const QStringList files = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files);
  for (const QString &file : files) {
    names << QFileInfo(file).completeBaseName();
  }
  names.sort(Qt::CaseInsensitive);
  return names;
}

bool SettingsManager::savePlaylist(const QString &name, const QVector<PlaylistItem> &items) {
  QDir dir(playlistsDir());
  dir.mkpath(QStringLiteral("."));
  return writePlaylistFile(playlistPath(name), name, items, nullptr);
}

QVector<PlaylistItem> SettingsManager::loadPlaylist(const QString &name) const {
  return readPlaylistFile(playlistPath(name), nullptr, nullptr, nullptr);
}

bool SettingsManager::exportPlaylistToFile(const QString &filePath,
                                           const QString &playlistName,
                                           const QVector<PlaylistItem> &items) const {
  return writePlaylistFile(filePath, playlistName, items, nullptr);
}

QVector<PlaylistItem> SettingsManager::importPlaylistFromFile(const QString &filePath,
                                                              QString *playlistName,
                                                              bool *ok,
                                                              QString *error) const {
  return readPlaylistFile(filePath, playlistName, ok, error);
}

QVariantMap SettingsManager::loadProjectMSettings() const {
  QSettings settings;
  settings.beginGroup(QStringLiteral("projectm"));

  QVariantMap map;
  map.insert(QStringLiteral("meshX"), settings.value(QStringLiteral("meshX"), 32));
  map.insert(QStringLiteral("meshY"), settings.value(QStringLiteral("meshY"), 24));
  map.insert(QStringLiteral("targetFps"), settings.value(QStringLiteral("targetFps"), 60));
  map.insert(QStringLiteral("beatSensitivity"), settings.value(QStringLiteral("beatSensitivity"), 1.0));
  map.insert(QStringLiteral("hardCutEnabled"), settings.value(QStringLiteral("hardCutEnabled"), true));
  map.insert(QStringLiteral("hardCutDuration"), settings.value(QStringLiteral("hardCutDuration"), 20));
  map.insert(QStringLiteral("upscalerPreset"), settings.value(QStringLiteral("upscalerPreset"), QStringLiteral("balanced")));
  map.insert(QStringLiteral("renderScalePercent"), settings.value(QStringLiteral("renderScalePercent"), 77));
  map.insert(QStringLiteral("upscalerSharpness"), settings.value(QStringLiteral("upscalerSharpness"), 0.2));
  map.insert(QStringLiteral("gpuPreference"), settings.value(QStringLiteral("gpuPreference"), QStringLiteral("dgpu")));
  map.insert(QStringLiteral("audioDeviceId"), settings.value(QStringLiteral("audioDeviceId"), QString()));

  settings.endGroup();
  return map;
}

void SettingsManager::saveProjectMSettings(const QVariantMap &projectMSettings) {
  QSettings settings;
  settings.beginGroup(QStringLiteral("projectm"));

  for (auto it = projectMSettings.begin(); it != projectMSettings.end(); ++it) {
    settings.setValue(it.key(), it.value());
  }

  settings.endGroup();
  settings.sync();
}

QHash<QString, PresetMetadata> SettingsManager::readMetadataFromPath(const QString &path,
                                                                     bool *ok,
                                                                     QString *error) const {
  if (ok != nullptr) {
    *ok = false;
  }

  QHash<QString, PresetMetadata> metadataMap;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return metadataMap;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject()) {
    if (error != nullptr) {
      *error = QStringLiteral("Metadata file is not a JSON object.");
    }
    return metadataMap;
  }

  const QJsonObject root = doc.object();
  QJsonObject presetsObject;

  if (root.contains(QStringLiteral("presets")) && root.value(QStringLiteral("presets")).isObject()) {
    presetsObject = root.value(QStringLiteral("presets")).toObject();
  } else {
    presetsObject = root;
  }

  for (auto it = presetsObject.begin(); it != presetsObject.end(); ++it) {
    metadataMap.insert(it.key(), metadataFromJson(it.value()));
  }

  if (ok != nullptr) {
    *ok = true;
  }
  return metadataMap;
}

bool SettingsManager::writeMetadataToPath(const QString &path,
                                          const QHash<QString, PresetMetadata> &metadataMap,
                                          QString *error) const {
  QJsonObject presets;
  for (auto it = metadataMap.begin(); it != metadataMap.end(); ++it) {
    presets.insert(it.key(), metadataToJson(it.value()));
  }

  QJsonObject root;
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("presets"), presets);

  QDir dir(QFileInfo(path).absolutePath());
  dir.mkpath(QStringLiteral("."));

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return true;
}

bool SettingsManager::writePlaylistFile(const QString &path,
                                        const QString &playlistName,
                                        const QVector<PlaylistItem> &items,
                                        QString *error) const {
  QJsonObject root;
  root.insert(QStringLiteral("name"), playlistName);

  QJsonArray entries;
  for (const PlaylistItem &item : items) {
    QJsonObject entry;
    entry.insert(QStringLiteral("presetName"), item.presetName);
    entry.insert(QStringLiteral("presetPath"), item.presetPath);
    entries.append(entry);
  }
  root.insert(QStringLiteral("items"), entries);

  QDir dir(QFileInfo(path).absolutePath());
  dir.mkpath(QStringLiteral("."));

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return true;
}

QVector<PlaylistItem> SettingsManager::readPlaylistFile(const QString &path,
                                                        QString *playlistName,
                                                        bool *ok,
                                                        QString *error) const {
  if (ok != nullptr) {
    *ok = false;
  }

  QVector<PlaylistItem> items;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return items;
  }

  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject()) {
    if (error != nullptr) {
      *error = QStringLiteral("Playlist file is not a JSON object.");
    }
    return items;
  }

  const QJsonObject root = doc.object();
  if (playlistName != nullptr) {
    *playlistName = root.value(QStringLiteral("name")).toString();
  }

  const QJsonArray entries = root.value(QStringLiteral("items")).toArray();
  items.reserve(entries.size());
  for (const QJsonValue &value : entries) {
    const QJsonObject obj = value.toObject();
    PlaylistItem item;
    item.presetName = obj.value(QStringLiteral("presetName")).toString();
    item.presetPath = obj.value(QStringLiteral("presetPath")).toString();
    if (!item.presetPath.isEmpty()) {
      items.push_back(item);
    }
  }

  if (ok != nullptr) {
    *ok = true;
  }
  return items;
}

QString SettingsManager::appDataDir() const {
  QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (root.isEmpty()) {
    root = QStringLiteral(".");
  }
  return root;
}

QString SettingsManager::metadataPath() const { return appDataDir() + QStringLiteral("/preset-metadata.json"); }

QString SettingsManager::playlistsDir() const { return appDataDir() + QStringLiteral("/playlists"); }

QString SettingsManager::playlistPath(const QString &name) const {
  return playlistsDir() + QStringLiteral("/") + sanitizePlaylistName(name) + QStringLiteral(".json");
}
