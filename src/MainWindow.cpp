#include "MainWindow.h"

#include "PlaylistModel.h"
#include "PresetFilterProxyModel.h"
#include "PresetLibraryModel.h"
#include "ProjectMEngine.h"
#include "SettingsManager.h"
#include "VisualizerWidget.h"
#include "audio/AudioSource.h"
#include "audio/AudioSourceFactory.h"
#include "audio/DummyAudioSource.h"
#include "widgets/RatingDelegate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDir>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSettings>
#include <QSizePolicy>
#include <QStringList>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTabWidget>
#include <QMetaObject>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString defaultPresetDirectory() {
  Q_UNUSED(QCoreApplication::applicationDirPath());
  return QDir::homePath() + QStringLiteral("/.projectM/presets");
}

QString playlistFallbackName(const QString &filePath) {
  return QFileInfo(filePath).completeBaseName();
}

bool upscalerPresetValues(const QString &presetId, int *scalePercent, double *sharpness) {
  if (scalePercent == nullptr || sharpness == nullptr) {
    return false;
  }

  const QString normalized = presetId.trimmed().toLower();
  if (normalized == QStringLiteral("quality")) {
    *scalePercent = 85;
    *sharpness = 0.15;
    return true;
  }
  if (normalized == QStringLiteral("balanced")) {
    *scalePercent = 77;
    *sharpness = 0.20;
    return true;
  }
  if (normalized == QStringLiteral("performance")) {
    *scalePercent = 67;
    *sharpness = 0.25;
    return true;
  }
  return false;
}

QString detectUpscalerPresetId(int scalePercent, double sharpness) {
  struct Candidate {
    const char *id;
  };
  const Candidate candidates[] = {{"quality"}, {"balanced"}, {"performance"}};
  for (const Candidate &candidate : candidates) {
    int presetScale = 0;
    double presetSharpness = 0.0;
    if (upscalerPresetValues(QString::fromLatin1(candidate.id), &presetScale, &presetSharpness) &&
        presetScale == scalePercent && qFuzzyCompare(1.0 + presetSharpness, 1.0 + sharpness)) {
      return QString::fromLatin1(candidate.id);
    }
  }
  return QStringLiteral("custom");
}

void allowHorizontalShrink(QWidget *widget) {
  if (widget == nullptr) {
    return;
  }

  QSizePolicy policy = widget->sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Preferred);
  widget->setSizePolicy(policy);
  widget->setMinimumWidth(0);
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  m_presetModel = new PresetLibraryModel(this);
  m_playlistModel = new PlaylistModel(this);
  m_settingsManager = new SettingsManager(this);
  m_projectMEngine = new ProjectMEngine(this);

  m_presetProxyModel = new PresetFilterProxyModel(this);
  m_presetProxyModel->setSourceModel(m_presetModel);

  buildUi();
  wireSignals();
  loadInitialState();

  bindAudioSource(createAudioSource(this));
  startCurrentAudioSourceWithFallback();
  updateAudioBackendIndicator();
  updateRenderBackendIndicator();
  refreshAudioDeviceList();

  connect(m_projectMEngine, &ProjectMEngine::frameReady, m_visualizerWidget, &VisualizerWidget::consumeFrame);

  m_playbackTimer = new QTimer(this);
  m_playbackTimer->setInterval(200);
  connect(m_playbackTimer, &QTimer::timeout, this, &MainWindow::onPlaybackTimerTick);
}

MainWindow::~MainWindow() {
  if (m_audioSource != nullptr) {
    m_audioSource->stop();
  }
}

