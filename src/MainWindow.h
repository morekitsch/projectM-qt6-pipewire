#pragma once

#include "PresetMetadata.h"

#include <QElapsedTimer>
#include <QMainWindow>

class AudioSource;
struct AudioDeviceInfo;
class PlaylistModel;
class PresetFilterProxyModel;
class PresetLibraryModel;
class ProjectMEngine;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTimer;
class SettingsManager;
class VisualizerWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private Q_SLOTS:
  void choosePresetDirectory();
  void addSelectedPresetToPlaylist();
  void removeSelectedPlaylistItem();
  void movePlaylistItemUp();
  void movePlaylistItemDown();
  void loadSelectedPreset();

  void savePlaylist();
  void loadPlaylist();
  void importPlaylist();
  void exportPlaylist();

  void importPresetMetadata();
  void exportPresetMetadata();

  void applyProjectMSettingsFromUi();

  void togglePlaylistPlayback();
  void playNextPlaylistItem();
  void playPreviousPlaylistItem();
  void onPlaybackTimerTick();
  void onAudioFrameForPlayback(const QVector<float> &monoFrame);
  void onPresetActivated(const QString &presetPath);
  void applyNowPlayingMetadata();
  void togglePreviewFloating();
  void togglePreviewFullscreen();

  void refreshAudioDeviceList();
  void applySelectedAudioDevice();
  void onAudioSourceError(const QString &message);
  void onProjectMStatusMessage(const QString &message);
  void setStatus(const QString &message);

private:
  void bindAudioSource(AudioSource *audioSource);
  void replaceAudioSource(AudioSource *audioSource);
  void updateAudioDeviceDebugPanel(const QVector<AudioDeviceInfo> &devices);
  void updateAudioBackendIndicator();
  void updateRenderBackendIndicator();
  bool startCurrentAudioSourceWithFallback();
  void buildUi();
  void wireSignals();
  void loadInitialState();
  void refreshPlaylistNames();
  void updatePresetDirectory(const QString &path);
  QModelIndex selectedPresetSourceIndex() const;
  bool loadPlaylistRow(int row);
  void updateNowPlayingPanel(const QString &presetPath);
  PresetMetadata currentNowPlayingMetadata() const;

  PresetLibraryModel *m_presetModel = nullptr;
  PlaylistModel *m_playlistModel = nullptr;
  PresetFilterProxyModel *m_presetProxyModel = nullptr;
  SettingsManager *m_settingsManager = nullptr;
  ProjectMEngine *m_projectMEngine = nullptr;
  AudioSource *m_audioSource = nullptr;

  QLineEdit *m_presetSearchEdit = nullptr;
  QCheckBox *m_favoritesOnlyCheck = nullptr;
  QTableView *m_presetTable = nullptr;
  QTableView *m_playlistTable = nullptr;
  QLineEdit *m_playlistNameEdit = nullptr;
  QComboBox *m_playlistPicker = nullptr;
  QLineEdit *m_presetDirectoryEdit = nullptr;
  QLabel *m_nowPlayingNameLabel = nullptr;
  QLabel *m_nowPlayingPathLabel = nullptr;
  QSpinBox *m_nowPlayingRatingSpin = nullptr;
  QCheckBox *m_nowPlayingFavoriteCheck = nullptr;
  QLineEdit *m_nowPlayingTagsEdit = nullptr;
  QComboBox *m_audioDeviceCombo = nullptr;
  QPushButton *m_refreshAudioDevicesButton = nullptr;
  QPlainTextEdit *m_audioDeviceDebugText = nullptr;
  QLabel *m_audioBackendLabel = nullptr;
  QLabel *m_renderBackendLabel = nullptr;

  QCheckBox *m_shuffleCheck = nullptr;
  QComboBox *m_autoAdvanceModeCombo = nullptr;
  QSpinBox *m_autoDurationSecondsSpin = nullptr;
  QSpinBox *m_autoBeatCountSpin = nullptr;
  QDoubleSpinBox *m_autoBeatThresholdSpin = nullptr;
  QPushButton *m_playPauseButton = nullptr;
  QPushButton *m_previewFloatButton = nullptr;
  QPushButton *m_previewFullscreenButton = nullptr;
  QCheckBox *m_showFpsCheck = nullptr;

  VisualizerWidget *m_visualizerWidget = nullptr;
  QDockWidget *m_previewDock = nullptr;

  QSpinBox *m_meshXSpin = nullptr;
  QSpinBox *m_meshYSpin = nullptr;
  QSpinBox *m_targetFpsSpin = nullptr;
  QDoubleSpinBox *m_beatSensitivitySpin = nullptr;
  QCheckBox *m_hardCutEnabledCheck = nullptr;
  QSpinBox *m_hardCutDurationSpin = nullptr;

  QTimer *m_playbackTimer = nullptr;
  QElapsedTimer m_trackElapsed;
  int m_beatsSinceSwitch = 0;
  bool m_lastBeatHigh = false;
  bool m_playlistPlaying = false;
  bool m_syncingNowPlayingUi = false;
  QString m_currentPresetPath;
  bool m_previewBorderlessFullscreen = false;
  QWidget *m_previewHiddenTitleBar = nullptr;
  bool m_audioFallbackApplied = false;
  bool m_syncingAudioDeviceUi = false;
  QString m_preferredAudioDeviceId;
};