void MainWindow::buildUi() {
  setWindowTitle(QStringLiteral("projectM Qt6 PipeWire Player"));
  resize(1380, 860);

  auto *central = new QWidget(this);
  auto *rootLayout = new QVBoxLayout(central);

  auto *tabs = new QTabWidget(central);
  rootLayout->addWidget(tabs);

  auto *mainTab = new QWidget(tabs);
  auto *mainTabLayout = new QVBoxLayout(mainTab);

  auto *topControls = new QGridLayout();
  m_presetDirectoryEdit = new QLineEdit(mainTab);
  m_presetDirectoryEdit->setReadOnly(true);
  auto *browseButton = new QPushButton(QStringLiteral("Choose Preset Directory"), mainTab);
  m_presetSearchEdit = new QLineEdit(mainTab);
  m_presetSearchEdit->setPlaceholderText(QStringLiteral("Search name/tags..."));
  m_favoritesOnlyCheck = new QCheckBox(QStringLiteral("Favorites only"), mainTab);
  allowHorizontalShrink(browseButton);
  allowHorizontalShrink(m_favoritesOnlyCheck);

  topControls->setColumnStretch(0, 1);
  topControls->setColumnStretch(1, 3);
  topControls->addWidget(new QLabel(QStringLiteral("Presets:"), mainTab), 0, 0);
  topControls->addWidget(m_presetDirectoryEdit, 0, 1);
  topControls->addWidget(browseButton, 0, 2);
  topControls->addWidget(m_presetSearchEdit, 1, 0, 1, 2);
  topControls->addWidget(m_favoritesOnlyCheck, 1, 2);
  mainTabLayout->addLayout(topControls);

  auto *splitter = new QSplitter(Qt::Horizontal, mainTab);
  splitter->setChildrenCollapsible(true);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  mainTabLayout->addWidget(splitter, 1);

  auto *presetPane = new QWidget(splitter);
  auto *presetLayout = new QVBoxLayout(presetPane);
  presetLayout->addWidget(new QLabel(QStringLiteral("Preset Browser"), presetPane));

  m_presetTable = new QTableView(presetPane);
  m_presetTable->setModel(m_presetProxyModel);
  m_presetTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_presetTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_presetTable->setAlternatingRowColors(true);
  m_presetTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_presetTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_presetTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_presetTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  m_presetTable->setItemDelegateForColumn(1, new RatingDelegate(m_presetTable));
  m_presetTable->setSortingEnabled(true);
  m_presetTable->sortByColumn(1, Qt::DescendingOrder);
  presetLayout->addWidget(m_presetTable, 1);

  auto *presetButtons = new QGridLayout();
  auto *loadPresetButton = new QPushButton(QStringLiteral("Load Preset"), presetPane);
  auto *addPresetButton = new QPushButton(QStringLiteral("Add to Playlist"), presetPane);
  auto *importMetadataButton = new QPushButton(QStringLiteral("Import Metadata"), presetPane);
  auto *exportMetadataButton = new QPushButton(QStringLiteral("Export Metadata"), presetPane);
  allowHorizontalShrink(loadPresetButton);
  allowHorizontalShrink(addPresetButton);
  allowHorizontalShrink(importMetadataButton);
  allowHorizontalShrink(exportMetadataButton);
  presetButtons->setColumnStretch(0, 1);
  presetButtons->setColumnStretch(1, 1);
  presetButtons->addWidget(loadPresetButton, 0, 0);
  presetButtons->addWidget(addPresetButton, 0, 1);
  presetButtons->addWidget(importMetadataButton, 1, 0);
  presetButtons->addWidget(exportMetadataButton, 1, 1);
  presetLayout->addLayout(presetButtons);

  auto *rightPane = new QWidget(splitter);
  auto *rightLayout = new QVBoxLayout(rightPane);

  auto *previewControls = new QGridLayout();
  m_previewFloatButton = new QPushButton(QStringLiteral("Float Preview"), rightPane);
  m_previewFullscreenButton = new QPushButton(QStringLiteral("Fullscreen Preview"), rightPane);
  m_showFpsCheck = new QCheckBox(QStringLiteral("Show FPS"), rightPane);
  allowHorizontalShrink(m_previewFloatButton);
  allowHorizontalShrink(m_previewFullscreenButton);
  allowHorizontalShrink(m_showFpsCheck);
  previewControls->setColumnStretch(0, 1);
  previewControls->setColumnStretch(1, 1);
  previewControls->addWidget(m_previewFloatButton, 0, 0);
  previewControls->addWidget(m_previewFullscreenButton, 0, 1);
  previewControls->addWidget(m_showFpsCheck, 1, 0, 1, 2);
  rightLayout->addLayout(previewControls);

  auto *nowPlayingGroup = new QGroupBox(QStringLiteral("Now Playing"), rightPane);
  auto *nowPlayingLayout = new QFormLayout(nowPlayingGroup);
  m_nowPlayingNameLabel = new QLabel(QStringLiteral("None"), nowPlayingGroup);
  m_nowPlayingPathLabel = new QLabel(QStringLiteral("-"), nowPlayingGroup);
  m_nowPlayingPathLabel->setWordWrap(true);
  m_nowPlayingPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_nowPlayingRatingSpin = new QSpinBox(nowPlayingGroup);
  m_nowPlayingRatingSpin->setRange(1, 5);
  m_nowPlayingFavoriteCheck = new QCheckBox(QStringLiteral("Favorite"), nowPlayingGroup);
  m_nowPlayingTagsEdit = new QLineEdit(nowPlayingGroup);
  m_nowPlayingTagsEdit->setPlaceholderText(QStringLiteral("comma,separated,tags"));
  auto *saveNowPlayingButton = new QPushButton(QStringLiteral("Save Metadata"), nowPlayingGroup);

  nowPlayingLayout->addRow(QStringLiteral("Preset"), m_nowPlayingNameLabel);
  nowPlayingLayout->addRow(QStringLiteral("Path"), m_nowPlayingPathLabel);
  nowPlayingLayout->addRow(QStringLiteral("Rating"), m_nowPlayingRatingSpin);
  nowPlayingLayout->addRow(QStringLiteral("Favorite"), m_nowPlayingFavoriteCheck);
  nowPlayingLayout->addRow(QStringLiteral("Tags"), m_nowPlayingTagsEdit);
  nowPlayingLayout->addRow(QString(), saveNowPlayingButton);
  rightLayout->addWidget(nowPlayingGroup, 1);

  auto *playlistGroup = new QGroupBox(QStringLiteral("Playlist (Ordered Two-Column List)"), rightPane);
  auto *playlistLayout = new QVBoxLayout(playlistGroup);

  auto *playlistTop = new QVBoxLayout();
  m_playlistNameEdit = new QLineEdit(playlistGroup);
  m_playlistNameEdit->setPlaceholderText(QStringLiteral("Playlist name"));
  auto *savePlaylistButton = new QPushButton(QStringLiteral("Save"), playlistGroup);
  m_playlistPicker = new QComboBox(playlistGroup);
  auto *loadPlaylistButton = new QPushButton(QStringLiteral("Load"), playlistGroup);
  auto *importPlaylistButton = new QPushButton(QStringLiteral("Import JSON"), playlistGroup);
  auto *exportPlaylistButton = new QPushButton(QStringLiteral("Export JSON"), playlistGroup);
  allowHorizontalShrink(savePlaylistButton);
  allowHorizontalShrink(m_playlistPicker);
  allowHorizontalShrink(loadPlaylistButton);
  allowHorizontalShrink(importPlaylistButton);
  allowHorizontalShrink(exportPlaylistButton);

  auto *playlistNameRow = new QHBoxLayout();
  playlistNameRow->addWidget(m_playlistNameEdit, 1);
  playlistNameRow->addWidget(savePlaylistButton);
  auto *playlistLoadRow = new QHBoxLayout();
  playlistLoadRow->addWidget(m_playlistPicker, 1);
  playlistLoadRow->addWidget(loadPlaylistButton);
  auto *playlistImportExportRow = new QHBoxLayout();
  playlistImportExportRow->addWidget(importPlaylistButton);
  playlistImportExportRow->addWidget(exportPlaylistButton);
  playlistImportExportRow->addStretch(1);
  playlistTop->addLayout(playlistNameRow);
  playlistTop->addLayout(playlistLoadRow);
  playlistTop->addLayout(playlistImportExportRow);
  playlistLayout->addLayout(playlistTop);

  m_playlistTable = new QTableView(playlistGroup);
  m_playlistTable->setModel(m_playlistModel);
  m_playlistTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_playlistTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_playlistTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_playlistTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  playlistLayout->addWidget(m_playlistTable, 1);

  auto *playlistButtons = new QHBoxLayout();
  auto *removeButton = new QPushButton(QStringLiteral("Remove"), playlistGroup);
  auto *upButton = new QPushButton(QStringLiteral("Move Up"), playlistGroup);
  auto *downButton = new QPushButton(QStringLiteral("Move Down"), playlistGroup);
  auto *clearButton = new QPushButton(QStringLiteral("Clear"), playlistGroup);
  allowHorizontalShrink(removeButton);
  allowHorizontalShrink(upButton);
  allowHorizontalShrink(downButton);
  allowHorizontalShrink(clearButton);

  playlistButtons->addWidget(removeButton);
  playlistButtons->addWidget(upButton);
  playlistButtons->addWidget(downButton);
  playlistButtons->addWidget(clearButton);
  playlistLayout->addLayout(playlistButtons);

  auto *playbackControls = new QVBoxLayout();
  auto *prevButton = new QPushButton(QStringLiteral("Prev"), playlistGroup);
  m_playPauseButton = new QPushButton(QStringLiteral("Play"), playlistGroup);
  auto *nextButton = new QPushButton(QStringLiteral("Next"), playlistGroup);
  m_shuffleCheck = new QCheckBox(QStringLiteral("Shuffle"), playlistGroup);
  allowHorizontalShrink(prevButton);
  allowHorizontalShrink(m_playPauseButton);
  allowHorizontalShrink(nextButton);
  allowHorizontalShrink(m_shuffleCheck);

  m_autoAdvanceModeCombo = new QComboBox(playlistGroup);
  m_autoAdvanceModeCombo->addItems(
      QStringList{QStringLiteral("None"), QStringLiteral("Duration"), QStringLiteral("Beat Count")});
  m_autoAdvanceModeCombo->setCurrentIndex(1);
  allowHorizontalShrink(m_autoAdvanceModeCombo);

  m_autoDurationSecondsSpin = new QSpinBox(playlistGroup);
  m_autoDurationSecondsSpin->setRange(2, 3600);
  m_autoDurationSecondsSpin->setValue(20);
  m_autoBeatCountSpin = new QSpinBox(playlistGroup);
  m_autoBeatCountSpin->setRange(1, 128);
  m_autoBeatCountSpin->setValue(16);
  m_autoBeatThresholdSpin = new QDoubleSpinBox(playlistGroup);
  m_autoBeatThresholdSpin->setRange(0.001, 1.0);
  m_autoBeatThresholdSpin->setDecimals(3);
  m_autoBeatThresholdSpin->setValue(0.12);
  m_autoBeatThresholdSpin->setSingleStep(0.01);

  auto *transportRow = new QHBoxLayout();
  transportRow->addWidget(prevButton);
  transportRow->addWidget(m_playPauseButton);
  transportRow->addWidget(nextButton);
  transportRow->addWidget(m_shuffleCheck);
  transportRow->addStretch(1);
  auto *timingRow = new QHBoxLayout();
  timingRow->addWidget(new QLabel(QStringLiteral("Advance"), playlistGroup));
  timingRow->addWidget(m_autoAdvanceModeCombo, 1);
  timingRow->addWidget(new QLabel(QStringLiteral("Seconds"), playlistGroup));
  timingRow->addWidget(m_autoDurationSecondsSpin);
  auto *beatRow = new QHBoxLayout();
  beatRow->addWidget(new QLabel(QStringLiteral("Beats"), playlistGroup));
  beatRow->addWidget(m_autoBeatCountSpin);
  beatRow->addWidget(new QLabel(QStringLiteral("Beat Threshold"), playlistGroup));
  beatRow->addWidget(m_autoBeatThresholdSpin);
  beatRow->addStretch(1);
  playbackControls->addLayout(transportRow);
  playbackControls->addLayout(timingRow);
  playbackControls->addLayout(beatRow);
  playlistLayout->addLayout(playbackControls);

  rightLayout->addWidget(playlistGroup, 3);

  tabs->addTab(mainTab, QStringLiteral("Library"));

  auto *settingsTab = new QWidget(tabs);
  auto *settingsLayout = new QVBoxLayout(settingsTab);
  auto *form = new QFormLayout();

  m_meshXSpin = new QSpinBox(settingsTab);
  m_meshXSpin->setRange(8, 256);
  m_meshYSpin = new QSpinBox(settingsTab);
  m_meshYSpin->setRange(8, 256);
  m_targetFpsSpin = new QSpinBox(settingsTab);
  m_targetFpsSpin->setRange(15, 240);
  m_beatSensitivitySpin = new QDoubleSpinBox(settingsTab);
  m_beatSensitivitySpin->setRange(0.1, 5.0);
  m_beatSensitivitySpin->setSingleStep(0.1);
  m_hardCutEnabledCheck = new QCheckBox(settingsTab);
  m_hardCutDurationSpin = new QSpinBox(settingsTab);
  m_hardCutDurationSpin->setRange(1, 120);
  m_upscalePresetCombo = new QComboBox(settingsTab);
  m_upscalePresetCombo->addItem(QStringLiteral("Quality"), QStringLiteral("quality"));
  m_upscalePresetCombo->addItem(QStringLiteral("Balanced"), QStringLiteral("balanced"));
  m_upscalePresetCombo->addItem(QStringLiteral("Performance"), QStringLiteral("performance"));
  m_upscalePresetCombo->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
  m_renderScaleSpin = new QSpinBox(settingsTab);
  m_renderScaleSpin->setRange(50, 100);
  m_renderScaleSpin->setSuffix(QStringLiteral("%"));
  m_upscaleSharpnessSpin = new QDoubleSpinBox(settingsTab);
  m_upscaleSharpnessSpin->setRange(0.0, 1.0);
  m_upscaleSharpnessSpin->setDecimals(2);
  m_upscaleSharpnessSpin->setSingleStep(0.05);
  m_gpuPreferenceCombo = new QComboBox(settingsTab);
  m_gpuPreferenceCombo->addItem(QStringLiteral("Auto (system default)"), QStringLiteral("auto"));
  m_gpuPreferenceCombo->addItem(QStringLiteral("Discrete GPU (dGPU)"), QStringLiteral("dgpu"));
  m_gpuPreferenceCombo->addItem(QStringLiteral("Integrated GPU (iGPU)"), QStringLiteral("igpu"));
  m_audioDeviceCombo = new QComboBox(settingsTab);
  m_audioDeviceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_audioDeviceCombo->setMinimumContentsLength(20);
  m_refreshAudioDevicesButton = new QPushButton(QStringLiteral("Refresh"), settingsTab);
  allowHorizontalShrink(m_audioDeviceCombo);
  allowHorizontalShrink(m_refreshAudioDevicesButton);

  auto *audioDeviceRowWidget = new QWidget(settingsTab);
  auto *audioDeviceRowLayout = new QHBoxLayout(audioDeviceRowWidget);
  audioDeviceRowLayout->setContentsMargins(0, 0, 0, 0);
  audioDeviceRowLayout->addWidget(m_audioDeviceCombo, 1);
  audioDeviceRowLayout->addWidget(m_refreshAudioDevicesButton);

  form->addRow(QStringLiteral("Mesh X"), m_meshXSpin);
  form->addRow(QStringLiteral("Mesh Y"), m_meshYSpin);
  form->addRow(QStringLiteral("Target FPS"), m_targetFpsSpin);
  form->addRow(QStringLiteral("Beat Sensitivity"), m_beatSensitivitySpin);
  form->addRow(QStringLiteral("Hard Cut Enabled"), m_hardCutEnabledCheck);
  form->addRow(QStringLiteral("Hard Cut Duration (s)"), m_hardCutDurationSpin);
  form->addRow(QStringLiteral("Upscaler Preset"), m_upscalePresetCombo);
  form->addRow(QStringLiteral("Render Scale"), m_renderScaleSpin);
  form->addRow(QStringLiteral("Upscale Sharpness"), m_upscaleSharpnessSpin);
  form->addRow(QStringLiteral("GPU Preference (restart app)"), m_gpuPreferenceCombo);
  form->addRow(QStringLiteral("Audio Input"), audioDeviceRowWidget);

  settingsLayout->addLayout(form);
  settingsLayout->addWidget(new QLabel(QStringLiteral("Audio Node Debug"), settingsTab));
  m_audioDeviceDebugText = new QPlainTextEdit(settingsTab);
  m_audioDeviceDebugText->setReadOnly(true);
  m_audioDeviceDebugText->setMinimumHeight(150);
  m_audioDeviceDebugText->setPlaceholderText(
      QStringLiteral("Audio node details will appear here after device refresh."));
  settingsLayout->addWidget(m_audioDeviceDebugText);

  auto *applySettingsButton = new QPushButton(QStringLiteral("Apply Settings"), settingsTab);
  settingsLayout->addWidget(applySettingsButton);
  settingsLayout->addStretch(1);

  tabs->addTab(settingsTab, QStringLiteral("Settings"));
  setCentralWidget(central);
  setDockNestingEnabled(true);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

  m_previewDock = new QDockWidget(QStringLiteral("Preview"), this);
  m_previewDock->setAllowedAreas(Qt::AllDockWidgetAreas);
  m_previewDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  m_visualizerWidget = new VisualizerWidget(m_projectMEngine);
  m_visualizerContainer = QWidget::createWindowContainer(m_visualizerWidget, m_previewDock);
  m_visualizerContainer->setFocusPolicy(Qt::StrongFocus);
  m_visualizerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_visualizerContainer->setMinimumWidth(0);
  m_previewDock->setWidget(m_visualizerContainer);
  m_previewDock->setMinimumWidth(240);
  addDockWidget(Qt::RightDockWidgetArea, m_previewDock);
  resizeDocks({m_previewDock}, {520}, Qt::Horizontal);
  m_previewHiddenTitleBar = new QWidget(m_previewDock);
  m_previewHiddenTitleBar->setFixedHeight(0);

  statusBar()->showMessage(QStringLiteral("Ready"));
  m_renderBackendLabel = new QLabel(QStringLiteral("Render: fallback"), this);
  m_audioBackendLabel = new QLabel(QStringLiteral("Audio: unavailable"), this);
  statusBar()->addPermanentWidget(m_renderBackendLabel);
  statusBar()->addPermanentWidget(m_audioBackendLabel);

  connect(browseButton, &QPushButton::clicked, this, &MainWindow::choosePresetDirectory);
  connect(addPresetButton, &QPushButton::clicked, this, &MainWindow::addSelectedPresetToPlaylist);
  connect(loadPresetButton, &QPushButton::clicked, this, &MainWindow::loadSelectedPreset);
  connect(importMetadataButton, &QPushButton::clicked, this, &MainWindow::importPresetMetadata);
  connect(exportMetadataButton, &QPushButton::clicked, this, &MainWindow::exportPresetMetadata);

  connect(savePlaylistButton, &QPushButton::clicked, this, &MainWindow::savePlaylist);
  connect(loadPlaylistButton, &QPushButton::clicked, this, &MainWindow::loadPlaylist);
  connect(importPlaylistButton, &QPushButton::clicked, this, &MainWindow::importPlaylist);
  connect(exportPlaylistButton, &QPushButton::clicked, this, &MainWindow::exportPlaylist);

  connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedPlaylistItem);
  connect(upButton, &QPushButton::clicked, this, &MainWindow::movePlaylistItemUp);
  connect(downButton, &QPushButton::clicked, this, &MainWindow::movePlaylistItemDown);
  connect(clearButton, &QPushButton::clicked, m_playlistModel, &PlaylistModel::clearAll);

  connect(prevButton, &QPushButton::clicked, this, &MainWindow::playPreviousPlaylistItem);
  connect(m_playPauseButton, &QPushButton::clicked, this, &MainWindow::togglePlaylistPlayback);
  connect(nextButton, &QPushButton::clicked, this, &MainWindow::playNextPlaylistItem);
  connect(m_previewFloatButton, &QPushButton::clicked, this, &MainWindow::togglePreviewFloating);
  connect(m_previewFullscreenButton, &QPushButton::clicked, this, &MainWindow::togglePreviewFullscreen);
  connect(m_showFpsCheck, &QCheckBox::toggled, m_visualizerWidget, &VisualizerWidget::setFpsDisplayEnabled);
  connect(saveNowPlayingButton, &QPushButton::clicked, this, &MainWindow::applyNowPlayingMetadata);
  connect(m_nowPlayingRatingSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
    if (!m_syncingNowPlayingUi) {
      applyNowPlayingMetadata();
    }
  });
  connect(m_nowPlayingFavoriteCheck, &QCheckBox::toggled, this, [this](bool) {
    if (!m_syncingNowPlayingUi) {
      applyNowPlayingMetadata();
    }
  });
  connect(m_nowPlayingTagsEdit, &QLineEdit::editingFinished, this, &MainWindow::applyNowPlayingMetadata);

  connect(m_upscalePresetCombo,
          qOverload<int>(&QComboBox::currentIndexChanged),
          this,
          [this](int) {
            if (m_syncingUpscalerPresetUi || m_upscalePresetCombo == nullptr || m_renderScaleSpin == nullptr ||
                m_upscaleSharpnessSpin == nullptr) {
              return;
            }

            const QString presetId = m_upscalePresetCombo->currentData().toString().trimmed().toLower();
            int presetScale = 0;
            double presetSharpness = 0.0;
            if (!upscalerPresetValues(presetId, &presetScale, &presetSharpness)) {
              return;
            }

            m_syncingUpscalerPresetUi = true;
            {
              const QSignalBlocker scaleBlocker(m_renderScaleSpin);
              const QSignalBlocker sharpnessBlocker(m_upscaleSharpnessSpin);
              m_renderScaleSpin->setValue(presetScale);
              m_upscaleSharpnessSpin->setValue(presetSharpness);
            }
            m_syncingUpscalerPresetUi = false;
            applyProjectMSettingsFromUi();
          });

  connect(m_renderScaleSpin,
          qOverload<int>(&QSpinBox::valueChanged),
          this,
          [this](int) {
            if (m_syncingUpscalerPresetUi || m_upscalePresetCombo == nullptr || m_upscaleSharpnessSpin == nullptr) {
              return;
            }
            const QString detected =
                detectUpscalerPresetId(m_renderScaleSpin->value(), m_upscaleSharpnessSpin->value());
            const int idx = m_upscalePresetCombo->findData(detected);
            if (idx >= 0 && idx != m_upscalePresetCombo->currentIndex()) {
              const QSignalBlocker blocker(m_upscalePresetCombo);
              m_upscalePresetCombo->setCurrentIndex(idx);
            }
          });

  connect(m_upscaleSharpnessSpin,
          qOverload<double>(&QDoubleSpinBox::valueChanged),
          this,
          [this](double) {
            if (m_syncingUpscalerPresetUi || m_upscalePresetCombo == nullptr || m_renderScaleSpin == nullptr) {
              return;
            }
            const QString detected =
                detectUpscalerPresetId(m_renderScaleSpin->value(), m_upscaleSharpnessSpin->value());
            const int idx = m_upscalePresetCombo->findData(detected);
            if (idx >= 0 && idx != m_upscalePresetCombo->currentIndex()) {
              const QSignalBlocker blocker(m_upscalePresetCombo);
              m_upscalePresetCombo->setCurrentIndex(idx);
            }
          });

  connect(applySettingsButton, &QPushButton::clicked, this, &MainWindow::applyProjectMSettingsFromUi);
  connect(m_refreshAudioDevicesButton, &QPushButton::clicked, this, &MainWindow::refreshAudioDeviceList);
  connect(m_audioDeviceCombo,
          qOverload<int>(&QComboBox::currentIndexChanged),
          this,
          [this](int) {
            if (!m_syncingAudioDeviceUi) {
              applySelectedAudioDevice();
            }
          });
  connect(m_previewDock, &QDockWidget::topLevelChanged, this, [this](bool floating) {
    m_previewFloatButton->setText(floating ? QStringLiteral("Attach Preview")
                                           : QStringLiteral("Float Preview"));
    if (!floating) {
      m_previewDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
      m_previewDock->setTitleBarWidget(nullptr);
      m_previewBorderlessFullscreen = false;
      m_previewFullscreenButton->setText(QStringLiteral("Fullscreen Preview"));
    }
  });

  auto *togglePreviewFullscreenShortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
  togglePreviewFullscreenShortcut->setContext(Qt::ApplicationShortcut);
  connect(togglePreviewFullscreenShortcut,
          &QShortcut::activated,
          this,
          &MainWindow::togglePreviewFullscreen);

  auto *redockPreviewShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  redockPreviewShortcut->setContext(Qt::ApplicationShortcut);
  connect(redockPreviewShortcut, &QShortcut::activated, this, [this]() {
    if (m_previewDock == nullptr) {
      return;
    }
    QWidget *dockWindow = m_previewDock->window();
    const bool fullscreenPreview = m_previewBorderlessFullscreen ||
                                   (dockWindow != nullptr && m_previewDock->isFloating() &&
                                    dockWindow->isFullScreen());
    if (fullscreenPreview) {
      togglePreviewFullscreen();
    }
  });

  auto *nextPresetShortcut = new QShortcut(QKeySequence(Qt::Key_BracketRight), this);
  nextPresetShortcut->setContext(Qt::ApplicationShortcut);
  connect(nextPresetShortcut, &QShortcut::activated, this, &MainWindow::playNextPresetInBrowser);

  auto *nextPresetMediaShortcut = new QShortcut(QKeySequence(Qt::Key_MediaNext), this);
  nextPresetMediaShortcut->setContext(Qt::ApplicationShortcut);
  connect(nextPresetMediaShortcut, &QShortcut::activated, this, &MainWindow::playNextPresetInBrowser);
}

void MainWindow::wireSignals() {
  connect(m_presetSearchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
    const QRegularExpression filter(QRegularExpression::escape(text),
                                    QRegularExpression::CaseInsensitiveOption);
    m_presetProxyModel->setFilterRegularExpression(filter);
  });
  connect(m_favoritesOnlyCheck,
          &QCheckBox::toggled,
          m_presetProxyModel,
          &PresetFilterProxyModel::setFavoritesOnly);

  connect(m_presetTable, &QTableView::doubleClicked, this, [this](const QModelIndex &) {
    const QModelIndex idx = m_presetTable->currentIndex();
    if (!idx.isValid() || idx.column() != 0) {
      return;
    }
    loadSelectedPreset();
  });

  connect(m_playlistTable, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
    loadPlaylistRow(index.row());
  });

  connect(m_presetModel,
          &PresetLibraryModel::metadataChanged,
          this,
          [this](const QString &path, int rating, bool favorite, const QStringList &tags) {
            PresetMetadata metadata;
            metadata.rating = rating;
            metadata.favorite = favorite;
            metadata.tags = tags;
            if (!m_settingsManager->savePresetMetadata(path, metadata)) {
              setStatus(QStringLiteral("Failed to persist preset metadata."));
            }
            if (path == m_currentPresetPath) {
              QMetaObject::invokeMethod(
                  this,
                  [this, path]() { updateNowPlayingPanel(path); },
                  Qt::QueuedConnection);
            }
          });

  connect(m_projectMEngine, &ProjectMEngine::statusMessage, this, &MainWindow::onProjectMStatusMessage);
  connect(m_projectMEngine, &ProjectMEngine::presetChanged, this, &MainWindow::onPresetActivated);
}

void MainWindow::loadInitialState() {
  m_presetModel->applyMetadata(m_settingsManager->loadPresetMetadata());

  QSettings settings;
  const QString presetDir = settings.value(QStringLiteral("ui/presetDirectory"), defaultPresetDirectory()).toString();
  updatePresetDirectory(presetDir);

  refreshPlaylistNames();

  const QVariantMap projectMSettings = m_settingsManager->loadProjectMSettings();
  m_meshXSpin->setValue(projectMSettings.value(QStringLiteral("meshX"), 32).toInt());
  m_meshYSpin->setValue(projectMSettings.value(QStringLiteral("meshY"), 24).toInt());
  m_targetFpsSpin->setValue(projectMSettings.value(QStringLiteral("targetFps"), 60).toInt());
  m_beatSensitivitySpin->setValue(projectMSettings.value(QStringLiteral("beatSensitivity"), 1.0).toDouble());
  m_hardCutEnabledCheck->setChecked(projectMSettings.value(QStringLiteral("hardCutEnabled"), true).toBool());
  m_hardCutDurationSpin->setValue(projectMSettings.value(QStringLiteral("hardCutDuration"), 20).toInt());
  m_renderScaleSpin->setValue(projectMSettings.value(QStringLiteral("renderScalePercent"), 77).toInt());
  m_upscaleSharpnessSpin->setValue(projectMSettings.value(QStringLiteral("upscalerSharpness"), 0.2).toDouble());
  QString upscalerPreset = projectMSettings.value(QStringLiteral("upscalerPreset"), QStringLiteral("balanced"))
                               .toString()
                               .trimmed()
                               .toLower();
  if (m_upscalePresetCombo != nullptr) {
    const int explicitPresetIndex = m_upscalePresetCombo->findData(upscalerPreset);
    if (explicitPresetIndex < 0) {
      upscalerPreset = detectUpscalerPresetId(m_renderScaleSpin->value(), m_upscaleSharpnessSpin->value());
    }
    const int presetIndex = m_upscalePresetCombo->findData(upscalerPreset);
    if (presetIndex >= 0) {
      const QSignalBlocker blocker(m_upscalePresetCombo);
      m_upscalePresetCombo->setCurrentIndex(presetIndex);
    }
  }
  const QString gpuPreference = projectMSettings.value(QStringLiteral("gpuPreference"), QStringLiteral("dgpu"))
                                    .toString()
                                    .trimmed()
                                    .toLower();
  const int gpuPreferenceIndex = m_gpuPreferenceCombo->findData(gpuPreference);
  m_gpuPreferenceCombo->setCurrentIndex(gpuPreferenceIndex >= 0 ? gpuPreferenceIndex : 1);
  m_appliedGpuPreference = m_gpuPreferenceCombo->currentData().toString();
  m_preferredAudioDeviceId = projectMSettings.value(QStringLiteral("audioDeviceId")).toString().trimmed();

  applyProjectMSettingsFromUi();
  updateNowPlayingPanel(QString());
}

void MainWindow::refreshPlaylistNames() {
  const QString current = m_playlistPicker->currentText();
  m_playlistPicker->clear();
  m_playlistPicker->addItems(m_settingsManager->listPlaylists());

  const int idx = m_playlistPicker->findText(current);
  if (idx >= 0) {
    m_playlistPicker->setCurrentIndex(idx);
  }
}

void MainWindow::updatePresetDirectory(const QString &path) {
  m_presetDirectoryEdit->setText(path);
  m_presetModel->setPresetDirectory(path);
  m_presetModel->applyMetadata(m_settingsManager->loadPresetMetadata());

  m_projectMEngine->setPresetDirectory(path);

  QSettings settings;
  settings.setValue(QStringLiteral("ui/presetDirectory"), path);
}

QModelIndex MainWindow::selectedPresetSourceIndex() const {
  const QModelIndex proxyIndex = m_presetTable->currentIndex();
  if (!proxyIndex.isValid()) {
    return {};
  }
  return m_presetProxyModel->mapToSource(proxyIndex.siblingAtColumn(0));
}

bool MainWindow::loadPlaylistRow(int row) {
  const QVector<PlaylistItem> items = m_playlistModel->items();
  if (row < 0 || row >= items.size()) {
    return false;
  }

  const PlaylistItem &item = items.at(row);
  if (!m_projectMEngine->loadPreset(item.presetPath)) {
    return false;
  }

  m_playlistTable->selectRow(row);
  m_trackElapsed.restart();
  m_beatsSinceSwitch = 0;
  return true;
}

void MainWindow::choosePresetDirectory() {
  const QString selected = QFileDialog::getExistingDirectory(
      this,
      QStringLiteral("Choose preset directory"),
      m_presetDirectoryEdit->text().isEmpty() ? defaultPresetDirectory() : m_presetDirectoryEdit->text());

  if (selected.isEmpty()) {
    return;
  }

  updatePresetDirectory(selected);
}

void MainWindow::addSelectedPresetToPlaylist() {
  const QModelIndex sourceIndex = selectedPresetSourceIndex();
  if (!sourceIndex.isValid()) {
    setStatus(QStringLiteral("Select a preset first."));
    return;
  }

  const int row = sourceIndex.row();
  PlaylistItem item;
  item.presetName = m_presetModel->presetNameForRow(row);
  item.presetPath = m_presetModel->presetPathForRow(row);
  m_playlistModel->addItem(item);
}

void MainWindow::removeSelectedPlaylistItem() {
  const QModelIndex index = m_playlistTable->currentIndex();
  if (!index.isValid()) {
    return;
  }

  m_playlistModel->removeAt(index.row());
}

void MainWindow::movePlaylistItemUp() {
  const QModelIndex index = m_playlistTable->currentIndex();
  if (!index.isValid()) {
    return;
  }

  if (m_playlistModel->moveUp(index.row())) {
    m_playlistTable->selectRow(index.row() - 1);
  }
}

void MainWindow::movePlaylistItemDown() {
  const QModelIndex index = m_playlistTable->currentIndex();
  if (!index.isValid()) {
    return;
  }

  if (m_playlistModel->moveDown(index.row())) {
    m_playlistTable->selectRow(index.row() + 1);
  }
}

void MainWindow::loadSelectedPreset() {
  const QModelIndex sourceIndex = selectedPresetSourceIndex();
  if (!sourceIndex.isValid()) {
    setStatus(QStringLiteral("Select a preset first."));
    return;
  }

  const QString path = m_presetModel->presetPathForRow(sourceIndex.row());
  if (!m_projectMEngine->loadPreset(path)) {
    setStatus(QStringLiteral("Unable to load preset."));
  }
}

void MainWindow::playNextPresetInBrowser() {
  const int rowCount = m_presetProxyModel->rowCount();
  if (rowCount <= 0) {
    setStatus(QStringLiteral("No presets available."));
    return;
  }

  int currentRow = -1;
  const QModelIndex currentProxyIndex = m_presetTable->currentIndex();
  if (currentProxyIndex.isValid()) {
    currentRow = currentProxyIndex.row();
  } else if (!m_currentPresetPath.isEmpty()) {
    const int currentSourceRow = m_presetModel->rowForPresetPath(m_currentPresetPath);
    if (currentSourceRow >= 0) {
      const QModelIndex sourceIndex = m_presetModel->index(currentSourceRow, 0);
      const QModelIndex mappedIndex = m_presetProxyModel->mapFromSource(sourceIndex);
      if (mappedIndex.isValid()) {
        currentRow = mappedIndex.row();
      }
    }
  }

  const int nextRow = (currentRow + 1 + rowCount) % rowCount;
  const QModelIndex nextProxyIndex = m_presetProxyModel->index(nextRow, 0);
  if (!nextProxyIndex.isValid()) {
    setStatus(QStringLiteral("Failed to select next preset."));
    return;
  }

  m_presetTable->setCurrentIndex(nextProxyIndex);
  m_presetTable->selectRow(nextRow);
  m_presetTable->scrollTo(nextProxyIndex, QAbstractItemView::PositionAtCenter);

  const QModelIndex nextSourceIndex = m_presetProxyModel->mapToSource(nextProxyIndex);
  const QString presetPath = m_presetModel->presetPathForRow(nextSourceIndex.row());
  if (!m_projectMEngine->loadPreset(presetPath)) {
    setStatus(QStringLiteral("Unable to load next preset."));
  }
}

void MainWindow::savePlaylist() {
  QString playlistName = m_playlistNameEdit->text().trimmed();
  if (playlistName.isEmpty()) {
    playlistName = QInputDialog::getText(this, QStringLiteral("Save playlist"), QStringLiteral("Playlist name:"));
  }
  if (playlistName.trimmed().isEmpty()) {
    return;
  }

  if (!m_settingsManager->savePlaylist(playlistName, m_playlistModel->items())) {
    QMessageBox::warning(this, QStringLiteral("Save failed"), QStringLiteral("Could not save playlist."));
    return;
  }

  m_playlistNameEdit->setText(playlistName);
  refreshPlaylistNames();
  const int idx = m_playlistPicker->findText(playlistName);
  if (idx >= 0) {
    m_playlistPicker->setCurrentIndex(idx);
  }
  setStatus(QStringLiteral("Saved playlist '%1'.").arg(playlistName));
}

void MainWindow::loadPlaylist() {
  const QString playlistName = m_playlistPicker->currentText().trimmed();
  if (playlistName.isEmpty()) {
    return;
  }

  const QVector<PlaylistItem> items = m_settingsManager->loadPlaylist(playlistName);
  m_playlistModel->replaceItems(items);
  m_playlistNameEdit->setText(playlistName);
  setStatus(QStringLiteral("Loaded playlist '%1'.").arg(playlistName));
}

void MainWindow::importPlaylist() {
  const QString filePath = QFileDialog::getOpenFileName(
      this,
      QStringLiteral("Import playlist"),
      QString(),
      QStringLiteral("JSON files (*.json);;All files (*)"));
  if (filePath.isEmpty()) {
    return;
  }

  bool ok = false;
  QString error;
  QString playlistName;
  const QVector<PlaylistItem> items =
      m_settingsManager->importPlaylistFromFile(filePath, &playlistName, &ok, &error);
  if (!ok) {
    QMessageBox::warning(this,
                         QStringLiteral("Import failed"),
                         QStringLiteral("Could not import playlist:\n%1").arg(error));
    return;
  }

  if (playlistName.trimmed().isEmpty()) {
    playlistName = playlistFallbackName(filePath);
  }

  m_playlistModel->replaceItems(items);
  m_playlistNameEdit->setText(playlistName);
  setStatus(QStringLiteral("Imported playlist '%1'.").arg(playlistName));
}

void MainWindow::exportPlaylist() {
  QString playlistName = m_playlistNameEdit->text().trimmed();
  if (playlistName.isEmpty()) {
    playlistName = QStringLiteral("playlist");
  }

  const QString filePath = QFileDialog::getSaveFileName(
      this,
      QStringLiteral("Export playlist"),
      playlistName + QStringLiteral(".json"),
      QStringLiteral("JSON files (*.json);;All files (*)"));
  if (filePath.isEmpty()) {
    return;
  }

  if (!m_settingsManager->exportPlaylistToFile(filePath, playlistName, m_playlistModel->items())) {
    QMessageBox::warning(this, QStringLiteral("Export failed"), QStringLiteral("Could not export playlist."));
    return;
  }

  setStatus(QStringLiteral("Exported playlist to %1").arg(filePath));
}

void MainWindow::importPresetMetadata() {
  const QString filePath = QFileDialog::getOpenFileName(
      this,
      QStringLiteral("Import preset metadata"),
      QString(),
      QStringLiteral("JSON files (*.json);;All files (*)"));
  if (filePath.isEmpty()) {
    return;
  }

  bool ok = false;
  QString error;
  const QHash<QString, PresetMetadata> metadata =
      m_settingsManager->importPresetMetadata(filePath, &ok, &error);
  if (!ok) {
    QMessageBox::warning(this,
                         QStringLiteral("Import failed"),
                         QStringLiteral("Could not import metadata:\n%1").arg(error));
    return;
  }

  m_presetModel->applyMetadata(metadata);
  if (!m_settingsManager->savePresetMetadataMap(m_presetModel->metadataMap())) {
    QMessageBox::warning(this,
                         QStringLiteral("Save failed"),
                         QStringLiteral("Imported metadata could not be persisted."));
  }

  setStatus(QStringLiteral("Imported preset metadata from %1").arg(filePath));
}

void MainWindow::exportPresetMetadata() {
  const QString filePath = QFileDialog::getSaveFileName(
      this,
      QStringLiteral("Export preset metadata"),
      QStringLiteral("preset-metadata.json"),
      QStringLiteral("JSON files (*.json);;All files (*)"));
  if (filePath.isEmpty()) {
    return;
  }

  if (!m_settingsManager->exportPresetMetadata(filePath, m_presetModel->metadataMap())) {
    QMessageBox::warning(this, QStringLiteral("Export failed"), QStringLiteral("Could not export metadata."));
    return;
  }

  setStatus(QStringLiteral("Exported preset metadata to %1").arg(filePath));
}

void MainWindow::applyProjectMSettingsFromUi() {
  QString gpuPreference = m_gpuPreferenceCombo != nullptr
                              ? m_gpuPreferenceCombo->currentData().toString().trimmed().toLower()
                              : QStringLiteral("dgpu");
  if (gpuPreference.isEmpty()) {
    gpuPreference = QStringLiteral("dgpu");
  }
  QString upscalerPreset = m_upscalePresetCombo != nullptr
                               ? m_upscalePresetCombo->currentData().toString().trimmed().toLower()
                               : QStringLiteral("balanced");
  if (upscalerPreset.isEmpty()) {
    upscalerPreset = QStringLiteral("balanced");
  }

  QVariantMap map;
  map.insert(QStringLiteral("meshX"), m_meshXSpin->value());
  map.insert(QStringLiteral("meshY"), m_meshYSpin->value());
  map.insert(QStringLiteral("targetFps"), m_targetFpsSpin->value());
  map.insert(QStringLiteral("beatSensitivity"), m_beatSensitivitySpin->value());
  map.insert(QStringLiteral("hardCutEnabled"), m_hardCutEnabledCheck->isChecked());
  map.insert(QStringLiteral("hardCutDuration"), m_hardCutDurationSpin->value());
  map.insert(QStringLiteral("upscalerPreset"), upscalerPreset);
  map.insert(QStringLiteral("renderScalePercent"), m_renderScaleSpin->value());
  map.insert(QStringLiteral("upscalerSharpness"), m_upscaleSharpnessSpin->value());
  map.insert(QStringLiteral("gpuPreference"), gpuPreference);
  map.insert(QStringLiteral("audioDeviceId"), m_preferredAudioDeviceId);

  if (m_visualizerWidget != nullptr) {
    m_visualizerWidget->setRenderScalePercent(m_renderScaleSpin->value());
    m_visualizerWidget->setUpscaleSharpness(m_upscaleSharpnessSpin->value());
  }

  const bool gpuPreferenceChanged = (m_appliedGpuPreference != gpuPreference);
  m_appliedGpuPreference = gpuPreference;
  m_settingsManager->saveProjectMSettings(map);
  m_projectMEngine->applySettings(map);
  if (gpuPreferenceChanged) {
    setStatus(QStringLiteral("Saved GPU preference. Restart app to apply renderer device change."));
  }
}

void MainWindow::togglePlaylistPlayback() {
  if (m_playlistModel->rowCount() == 0) {
    setStatus(QStringLiteral("Playlist is empty."));
    return;
  }

  m_playlistPlaying = !m_playlistPlaying;
  if (!m_playlistPlaying) {
    m_playPauseButton->setText(QStringLiteral("Play"));
    m_playbackTimer->stop();
    return;
  }

  m_playPauseButton->setText(QStringLiteral("Pause"));

  const QVector<PlaylistItem> items = m_playlistModel->items();
  int targetRow = m_playlistTable->currentIndex().isValid() ? m_playlistTable->currentIndex().row() : 0;
  if (targetRow < 0 || targetRow >= items.size()) {
    targetRow = 0;
  }

  bool needsLoad = m_currentPresetPath.isEmpty();
  if (!needsLoad && targetRow >= 0 && targetRow < items.size()) {
    needsLoad = items.at(targetRow).presetPath != m_currentPresetPath;
  }

  if (needsLoad && !loadPlaylistRow(targetRow)) {
    setStatus(QStringLiteral("Failed to load selected playlist preset."));
    m_playlistPlaying = false;
    m_playPauseButton->setText(QStringLiteral("Play"));
    return;
  }

  if (!m_trackElapsed.isValid()) {
    m_trackElapsed.start();
  }
  m_playbackTimer->start();
}

void MainWindow::playNextPlaylistItem() {
  const int rows = m_playlistModel->rowCount();
  if (rows == 0) {
    return;
  }

  const int current = m_playlistTable->currentIndex().isValid() ? m_playlistTable->currentIndex().row() : -1;
  int next = 0;

  if (m_shuffleCheck->isChecked() && rows > 1) {
    do {
      next = QRandomGenerator::global()->bounded(rows);
    } while (next == current);
  } else {
    next = (current + 1 + rows) % rows;
  }

  loadPlaylistRow(next);
}

void MainWindow::playPreviousPlaylistItem() {
  const int rows = m_playlistModel->rowCount();
  if (rows == 0) {
    return;
  }

  const int current = m_playlistTable->currentIndex().isValid() ? m_playlistTable->currentIndex().row() : 0;
  const int prev = (current - 1 + rows) % rows;
  loadPlaylistRow(prev);
}

void MainWindow::onPlaybackTimerTick() {
  if (!m_playlistPlaying) {
    return;
  }

  if (m_autoAdvanceModeCombo->currentIndex() == 1 && m_trackElapsed.isValid()) {
    const qint64 maxMs = static_cast<qint64>(m_autoDurationSecondsSpin->value()) * 1000;
    if (m_trackElapsed.elapsed() >= maxMs) {
      playNextPlaylistItem();
    }
  }
}

void MainWindow::onAudioFrameForPlayback(const QVector<float> &monoFrame) {
  if (!m_playlistPlaying || m_autoAdvanceModeCombo->currentIndex() != 2 || monoFrame.isEmpty()) {
    return;
  }

  const int sampleCount = qMin(monoFrame.size(), 1024);
  float energy = 0.0f;
  for (int i = 0; i < sampleCount; ++i) {
    energy += qAbs(monoFrame.at(i));
  }
  energy /= static_cast<float>(sampleCount);

  const float threshold = static_cast<float>(m_autoBeatThresholdSpin->value());
  const bool high = energy >= threshold;
  if (high && !m_lastBeatHigh) {
    ++m_beatsSinceSwitch;
    if (m_beatsSinceSwitch >= m_autoBeatCountSpin->value()) {
      playNextPlaylistItem();
    }
  }

  if (high) {
    m_lastBeatHigh = true;
  } else if (energy < threshold * 0.6f) {
    m_lastBeatHigh = false;
  }
}

void MainWindow::onPresetActivated(const QString &presetPath) {
  m_currentPresetPath = presetPath;
  if (m_visualizerWidget != nullptr) {
    m_visualizerWidget->showPresetOverlay(presetPath);
  }
  updateNowPlayingPanel(presetPath);
}

void MainWindow::applyNowPlayingMetadata() {
  if (m_syncingNowPlayingUi || m_currentPresetPath.isEmpty()) {
    return;
  }

  QStringList tags;
  for (const QString &piece : m_nowPlayingTagsEdit->text().split(',', Qt::SkipEmptyParts)) {
    const QString cleaned = piece.trimmed();
    if (!cleaned.isEmpty()) {
      tags.push_back(cleaned);
    }
  }
  tags.removeDuplicates();

  PresetMetadata metadata;
  metadata.rating = m_nowPlayingRatingSpin->value();
  metadata.favorite = m_nowPlayingFavoriteCheck->isChecked();
  metadata.tags = tags;

  if (m_presetModel->rowForPresetPath(m_currentPresetPath) >= 0) {
    if (!m_presetModel->updateMetadataForPath(m_currentPresetPath, metadata)) {
      setStatus(QStringLiteral("Failed to update metadata for now playing preset."));
    }
    return;
  }

  if (!m_settingsManager->savePresetMetadata(m_currentPresetPath, metadata)) {
    setStatus(QStringLiteral("Failed to persist now playing metadata."));
    return;
  }

  setStatus(QStringLiteral("Updated metadata for now playing preset."));
}

void MainWindow::togglePreviewFloating() {
  if (m_previewDock == nullptr) {
    return;
  }

  const bool currentlyFloating = m_previewDock->isFloating();
  if (currentlyFloating) {
    QWidget *dockWindow = m_previewDock->window();
    if (dockWindow != nullptr && dockWindow->isFullScreen()) {
      dockWindow->showNormal();
    }
    if (dockWindow != nullptr && m_previewBorderlessFullscreen) {
      dockWindow->setWindowFlag(Qt::FramelessWindowHint, false);
      dockWindow->show();
      m_previewBorderlessFullscreen = false;
    }
    m_previewDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_previewDock->setTitleBarWidget(nullptr);
    m_previewFullscreenButton->setText(QStringLiteral("Fullscreen Preview"));
    QTimer::singleShot(0, this, [this]() {
      if (m_previewDock != nullptr) {
        addDockWidget(Qt::RightDockWidgetArea, m_previewDock);
        m_previewDock->setFloating(false);
        resizeDocks({m_previewDock}, {520}, Qt::Horizontal);
      }
    });
    return;
  }

  m_previewDock->setFloating(true);
  m_previewBorderlessFullscreen = false;
  m_previewDock->resize(960, 540);
  m_previewDock->show();
  m_previewDock->raise();
}

void MainWindow::togglePreviewFullscreen() {
  if (m_previewDock == nullptr) {
    return;
  }

  if (!m_previewDock->isFloating()) {
    m_previewDock->setFloating(true);
    m_previewDock->resize(960, 540);
    QTimer::singleShot(0, this, [this]() {
      if (m_previewDock != nullptr && m_previewDock->isFloating()) {
        togglePreviewFullscreen();
      }
    });
    return;
  }

  QWidget *dockWindow = m_previewDock->window();
  if (dockWindow == nullptr) {
    return;
  }

  if (dockWindow->isFullScreen() || m_previewBorderlessFullscreen) {
    if (m_previewBorderlessFullscreen) {
      dockWindow->setWindowFlag(Qt::FramelessWindowHint, false);
      m_previewBorderlessFullscreen = false;
      dockWindow->show();
    }
    m_previewDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_previewDock->setTitleBarWidget(nullptr);
    m_previewFullscreenButton->setText(QStringLiteral("Fullscreen Preview"));
    QTimer::singleShot(0, this, [this]() {
      if (m_previewDock != nullptr) {
        addDockWidget(Qt::RightDockWidgetArea, m_previewDock);
        m_previewDock->setFloating(false);
        resizeDocks({m_previewDock}, {520}, Qt::Horizontal);
      }
    });
    return;
  }

  m_previewDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
  if (m_previewHiddenTitleBar != nullptr) {
    m_previewDock->setTitleBarWidget(m_previewHiddenTitleBar);
  }
  dockWindow->setWindowFlag(Qt::Tool, false);
  dockWindow->setWindowFlag(Qt::Window, true);
  dockWindow->setWindowFlag(Qt::FramelessWindowHint, true);
  m_previewBorderlessFullscreen = true;
  dockWindow->showNormal();
  dockWindow->showFullScreen();
  dockWindow->raise();
  m_previewFullscreenButton->setText(QStringLiteral("Exit Fullscreen"));
}

void MainWindow::bindAudioSource(AudioSource *audioSource) {
  if (audioSource == nullptr) {
    return;
  }

  m_audioSource = audioSource;
  m_audioSource->setSelectedDeviceId(m_preferredAudioDeviceId);
  connect(m_audioSource, &AudioSource::pcmFrameReady, m_projectMEngine, &ProjectMEngine::submitAudioFrame);
  connect(m_audioSource, &AudioSource::pcmFrameReady, this, &MainWindow::onAudioFrameForPlayback);
  connect(m_audioSource, &AudioSource::statusMessage, this, &MainWindow::setStatus);
  connect(m_audioSource, &AudioSource::errorMessage, this, &MainWindow::onAudioSourceError);
}

void MainWindow::replaceAudioSource(AudioSource *audioSource) {
  if (audioSource == nullptr) {
    return;
  }

  if (m_audioSource != nullptr) {
    disconnect(m_audioSource, nullptr, this, nullptr);
    disconnect(m_audioSource, nullptr, m_projectMEngine, nullptr);
    m_audioSource->stop();
    m_audioSource->deleteLater();
  }

  bindAudioSource(audioSource);
  updateAudioBackendIndicator();
  refreshAudioDeviceList();
}

void MainWindow::updateAudioDeviceDebugPanel(const QVector<AudioDeviceInfo> &devices) {
  if (m_audioDeviceDebugText == nullptr) {
    return;
  }

  QStringList lines;
  const QString backend = (m_audioSource != nullptr) ? m_audioSource->backendName() : QStringLiteral("None");
  const QString selectedId =
      m_preferredAudioDeviceId.isEmpty() ? QStringLiteral("<default>") : m_preferredAudioDeviceId;
  lines << QStringLiteral("Backend: %1").arg(backend);
  lines << QStringLiteral("Selected device id: %1").arg(selectedId);
  if (m_upscalePresetCombo != nullptr) {
    lines << QStringLiteral("Upscaler preset: %1").arg(m_upscalePresetCombo->currentText());
  }
  lines << QStringLiteral("Render scale: %1%").arg(m_renderScaleSpin != nullptr ? m_renderScaleSpin->value() : 100);
  if (m_gpuPreferenceCombo != nullptr) {
    lines << QStringLiteral("GPU preference: %1").arg(m_gpuPreferenceCombo->currentText());
  }
  const QString activeDriPrime = qEnvironmentVariable("DRI_PRIME");
  lines << QStringLiteral("DRI_PRIME (current process): %1")
               .arg(activeDriPrime.isEmpty() ? QStringLiteral("<unset>") : activeDriPrime);
  lines << QStringLiteral("Discovered devices: %1").arg(devices.size());
  lines << QString();

  if (devices.isEmpty()) {
    lines << QStringLiteral("(No explicit devices reported; backend default will be used.)");
  } else {
    for (int i = 0; i < devices.size(); ++i) {
      const AudioDeviceInfo &device = devices.at(i);
      const QString name = device.name.isEmpty() ? QStringLiteral("<unnamed>") : device.name;
      const QString id = device.id.isEmpty() ? QStringLiteral("<none>") : device.id;
      lines << QStringLiteral("%1. %2").arg(i + 1).arg(name);
      lines << QStringLiteral("   id: %1").arg(id);
      if (!device.description.isEmpty()) {
        lines << QStringLiteral("   detail: %1").arg(device.description);
      }
    }
  }

  m_audioDeviceDebugText->setPlainText(lines.join(QLatin1Char('\n')));
}

bool MainWindow::startCurrentAudioSourceWithFallback() {
  if (m_audioSource == nullptr) {
    return false;
  }

  if (m_audioSource->start()) {
    m_audioFallbackApplied = false;
    updateAudioBackendIndicator();
    return true;
  }

  setStatus(QStringLiteral("PipeWire unavailable, falling back to dummy audio backend."));
  replaceAudioSource(new DummyAudioSource(this));
  if (m_audioSource != nullptr && m_audioSource->start()) {
    m_audioFallbackApplied = true;
    updateAudioBackendIndicator();
    return true;
  }

  updateAudioBackendIndicator();
  return false;
}

void MainWindow::refreshAudioDeviceList() {
  if (m_audioDeviceCombo == nullptr) {
    return;
  }

  QVector<AudioDeviceInfo> devices;
  m_syncingAudioDeviceUi = true;
  const QSignalBlocker blocker(m_audioDeviceCombo);
  m_audioDeviceCombo->clear();
  m_audioDeviceCombo->addItem(QStringLiteral("Default"), QString());

  if (m_audioSource != nullptr) {
    devices = m_audioSource->availableDevices();
    for (const AudioDeviceInfo &device : devices) {
      const QString name = device.name.isEmpty() ? QStringLiteral("Unnamed Device") : device.name;
      m_audioDeviceCombo->addItem(name, device.id);
      if (!device.description.isEmpty()) {
        const int idx = m_audioDeviceCombo->count() - 1;
        m_audioDeviceCombo->setItemData(idx, device.description, Qt::ToolTipRole);
      }
    }
  }

  int selected = m_audioDeviceCombo->findData(m_preferredAudioDeviceId);
  if (selected < 0) {
    selected = 0;
  }
  m_audioDeviceCombo->setCurrentIndex(selected);
  m_audioDeviceCombo->setEnabled(m_audioSource != nullptr);
  if (m_refreshAudioDevicesButton != nullptr) {
    m_refreshAudioDevicesButton->setEnabled(m_audioSource != nullptr);
  }
  m_syncingAudioDeviceUi = false;
  updateAudioDeviceDebugPanel(devices);
}

void MainWindow::applySelectedAudioDevice() {
  if (m_audioDeviceCombo == nullptr) {
    return;
  }

  const QString selectedDeviceId = m_audioDeviceCombo->currentData().toString().trimmed();
  if (selectedDeviceId == m_preferredAudioDeviceId) {
    return;
  }
  m_preferredAudioDeviceId = selectedDeviceId;

  QVariantMap settings = m_settingsManager->loadProjectMSettings();
  settings.insert(QStringLiteral("audioDeviceId"), m_preferredAudioDeviceId);
  m_settingsManager->saveProjectMSettings(settings);

  if (m_audioSource == nullptr) {
    return;
  }

  replaceAudioSource(createAudioSource(this));
  if (!startCurrentAudioSourceWithFallback()) {
    setStatus(QStringLiteral("Failed to apply audio device; backend restart failed."));
    return;
  }

  if (m_audioSource != nullptr && m_audioSource->backendName() == QStringLiteral("PipeWire")) {
    setStatus(QStringLiteral("Applied audio input device: %1").arg(m_audioDeviceCombo->currentText()));
  } else {
    setStatus(QStringLiteral("Saved audio device preference (PipeWire backend not active in this build)."));
  }
}

void MainWindow::updateAudioBackendIndicator() {
  if (m_audioBackendLabel == nullptr) {
    return;
  }

  if (m_audioSource == nullptr) {
    m_audioBackendLabel->setText(QStringLiteral("Audio: unavailable"));
    return;
  }

  const QString state = m_audioSource->isRunning() ? QStringLiteral("running") : QStringLiteral("stopped");
  m_audioBackendLabel->setText(
      QStringLiteral("Audio: %1 (%2)").arg(m_audioSource->backendName(), state));
}

void MainWindow::updateRenderBackendIndicator() {
  if (m_renderBackendLabel == nullptr || m_projectMEngine == nullptr) {
    return;
  }
  m_renderBackendLabel->setText(
      m_projectMEngine->hasProjectMBackend() ? QStringLiteral("Render: projectM")
                                             : QStringLiteral("Render: fallback"));
}

void MainWindow::onAudioSourceError(const QString &message) {
  setStatus(message);
  updateAudioBackendIndicator();

  if (m_audioFallbackApplied || m_audioSource == nullptr) {
    return;
  }

  if (m_audioSource->backendName() != QStringLiteral("PipeWire") || m_audioSource->isRunning()) {
    return;
  }

  m_audioFallbackApplied = true;
  replaceAudioSource(new DummyAudioSource(this));
  if (m_audioSource != nullptr && m_audioSource->start()) {
    setStatus(QStringLiteral("PipeWire failed; switched to dummy audio backend."));
  } else {
    setStatus(QStringLiteral("Audio backend failed and dummy fallback could not start."));
  }
  updateAudioBackendIndicator();
  refreshAudioDeviceList();
}

void MainWindow::onProjectMStatusMessage(const QString &message) {
  setStatus(message);
  updateRenderBackendIndicator();
}

PresetMetadata MainWindow::currentNowPlayingMetadata() const {
  if (m_currentPresetPath.isEmpty()) {
    return {};
  }

  const int row = m_presetModel->rowForPresetPath(m_currentPresetPath);
  if (row >= 0) {
    return m_presetModel->presetMetadataForRow(row);
  }

  const QHash<QString, PresetMetadata> metadata = m_settingsManager->loadPresetMetadata();
  return metadata.value(m_currentPresetPath);
}

void MainWindow::updateNowPlayingPanel(const QString &presetPath) {
  m_syncingNowPlayingUi = true;
  const QSignalBlocker ratingBlocker(m_nowPlayingRatingSpin);
  const QSignalBlocker favoriteBlocker(m_nowPlayingFavoriteCheck);
  const QSignalBlocker tagsBlocker(m_nowPlayingTagsEdit);

  const bool hasPreset = !presetPath.isEmpty();
  m_nowPlayingRatingSpin->setEnabled(hasPreset);
  m_nowPlayingFavoriteCheck->setEnabled(hasPreset);
  m_nowPlayingTagsEdit->setEnabled(hasPreset);

  if (!hasPreset) {
    m_nowPlayingNameLabel->setText(QStringLiteral("None"));
    m_nowPlayingPathLabel->setText(QStringLiteral("-"));
    m_nowPlayingRatingSpin->setValue(3);
    m_nowPlayingFavoriteCheck->setChecked(false);
    m_nowPlayingTagsEdit->clear();
    m_syncingNowPlayingUi = false;
    return;
  }

  m_nowPlayingNameLabel->setText(QFileInfo(presetPath).completeBaseName());
  m_nowPlayingPathLabel->setText(presetPath);

  const PresetMetadata metadata = currentNowPlayingMetadata();
  m_nowPlayingRatingSpin->setValue(qBound(1, metadata.rating, 5));
  m_nowPlayingFavoriteCheck->setChecked(metadata.favorite);
  m_nowPlayingTagsEdit->setText(metadata.tags.join(QStringLiteral(", ")));
  m_syncingNowPlayingUi = false;
}

void MainWindow::setStatus(const QString &message) {
  statusBar()->showMessage(message, 5000);
  qInfo().noquote() << "[qt6mplayer]" << message;
}
