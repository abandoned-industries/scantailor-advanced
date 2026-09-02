// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "MainWindow.h"

#include <core/ApplicationSettings.h>
#include <core/IconProvider.h>

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QShortcut>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QTextStream>
#include <QDateTime>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QCryptographicHash>
#include <QLabel>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProgressDialog>
#include <QResource>
#include <QSaveFile>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <cstdlib>
#include <set>
#include <QtWidgets/QInputDialog>
#include <boost/lambda/lambda.hpp>
#include <memory>

#include "AbstractCacheDrivenOutputTask.h"
#include "AbstractOutputTask.h"
#include "AbstractRelinker.h"
#include "Application.h"
#include "AutoAccept.h"
#include "BenchmarkLog.h"
#include "TempOutputCleanup.h"
#include "ProjectFolder.h"
#include "ProjectFolderRelinker.h"
#include "AutoRemovingFile.h"
#include "BasicImageView.h"
#include "ContentBoxPropagator.h"
#include "DebugImageView.h"
#include "DebugImages.h"
#include "DefaultParamsDialog.h"
#include "DurationFormatter.h"
#include "AutoProcessDialog.h"
#include "filters/finalize/AutoColorDecisionReset.h"
#include "filters/finalize/ThumbnailColorModeFilter.h"
#include "ErrorWidget.h"
#include "FilterOptionsWidget.h"
#include "FixDpiDialog.h"
#include "ImageInfo.h"
#include "ImageLoader.h"
#include "ImageMetadataLoader.h"
#include "LoadFileTask.h"
#include "LoadFilesStatusDialog.h"
#include "NewOpenProjectPanel.h"
#include "OutOfMemoryDialog.h"
#include "OutOfMemoryHandler.h"
#include "PageOrientationPropagator.h"
#include "PageSelectionAccessor.h"
#include "PageSequence.h"
#include "ProcessingIndicationWidget.h"
#include "ProcessingTaskQueue.h"
#include "ProjectCreationContext.h"
#include "ProjectOpeningContext.h"
#include "ProjectPages.h"
#include "ApplicationSettings.h"
#include "BookMetadata.h"
#include "PdfExporter.h"
#include "ZoteroClient.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "RecentProjects.h"
#include "RelinkingDialog.h"
#include "ScopedIncDec.h"
#include "SettingsDialog.h"
#include "PdfImportDialog.h"
#include "PdfReadError.h"
#include "PdfReader.h"
#include "SkinnedButton.h"
#include "SmartFilenameOrdering.h"
#include "StageSequence.h"
#include "SystemLoadWidget.h"
#include "TabbedDebugImages.h"
#include "ThumbnailFactory.h"
#include "UnitsProvider.h"
#include "Utils.h"
#include "WorkerThreadPool.h"
#include "ZoteroPluginInstaller.h"
#include "config.h"
#include "filters/deskew/CacheDrivenTask.h"
#include "filters/deskew/Dependencies.h"
#include "filters/deskew/Filter.h"
#include "filters/deskew/Params.h"
#include "filters/deskew/Settings.h"
#include "filters/deskew/Task.h"
#include "filters/fix_orientation/CacheDrivenTask.h"
#include "filters/fix_orientation/Task.h"
#include "filters/output/CacheDrivenTask.h"
#include "filters/output/ColorParams.h"
#include "filters/output/Filter.h"
#include "filters/output/OutputProcessingParams.h"
#include "filters/output/Settings.h"
#include "filters/output/TabbedImageView.h"
#include "filters/output/Task.h"
#include "filters/export/CacheDrivenTask.h"
#include "filters/export/Filter.h"
#include "filters/export/OptionsWidget.h"
#include "filters/export/Settings.h"
#include "filters/export/Task.h"
#include "filters/ocr/CacheDrivenTask.h"
#include "filters/ocr/Filter.h"
#include "filters/ocr/OcrResult.h"
#include "filters/ocr/Settings.h"
#include "filters/ocr/Task.h"
#include "filters/finalize/CacheDrivenTask.h"
#include "filters/finalize/Settings.h"
#include "filters/finalize/Task.h"
#include "filters/page_layout/CacheDrivenTask.h"
#include "filters/page_layout/Settings.h"
#include "filters/page_layout/Task.h"
#include "filters/page_split/CacheDrivenTask.h"
#include "filters/page_split/Filter.h"
#include "filters/page_split/LayoutType.h"
#include "filters/page_split/PageLayout.h"
#include "filters/page_split/Params.h"
#include "filters/page_split/Settings.h"
#include "filters/page_split/Task.h"
#include "BatchProcessingSummaryDialog.h"
#include "ContentCoverageSummaryDialog.h"
#include "PageBoxSummaryDialog.h"
#include "PageSizeWarningDialog.h"
#include "filters/page_box/CacheDrivenTask.h"
#include "filters/page_box/Settings.h"
#include "filters/page_box/Task.h"
#include "filters/select_content/Settings.h"
#include "filters/page_layout/Settings.h"
#include "filters/select_content/Params.h"
#include "filters/select_content/CacheDrivenTask.h"
#include "filters/select_content/Task.h"
#include "ui_AboutDialog.h"
#include "ui_BatchProcessingLowerPanel.h"
#include "ui_RemovePagesDialog.h"
#include "version.h"

using namespace core;


class MainWindow::PageSelectionProviderImpl : public PageSelectionProvider {
 public:
  explicit PageSelectionProviderImpl(MainWindow* wnd) : m_wnd(wnd) {}

  PageSequence allPages() const override { return m_wnd ? m_wnd->allPages() : PageSequence(); }

  std::set<PageId> selectedPages() const override { return m_wnd ? m_wnd->selectedPages() : std::set<PageId>(); }

  std::vector<PageRange> selectedRanges() const override {
    return m_wnd ? m_wnd->selectedRanges() : std::vector<PageRange>();
  }

 private:
  QPointer<MainWindow> m_wnd;
};


MainWindow::MainWindow(bool restoreGeometry)
    : m_pages(std::make_shared<ProjectPages>()),
      m_stages(std::make_shared<StageSequence>(m_pages, newPageSelectionAccessor())),
      m_workerThreadPool(std::make_unique<WorkerThreadPool>()),
      m_interactiveQueue(std::make_unique<ProcessingTaskQueue>()),
      m_outOfMemoryDialog(std::make_unique<OutOfMemoryDialog>()),
      m_curFilter(0),
      m_ignoreSelectionChanges(0),
      m_ignorePageOrderingChanges(0),
      m_restoreGeometry(restoreGeometry),
      m_debug(false),
      m_closing(false),
      m_closeEventPending(false),
      m_quitting(false),
      m_twoPassBatchInProgress(false),
      m_twoPassTargetFilter(-1) {
  ApplicationSettings& settings = ApplicationSettings::getInstance();

  m_maxLogicalThumbSize = settings.getMaxLogicalThumbnailSize();
  const ThumbnailSequence::ViewMode viewMode = settings.isSingleColumnThumbnailDisplayEnabled()
                                                   ? ThumbnailSequence::SINGLE_COLUMN
                                                   : ThumbnailSequence::MULTI_COLUMN;
  m_thumbSequence = std::make_unique<ThumbnailSequence>(m_maxLogicalThumbSize, viewMode);
  m_batchSummaries = std::make_unique<BatchSummaries>(static_cast<BatchSummariesContext&>(*this), this);
  m_pdfExportFlow = std::make_unique<PdfExportFlow>(static_cast<PdfExportFlowContext&>(*this), this);

  m_autoSaveTimer.setSingleShot(true);
  connect(&m_autoSaveTimer, SIGNAL(timeout()), SLOT(autoSaveProject()));

  setupUi(this);
  setupIcons();
  m_filterDockBaseMinimumWidth = filterDockWidget->minimumWidth();
  scrollArea->viewport()->installEventFilter(this);

  sortOptions->setVisible(false);

  createBatchProcessingWidget();
  m_processingIndicationWidget = std::make_unique<ProcessingIndicationWidget>();

  filterList->setStages(m_stages);
  filterList->selectRow(0);

  setupThumbView();  // Expects m_thumbSequence to be initialized.
  m_tabbedDebugImages = std::make_unique<TabbedDebugImages>();

  m_debug = actionDebug->isChecked();

  m_imageFrameLayout = new QStackedLayout(imageViewFrame);
  m_imageFrameLayout->setStackingMode(QStackedLayout::StackAll);

  m_optionsFrameLayout = new QStackedLayout(filterOptions);

  m_timingStatusLabel = new QLabel;
  m_timingStatusLabel->setObjectName(QStringLiteral("timingStatusLabel"));
  QMainWindow::statusBar()->addPermanentWidget(m_timingStatusLabel, 1);

  m_statusBarPanel = new StatusBarPanel;
  QMainWindow::statusBar()->addPermanentWidget(m_statusBarPanel);
  connect(m_thumbSequence.get(), &ThumbnailSequence::newSelectionLeader, [this](const PageInfo& pageInfo) {
    PageSequence pageSequence = m_thumbSequence->toPageSequence();
    if (pageSequence.numPages() > 0) {
      m_statusBarPanel->updatePage(pageSequence.pageNo(pageInfo.id()) + 1, pageSequence.numPages(), pageInfo.id());
    } else {
      m_statusBarPanel->clear();
    }
  });

  m_unitsMenuActionGroup = new QActionGroup(this);
  for (QAction* action : menuUnits->actions()) {
    m_unitsMenuActionGroup->addAction(action);
  }
  switch (unitsFromString(settings.getUnits())) {
    case PIXELS:
      actionPixels->setChecked(true);
      break;
    case MILLIMETRES:
      actionMilimeters->setChecked(true);
      break;
    case CENTIMETRES:
      actionCentimetres->setChecked(true);
      break;
    case INCHES:
      actionInches->setChecked(true);
      break;
  }
  connect(actionPixels, &QAction::toggled, [this, &settings](bool checked) {
    if (checked) {
      UnitsProvider::getInstance().setUnits(PIXELS);
      settings.setUnits(unitsToString(PIXELS));
    }
  });
  connect(actionMilimeters, &QAction::toggled, [this, &settings](bool checked) {
    if (checked) {
      UnitsProvider::getInstance().setUnits(MILLIMETRES);
      settings.setUnits(unitsToString(MILLIMETRES));
    }
  });
  connect(actionCentimetres, &QAction::toggled, [this, &settings](bool checked) {
    if (checked) {
      UnitsProvider::getInstance().setUnits(CENTIMETRES);
      settings.setUnits(unitsToString(CENTIMETRES));
    }
  });
  connect(actionInches, &QAction::toggled, [this, &settings](bool checked) {
    if (checked) {
      UnitsProvider::getInstance().setUnits(INCHES);
      settings.setUnits(unitsToString(INCHES));
    }
  });

  thumbColumnViewBtn->setChecked(settings.isSingleColumnThumbnailDisplayEnabled());
  deviationHighlightingBtn->setChecked(settings.isHighlightDeviationEnabled());

  addAction(actionFirstPage);
  addAction(actionLastPage);
  addAction(actionNextPage);
  addAction(actionPrevPage);
  addAction(actionPrevPageQ);
  addAction(actionNextPageW);
  addAction(actionPrevPageLeft);
  addAction(actionNextPageRight);
  addAction(actionNextSelectedPage);
  addAction(actionPrevSelectedPage);
  addAction(actionNextSelectedPageW);
  addAction(actionPrevSelectedPageQ);
  addAction(actionGotoPage);
  addAction(actionMagnifyThumbnails);
  addAction(actionDiminishThumbnails);
  addAction(actionReloadPage);
  addAction(actionRemovePages);
  addAction(actionSelectAll);

  addAction(actionSwitchFilter1);
  addAction(actionSwitchFilter2);
  addAction(actionSwitchFilter3);
  addAction(actionSwitchFilter4);
  addAction(actionSwitchFilter5);
  addAction(actionSwitchFilter6);
  // Should be enough to save a project.
  OutOfMemoryHandler::instance().allocateEmergencyMemory(3 * 1024 * 1024);

  connect(actionFirstPage, SIGNAL(triggered(bool)), SLOT(goFirstPage()));
  connect(actionLastPage, SIGNAL(triggered(bool)), SLOT(goLastPage()));
  connect(actionPrevPage, SIGNAL(triggered(bool)), SLOT(goPrevPage()));
  connect(actionNextPage, SIGNAL(triggered(bool)), SLOT(goNextPage()));
  connect(actionPrevPageQ, SIGNAL(triggered(bool)), this, SLOT(goPrevPage()));
  connect(actionNextPageW, SIGNAL(triggered(bool)), this, SLOT(goNextPage()));
  connect(actionPrevPageLeft, SIGNAL(triggered(bool)), this, SLOT(goPrevPage()));
  connect(actionNextPageRight, SIGNAL(triggered(bool)), this, SLOT(goNextPage()));
  connect(actionPrevSelectedPage, SIGNAL(triggered(bool)), SLOT(goPrevSelectedPage()));
  connect(actionNextSelectedPage, SIGNAL(triggered(bool)), SLOT(goNextSelectedPage()));
  connect(actionPrevSelectedPageQ, SIGNAL(triggered(bool)), this, SLOT(goPrevSelectedPage()));
  connect(actionNextSelectedPageW, SIGNAL(triggered(bool)), this, SLOT(goNextSelectedPage()));
  connect(actionGotoPage, SIGNAL(triggered(bool)), this, SLOT(execGotoPageDialog()));
  connect(actionAbout, SIGNAL(triggered(bool)), this, SLOT(showAboutDialog()));
  connect(actionInstallZoteroPlugin, &QAction::triggered, this, &MainWindow::revealZoteroPlugin);
  connect(actionImportPdf, &QAction::triggered, this, &MainWindow::importPdf);
  connect(&OutOfMemoryHandler::instance(), SIGNAL(outOfMemory()), SLOT(handleOutOfMemorySituation()));
  connect(prevPageBtn, &QToolButton::clicked, this, [this]() {
    if (filterSelectedBtn->isChecked()) {
      goPrevSelectedPage();
    } else {
      goPrevPage();
    }
  });
  connect(nextPageBtn, &QToolButton::clicked, this, [this]() {
    if (filterSelectedBtn->isChecked()) {
      goNextSelectedPage();
    } else {
      goNextPage();
    }
  });
  connect(gotoPageBtn, SIGNAL(clicked()), this, SLOT(execGotoPageDialog()));

  auto magnifyThumbnails = [this]() { scaleThumbnails(1); };
  auto diminishThumbnails = [this]() { scaleThumbnails(-1); };
  connect(magnifyThumbnailsBtn, &QPushButton::clicked, magnifyThumbnails);
  connect(diminishThumbnailsBtn, &QPushButton::clicked, diminishThumbnails);
  connect(actionMagnifyThumbnails, &QAction::triggered, magnifyThumbnails);
  connect(actionDiminishThumbnails, &QAction::triggered, diminishThumbnails);
  connect(actionReloadPage, SIGNAL(triggered(bool)), SLOT(reloadCurrentPage()));
  connect(actionRemovePages, &QAction::triggered, [this]() {
    if (isProjectLoaded() && !m_thumbSequence->selectedItems().empty()) {
      showRemovePagesDialog(m_thumbSequence->selectedItems());
    }
  });

  connect(actionSwitchFilter1, SIGNAL(triggered(bool)), SLOT(switchFilter1()));
  connect(actionSwitchFilter2, SIGNAL(triggered(bool)), SLOT(switchFilter2()));
  connect(actionSwitchFilter3, SIGNAL(triggered(bool)), SLOT(switchFilter3()));
  connect(actionSwitchFilter4, SIGNAL(triggered(bool)), SLOT(switchFilter4()));
  connect(actionSwitchFilter5, SIGNAL(triggered(bool)), SLOT(switchFilter5()));
  connect(actionSwitchFilter6, SIGNAL(triggered(bool)), SLOT(switchFilter6()));

  connect(filterList->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this,
          SLOT(filterSelectionChanged(const QItemSelection&)));
  connect(filterList, SIGNAL(launchBatchProcessing()), this, SLOT(startBatchProcessing()));

  connect(m_workerThreadPool.get(), SIGNAL(taskResult(const BackgroundTaskPtr&, const FilterResultPtr&)), this,
          SLOT(filterResult(const BackgroundTaskPtr&, const FilterResultPtr&)));

  connect(m_thumbSequence.get(),
          SIGNAL(newSelectionLeader(const PageInfo&, const QRectF&, ThumbnailSequence::SelectionFlags)), this,
          SLOT(currentPageChanged(const PageInfo&, const QRectF&, ThumbnailSequence::SelectionFlags)));
  connect(m_thumbSequence.get(), SIGNAL(pageContextMenuRequested(const PageInfo&, const QPoint&, bool)), this,
          SLOT(pageContextMenuRequested(const PageInfo&, const QPoint&, bool)));
  connect(m_thumbSequence.get(), SIGNAL(pastLastPageContextMenuRequested(const QPoint&)),
          SLOT(pastLastPageContextMenuRequested(const QPoint&)));
  connect(selectionModeBtn, SIGNAL(clicked(bool)), m_thumbSequence.get(), SLOT(setSelectionModeEnabled(bool)));
  connect(actionSelectAll, &QAction::triggered, m_thumbSequence.get(), &ThumbnailSequence::selectAll);
  connect(actionAutoMode, &QAction::triggered, this, &MainWindow::startAutoMode);
  actionAutoMode->setShortcuts(
      {QKeySequence(QStringLiteral("Meta+Shift+A")), QKeySequence(QStringLiteral("Meta+Shift+O"))});

  connect(thumbView->verticalScrollBar(), SIGNAL(sliderMoved(int)), this, SLOT(thumbViewScrolled()));
  connect(thumbView->verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(thumbViewScrolled()));
  connect(focusButton, SIGNAL(clicked(bool)), this, SLOT(thumbViewFocusToggled(bool)));
  connect(sortOptions, SIGNAL(currentIndexChanged(int)), this, SLOT(pageOrderingChanged(int)));
  connect(thumbColumnViewBtn, &QToolButton::clicked, this, [this, &settings](bool checked) {
    settings.setSingleColumnThumbnailDisplayEnabled(checked);
    updateThumbnailViewMode();
    m_thumbSequence->updateSceneItemsPos();
  });
  connect(sortingOrderBtn, &QToolButton::clicked, this,
          [this](bool) { pageOrderingChanged(m_stages->filterAt(m_curFilter)->selectedPageOrder()); });
  connect(deviationHighlightingBtn, &QToolButton::clicked, this, [this, &settings](bool checked) {
    settings.setHighlightDeviationEnabled(checked);
    m_thumbSequence->invalidateAllThumbnails();
  });

  // Color mode filter buttons - filter thumbnails by detected color mode
  auto updateColorModeFilter = [this]() {
    resetThumbSequence(currentPageOrderProvider(), ThumbnailSequence::KEEP_SELECTION);
  };
  connect(filterBwBtn, &QToolButton::toggled, this, updateColorModeFilter);
  connect(filterGrayBtn, &QToolButton::toggled, this, updateColorModeFilter);
  connect(filterMixedBtn, &QToolButton::toggled, this, updateColorModeFilter);
  connect(filterColorBtn, &QToolButton::toggled, this, updateColorModeFilter);

  // c/g/b/p - Set color mode (Finalize & Output stages)
  auto* shortcutC = new QShortcut(QKeySequence("c"), this);
  connect(shortcutC, &QShortcut::activated, this, [this]() {
    if (isFinalizeOrOutputFilter()) setColorModeForSelectedPages(output::COLOR);
  });
  auto* shortcutG = new QShortcut(QKeySequence("g"), this);
  connect(shortcutG, &QShortcut::activated, this, [this]() {
    if (isFinalizeOrOutputFilter()) setColorModeForSelectedPages(output::GRAYSCALE);
  });
  auto* shortcutB = new QShortcut(QKeySequence("b"), this);
  connect(shortcutB, &QShortcut::activated, this, [this]() {
    if (isFinalizeOrOutputFilter()) setColorModeForSelectedPages(output::BLACK_AND_WHITE);
  });
  // Note: 'p' for pass-through is handled in keyPressEvent to avoid falling through to QMainWindow

  // Shift+C/M/G/B - Toggle page filters (Finalize & Output stages)
  auto* shortcutShiftC = new QShortcut(QKeySequence("Shift+c"), this);
  connect(shortcutShiftC, &QShortcut::activated, this, [=]() {
    if (isFinalizeOrOutputFilter()) filterColorBtn->toggle();
  });
  auto* shortcutShiftG = new QShortcut(QKeySequence("Shift+g"), this);
  connect(shortcutShiftG, &QShortcut::activated, this, [=]() {
    if (isFinalizeOrOutputFilter()) filterGrayBtn->toggle();
  });
  auto* shortcutShiftM = new QShortcut(QKeySequence("Shift+m"), this);
  connect(shortcutShiftM, &QShortcut::activated, this, [=]() {
    if (isFinalizeOrOutputFilter()) filterMixedBtn->toggle();
  });
  auto* shortcutShiftB = new QShortcut(QKeySequence("Shift+b"), this);
  connect(shortcutShiftB, &QShortcut::activated, this, [=]() {
    if (isFinalizeOrOutputFilter()) filterBwBtn->toggle();
  });

  connect(actionFixDpi, SIGNAL(triggered(bool)), SLOT(fixDpiDialogRequested()));
  connect(actionRelinking, SIGNAL(triggered(bool)), SLOT(showRelinkingDialog()));
#ifdef ENABLE_DEBUG_FEATURES
  connect(actionDebug, SIGNAL(toggled(bool)), SLOT(debugToggled(bool)));
#else
  actionDebug->setVisible(false);
#endif

  connect(actionSettings, SIGNAL(triggered(bool)), this, SLOT(openSettingsDialog()));
  connect(actionDefaults, SIGNAL(triggered(bool)), this, SLOT(openDefaultParamsDialog()));

  connect(actionNewProject, SIGNAL(triggered(bool)), this, SLOT(newProject()));
  connect(actionOpenProject, SIGNAL(triggered(bool)), this, SLOT(openProject()));
  connect(actionSaveProject, SIGNAL(triggered(bool)), this, SLOT(saveProjectTriggered()));
  connect(actionSaveProjectAs, SIGNAL(triggered(bool)), this, SLOT(saveProjectAsTriggered()));
  connect(actionExportPdf, &QAction::triggered, this, &MainWindow::exportToPdf);
  connect(actionCloseProject, SIGNAL(triggered(bool)), this, SLOT(closeProject()));
  connect(actionQuit, SIGNAL(triggered(bool)), this, SLOT(quitApp()));

  updateProjectActions();
  updateWindowTitle();
  updateMainArea();

  if (m_restoreGeometry) {
    QSettings qSettings;
    if (!qSettings.value("mainWindow/maximized", false).toBool()) {
      const QVariant geom(qSettings.value("mainWindow/nonMaximizedGeometry"));
      if (!QMainWindow::restoreGeometry(geom.toByteArray())) {
        resize(800, 550);  // A sensible value.
      }
    }
  } else {
    resize(800, 550);  // Default size for cascaded windows
  }

  m_maxLogicalThumbSizeUpdater.setSingleShot(true);
  connect(&m_maxLogicalThumbSizeUpdater, &QTimer::timeout, this, &MainWindow::updateMaxLogicalThumbSize);

  m_sceneItemsPosUpdater.setSingleShot(true);
  connect(&m_sceneItemsPosUpdater, &QTimer::timeout, m_thumbSequence.get(), &ThumbnailSequence::updateSceneItemsPos);
}

MainWindow::~MainWindow() {
  m_interactiveQueue->cancelAndClear();
  if (m_batchQueue) {
    m_batchQueue->cancelAndClear();
  }
  m_workerThreadPool->shutdown();

  removeWidgetsFromLayout(m_imageFrameLayout);
  removeWidgetsFromLayout(m_optionsFrameLayout);

  // Release ownership - Qt will delete it as a child widget
  // (it was parented when added to m_imageFrameLayout)
  m_tabbedDebugImages.release();
}

PageSequence MainWindow::allPages() const {
  return m_thumbSequence->toPageSequence();
}

std::set<PageId> MainWindow::selectedPages() const {
  return m_thumbSequence->selectedItems();
}

std::vector<PageRange> MainWindow::selectedRanges() const {
  return m_thumbSequence->selectedRanges();
}

void MainWindow::switchToNewProject(const std::shared_ptr<ProjectPages>& pages,
                                    const QString& outDir,
                                    const QString& projectFilePath,
                                    const ProjectReader* projectReader) {
  stopBatchProcessing(CLEAR_MAIN_AREA);
  m_interactiveQueue->cancelAndClear();

  if (!outDir.isEmpty()) {
    Utils::maybeCreateCacheDir(outDir);
  }
  m_pages = pages;
  m_projectFile = projectFilePath;

  const PageSequence sourcePages = pages->toPageSequence(IMAGE_VIEW);
  m_sourceDocumentPath = sourcePages.numPages() > 0
                             ? sourcePages.pageAt(0).id().imageId().filePath()
                             : QString();

  // When opening an existing project (projectReader exists), mark it as saved
  if (projectReader && !projectFilePath.isEmpty()) {
    m_projectSavedToFolder = true;
    m_projectFolderPath = QFileInfo(projectFilePath).absolutePath();
  } else if (projectFilePath.isEmpty()) {
    // New project that hasn't been saved yet
    m_projectSavedToFolder = false;
    m_projectFolderPath.clear();
  }

  if (projectReader) {
    m_selectedPage = projectReader->selectedPage();
  }

  std::shared_ptr<FileNameDisambiguator> disambiguator;
  if (projectReader) {
    disambiguator = projectReader->namingDisambiguator();
  } else {
    disambiguator.reset(new FileNameDisambiguator);
  }

  m_defaultOutDir = outDir;  // Store original (temp) output directory
  m_outFileNameGen = OutputFileNameGenerator(disambiguator, outDir, pages->layoutDirection());
  // These two need to go in this order.
  updateDisambiguationRecords(pages->toPageSequence(IMAGE_VIEW));

  // Recreate the stages and load their state.
  m_stages = std::make_shared<StageSequence>(pages, newPageSelectionAccessor());
  if (projectReader) {
    projectReader->readFilterSettings(m_stages->filters());
  }
  // Discover the Zotero sidecar after loading the project's explicit Export
  // choice so an absent choice can be armed without overriding a saved one.
  m_stages->exportFilter()->setProjectFilePath(projectFilePath);

  // Connect the filter list model to the view and select
  // the first item.
  {
    ScopedIncDec<int> guard(m_ignoreSelectionChanges);
    filterList->setStages(m_stages);
    filterList->selectRow(0);
    m_curFilter = 0;
    // Setting a data model also implicitly sets a new
    // selection model, so we have to reconnect to it.
    connect(filterList->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this,
            SLOT(filterSelectionChanged(const QItemSelection&)));
  }

  updateSortOptions();

  m_contentBoxPropagator = std::make_unique<ContentBoxPropagator>(
      m_stages->pageLayoutFilter(), createCompositeCacheDrivenTask(m_stages->selectContentFilterIdx()));

  m_pageOrientationPropagator = std::make_unique<PageOrientationPropagator>(
      m_stages->pageSplitFilter(), createCompositeCacheDrivenTask(m_stages->fixOrientationFilterIdx()));

  // Connect export filter signal to trigger PDF export
  connect(m_stages->exportFilter()->optionsWidget(), &export_::OptionsWidget::exportToPdfRequested,
          this, &MainWindow::exportToPdfFromFilter);

  // Thumbnails are stored relative to the output directory,
  // so recreate the thumbnail cache.
  if (outDir.isEmpty()) {
    m_thumbnailCache.reset();
  } else {
    m_thumbnailCache = Utils::createThumbnailCache(m_outFileNameGen.outDir());
  }
  resetThumbSequence(currentPageOrderProvider());

  removeFilterOptionsWidget();
  updateProjectActions();
  updateWindowTitle();
  updateMainArea();

  // Resize window larger when loading a project with pages
  // Skip centering for cascaded windows (m_restoreGeometry == false means explicit positioning)
  if (m_pages->numImages() > 0 && m_restoreGeometry) {
    resize(1200, 800);
    // Center on screen
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
      QRect screenGeometry = screen->availableGeometry();
      int x = (screenGeometry.width() - width()) / 2 + screenGeometry.x();
      int y = (screenGeometry.height() - height()) / 2 + screenGeometry.y();
      move(x, y);
    }
  }

  // Only show relinking dialog for existing projects with missing paths
  // For new projects (no projectReader), just create the output directory
  if (!outDir.isEmpty() && !QDir(outDir).exists()) {
    if (projectReader) {
      // Existing project with missing output directory - show relinking dialog
      showRelinkingDialog();
    } else {
      // New project - just create the output directory
      QDir().mkpath(outDir);
    }
  }
}  // MainWindow::switchToNewProject

void MainWindow::showNewOpenProjectPanel() {
#ifdef Q_OS_MAC
  removeImageWidget();
  if (isVisible() && !m_closing) {
    QTimer::singleShot(0, this, [this]() {
      if (m_closing || !m_pages || m_pages->numImages() != 0) {
        return;
      }

      QSettings settings;
      settings.setValue("mainWindow/maximized", isMaximized());
      if (!isMaximized()) {
        settings.setValue("mainWindow/nonMaximizedGeometry", saveGeometry());
      }

      m_closing = true;
      close();
    });
  }
  return;
#else
  auto outerWidget = std::make_unique<QWidget>();
  auto* layout = new QGridLayout(outerWidget.get());
  outerWidget->setLayout(layout);

  auto* nop = new NewOpenProjectPanel(outerWidget.get());
  // We use asynchronous connections because otherwise we
  // would be deleting a widget from its event handler, which
  // Qt doesn't like.
  connect(nop, SIGNAL(importPdf()), this, SLOT(importPdf()), Qt::QueuedConnection);
  connect(nop, SIGNAL(importFolder()), this, SLOT(newProject()), Qt::QueuedConnection);
  connect(nop, SIGNAL(openProject()), this, SLOT(openProject()), Qt::QueuedConnection);
  connect(nop, SIGNAL(openRecentProject(const QString&)), this, SLOT(openProject(const QString&)),
          Qt::QueuedConnection);

  layout->addWidget(nop, 0, 0);
  layout->setColumnStretch(1, 1);
  layout->setRowStretch(1, 1);
  setImageWidget(outerWidget.release(), TRANSFER_OWNERSHIP);

  filterList->setBatchProcessingPossible(false);
#endif
}  // MainWindow::showNewOpenProjectPanel

void MainWindow::createBatchProcessingWidget() {
  m_batchProcessingWidget = std::make_unique<QWidget>();
  auto* layout = new QGridLayout(m_batchProcessingWidget.get());
  m_batchProcessingWidget->setLayout(layout);

  const auto& iconProvider = IconProvider::getInstance();
  auto* stopBtn = new SkinnedButton(iconProvider.getIcon("stop"), iconProvider.getIcon("stop-hovered"),
                                    iconProvider.getIcon("stop-pressed"), m_batchProcessingWidget.get());
  stopBtn->setIconSize({128, 128});
  stopBtn->setStatusTip(tr("Stop batch processing"));

  // Progress label showing "p X / Y"
  m_batchProgressLabel = new QLabel(m_batchProcessingWidget.get());
  m_batchProgressLabel->setAlignment(Qt::AlignCenter);
  QFont progressFont = m_batchProgressLabel->font();
  progressFont.setPointSize(24);
  progressFont.setBold(true);
  m_batchProgressLabel->setFont(progressFont);

  class LowerPanel : public QWidget {
   public:
    explicit LowerPanel(QWidget* parent = 0) : QWidget(parent) { ui.setupUi(this); }

    Ui::BatchProcessingLowerPanel ui;
  };


  auto* lowerPanel = new LowerPanel(m_batchProcessingWidget.get());
  m_checkBeepWhenFinished = [lowerPanel]() { return lowerPanel->ui.beepWhenFinished->isChecked(); };

  int row = 0;  // Row 0 is reserved.
  layout->addWidget(m_batchProgressLabel, ++row, 1, Qt::AlignCenter);
  layout->addWidget(stopBtn, ++row, 1, Qt::AlignCenter);
  layout->addWidget(lowerPanel, ++row, 0, 1, 3, Qt::AlignHCenter | Qt::AlignTop);
  layout->setColumnStretch(0, 1);
  layout->setColumnStretch(2, 1);
  layout->setRowStretch(0, 1);
  layout->setRowStretch(row, 1);

  connect(stopBtn, SIGNAL(clicked()), SLOT(stopBatchProcessing()));
}  // MainWindow::createBatchProcessingWidget

void MainWindow::updateThumbViewMinWidth() {
  const int sb = thumbView->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
  int innerWidth = thumbView->maximumViewportSize().width() - sb;
  if (thumbView->style()->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents, nullptr, thumbView)) {
    innerWidth -= thumbView->frameWidth() * 2;
  }
  const int deltaX = thumbView->size().width() - innerWidth;
  thumbView->setMinimumWidth((int) std::ceil(m_maxLogicalThumbSize.width() + deltaX));
}

void MainWindow::setupThumbView() {
  updateThumbViewMinWidth();
  m_thumbSequence->attachView(thumbView);
  thumbView->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
  if ((obj == scrollArea->viewport()) && (ev->type() == QEvent::Resize)
      && qobject_cast<export_::OptionsWidget*>(m_optionsWidget.data())) {
    const int viewportHeight = scrollArea->viewport()->height();
    if (viewportHeight > 0) {
      const int minimumOptionsHeight = m_optionsWidget ? m_optionsWidget->minimumSizeHint().height() : viewportHeight;
      filterOptions->setFixedHeight(qMax(viewportHeight, minimumOptionsHeight));
    }
  }

  if ((obj == thumbView) && (ev->type() == QEvent::Resize)) {
    if (!m_sceneItemsPosUpdater.isActive()) {
      m_sceneItemsPosUpdater.start(150);
    }
  }

  if ((obj == thumbView || obj == thumbView->verticalScrollBar()) && (ev->type() == QEvent::Wheel)) {
    auto* wheelEvent = static_cast<QWheelEvent*>(ev);
    if (wheelEvent->modifiers() == Qt::AltModifier) {
      const QPoint& angleDelta = wheelEvent->angleDelta();
      const int wheelDist = angleDelta.x() + angleDelta.y();
      if (std::abs(wheelDist) >= 30) {
        scaleThumbnails(std::copysign(1, wheelDist));
      }
      wheelEvent->accept();
      return true;
    }
  }
  return false;
}

void MainWindow::closeEvent(QCloseEvent* const event) {
  if (m_closing) {
    event->accept();
  } else {
    event->ignore();
    if (!m_closeEventPending) {
      m_closeEventPending = true;
      startTimer(0);
    }
  }
}

void MainWindow::timerEvent(QTimerEvent* const event) {
  // We only use the timer event for delayed closing of the window.
  killTimer(event->timerId());

  const bool closeAccepted = closeProjectInteractive();
  if (!closeAccepted) {
    m_closeEventPending = false;
#ifdef Q_OS_MAC
    // The user cancelled the save prompt: the window stays open, and so does the
    // application, even if this close was part of a quit sequence.
    if (m_quitting) {
      m_quitting = false;
      emit quitAborted();
    }
#endif
  }
  if (closeAccepted) {
#ifdef Q_OS_MAC
    // On macOS, only close window if user explicitly chose to quit (Cmd+Q or Quit menu)
    // Otherwise emit projectClosed signal so AppController can show startup window
    if (!m_quitting) {
      m_closing = true;  // Set before emitting so subsequent close() is accepted immediately
      QSettings settings;
      settings.setValue("mainWindow/maximized", isMaximized());
      if (!isMaximized()) {
        settings.setValue("mainWindow/nonMaximizedGeometry", saveGeometry());
      }
      emit projectClosed();
      return;
    }
#endif
    m_closing = true;
    QSettings settings;
    settings.setValue("mainWindow/maximized", isMaximized());
    if (!isMaximized()) {
      settings.setValue("mainWindow/nonMaximizedGeometry", saveGeometry());
    }
#ifdef Q_OS_MAC
    // On macOS, coordinate quit across all windows
    if (m_quitting) {
      // First emit signal for AppController (if connected)
      emit quitRequested();

      // Also directly find and quit other MainWindows in case signal isn't connected
      // (e.g., windows created directly by MainWindow::importPdf)
      bool foundOther = false;
      for (QWidget* widget : QApplication::topLevelWidgets()) {
        MainWindow* other = qobject_cast<MainWindow*>(widget);
        if (other && other != this && other->isVisible()) {
          // Schedule on next event loop to avoid issues with current close
          QTimer::singleShot(0, other, &MainWindow::quitApp);
          foundOther = true;
          break;  // One at a time
        }
      }
      // If no other windows found, schedule app quit. Exiting the event loop
      // directly rather than through QApplication::quit(), which would ask the
      // platform to quit and come back as another system quit request.
      if (!foundOther) {
        QTimer::singleShot(0, qApp, []() { QCoreApplication::exit(0); });
      }
    }
#endif
    close();
  }
}

MainWindow::SavePromptResult MainWindow::promptProjectSave() {
  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::NoIcon);
  msgBox.setWindowTitle(tr("Save Project"));
  msgBox.setText(tr("Do you want to save the changes to this project?"));
  msgBox.setInformativeText(tr("Your changes will be lost if you don't save them."));

  QPushButton* saveButton = msgBox.addButton(tr("Save"), QMessageBox::AcceptRole);
  QPushButton* dontSaveButton = msgBox.addButton(tr("Don't Save"), QMessageBox::DestructiveRole);
  msgBox.addButton(QMessageBox::Cancel);
  msgBox.setDefaultButton(saveButton);

  msgBox.exec();

  if (msgBox.clickedButton() == saveButton) {
    return SAVE;
  } else if (msgBox.clickedButton() == dontSaveButton) {
    return DONT_SAVE;
  }
  return CANCEL;
}

bool MainWindow::compareFiles(const QString& fpath1, const QString& fpath2) {
  QFile file1(fpath1);
  QFile file2(fpath2);

  if (!file1.open(QIODevice::ReadOnly)) {
    return false;
  }
  if (!file2.open(QIODevice::ReadOnly)) {
    file1.close();
    return false;
  }

  if (!file1.isSequential() && !file2.isSequential()) {
    if (file1.size() != file2.size()) {
      return false;
    }
  }

  const int chunkSize = 4096;
  while (true) {
    const QByteArray chunk1(file1.read(chunkSize));
    const QByteArray chunk2(file2.read(chunkSize));
    if (chunk1 != chunk2) {
      return false;
    }
    if (chunk1.isEmpty()) {
      return true;
    }
  }
}

std::shared_ptr<const PageOrderProvider> MainWindow::currentPageOrderProvider() const {
  const int idx = sortOptions->currentIndex();
  if (idx < 0) {
    return nullptr;
  }

  const std::shared_ptr<AbstractFilter> filter(m_stages->filterAt(m_curFilter));
  std::shared_ptr<const PageOrderProvider> currentOrderProvider = filter->pageOrderOptions()[idx].provider();
  return (currentOrderProvider && sortingOrderBtn->isChecked()) ? currentOrderProvider->reversed()
                                                                : currentOrderProvider;
}

void MainWindow::updateSortOptions() {
  const ScopedIncDec<int> guard(m_ignorePageOrderingChanges);

  const std::shared_ptr<AbstractFilter> filter(m_stages->filterAt(m_curFilter));

  sortOptions->clear();

  for (const PageOrderOption& opt : filter->pageOrderOptions()) {
    sortOptions->addItem(opt.name());
  }

  sortOptions->setVisible(sortOptions->count() > 0);

  if (sortOptions->count() > 0) {
    sortOptions->setCurrentIndex(filter->selectedPageOrder());
  }
}

void MainWindow::resetThumbSequence(const std::shared_ptr<const PageOrderProvider>& pageOrderProvider,
                                    const ThumbnailSequence::SelectionAction selectionAction) {
  if (m_thumbnailCache) {
    const std::shared_ptr<CompositeCacheDrivenTask> task(createCompositeCacheDrivenTask(m_curFilter));

    m_thumbSequence->setThumbnailFactory(
        std::make_shared<ThumbnailFactory>(m_thumbnailCache, m_maxLogicalThumbSize, task));
  }

  m_thumbSequence->reset(currentPageSequence(), selectionAction, pageOrderProvider);

  if (!m_thumbnailCache) {
    // Empty project.
    assert(m_pages->numImages() == 0);
    m_thumbSequence->setThumbnailFactory(nullptr);
  }

  if (selectionAction != ThumbnailSequence::KEEP_SELECTION) {
    const PageId page(m_selectedPage.get(getCurrentView()));
    if (m_thumbSequence->setSelection(page)) {
      // OK
    } else if (m_thumbSequence->setSelection(PageId(page.imageId(), PageId::LEFT_PAGE))) {
      // OK
    } else if (m_thumbSequence->setSelection(PageId(page.imageId(), PageId::RIGHT_PAGE))) {
      // OK
    } else if (m_thumbSequence->setSelection(PageId(page.imageId(), PageId::SINGLE_PAGE))) {
      // OK
    } else {
      // Last resort.
      m_thumbSequence->setSelection(m_thumbSequence->firstPage().id());
    }
  }
}

void MainWindow::setOptionsWidget(FilterOptionsWidget* widget, const Ownership ownership) {
  if (isBatchProcessingInProgress()) {
    if (ownership == TRANSFER_OWNERSHIP) {
      delete widget;
    }
    return;
  }

  if (m_optionsWidget != widget) {
    removeWidgetsFromLayout(m_optionsFrameLayout);
  }
  // Delete the old widget we were owning, if any.
  m_optionsWidgetCleanup.clear();

  if (ownership == TRANSFER_OWNERSHIP) {
    m_optionsWidgetCleanup.add(widget);
  }

  if (m_optionsWidget == widget) {
    return;
  }

  if (m_optionsWidget) {
    disconnect(m_optionsWidget, SIGNAL(reloadRequested()), this, SLOT(reloadRequested()));
    disconnect(m_optionsWidget, SIGNAL(invalidateThumbnail(const PageId&)), this,
               SLOT(invalidateThumbnail(const PageId&)));
    disconnect(m_optionsWidget, SIGNAL(invalidateThumbnail(const PageInfo&)), this,
               SLOT(invalidateThumbnail(const PageInfo&)));
    disconnect(m_optionsWidget, SIGNAL(invalidateAllThumbnails()), this, SLOT(invalidateAllThumbnails()));
    disconnect(m_optionsWidget, SIGNAL(goToPage(const PageId&)), this, SLOT(goToPage(const PageId&)));
  }

  m_optionsFrameLayout->addWidget(widget);
  m_optionsWidget = widget;

  // Export owns an inner scroller so its action footer can stay pinned.  Bind
  // its frame to the outer viewport instead of letting the outer scroll area
  // grow it to Export's full settings-content size hint. Other filters retain
  // the traditional outer-scroller behavior.
  const bool hasIndependentScroller = qobject_cast<export_::OptionsWidget*>(widget) != nullptr;
  filterOptions->setMinimumHeight(0);
  filterOptions->setMaximumHeight(QWIDGETSIZE_MAX);
  if (hasIndependentScroller) {
    // Export intentionally disables horizontal scrolling. Its runtime compact
    // minimum must therefore be satisfied by the dock itself; otherwise the
    // outer viewport silently clips the footer and primary action on the
    // right. Include the dock/layout chrome already visible around the
    // viewport, then restore the normal minimum for every other stage.
    const int dockChromeWidth = qMax(0, filterDockWidget->width() - scrollArea->viewport()->width());
    filterDockWidget->setMinimumWidth(
        qMax(m_filterDockBaseMinimumWidth, widget->minimumWidth() + dockChromeWidth));
  } else {
    filterDockWidget->setMinimumWidth(m_filterDockBaseMinimumWidth);
  }
  if (hasIndependentScroller && scrollArea->viewport()->height() > 0) {
    filterOptions->setFixedHeight(
        qMax(scrollArea->viewport()->height(), m_optionsWidget->minimumSizeHint().height()));
  }

  // We use an asynchronous connection here, because the slot
  // will probably delete the options panel, which could be
  // responsible for the emission of this signal.  Qt doesn't
  // like when we delete an object while it's emitting a signal.
  connect(widget, SIGNAL(reloadRequested()), this, SLOT(reloadRequested()), Qt::QueuedConnection);
  connect(widget, SIGNAL(invalidateThumbnail(const PageId&)), this, SLOT(invalidateThumbnail(const PageId&)));
  connect(widget, SIGNAL(invalidateThumbnail(const PageInfo&)), this, SLOT(invalidateThumbnail(const PageInfo&)));
  connect(widget, SIGNAL(invalidateAllThumbnails()), this, SLOT(invalidateAllThumbnails()));
  connect(widget, SIGNAL(goToPage(const PageId&)), this, SLOT(goToPage(const PageId&)));
  connect(widget, SIGNAL(batchProcessingRequested(const std::set<PageId>&)), this,
          SLOT(batchProcessPages(const std::set<PageId>&)));
  // Output destination and encoding controls live only on Finalize.
  // Check the signal itself rather than including finalize/OptionsWidget.h
  // here; several filters generate a same-named ui_OptionsWidget.h.
  if (widget->metaObject()->indexOfSignal("outputDirectoryChanged(QString)") >= 0) {
    connect(widget, SIGNAL(outputDirectoryChanged(const QString&)),
            this, SLOT(outputDirectoryChanged(const QString&)));
    connect(widget, SIGNAL(outputFormatSettingChanged(int)),
            this, SLOT(outputFormatSettingChanged(int)));
    connect(widget, SIGNAL(tiffCompressionSettingChanged(int)),
            this, SLOT(tiffCompressionSettingChanged(int)));
    connect(widget, SIGNAL(jpegQualitySettingChanged(int)),
            this, SLOT(jpegQualitySettingChanged(int)));
  }
}  // MainWindow::setOptionsWidget

void MainWindow::setImageWidget(QWidget* widget, const Ownership ownership, DebugImages* debugImages, bool overlay) {
  if (isBatchProcessingInProgress() && (widget != m_batchProcessingWidget.get())) {
    if (ownership == TRANSFER_OWNERSHIP) {
      delete widget;
    }
    return;
  }

  if (!overlay) {
    removeImageWidget();
  }

  if (ownership == TRANSFER_OWNERSHIP) {
    m_imageWidgetCleanup.add(widget);
  }

  if (!debugImages || debugImages->empty()) {
    if (widget != m_imageFrameLayout->currentWidget()) {
      m_imageFrameLayout->addWidget(widget);
      if (overlay) {
        m_imageFrameLayout->setCurrentWidget(widget);
      }
    }
  } else {
    m_tabbedDebugImages->addTab(widget, "Main");
    AutoRemovingFile file;
    QString label;
    while (!(file = debugImages->retrieveNext(&label)).get().isNull()) {
      QWidget* view = new DebugImageView(file);
      m_imageWidgetCleanup.add(view);
      m_tabbedDebugImages->addTab(view, label);
    }
    m_imageFrameLayout->addWidget(m_tabbedDebugImages.get());
  }
}

void MainWindow::removeImageWidget() {
  removeWidgetsFromLayout(m_imageFrameLayout);

  m_tabbedDebugImages->clear();
  // Delete the old widget we were owning, if any.
  m_imageWidgetCleanup.clear();
}

void MainWindow::invalidateThumbnail(const PageId& pageId) {
  m_thumbSequence->invalidateThumbnail(pageId);
}

void MainWindow::invalidateThumbnail(const PageInfo& pageInfo) {
  m_thumbSequence->invalidateThumbnail(pageInfo);
}

void MainWindow::invalidateAllThumbnails() {
  m_thumbSequence->invalidateAllThumbnails();
}

void MainWindow::batchProcessPages(const std::set<PageId>& pages) {
  if (pages.empty() || !isProjectLoaded()) {
    return;
  }

  // If batch processing is already in progress, add pages to queue
  // Otherwise start a new batch for just these pages
  if (isBatchProcessingInProgress()) {
    // Can't interrupt current batch processing
    return;
  }

  m_interactiveQueue->cancelAndClear();

  m_batchQueue = std::make_unique<ProcessingTaskQueue>();

  // Get the page sequence to look up PageInfo
  const PageSequence pageSequence = m_pages->toPageSequence(PAGE_VIEW);

  // Add only the specified pages to the batch queue
  for (const PageId& pageId : pages) {
    const PageInfo& pageInfo = pageSequence.pageAt(pageId);
    if (!pageInfo.isNull()) {
      m_batchQueue->addProcessingTask(pageInfo, createCompositeTask(pageInfo, m_curFilter, /*batch=*/true, m_debug));
    }
  }

  if (m_batchQueue->totalPages() == 0) {
    m_batchQueue.reset();
    return;
  }

  startBatchTimer();
  focusButton->setChecked(true);

  removeFilterOptionsWidget();
  filterList->setBatchProcessingInProgress(true);
  filterList->setEnabled(false);

  // Initialize progress display
  if (m_batchProgressLabel) {
    m_batchProgressLabel->setText(tr("p. %1 / %2").arg(1).arg(m_batchQueue->totalPages()));
  }

  BackgroundTaskPtr task(m_batchQueue->takeForProcessing());
  if (task) {
    do {
      m_workerThreadPool->submitTask(task);
      if (!m_workerThreadPool->hasSpareCapacity()) {
        break;
      }
    } while ((task = m_batchQueue->takeForProcessing()));
  } else {
    stopBatchProcessing();
  }

  PageInfo page = m_batchQueue->selectedPage();
  if (!page.isNull()) {
    m_thumbSequence->setSelection(page.id());
  }
  // Display the batch processing screen.
  updateMainArea();
}

std::shared_ptr<AbstractCommand<void>> MainWindow::relinkingDialogRequester() {
  class Requester : public AbstractCommand<void> {
   public:
    explicit Requester(MainWindow* wnd) : m_wnd(wnd) {}

    virtual void operator()() {
      if (!m_wnd.isNull()) {
        m_wnd->showRelinkingDialog();
      }
    }

   private:
    QPointer<MainWindow> m_wnd;
  };
  return std::make_shared<Requester>(this);
}

void MainWindow::showRelinkingDialog() {
  if (!isProjectLoaded()) {
    return;
  }

  auto* dialog = new RelinkingDialog(m_projectFile, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);

  m_pages->listRelinkablePaths(dialog->pathCollector());
  dialog->pathCollector()(RelinkablePath(m_outFileNameGen.outDir(), RelinkablePath::Dir));

  connect(dialog, &QDialog::accepted, [this, dialog]() { this->performRelinking(dialog->relinker()); });

  dialog->show();
}

void MainWindow::performRelinking(const std::shared_ptr<AbstractRelinker>& relinker) {
  assert(relinker);

  if (!isProjectLoaded()) {
    return;
  }

  m_pages->performRelinking(*relinker);
  m_stages->performRelinking(*relinker);
  m_outFileNameGen.performRelinking(*relinker);

  Utils::maybeCreateCacheDir(m_outFileNameGen.outDir());

  m_thumbnailCache->setThumbDir(Utils::outputDirToThumbDir(m_outFileNameGen.outDir()));
  resetThumbSequence(currentPageOrderProvider());
  m_selectedPage.set(m_thumbSequence->selectionLeader().id(), getCurrentView());

  reloadRequested();
}

void MainWindow::goFirstPage() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  const PageInfo firstPage(m_thumbSequence->firstPage());
  if (!firstPage.isNull()) {
    goToPage(firstPage.id());
  }
}

void MainWindow::goLastPage() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  const PageInfo lastPage(m_thumbSequence->lastPage());
  if (!lastPage.isNull()) {
    goToPage(lastPage.id());
  }
}

void MainWindow::goNextPage() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  const PageInfo nextPage(m_thumbSequence->nextPage(m_thumbSequence->selectionLeader().id()));
  if (!nextPage.isNull()) {
    goToPage(nextPage.id());
  }
}

void MainWindow::goPrevPage() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  const PageInfo prevPage(m_thumbSequence->prevPage(m_thumbSequence->selectionLeader().id()));
  if (!prevPage.isNull()) {
    goToPage(prevPage.id());
  }
}

void MainWindow::goNextSelectedPage() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  const PageInfo nextSelectedPage(m_thumbSequence->nextSelectedPage(m_thumbSequence->selectionLeader().id()));
  if (!nextSelectedPage.isNull()) {
    goToPage(nextSelectedPage.id(), ThumbnailSequence::KEEP_SELECTION);
  }
}

void MainWindow::goPrevSelectedPage() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  const PageInfo prevSelectedPage(m_thumbSequence->prevSelectedPage(m_thumbSequence->selectionLeader().id()));
  if (!prevSelectedPage.isNull()) {
    goToPage(prevSelectedPage.id(), ThumbnailSequence::KEEP_SELECTION);
  }
}

void MainWindow::goToPage(const PageId& pageId, const ThumbnailSequence::SelectionAction selectionAction) {
  focusButton->setChecked(true);

  m_thumbSequence->setSelection(pageId, selectionAction);

  // If the page was already selected, it will be reloaded.
  // That's by design.
  updateMainArea();
}

void MainWindow::currentPageChanged(const PageInfo& pageInfo,
                                    const QRectF& thumbRect,
                                    const ThumbnailSequence::SelectionFlags flags) {
  m_selectedPage.set(pageInfo.id(), getCurrentView());

  if ((flags & ThumbnailSequence::SELECTED_BY_USER) || focusButton->isChecked()) {
    if (!(flags & ThumbnailSequence::AVOID_SCROLLING_TO)) {
      thumbView->ensureVisible(thumbRect, 0, 0);
    }
  }

  if (flags & ThumbnailSequence::SELECTED_BY_USER) {
    if (isBatchProcessingInProgress()) {
      stopBatchProcessing();
    } else if (!(flags & ThumbnailSequence::REDUNDANT_SELECTION)) {
      // Start loading / processing the newly selected page.
      updateMainArea();
    }
  }

  if ((flags & ThumbnailSequence::SELECTION_CLEARED) && selectionModeBtn->isChecked()) {
    selectionModeBtn->setChecked(false);
  }

  updateAutoSaveTimer();
}

void MainWindow::autoSaveProject() {
  if (m_projectFile.isEmpty()) {
    return;
  }
  if (!ApplicationSettings::getInstance().isAutoSaveProjectEnabled()) {
    return;
  }

  saveProjectWithFeedback(m_projectFile);
}

void MainWindow::pageContextMenuRequested(const PageInfo& pageInfo_, const QPoint& screenPos, bool selected) {
  if (isBatchProcessingInProgress()) {
    return;
  }
  // Make a copy to prevent it from being invalidated.
  const PageInfo pageInfo = pageInfo_;

  if (!selected) {
    goToPage(pageInfo.id());
  }

  QMenu menu;

  auto& iconProvider = IconProvider::getInstance();
  QAction* insBefore = menu.addAction(iconProvider.getIcon("insert-before"), tr("Insert before ..."));
  QAction* insAfter = menu.addAction(iconProvider.getIcon("insert-after"), tr("Insert after ..."));

  menu.addSeparator();

  QAction* remove = menu.addAction(iconProvider.getIcon("user-trash"), tr("Remove from project ..."));

  menu.addSeparator();

  QAction* forceTwoPages = menu.addAction(iconProvider.getIcon("two_pages"), tr("Force two-page layout"));
  QAction* forceSinglePage = menu.addAction(iconProvider.getIcon("single_page_uncut"), tr("Force single page layout"));
  QAction* resetAutoLayout = menu.addAction(iconProvider.getIcon("layout_type_auto"), tr("Reset to auto layout"));

  menu.addSeparator();

  QAction* processFromHere = menu.addAction(iconProvider.getIcon("play"), tr("Process from here..."));

  QAction* convertToBlackAndWhite = nullptr;
  QAction* convertToGrayscale = nullptr;
  QAction* convertToColor = nullptr;
  if (isFinalizeOrOutputFilter()) {
    menu.addSeparator();
    convertToBlackAndWhite = menu.addAction(tr("Convert to Black and White"));
    convertToGrayscale = menu.addAction(tr("Convert to Grayscale"));
    convertToColor = menu.addAction(tr("Convert to Color"));
  }

  QAction* action = menu.exec(screenPos);
  if (action == insBefore) {
    showInsertFileDialog(BEFORE, pageInfo.imageId());
  } else if (action == insAfter) {
    showInsertFileDialog(AFTER, pageInfo.imageId());
  } else if (action == remove) {
    showRemovePagesDialog(m_thumbSequence->selectedItems());
  } else if (action == forceTwoPages) {
    forcePageSplitLayout(page_split::TWO_PAGES);
  } else if (action == forceSinglePage) {
    forcePageSplitLayout(page_split::SINGLE_PAGE_UNCUT);
  } else if (action == resetAutoLayout) {
    forcePageSplitLayout(page_split::AUTO_LAYOUT_TYPE);
  } else if (action == processFromHere) {
    startBatchProcessingFrom(pageInfo);
  } else if (convertToBlackAndWhite && action == convertToBlackAndWhite) {
    setColorModeForSelectedPages(output::BLACK_AND_WHITE);
  } else if (convertToGrayscale && action == convertToGrayscale) {
    setColorModeForSelectedPages(output::GRAYSCALE);
  } else if (convertToColor && action == convertToColor) {
    setColorModeForSelectedPages(output::COLOR);
  }
}  // MainWindow::pageContextMenuRequested

void MainWindow::pastLastPageContextMenuRequested(const QPoint& screenPos) {
  if (!isProjectLoaded()) {
    return;
  }

  QMenu menu;
  menu.addAction(IconProvider::getInstance().getIcon("insert-here"), tr("Insert here ..."));

  if (menu.exec(screenPos)) {
    showInsertFileDialog(BEFORE, ImageId());
  }
}

void MainWindow::thumbViewFocusToggled(const bool checked) {
  const QRectF rect(m_thumbSequence->selectionLeaderSceneRect());
  if (rect.isNull()) {
    // No selected items.
    return;
  }

  if (checked) {
    thumbView->ensureVisible(rect, 0, 0);
  }
}

void MainWindow::thumbViewScrolled() {
  const QRectF rect(m_thumbSequence->selectionLeaderSceneRect());
  if (rect.isNull()) {
    // No items selected.
    return;
  }

  const QRectF viewportRect(thumbView->viewport()->rect());
  const QRectF viewportItemRect(thumbView->viewportTransform().mapRect(rect));

  const double intersectionThreshold = 0.5;
  if ((viewportItemRect.top() >= viewportRect.top())
      && (viewportItemRect.top() + viewportItemRect.height() * intersectionThreshold <= viewportRect.bottom())) {
    // Item is visible.
  } else if ((viewportItemRect.bottom() <= viewportRect.bottom())
             && (viewportItemRect.bottom() - viewportItemRect.height() * intersectionThreshold >= viewportRect.top())) {
    // Item is visible.
  } else {
    focusButton->setChecked(false);
  }
}

void MainWindow::filterSelectionChanged(const QItemSelection& selected) {
  if (m_ignoreSelectionChanges) {
    return;
  }

  if (selected.empty()) {
    return;
  }

  m_interactiveQueue->cancelAndClear();
  if (m_batchQueue) {
    // Should not happen, but just in case.
    m_batchQueue->cancelAndClear();
  }

  const bool wasBelowFixOrientation = isBelowFixOrientation(m_curFilter);
  const bool wasBelowSelectContent = isBelowSelectContent(m_curFilter);
  m_curFilter = selected.front().top();
  const bool nowBelowFixOrientation = isBelowFixOrientation(m_curFilter);
  const bool nowBelowSelectContent = isBelowSelectContent(m_curFilter);

  m_stages->filterAt(m_curFilter)->selected();

  updateSortOptions();

  // Propagate context boxes down the stage list, if necessary.
  if (!wasBelowSelectContent && nowBelowSelectContent) {
    // IMPORTANT: this needs to go before resetting thumbnails,
    // because it may affect them.
    if (m_contentBoxPropagator) {
      m_contentBoxPropagator->propagate(*m_pages);
    }  // Otherwise probably no project is loaded.
  }
  // Propagate page orientations (that might have changed) to the "Split Pages" stage.
  if (!wasBelowFixOrientation && nowBelowFixOrientation) {
    // IMPORTANT: this needs to go before resetting thumbnails,
    // because it may affect them.
    if (m_pageOrientationPropagator) {
      m_pageOrientationPropagator->propagate(*m_pages);
    }  // Otherwise probably no project is loaded.
  }

  const int horScrollBarPos = thumbView->horizontalScrollBar()->value();
  const int verScrollBarPos = thumbView->verticalScrollBar()->value();

  resetThumbSequence(currentPageOrderProvider(), ThumbnailSequence::KEEP_SELECTION);

  if (!focusButton->isChecked()) {
    thumbView->horizontalScrollBar()->setValue(horScrollBarPos);
    thumbView->verticalScrollBar()->setValue(verScrollBarPos);
  }

  updateMainArea();
}  // MainWindow::filterSelectionChanged

void MainWindow::switchFilter1() {
  filterList->selectRow(0);
}

void MainWindow::switchFilter2() {
  filterList->selectRow(1);
}

void MainWindow::switchFilter3() {
  filterList->selectRow(2);
}

void MainWindow::switchFilter4() {
  filterList->selectRow(3);
}

void MainWindow::switchFilter5() {
  filterList->selectRow(4);
}

void MainWindow::switchFilter6() {
  filterList->selectRow(5);
}

void MainWindow::switchFilter7() {
  filterList->selectRow(6);
}

void MainWindow::switchFilter8() {
  filterList->selectRow(7);
}

void MainWindow::pageOrderingChanged(int idx) {
  if (m_ignorePageOrderingChanges) {
    return;
  }

  const int horScrollBarPos = thumbView->horizontalScrollBar()->value();
  const int verScrollBarPos = thumbView->verticalScrollBar()->value();

  m_stages->filterAt(m_curFilter)->selectPageOrder(idx);

  m_thumbSequence->reset(currentPageSequence(), ThumbnailSequence::KEEP_SELECTION, currentPageOrderProvider());

  if (!focusButton->isChecked()) {
    thumbView->horizontalScrollBar()->setValue(horScrollBarPos);
    thumbView->verticalScrollBar()->setValue(verScrollBarPos);
  }
}

void MainWindow::reloadRequested() {
  // Start loading / processing the current page.
  updateMainArea();
}

void MainWindow::outputDirectoryChanged(const QString& newDir) {
  // Empty string means switch back to default (temp) directory
  const QString effectiveDir = newDir.isEmpty() ? m_defaultOutDir : newDir;
  qDebug() << "Output directory changed to:" << effectiveDir;
  m_outFileNameGen.setOutDir(effectiveDir);
  // Recreate thumbnail cache for the new output directory
  if (!effectiveDir.isEmpty()) {
    m_thumbnailCache = Utils::createThumbnailCache(effectiveDir);
  }
}

void MainWindow::outputFormatSettingChanged(int format) {
  m_outFileNameGen.setOutputFormat(static_cast<OutputImageFormat>(format));
  qDebug() << "Output format setting changed to:" << format;
}

void MainWindow::tiffCompressionSettingChanged(int compression) {
  m_outFileNameGen.setTiffCompression(static_cast<OutputTiffCompression>(compression));
  qDebug() << "TIFF compression setting changed to:" << compression;
}

void MainWindow::jpegQualitySettingChanged(int quality) {
  m_outFileNameGen.setJpegQuality(quality);
  qDebug() << "JPEG quality setting changed to:" << quality;
}

void MainWindow::startBatchProcessing() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  // Export filter is control panel only - no batch processing
  if (m_curFilter == m_stages->exportFilterIdx()) {
    return;
  }

  m_interactiveQueue->cancelAndClear();

  // Check if we're at Output but not all pages have their content sizes defined.
  // If so, we need a two-pass batch: first run Page Layout to gather all sizes,
  // then automatically run Output with the complete aggregate size information.
  int effectiveFilter = m_curFilter;
  if (isOutputFilter() && !checkReadyForOutput()) {
    // Need two-pass batch: first run Page Layout only
    m_twoPassBatchInProgress = true;
    m_twoPassTargetFilter = m_curFilter;
    effectiveFilter = m_stages->pageLayoutFilterIdx();
  } else {
    m_twoPassBatchInProgress = false;
    m_twoPassTargetFilter = -1;
  }

  m_batchQueue = std::make_unique<ProcessingTaskQueue>();
  PageInfo page(m_thumbSequence->selectionLeader());
  for (; !page.isNull(); page = m_thumbSequence->nextPage(page.id())) {
    for (int i = 0; i < m_stages->count(); i++) {
      m_stages->filterAt(i)->loadDefaultSettings(page);
    }
    m_batchQueue->addProcessingTask(page, createCompositeTask(page, effectiveFilter, /*batch=*/true, m_debug));
  }

  startBatchTimer();
  focusButton->setChecked(true);

  removeFilterOptionsWidget();
  filterList->setBatchProcessingInProgress(true);
  filterList->setEnabled(false);

  // Initialize progress display
  if (m_batchProgressLabel) {
    m_batchProgressLabel->setText(tr("p. %1 / %2").arg(1).arg(m_batchQueue->totalPages()));
  }

  BackgroundTaskPtr task(m_batchQueue->takeForProcessing());
  if (task) {
    do {
      m_workerThreadPool->submitTask(task);
      if (!m_workerThreadPool->hasSpareCapacity()) {
        break;
      }
    } while ((task = m_batchQueue->takeForProcessing()));

    page = m_batchQueue->selectedPage();
    if (!page.isNull()) {
      m_thumbSequence->setSelection(page.id());
    }
    // Display the batch processing screen.
    updateMainArea();
  } else {
    // No tasks to process - stop immediately (this resets m_batchQueue)
    stopBatchProcessing();
  }
}  // MainWindow::startBatchProcessing

void MainWindow::startAutoMode() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) return;
  if (m_thumbSequence->firstPage().isNull()) return;

  AutoProcessDialog::ColorHandling colorHandling = AutoProcessDialog::ColorHandling::BestGuess;
  bool autoFastOcr = true;
  bool autoMultilingualOcr = false;
  bool autoSkipOcrLanguageCorrection = true;
  if (benchmark::benchmarkAutoEnabled()) {
    m_autoModeIncludeOcr = benchmark::benchmarkOcrEnabled();
    m_autoModeRedetectColor = true;
  } else {
    AutoProcessDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
      return;
    }
    m_autoModeIncludeOcr = dialog.includeOcr();
    m_autoModeRedetectColor = dialog.redetectColor();
    colorHandling = dialog.colorHandling();
    autoFastOcr = dialog.fastOcr();
    autoMultilingualOcr = dialog.multilingualOcr();
    autoSkipOcrLanguageCorrection = dialog.skipOcrLanguageCorrection();
  }

  if (m_autoModeIncludeOcr) {
    auto ocrSettings = m_stages->ocrFilter()->settings();
    ocrSettings->setOcrEnabled(true);
    if (benchmark::benchmarkAutoEnabled()) {
      bool accurateOk = false;
      const int accurate =
          qEnvironmentVariableIntValue("SCANTAILOR_BENCHMARK_OCR_ACCURATE", &accurateOk);
      ocrSettings->setUseAccurateRecognition(!accurateOk || accurate != 0);
      bool languageCorrectionOk = false;
      const int languageCorrection = qEnvironmentVariableIntValue(
          "SCANTAILOR_BENCHMARK_OCR_LANGUAGE_CORRECTION", &languageCorrectionOk);
      if (languageCorrectionOk) {
        ocrSettings->setUseLanguageCorrection(languageCorrection != 0);
      }
      ocrSettings->setLanguage(qEnvironmentVariable("SCANTAILOR_BENCHMARK_OCR_LANGUAGE"));
    } else {
      ocrSettings->setUseAccurateRecognition(!autoFastOcr);
      if (autoMultilingualOcr) {
        ocrSettings->setLanguage(QStringLiteral("auto"));
      } else if (ocrSettings->language() == QStringLiteral("auto")) {
        // Preserve an explicit primary language chosen on the OCR stage.
        ocrSettings->setLanguage(QString());
      }
      ocrSettings->setUseLanguageCorrection(!autoSkipOcrLanguageCorrection);
    }
  }

  auto finalizeSettings = m_stages->finalizeFilter()->settings();
  switch (colorHandling) {
    case AutoProcessDialog::ColorHandling::ForceBlackAndWhite:
      m_autoModeForceBlackAndWhite = true;
      finalizeSettings->setAutoColorModePolicy(finalize::AutoColorModePolicy::ForceBlackAndWhite);
      break;
    case AutoProcessDialog::ColorHandling::BlackAndWhiteAndGrayscale:
      m_autoModeForceBlackAndWhite = false;
      finalizeSettings->setAutoColorModePolicy(finalize::AutoColorModePolicy::NeverColor);
      break;
    case AutoProcessDialog::ColorHandling::BestGuess:
      m_autoModeForceBlackAndWhite = false;
      finalizeSettings->setAutoColorModePolicy(finalize::AutoColorModePolicy::BestGuess);
      break;
  }
  if (m_autoModeRedetectColor) {
    finalize::clearAutomaticColorDecisions(
        *finalizeSettings,
        *m_stages->outputFilter()->settings(),
        m_pages->toPageSequence(PAGE_VIEW));
  }
  applyAutoProcessColorSettings();

  m_autoStageElapsedMs.fill(0);
  m_autoTimingSummary.clear();
  m_autoTimingBreakdown.clear();
  m_timingStatusLabel->clear();
  ImageLoader::resetStatistics();
  m_autoModeTimer.start();
  m_autoModeStage = AUTO_PAGE_SPLIT;
  m_autoStageTimer.start();
  filterList->selectRow(m_stages->pageSplitFilterIdx());
  goFirstPage();
  startBatchProcessing();
}

void MainWindow::autoModeAdvance() {
  if (m_autoModeStage == AUTO_NONE) return;

  switch (m_autoModeStage) {
    case AUTO_PAGE_SPLIT:
      auto_accept::autoAcceptPageSplit(*m_stages, *m_pages, *m_batchSummaries);
      recordCurrentAutoStageTime();

      if (!benchmark::useLegacyAutoProcessSequence()) {
        // Auto Process has always discarded every detected deskew angle by
        // replacing it with manual zero. Set that policy now, then let the
        // Select Content composite run deskew pass-through, Page Box, and
        // content detection from one source load.
        auto_accept::autoSetDeskewZero(*m_stages, *m_pages, getCurrentView());
        m_autoModeStage = AUTO_SELECT_CONTENT;
        m_autoStageTimer.start();
        filterList->selectRow(m_stages->selectContentFilterIdx());
        goFirstPage();
        startBatchProcessing();
        break;
      }

      // A/B escape hatch: SCANTAILOR_LEGACY_AUTO_PROCESS_SEQUENCE=1 restores
      // the old separate Deskew and Page Box whole-book passes.
      m_autoModeStage = AUTO_DESKEW;
      m_autoStageTimer.start();
      filterList->selectRow(m_stages->deskewFilterIdx());
      goFirstPage();
      startBatchProcessing();
      break;

    case AUTO_DESKEW:
      auto_accept::autoSetDeskewZero(*m_stages, *m_pages, getCurrentView());
      recordCurrentAutoStageTime();
      m_autoModeStage = AUTO_PAGE_BOX;
      m_autoStageTimer.start();
      filterList->selectRow(m_stages->pageBoxFilterIdx());
      goFirstPage();
      startBatchProcessing();
      break;

    case AUTO_PAGE_BOX:
      // Legacy A/B sequence: Page Box was a separate whole-book pass.
      recordCurrentAutoStageTime();
      m_autoModeStage = AUTO_SELECT_CONTENT;
      m_autoStageTimer.start();
      filterList->selectRow(m_stages->selectContentFilterIdx());
      goFirstPage();
      startBatchProcessing();
      break;

    case AUTO_SELECT_CONTENT:
      auto_accept::autoAcceptContentOutliers(*m_stages, *m_pages, getCurrentView(), *m_batchSummaries);
      recordCurrentAutoStageTime();
      m_autoModeStage = AUTO_PAGE_LAYOUT;
      m_autoStageTimer.start();
      filterList->selectRow(m_stages->pageLayoutFilterIdx());
      goFirstPage();
      startBatchProcessing();
      break;

    case AUTO_PAGE_LAYOUT:
      auto_accept::autoAcceptPageSizeOutliers(*m_stages, *m_batchSummaries);
      // Page Split may have changed the set of PageIds since the run began.
      // Reapply the preset now so every final page is authoritative.
      if (m_autoModeRedetectColor) {
        finalize::clearAutomaticColorDecisions(
            *m_stages->finalizeFilter()->settings(),
            *m_stages->outputFilter()->settings(),
            m_pages->toPageSequence(PAGE_VIEW));
      }
      applyAutoProcessColorSettings();
      recordCurrentAutoStageTime();
      // The OCR composite already runs Output and hands Vision that exact
      // in-memory image. Going through a separate whole-book Output pass first
      // only makes OCR reload sources, rebuild FilterData, and reread TIFFs.
      m_autoModeStage = m_autoModeIncludeOcr ? AUTO_OCR : AUTO_OUTPUT;
      m_autoStageTimer.start();
      filterList->selectRow(
          m_autoModeIncludeOcr ? m_stages->ocrFilterIdx() : m_stages->outputFilterIdx());
      goFirstPage();
      startBatchProcessing();
      break;

    case AUTO_OUTPUT:
      // Retain this transition for interrupted or legacy Auto state, though
      // new OCR runs go directly from Layout to the fused OCR composite.
      if (m_autoModeIncludeOcr) {
        recordCurrentAutoStageTime();
        m_autoModeStage = AUTO_OCR;
        m_autoStageTimer.start();
        filterList->selectRow(m_stages->ocrFilterIdx());
        goFirstPage();
        startBatchProcessing();
      } else {
        finishAutoProcess(/*completed=*/true);
        QApplication::alert(this);
        if (m_checkBeepWhenFinished()) {
          QApplication::beep();
        }
        goFirstPage();
      }
      break;

    case AUTO_OCR:
      finishAutoProcess(/*completed=*/true);
      QApplication::alert(this);
      if (m_checkBeepWhenFinished()) {
        QApplication::beep();
      }
      goFirstPage();
      break;

    default:
      finishAutoProcess(/*completed=*/false);
      break;
  }
}

void MainWindow::startBatchTimer() {
  if (m_autoModeStage != AUTO_NONE && m_autoModeTimer.isValid()) {
    if (!m_autoStageTimer.isValid()) {
      m_autoStageTimer.start();
    }
  } else {
    m_timingStatusLabel->clear();
    m_batchTimer.start();
  }
}

void MainWindow::recordCurrentAutoStageTime() {
  if (m_autoModeStage == AUTO_NONE || !m_autoStageTimer.isValid()) {
    return;
  }
  m_autoStageElapsedMs[static_cast<size_t>(m_autoModeStage)] += m_autoStageTimer.elapsed();
  m_autoStageTimer.invalidate();
}

QString MainWindow::autoProcessBreakdown() const {
  const auto seconds = [this](const AutoModeStage stage) {
    return qRound64(m_autoStageElapsedMs[static_cast<size_t>(stage)] / 1000.0);
  };
  const qint64 contentMilliseconds
      = m_autoStageElapsedMs[AUTO_PAGE_BOX] + m_autoStageElapsedMs[AUTO_SELECT_CONTENT];

  QStringList stages{
      tr("Split %1 s").arg(seconds(AUTO_PAGE_SPLIT)),
      tr("Deskew %1 s").arg(seconds(AUTO_DESKEW)),
      tr("Content %1 s").arg(qRound64(contentMilliseconds / 1000.0)),
      tr("Margins %1 s").arg(seconds(AUTO_PAGE_LAYOUT)),
      tr("Output %1 s").arg(seconds(AUTO_OUTPUT))
  };
  if (m_autoModeIncludeOcr) {
    stages.append(tr("OCR %1 s").arg(seconds(AUTO_OCR)));
  }
  return stages.join(QStringLiteral(" \u00b7 "));
}

void MainWindow::finishAutoProcess(const bool completed) {
  if (!m_autoModeTimer.isValid()) {
    m_autoStageTimer.invalidate();
    m_autoModeStage = AUTO_NONE;
    m_autoModeIncludeOcr = false;
    m_autoTimingSummary.clear();
    m_autoTimingBreakdown.clear();
    resetAutoProcessColorPolicy();
    return;
  }

  recordCurrentAutoStageTime();
  const qint64 elapsedMilliseconds = m_autoModeTimer.elapsed();
  const size_t pageCount = m_pages ? m_pages->toPageSequence(PAGE_VIEW).numPages() : 0;
  const QString duration = core::formatDuration(elapsedMilliseconds);
  const double secondsPerPage = pageCount == 0
                                    ? 0.0
                                    : elapsedMilliseconds / 1000.0 / static_cast<double>(pageCount);
  m_autoTimingSummary = completed
      ? tr("Auto process completed in %1 \u2014 %2 pages, %3 s/page")
            .arg(duration)
            .arg(pageCount)
            .arg(secondsPerPage, 0, 'f', 2)
      : tr("Auto process stopped after %1 \u2014 %2 pages, %3 s/page")
            .arg(duration)
            .arg(pageCount)
            .arg(secondsPerPage, 0, 'f', 2);
  m_autoTimingBreakdown = autoProcessBreakdown();
  m_timingStatusLabel->setText(m_autoTimingSummary);

  const auto logStage = [this](const QString& name, const qint64 milliseconds) {
    qDebug().noquote() << QStringLiteral("Auto process timing: %1 %2 s")
                             .arg(name)
                             .arg(qRound64(milliseconds / 1000.0));
  };
  logStage(QStringLiteral("Split"), m_autoStageElapsedMs[AUTO_PAGE_SPLIT]);
  logStage(QStringLiteral("Deskew"), m_autoStageElapsedMs[AUTO_DESKEW]);
  logStage(QStringLiteral("Content"),
           m_autoStageElapsedMs[AUTO_PAGE_BOX] + m_autoStageElapsedMs[AUTO_SELECT_CONTENT]);
  logStage(QStringLiteral("Margins"), m_autoStageElapsedMs[AUTO_PAGE_LAYOUT]);
  logStage(QStringLiteral("Output"), m_autoStageElapsedMs[AUTO_OUTPUT]);
  if (m_autoModeIncludeOcr) {
    logStage(QStringLiteral("OCR"), m_autoStageElapsedMs[AUTO_OCR]);
  }
  qDebug().noquote() << QStringLiteral("Auto process timing: Total %1")
                           .arg(core::formatDuration(elapsedMilliseconds));
  const ImageLoader::Statistics imageLoadStats = ImageLoader::statistics();
  qDebug().noquote()
      << QStringLiteral(
             "Auto process image loads: cache hits %1, unique misses %2, leader decodes %3, "
             "coalesced waiters %4, PDF rasterizations %5, decoded %6 MiB")
             .arg(imageLoadStats.cacheHits)
             .arg(imageLoadStats.cacheMisses)
             .arg(imageLoadStats.leaderDecodes)
             .arg(imageLoadStats.coalescedWaiters)
             .arg(imageLoadStats.pdfRasterizations)
             .arg(imageLoadStats.decodedBytes / (1024.0 * 1024.0), 0, 'f', 1);
  benchmark::BenchmarkOcrMetrics ocrMetrics;
  if (benchmark::benchmarkAutoEnabled() && m_autoModeIncludeOcr) {
    ocrMetrics = benchmark::collectBenchmarkOcrMetrics(*m_stages, *m_pages);
  }
  if (benchmark::benchmarkAutoEnabled()) {
    benchmark::writeBenchmarkResult(
        completed, elapsedMilliseconds, pageCount, m_autoStageElapsedMs, imageLoadStats, ocrMetrics);
  }

  const bool showSummary = completed && !benchmark::benchmarkAutoEnabled();
  m_autoModeTimer.invalidate();
  m_autoStageTimer.invalidate();
  m_autoModeStage = AUTO_NONE;
  m_autoModeIncludeOcr = false;
  m_autoModeRedetectColor = false;
  resetAutoProcessColorPolicy();

  if (showSummary) {
    showBatchProcessingSummary();
  }
  m_autoTimingSummary.clear();
  m_autoTimingBreakdown.clear();
  if (benchmark::benchmarkAutoEnabled()) {
    // Qt's normal quit path can leave a GUI process resident under
    // LaunchServices even after the event loop is asked to stop. All worker
    // tasks are complete and the atomic result file is committed at this
    // point, so terminate the benchmark-only process deterministically.
    std::_Exit(EXIT_SUCCESS);
  }
}

void MainWindow::reportManualBatchCompletion(const qint64 elapsedMilliseconds, const size_t pageCount) {
  const QString duration = core::formatDuration(elapsedMilliseconds);
  const double secondsPerPage = pageCount == 0
                                    ? 0.0
                                    : elapsedMilliseconds / 1000.0 / static_cast<double>(pageCount);
  m_timingStatusLabel->setText(
      tr("Batch process completed in %1 \u2014 %2 pages, %3 s/page")
          .arg(duration)
          .arg(pageCount)
          .arg(secondsPerPage, 0, 'f', 2));
  m_batchTimer.invalidate();
}

void MainWindow::applyAutoProcessColorSettings() {
  if (!m_stages || !m_pages) {
    return;
  }

  const auto outputSettings = m_stages->outputFilter()->settings();
  const auto finalizeSettings = m_stages->finalizeFilter()->settings();
  const PageSequence pages = m_pages->toPageSequence(PAGE_VIEW);
  for (const PageInfo& pageInfo : pages) {
    output::Params params = outputSettings->getParams(pageInfo.id());
    output::ColorParams colorParams = params.colorParams();
    if (m_autoModeForceBlackAndWhite) {
      colorParams.setColorMode(output::BLACK_AND_WHITE);
      colorParams.setColorModeUserSet(true);
      colorParams.setColorModePresetSet(true);
      params.setColorParams(colorParams);
      // Store the complete Params returned by getParams(). On a missing
      // record this retains Settings' project-default DPI.
      outputSettings->setParams(pageInfo.id(), params);
      finalizeSettings->setColorMode(pageInfo.id(), finalize::ColorMode::BlackAndWhite);
    } else if (colorParams.isColorModePresetSet()) {
      colorParams.setColorModePresetSet(false);
      colorParams.setColorModeUserSet(false);
      params.setColorParams(colorParams);
      outputSettings->setParams(pageInfo.id(), params);
      // The B&W Finalize value came from the preset, not detection. Make only
      // those pages undecided so Best Guess / NeverColor can detect them.
      finalizeSettings->clearDetectionCacheForPage(pageInfo.id());
    }
  }
}

void MainWindow::resetAutoProcessColorPolicy() {
  m_autoModeForceBlackAndWhite = false;
  if (m_stages) {
    m_stages->finalizeFilter()->settings()->setAutoColorModePolicy(
        finalize::AutoColorModePolicy::BestGuess);
  }
}

void MainWindow::startBatchProcessingFrom(const PageInfo& startPage) {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  m_interactiveQueue->cancelAndClear();

  m_batchQueue = std::make_unique<ProcessingTaskQueue>();

  // Start from the specified page and add all subsequent pages
  PageInfo page = startPage;
  for (; !page.isNull(); page = m_thumbSequence->nextPage(page.id())) {
    for (int i = 0; i < m_stages->count(); i++) {
      m_stages->filterAt(i)->loadDefaultSettings(page);
    }
    m_batchQueue->addProcessingTask(page, createCompositeTask(page, m_curFilter, /*batch=*/true, m_debug));
  }

  startBatchTimer();
  focusButton->setChecked(true);

  removeFilterOptionsWidget();
  filterList->setBatchProcessingInProgress(true);
  filterList->setEnabled(false);

  // Initialize progress display
  if (m_batchProgressLabel) {
    m_batchProgressLabel->setText(tr("p. %1 / %2").arg(1).arg(m_batchQueue->totalPages()));
  }

  BackgroundTaskPtr task(m_batchQueue->takeForProcessing());
  if (task) {
    do {
      m_workerThreadPool->submitTask(task);
      if (!m_workerThreadPool->hasSpareCapacity()) {
        break;
      }
    } while ((task = m_batchQueue->takeForProcessing()));

    page = m_batchQueue->selectedPage();
    if (!page.isNull()) {
      m_thumbSequence->setSelection(page.id());
    }
    // Display the batch processing screen.
    updateMainArea();
  } else {
    // No tasks to process - stop immediately (this resets m_batchQueue)
    stopBatchProcessing();
  }
}  // MainWindow::startBatchProcessingFrom

void MainWindow::stopBatchProcessing(MainAreaAction mainArea, bool resetAutoMode) {
  // An auto stage handoff briefly has no batch queue while autoModeAdvance()
  // is pending. Reset timing and state before checking the queue so a stop in
  // that window also makes the queued advance a no-op.
  if (resetAutoMode) {
    if (m_autoModeStage != AUTO_NONE) {
      finishAutoProcess(/*completed=*/false);
    } else {
      m_autoModeTimer.invalidate();
      m_autoStageTimer.invalidate();
    }
    m_batchTimer.invalidate();
    m_autoModeStage = AUTO_NONE;
    m_autoModeIncludeOcr = false;
    resetAutoProcessColorPolicy();
  }

  if (!isBatchProcessingInProgress()) {
    return;
  }

  // Reset two-pass batch state
  m_twoPassBatchInProgress = false;
  m_twoPassTargetFilter = -1;

  const PageInfo page(m_batchQueue->selectedPage());
  if (!page.isNull()) {
    m_thumbSequence->setSelection(page.id());
  }

  m_batchQueue->cancelAndClear();
  m_batchQueue.reset();

  filterList->setBatchProcessingInProgress(false);
  filterList->setEnabled(true);

  switch (mainArea) {
    case UPDATE_MAIN_AREA:
      updateMainArea();
      break;
    case CLEAR_MAIN_AREA:
      removeImageWidget();
      break;
  }

  resetThumbSequence(currentPageOrderProvider());
}

void MainWindow::filterResult(const BackgroundTaskPtr& task, const FilterResultPtr& result) {
  // Cancelled or not, we must mark it as finished.
  m_interactiveQueue->processingFinished(task);
  if (m_batchQueue) {
    m_batchQueue->processingFinished(task);
    // Update progress display
    if (m_batchProgressLabel) {
      m_batchProgressLabel->setText(
          tr("p. %1 / %2").arg(m_batchQueue->processedPages() + 1).arg(m_batchQueue->totalPages()));
    }
  }

  if (task->isCancelled()) {
    return;
  }

  if (!isBatchProcessingInProgress()) {
    if (!result->filter()) {
      // Error loading file.  No special action is necessary.
    } else if (result->filter() != m_stages->filterAt(m_curFilter)) {
      // Error from one of the previous filters.
      const int idx = m_stages->findFilter(result->filter());
      assert(idx >= 0);
      m_curFilter = idx;

      ScopedIncDec<int> selectionGuard(m_ignoreSelectionChanges);
      filterList->selectRow(idx);
    }
  }

  // Keep the worker pool fed before doing thumbnail invalidation and other
  // GUI-thread work in updateUI().
  if (isBatchProcessingInProgress() && !m_batchQueue->allProcessed()) {
    do {
      const BackgroundTaskPtr nextTask(m_batchQueue->takeForProcessing());
      if (!nextTask) {
        break;
      }
      m_workerThreadPool->submitTask(nextTask);
    } while (m_workerThreadPool->hasSpareCapacity());
  }

  // This needs to be done even if batch processing is taking place,
  // for instance because thumbnail invalidation is done from here.
  result->updateUI(this);

  if (isBatchProcessingInProgress()) {
    if (m_batchQueue->allProcessed()) {
      // Check if we're in a two-pass batch and need to start the second pass
      if (m_twoPassBatchInProgress && m_twoPassTargetFilter >= 0) {
        // First pass (Page Layout) is complete. Now start the second pass (Output).
        const int targetFilter = m_twoPassTargetFilter;
        m_twoPassBatchInProgress = false;
        m_twoPassTargetFilter = -1;

        // Stop the current batch without alerts
        m_batchQueue->cancelAndClear();
        m_batchQueue.reset();

        // Build new batch queue for the target filter (Output)
        m_batchQueue = std::make_unique<ProcessingTaskQueue>();
        PageInfo page(m_thumbSequence->firstPage());
        for (; !page.isNull(); page = m_thumbSequence->nextPage(page.id())) {
          m_batchQueue->addProcessingTask(page, createCompositeTask(page, targetFilter, /*batch=*/true, m_debug));
        }

        // Update progress display for second pass
        if (m_batchProgressLabel) {
          m_batchProgressLabel->setText(tr("p. %1 / %2").arg(1).arg(m_batchQueue->totalPages()));
        }

        // Start processing
        BackgroundTaskPtr nextTask(m_batchQueue->takeForProcessing());
        if (nextTask) {
          do {
            m_workerThreadPool->submitTask(nextTask);
            if (!m_workerThreadPool->hasSpareCapacity()) {
              break;
            }
          } while ((nextTask = m_batchQueue->takeForProcessing()));
        }

        page = m_batchQueue->selectedPage();
        if (!page.isNull()) {
          m_thumbSequence->setSelection(page.id());
        }
        return;
      }

      // Auto mode: auto-accept and advance to next stage
      if (m_autoModeStage != AUTO_NONE) {
        stopBatchProcessing(UPDATE_MAIN_AREA, /*resetAutoMode=*/false);
        QTimer::singleShot(0, this, &MainWindow::autoModeAdvance);
        return;
      }

      // Check if we just finished processing certain filters
      const bool wasPageSplitBatch = (m_curFilter == m_stages->pageSplitFilterIdx());
      const bool wasPageBoxBatch = (m_curFilter == m_stages->pageBoxFilterIdx());
      const bool wasSelectContentBatch = (m_curFilter == m_stages->selectContentFilterIdx());
      const bool wasPageLayoutBatch = (m_curFilter == m_stages->pageLayoutFilterIdx());
      const size_t completedPageCount = m_batchQueue->totalPages();
      const qint64 batchElapsedMilliseconds = m_batchTimer.isValid() ? m_batchTimer.elapsed() : 0;

      qDebug() << "filterResult: batch complete, m_curFilter:" << m_curFilter
               << "pageBoxFilterIdx:" << m_stages->pageBoxFilterIdx()
               << "wasPageBoxBatch:" << wasPageBoxBatch
               << "pageLayoutFilterIdx:" << m_stages->pageLayoutFilterIdx()
               << "wasPageLayoutBatch:" << wasPageLayoutBatch;

      if (m_batchTimer.isValid()) {
        reportManualBatchCompletion(batchElapsedMilliseconds, completedPageCount);
      }
      stopBatchProcessing();

      QApplication::alert(this);  // Flash the taskbar entry.
      if (m_checkBeepWhenFinished()) {
#if defined(Q_OS_MACOS)
        QString extPlayCmd;  // Use QApplication::beep() on macOS
#elif defined(Q_OS_UNIX)
        QString extPlayCmd("play /usr/share/sounds/freedesktop/stereo/bell.oga");
#else
        QString extPlayCmd;
#endif
        QSettings settings;
        QString cmd = settings.value("main_window/external_alarm_cmd", extPlayCmd).toString();
        if (cmd.isEmpty()) {
          QApplication::beep();
        } else {
          const std::string cmdStr = cmd.toStdString();
          Q_UNUSED(std::system(cmdStr.c_str()));
        }
      }

      // Show batch processing summary for Page Split filter
      if (wasPageSplitBatch) {
        showBatchProcessingSummary();
      }

      // Show page box size summary for Page Box filter
      if (wasPageBoxBatch) {
        showPageBoxSummary();
      }

      // Show content coverage summary for Select Content filter
      if (wasSelectContentBatch) {
        showContentCoverageSummary();
      }

      // Show page size warning for Margins filter (if mixed page sizes detected)
      if (wasPageLayoutBatch) {
        showPageSizeWarning();
      }

      if (m_selectedPage.get(getCurrentView()) == m_thumbSequence->lastPage().id()) {
        // If batch processing finished at the last page, jump to the first one.
        goFirstPage();
      }
      return;
    }

    const PageInfo page(m_batchQueue->selectedPage());
    if (!page.isNull()) {
      m_thumbSequence->setSelection(page.id());
    }
  }
}  // MainWindow::filterResult

void MainWindow::debugToggled(const bool enabled) {
  m_debug = enabled;
}

void MainWindow::fixDpiDialogRequested() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  assert(!m_fixDpiDialog);
  m_fixDpiDialog = new FixDpiDialog(m_pages->toImageFileInfo(), this);
  m_fixDpiDialog->setAttribute(Qt::WA_DeleteOnClose);
  m_fixDpiDialog->setWindowModality(Qt::WindowModal);

  connect(m_fixDpiDialog, SIGNAL(accepted()), SLOT(fixedDpiSubmitted()));

  m_fixDpiDialog->show();
}

void MainWindow::fixedDpiSubmitted() {
  assert(m_fixDpiDialog);
  assert(m_pages);
  assert(m_thumbSequence);

  const PageInfo selectedPageBefore(m_thumbSequence->selectionLeader());

  m_pages->updateMetadataFrom(m_fixDpiDialog->files());

  // The thumbnail list also stores page metadata, including the DPI.
  m_thumbSequence->reset(currentPageSequence(), ThumbnailSequence::KEEP_SELECTION,
                         m_thumbSequence->pageOrderProvider());

  const PageInfo selectedPageAfter(m_thumbSequence->selectionLeader());

  // Reload if the current page was affected.
  // Note that imageId() isn't supposed to change - we check just in case.
  if ((selectedPageBefore.imageId() != selectedPageAfter.imageId())
      || (selectedPageBefore.metadata() != selectedPageAfter.metadata())) {
    reloadRequested();
  }
}

bool MainWindow::saveProjectTriggered() {
  if (m_projectFile.isEmpty() || !m_projectSavedToFolder) {
    return saveProjectAsTriggered();
  }

  // Already saved to a project folder - just update the project file
  if (saveProjectWithFeedback(m_projectFile)) {
    updateWindowTitle();
    return true;
  }
  return false;
}

bool MainWindow::saveProjectAsTriggered() {
  // Get suggested project name and starting directory
  QString suggestedName = suggestProjectName();

  QString startDir;
  if (!m_projectFolderPath.isEmpty()) {
    startDir = QFileInfo(m_projectFolderPath).absolutePath();
  } else {
    QSettings settings;
    startDir = settings.value("project/lastDir").toString();
    if (startDir.isEmpty()) {
      startDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
  }

  // Use a save file dialog - user picks location and names the folder in one step
  QString projectFolderPath = QFileDialog::getSaveFileName(
      this, tr("Save Project"),
      QDir(startDir).filePath(suggestedName),
      QString(),  // No filter - we're creating a folder
      nullptr,
      QFileDialog::ShowDirsOnly);

  if (projectFolderPath.isEmpty()) {
    return false;  // User cancelled
  }

  // Extract project name from the path
  QString projectName = QFileInfo(projectFolderPath).fileName();

  // Clean up name - remove characters that are problematic for filenames
  projectName.remove(QRegularExpression("[<>:\"/\\\\|?*]"));

  if (projectName.isEmpty()) {
    QMessageBox::warning(this, tr("Invalid Name"), tr("Please enter a valid project name."));
    return false;
  }

  // Reconstruct path with cleaned name
  QString parentDir = QFileInfo(projectFolderPath).absolutePath();
  projectFolderPath = QDir(parentDir).filePath(projectName);

  // Check if folder already exists
  if (QDir(projectFolderPath).exists()) {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Folder Exists"),
        tr("A folder named '%1' already exists. Overwrite?").arg(projectName),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
      return false;
    }
  }

  if (saveProjectToFolder(projectFolderPath)) {
    m_projectFolderPath = projectFolderPath;
    m_projectFile = ProjectFolder(projectFolderPath).projectFilePath();
    m_projectSavedToFolder = true;
    updateWindowTitle();

    QSettings settings;
    settings.setValue("project/lastDir", parentDir);

    RecentProjects rp;
    rp.read();
    rp.setMostRecent(m_projectFile);
    rp.write();
    return true;
  }
  return false;
}  // MainWindow::saveProjectAsTriggered

void MainWindow::exportToPdf() {
  m_pdfExportFlow->exportToPdf();
}

void MainWindow::exportToPdfFromFilter() {
  m_pdfExportFlow->exportToPdfFromFilter();
}

PageSequence MainWindow::exportPageSequence() const {
  return m_thumbSequence->toPageSequence();
}

const OutputFileNameGenerator& MainWindow::outFileNameGen() const {
  return m_outFileNameGen;
}

void MainWindow::newProject() {
  if (!closeProjectInteractive()) {
    return;
  }

#ifdef Q_OS_MAC
  // On macOS, emit signal to show startup window instead of dialog
  emit newProjectRequested();
#else
  // It will delete itself when it's done.
  auto* context = new ProjectCreationContext(this);
  connect(context, SIGNAL(done(ProjectCreationContext*)), this, SLOT(newProjectCreated(ProjectCreationContext*)));
#endif
}

void MainWindow::importPdf() {
  QSettings settings;
  QString lastDir = settings.value("lastInputDir").toString();
  if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
    lastDir = QDir::homePath();
  }

  const QString pdfPath = QFileDialog::getOpenFileName(
      this, tr("Import PDF"), lastDir, tr("PDF Files (*.pdf)"));

  if (pdfPath.isEmpty()) {
    return;
  }

  // Save the directory for next time
  settings.setValue("lastInputDir", QFileInfo(pdfPath).absolutePath());

  // Detect PDF info and show import dialog
  PdfReader::PdfInfo pdfInfo = PdfReader::readPdfInfo(pdfPath);
  if (pdfInfo.pageCount == 0) {
    QMessageBox::warning(this, tr("Error"), PdfReadError::message(pdfPath));
    return;
  }

  PdfImportDialog dialog(this, pdfPath, pdfInfo.pageCount, pdfInfo.detectedDpi);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  // Store the selected import DPI for this PDF
  PdfReader::setImportDpi(pdfPath, dialog.selectedDpi());

#ifdef Q_OS_MAC
  // On macOS, if we have a project loaded, import in a new window
  if (isProjectLoaded()) {
    auto* newWindow = new MainWindow(false);  // Skip geometry restore for cascading
    newWindow->setAttribute(Qt::WA_DeleteOnClose, true);
    // Cascade: offset new window by 22px (macOS title bar height)
    const int cascade = 22;
    QPoint newPos = pos() + QPoint(cascade, cascade);
    QSize newSize = size();
    // Check if window would go off screen, reset to top-left if so
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
      QRect screenGeom = screen->availableGeometry();
      if (newPos.x() + newSize.width() > screenGeom.right() ||
          newPos.y() + newSize.height() > screenGeom.bottom()) {
        newPos = screenGeom.topLeft() + QPoint(cascade, cascade);
      }
    }
    newWindow->setGeometry(newPos.x(), newPos.y(), newSize.width(), newSize.height());
    newWindow->show();
    newWindow->importPdfFile(pdfPath);
    return;
  }
#else
  if (!closeProjectInteractive()) {
    return;
  }
#endif

  // Load PDF metadata to get page count and sizes (uses stored import DPI)
  std::vector<ImageFileInfo> files;
  const ImageMetadataLoader::Status status = ImageMetadataLoader::load(
      pdfPath, [&](const ImageMetadata& metadata) {
        // For each page in the PDF, create an ImageFileInfo entry
        // The page number will be encoded in the metadata
        files.emplace_back(QFileInfo(pdfPath), std::vector<ImageMetadata>{metadata});
      });

  if (status != ImageMetadataLoader::LOADED || files.empty()) {
    QMessageBox::warning(this, tr("Error"), tr("Failed to load PDF file."));
    return;
  }

  // Consolidate all pages into a single file entry with multiple pages
  std::vector<ImageMetadata> allPages;
  for (const auto& file : files) {
    for (const auto& meta : file.imageInfo()) {
      allPages.push_back(meta);
    }
  }

  std::vector<ImageFileInfo> consolidatedFiles;
  consolidatedFiles.emplace_back(QFileInfo(pdfPath), allPages);

  // Use temp directory for output (cleaned on project close/quit)
  QString outDir = finalize::Settings::getTempOutputDir(pdfPath);

  // Create project pages
  auto pages = std::make_shared<ProjectPages>(consolidatedFiles, ProjectPages::AUTO_PAGES, Qt::LeftToRight);
  switchToNewProject(pages, outDir);

  // Set output DPI to match PDF import DPI (preserves original scan resolution)
  const int importDpi = PdfReader::getImportDpi(pdfPath);
  if (m_stages && m_stages->outputFilter()) {
    m_stages->outputFilter()->setDefaultDpi(Dpi(importDpi, importDpi));
  }
}

void MainWindow::newProjectCreated(ProjectCreationContext* context) {
  auto pages = std::make_shared<ProjectPages>(context->files(), ProjectPages::AUTO_PAGES, context->layoutDirection());
  switchToNewProject(pages, context->outDir());
}

void MainWindow::importPdfFile(const QString& pdfPath) {
  importPdfFileToProject(pdfPath, QString());
}

bool MainWindow::importPdfFileToProject(const QString& pdfPath, const QString& projectDirectory) {
  // Load PDF metadata to get page count and sizes
  std::vector<ImageFileInfo> files;
  const ImageMetadataLoader::Status status = ImageMetadataLoader::load(pdfPath, [&](const ImageMetadata& metadata) {
    files.emplace_back(QFileInfo(pdfPath), std::vector<ImageMetadata>{metadata});
  });

  if (status != ImageMetadataLoader::LOADED || files.empty()) {
    QMessageBox::warning(this, tr("Error"), tr("Failed to load PDF file."));
    return false;
  }

  // Consolidate all pages into a single file entry with multiple pages
  std::vector<ImageMetadata> allPages;
  for (const auto& file : files) {
    for (const auto& meta : file.imageInfo()) {
      allPages.push_back(meta);
    }
  }

  std::vector<ImageFileInfo> consolidatedFiles;
  consolidatedFiles.emplace_back(QFileInfo(pdfPath), allPages);

  // Use temp directory for output
  QString outDir = finalize::Settings::getTempOutputDir(pdfPath);

  // Create project pages
  auto pages = std::make_shared<ProjectPages>(consolidatedFiles, ProjectPages::AUTO_PAGES, Qt::LeftToRight);
  switchToNewProject(pages, outDir);

  // Set output DPI to match PDF import DPI (preserves original scan resolution)
  const int importDpi = PdfReader::getImportDpi(pdfPath);
  if (m_stages && m_stages->outputFilter()) {
    m_stages->outputFilter()->setDefaultDpi(Dpi(importDpi, importDpi));
  }

  if (projectDirectory.isEmpty()) {
    return true;
  }

  if (!saveProjectToFolder(projectDirectory)) {
    return false;
  }

  m_projectFolderPath = QFileInfo(projectDirectory).absoluteFilePath();
  m_projectFile = ProjectFolder(m_projectFolderPath).projectFilePath();
  m_projectSavedToFolder = true;
  updateWindowTitle();

  // Re-open the freshly written file through the normal project reader so the
  // launch path and a later resume have identical in-memory state.
  openProject(m_projectFile);
  return true;
}

void MainWindow::startBenchmarkAutoProcess() {
  if (!benchmark::benchmarkAutoEnabled()) {
    return;
  }
  startAutoMode();
}

void MainWindow::createProjectFromFiles(const QString& inputDir,
                                        const QString& outputDir,
                                        const std::vector<ImageFileInfo>& files,
                                        bool rtl,
                                        bool fixDpi) {
  Q_UNUSED(inputDir)
  Q_UNUSED(fixDpi)

  Qt::LayoutDirection layoutDir = rtl ? Qt::RightToLeft : Qt::LeftToRight;
  auto pages = std::make_shared<ProjectPages>(files, ProjectPages::AUTO_PAGES, layoutDir);
  switchToNewProject(pages, outputDir);
}

void MainWindow::openProject() {
  const QString projectDir(QSettings().value("project/lastDir").toString());
  const QString projectFile(QFileDialog::getOpenFileName(this, tr("Open Project"), projectDir,
                                                         tr("Scan Tailor Projects") + " (*.ScanTailor)"));
  if (projectFile.isEmpty()) {
    // Cancelled by user.
    return;
  }

#ifdef Q_OS_MAC
  // On macOS, if we have a project loaded, open in a new window
  if (isProjectLoaded()) {
    auto* newWindow = new MainWindow(false);  // Skip geometry restore for cascading
    newWindow->setAttribute(Qt::WA_DeleteOnClose, true);
    // Cascade: offset new window by 22px (macOS title bar height)
    const int cascade = 22;
    QPoint newPos = pos() + QPoint(cascade, cascade);
    QSize newSize = size();
    // Check if window would go off screen, reset to top-left if so
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
      QRect screenGeom = screen->availableGeometry();
      if (newPos.x() + newSize.width() > screenGeom.right() ||
          newPos.y() + newSize.height() > screenGeom.bottom()) {
        newPos = screenGeom.topLeft() + QPoint(cascade, cascade);
      }
    }
    newWindow->setGeometry(newPos.x(), newPos.y(), newSize.width(), newSize.height());
    newWindow->show();
    newWindow->openProject(projectFile);
    return;
  }
#else
  if (!closeProjectInteractive()) {
    return;
  }
#endif

  openProject(projectFile);
}

void MainWindow::openProject(const QString& projectFile) {
  QFile file(projectFile);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Error"), tr("Unable to open the project file."));
    return;
  }

  QDomDocument doc;
  if (!doc.setContent(&file)) {
    QMessageBox::warning(this, tr("Error"), tr("The project file is broken."));
    return;
  }

  file.close();

  auto* context = new ProjectOpeningContext(this, projectFile, doc);
  connect(context, SIGNAL(done(ProjectOpeningContext*)), SLOT(projectOpened(ProjectOpeningContext*)));
  context->proceed();
}

void MainWindow::projectOpened(ProjectOpeningContext* context) {
  RecentProjects rp;
  rp.read();
  rp.setMostRecent(context->projectFile());
  rp.write();

  QSettings().setValue("project/lastDir", QFileInfo(context->projectFile()).absolutePath());

  switchToNewProject(context->projectReader()->pages(), context->projectReader()->outputDirectory(),
                     context->projectFile(), context->projectReader());
}

void MainWindow::closeProject() {
  closeProjectInteractive();
}

void MainWindow::openSettingsDialog() {
  auto* dialog = new SettingsDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  connect(dialog, SIGNAL(settingsChanged()), this, SLOT(onSettingsChanged()));
  dialog->show();
}

void MainWindow::openDefaultParamsDialog() {
  auto* dialog = new DefaultParamsDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  dialog->show();
}

void MainWindow::onSettingsChanged() {
  ApplicationSettings& settings = ApplicationSettings::getInstance();
  bool needInvalidate = true;

  static_cast<Application*>(qApp)->installLanguage(settings.getLanguage());

  if (m_thumbnailCache) {
    const QSize maxThumbSize = settings.getThumbnailQuality();
    if (m_thumbnailCache->getMaxThumbSize() != maxThumbSize) {
      m_thumbnailCache->setMaxThumbSize(maxThumbSize);
    }
  }

  updateThumbnailViewMode();

  const QSizeF maxLogicalThumbSize = settings.getMaxLogicalThumbnailSize();
  if (m_maxLogicalThumbSize != maxLogicalThumbSize) {
    m_maxLogicalThumbSize = maxLogicalThumbSize;
    updateMaxLogicalThumbSize();
    needInvalidate = false;
  }

  if (needInvalidate) {
    m_thumbSequence->invalidateAllThumbnails();
  }
}

void MainWindow::showAboutDialog() {
  Ui::AboutDialog ui;
  auto* dialog = new QDialog(this);
  ui.setupUi(dialog);
  ui.version->setText(QString(tr("version ")) + QString::fromUtf8(VERSION) +
                      QString(" (") + QString::fromUtf8(BUILD_TIMESTAMP) + QString(")"));

  QResource license(":/GPLv3.html");
  ui.licenseViewer->setHtml(QString::fromUtf8((const char*) license.data(), static_cast<int>(license.size())));

  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  dialog->show();
}

void MainWindow::revealZoteroPlugin() {
  ZoteroPluginInstaller::reveal(this);
}

/**
 * This function is called asynchronously, always from the main thread.
 */
void MainWindow::handleOutOfMemorySituation() {
  deleteLater();

  m_outOfMemoryDialog->setParams(m_projectFile, m_stages, m_pages, m_selectedPage, m_outFileNameGen);

  closeProjectWithoutSaving();

  m_outOfMemoryDialog->setAttribute(Qt::WA_DeleteOnClose);
  m_outOfMemoryDialog.release()->show();
}

void MainWindow::quitApp() {
  m_quitting = true;
  close();
}

/**
 * Note: the removed widgets are not deleted.
 */
void MainWindow::removeWidgetsFromLayout(QLayout* layout) {
  QLayoutItem* child;
  while ((child = layout->takeAt(0))) {
    delete child;
  }
}

void MainWindow::removeFilterOptionsWidget() {
  removeWidgetsFromLayout(m_optionsFrameLayout);
  // Delete the old widget we were owning, if any.
  m_optionsWidgetCleanup.clear();

  m_optionsWidget = nullptr;
  filterOptions->setMinimumHeight(0);
  filterOptions->setMaximumHeight(QWIDGETSIZE_MAX);
  filterDockWidget->setMinimumWidth(m_filterDockBaseMinimumWidth);
}

void MainWindow::updateProjectActions() {
  const bool loaded = isProjectLoaded();
  actionSaveProject->setEnabled(loaded);
  actionSaveProjectAs->setEnabled(loaded);
  actionFixDpi->setEnabled(loaded);
  actionRelinking->setEnabled(loaded);
  actionAutoMode->setEnabled(loaded);
}

bool MainWindow::isBatchProcessingInProgress() const {
  return m_batchQueue != nullptr;
}

bool MainWindow::isProjectLoaded() const {
  return !m_outFileNameGen.outDir().isEmpty();
}

bool MainWindow::isBelowSelectContent() const {
  return isBelowSelectContent(m_curFilter);
}

bool MainWindow::isBelowSelectContent(const int filterIdx) const {
  return filterIdx > m_stages->selectContentFilterIdx();
}

bool MainWindow::isBelowFixOrientation(int filterIdx) const {
  return filterIdx > m_stages->fixOrientationFilterIdx();
}

bool MainWindow::isOutputFilter() const {
  return isOutputFilter(m_curFilter);
}

bool MainWindow::isOutputFilter(const int filterIdx) const {
  // Output filter and Export filter both require the same prerequisites
  // (pages must have content sizes determined through Page Layout processing)
  return filterIdx == m_stages->outputFilterIdx() || filterIdx == m_stages->exportFilterIdx();
}

bool MainWindow::isFinalizeOrOutputFilter() const {
  return m_stages &&
         (m_curFilter == m_stages->finalizeFilterIdx() || m_curFilter == m_stages->outputFilterIdx());
}

PageView MainWindow::getCurrentView() const {
  return m_stages->filterAt(m_curFilter)->getView();
}

void MainWindow::updateMainArea() {
  if (m_pages->numImages() == 0) {
    filterList->setBatchProcessingPossible(false);
    setDockWidgetsVisible(false);
    showNewOpenProjectPanel();
    m_statusBarPanel->clear();
  } else if (isBatchProcessingInProgress()) {
    filterList->setBatchProcessingPossible(false);
    setImageWidget(m_batchProcessingWidget.get(), KEEP_OWNERSHIP);
  } else {
    setDockWidgetsVisible(true);
    const PageInfo page(m_thumbSequence->selectionLeader());
    if (page.isNull()) {
      filterList->setBatchProcessingPossible(false);
      removeImageWidget();
      removeFilterOptionsWidget();
    } else {
      // Note that loadPageInteractive may reset it to false.
      filterList->setBatchProcessingPossible(true);
      PageSequence pageSequence = m_thumbSequence->toPageSequence();
      if (pageSequence.numPages() > 0) {
        m_statusBarPanel->updatePage(pageSequence.pageNo(page.id()) + 1, pageSequence.numPages(), page.id());
      }
      loadPageInteractive(page);
    }
  }
}

bool MainWindow::checkReadyForOutput(const PageId* ignore) const {
  return m_stages->pageLayoutFilter()->checkReadyForOutput(*m_pages, ignore);
}

void MainWindow::loadPageInteractive(const PageInfo& page) {
  assert(!isBatchProcessingInProgress());

  m_interactiveQueue->cancelAndClear();

  if (isOutputFilter() && !checkReadyForOutput(&page.id())) {
    filterList->setBatchProcessingPossible(false);

    const QString errText(
        tr("Output is not yet possible, as the final size"
           " of pages is not yet known.\nTo determine it,"
           " run batch processing at \"Select Content\" or"
           " \"Margins\"."));

    removeFilterOptionsWidget();
    setImageWidget(new ErrorWidget(errText), TRANSFER_OWNERSHIP);
    return;
  }

  // Export filter has no batch processing - it only has the "Export to PDF" button
  if (m_curFilter == m_stages->exportFilterIdx()) {
    filterList->setBatchProcessingPossible(false);
  }

  for (int i = 0; i < m_stages->count(); i++) {
    m_stages->filterAt(i)->loadDefaultSettings(page);
  }

  if (!isBatchProcessingInProgress()) {
    // Show processing spinner while the page's view is built. The Export stage
    // uses the same per-page pipeline as every other stage so that clicking a
    // thumbnail navigates and renders that page (its Task delegates to the
    // cached OCR/Output result).
    if (m_imageFrameLayout->indexOf(m_processingIndicationWidget.get()) != -1) {
      m_processingIndicationWidget->processingRestartedEffect();
    }
    bool currentWidgetIsImage = (Utils::castOrFindChild<ImageViewBase*>(m_imageFrameLayout->widget(0)) != nullptr);
    setImageWidget(m_processingIndicationWidget.get(), KEEP_OWNERSHIP, nullptr, currentWidgetIsImage);
    m_stages->filterAt(m_curFilter)->preUpdateUI(this, page);
  }

  assert(m_thumbnailCache);

  m_interactiveQueue->cancelAndClear();
  m_interactiveQueue->addProcessingTask(page, createCompositeTask(page, m_curFilter, false, m_debug));
  m_workerThreadPool->submitTask(m_interactiveQueue->takeForProcessing());
}  // MainWindow::loadPageInteractive

void MainWindow::updateWindowTitle() {
  QString projectName;

  if (m_projectFile.isEmpty()) {
    projectName = tr("Unnamed");
  } else {
    projectName = QFileInfo(m_projectFile).completeBaseName();
  }
  const QString version(QString::fromUtf8(VERSION));
  setWindowTitle(tr("%2 - ScanTailor Spectre [%1bit]").arg(sizeof(void*) * 8).arg(projectName));
}

/**
 * \brief Closes the currently project, prompting to save it if necessary.
 *
 * \return true if the project was closed, false if the user cancelled the process.
 */
bool MainWindow::closeProjectInteractive() {
  if (!isProjectLoaded()) {
    return true;
  }

  if (m_projectFile.isEmpty()) {
    switch (promptProjectSave()) {
      case SAVE:
        if (!saveProjectTriggered()) {
          return false;  // User cancelled save dialog
        }
        break;
      case DONT_SAVE:
        break;
      case CANCEL:
        return false;
    }
    closeProjectWithoutSaving();
    return true;
  }

  const QFileInfo projectFile(m_projectFile);
  const QFileInfo backupFile(projectFile.absoluteDir(), QString::fromLatin1("Backup.") + projectFile.fileName());
  const QString backupFilePath(backupFile.absoluteFilePath());

  ProjectWriter writer(m_pages, m_selectedPage, m_outFileNameGen);

  if (!writer.write(backupFilePath, m_stages->filters())) {
    // Backup file could not be written???
    QFile::remove(backupFilePath);
    switch (promptProjectSave()) {
      case SAVE:
        if (!saveProjectTriggered()) {
          return false;  // User cancelled save dialog
        }
        break;
      case DONT_SAVE:
        break;
      case CANCEL:
        return false;
    }
    closeProjectWithoutSaving();
    return true;
  }

  if (compareFiles(m_projectFile, backupFilePath)) {
    // The project hasn't really changed.
    QFile::remove(backupFilePath);
    closeProjectWithoutSaving();
    return true;
  }

  switch (promptProjectSave()) {
    case SAVE:
      if (!Utils::overwritingRename(backupFilePath, m_projectFile)) {
        QMessageBox::warning(this, tr("Error"), tr("Error saving the project file!"));
        return false;
      }
      // fall through
    case DONT_SAVE:
      QFile::remove(backupFilePath);
      break;
    case CANCEL:
      return false;
  }

  closeProjectWithoutSaving();
  return true;
}  // MainWindow::closeProjectInteractive

void MainWindow::closeProjectWithoutSaving() {
  // Clean up temp output files if not preserving them
  temp_cleanup::cleanupTempOutputFiles(m_stages.get(), m_defaultOutDir, this);

  auto pages = std::make_shared<ProjectPages>();
  switchToNewProject(pages, QString());
}

bool MainWindow::saveProjectWithFeedback(const QString& projectFile) {
  ProjectWriter writer(m_pages, m_selectedPage, m_outFileNameGen);

  if (!writer.write(projectFile, m_stages->filters())) {
    QMessageBox::warning(this, tr("Error"), tr("Error saving the project file!"));
    return false;
  }
  return true;
}

bool MainWindow::saveProjectToFolder(const QString& folderPath) {
  ProjectFolder projectFolder(folderPath);
  const QString previousOutputDir = m_outFileNameGen.outDir();

  // Create folder structure
  if (!projectFolder.create()) {
    QMessageBox::warning(this, tr("Error"), tr("Failed to create project folder structure."));
    return false;
  }

  // Get all pages to copy originals
  const PageSequence pages = m_pages->toPageSequence(IMAGE_VIEW);
  if (pages.numPages() == 0) {
    QMessageBox::warning(this, tr("Error"), tr("No pages in project."));
    return false;
  }

  // Build list of unique source files (handle multi-page files)
  QMap<QString, QString> pathMapping;  // old path -> new path
  QStringList sourceFiles;
  for (const PageInfo& page : pages) {
    const QString originalPath = page.imageId().filePath();
    if (!pathMapping.contains(originalPath)) {
      pathMapping[originalPath] = QString();  // Placeholder, will be filled during copy
      sourceFiles.append(originalPath);
    }
  }

  // Copy original images with progress dialog
  QProgressDialog progress(tr("Copying original images..."), tr("Cancel"), 0, sourceFiles.size(), this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(500);

  int i = 0;
  for (const QString& sourcePath : sourceFiles) {
    if (progress.wasCanceled()) {
      return false;
    }

    QString newPath = projectFolder.copyOriginal(sourcePath);
    if (newPath.isEmpty()) {
      QMessageBox::warning(this, tr("Error"), tr("Failed to copy file: %1").arg(sourcePath));
      return false;
    }
    pathMapping[sourcePath] = newPath;
    progress.setValue(++i);
  }
  progress.setValue(sourceFiles.size());

  progress.setLabelText(tr("Copying generated output..."));
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  if (!projectFolder.copyOutputFrom(previousOutputDir)) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Failed to copy generated output into the project folder."));
    return false;
  }

  // Create relinker with the path mappings
  auto relinker = std::make_shared<ProjectFolderRelinker>();
  for (auto it = pathMapping.constBegin(); it != pathMapping.constEnd(); ++it) {
    relinker->addMapping(it.key(), it.value());
  }

  // Apply relinking to update all paths
  performRelinking(relinker);

  // Update output directory to project folder's output directory
  m_outFileNameGen.setOutDir(projectFolder.outputDir());

  // Update thumbnail cache for new output directory
  if (!projectFolder.outputDir().isEmpty()) {
    m_thumbnailCache = Utils::createThumbnailCache(projectFolder.outputDir());
  }

  // Save the project file before removing any temporary data.
  if (!saveProjectWithFeedback(projectFolder.projectFilePath())) {
    return false;
  }

  if (temp_cleanup::isSpectreTempOutputDir(previousOutputDir)
      && QDir::cleanPath(previousOutputDir) != QDir::cleanPath(projectFolder.outputDir())) {
    QDir(previousOutputDir).removeRecursively();
  }
  m_defaultOutDir = projectFolder.outputDir();
  return true;
}

QString MainWindow::suggestProjectName() const {
  // Use first input file name as base, or "Untitled"
  const PageSequence pages = m_pages->toPageSequence(IMAGE_VIEW);
  if (pages.numPages() > 0) {
    const PageInfo& firstPage = pages.pageAt(0);
    QString baseName = QFileInfo(firstPage.imageId().filePath()).completeBaseName();
    // Clean up the name - remove non-alphanumeric characters except underscore and hyphen
    baseName.remove(QRegularExpression("[^a-zA-Z0-9_-]"));
    if (!baseName.isEmpty()) {
      return baseName + "_project";
    }
  }
  return "Untitled_project";
}

QString MainWindow::defaultPdfExportPath(const BookMetadata& metadata) const {
  QFileInfo sourceInfo(m_sourceDocumentPath);
  QString directory;

  if (sourceInfo.exists()) {
    directory = sourceInfo.absolutePath();
    if (!m_projectFolderPath.isEmpty()) {
      const QString relativeSource = QDir(m_projectFolderPath).relativeFilePath(sourceInfo.absoluteFilePath());
      if (relativeSource == QStringLiteral("originals")
          || relativeSource.startsWith(QStringLiteral("originals/"))) {
        directory = m_projectFolderPath;
      }
    }
  } else if (!m_projectFile.isEmpty()) {
    directory = QFileInfo(m_projectFile).absolutePath();
  } else {
    directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  }

  QString sourceBase = sourceInfo.completeBaseName();
  if (sourceBase.isEmpty() && !m_projectFile.isEmpty()) {
    sourceBase = QFileInfo(m_projectFile).completeBaseName();
  }
  if (sourceBase.isEmpty()) {
    sourceBase = QStringLiteral("output");
  }

  QString fileName;
  if (ApplicationSettings::getInstance().isPdfRecommendedNameEnabled()
      && !metadata.title.trimmed().isEmpty()) {
    fileName = buildRecommendedPdfFileName(metadata, sourceBase + QStringLiteral("_processed"));
  } else {
    fileName = sourceBase + QStringLiteral("_processed.pdf");
  }
  return QDir(directory).filePath(fileName);
}

/**
 * Note: showInsertFileDialog(BEFORE, ImageId()) is legal and means inserting at the end.
 */
void MainWindow::showInsertFileDialog(BeforeOrAfter beforeOrAfter, const ImageId& existing) {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }
  // We need to filter out files already in project.
  class ProxyModel : public QSortFilterProxyModel {
   public:
    explicit ProxyModel(const ProjectPages& pages) {
      setDynamicSortFilter(true);

      const PageSequence sequence(pages.toPageSequence(IMAGE_VIEW));
      for (const PageInfo& page : sequence) {
        m_inProjectFiles.push_back(QFileInfo(page.imageId().filePath()));
      }
    }

   protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
      const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
      const QVariant data(idx.data(QFileSystemModel::FilePathRole));
      if (data.isNull()) {
        return true;
      }
      return !m_inProjectFiles.contains(QFileInfo(data.toString()));
    }

    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override { return left.row() < right.row(); }

   private:
    QFileInfoList m_inProjectFiles;
  };


  auto dialog
      = std::make_unique<QFileDialog>(this, tr("Files to insert"), QFileInfo(existing.filePath()).absolutePath());
  dialog->setFileMode(QFileDialog::ExistingFiles);
  dialog->setProxyModel(new ProxyModel(*m_pages));
  dialog->setNameFilter(tr("Images not in project (%1)").arg("*.png *.tiff *.tif *.jpeg *.jpg *.jp2 *.heic *.heif *.webp *.bmp *.gif *.pbm *.pgm *.ppm"));
  // XXX: Adding individual pages from a multi-page TIFF where
  // some of the pages are already in project is not supported right now.
  if (dialog->exec() != QDialog::Accepted) {
    return;
  }

  QStringList files(dialog->selectedFiles());
  if (files.empty()) {
    return;
  }

  // The order of items returned by QFileDialog is platform-dependent,
  // so we enforce our own ordering.
  std::sort(files.begin(), files.end(), SmartFilenameOrdering());

  // I suspect on some platforms it may be possible to select the same file twice,
  // so to be safe, remove duplicates.
  files.erase(std::unique(files.begin(), files.end()), files.end());


  std::vector<ImageFileInfo> newFiles;
  std::vector<QString> loadedFiles;
  std::vector<QString> failedFiles;  // Those we failed to read metadata from.
  // dialog->selectedFiles() returns file list in reverse order.
  for (int i = files.size() - 1; i >= 0; --i) {
    const QFileInfo fileInfo(files[i]);
    ImageFileInfo imageFileInfo(fileInfo, std::vector<ImageMetadata>());

    const ImageMetadataLoader::Status status = ImageMetadataLoader::load(
        files.at(i), [&](const ImageMetadata& metadata) { imageFileInfo.imageInfo().push_back(metadata); });

    if (status == ImageMetadataLoader::LOADED) {
      newFiles.push_back(imageFileInfo);
      loadedFiles.push_back(fileInfo.absoluteFilePath());
    } else {
      failedFiles.push_back(fileInfo.absoluteFilePath());
    }
  }

  if (!failedFiles.empty()) {
    auto errDialog = std::make_unique<LoadFilesStatusDialog>(this);
    errDialog->setLoadedFiles(loadedFiles);
    errDialog->setFailedFiles(failedFiles);
    errDialog->setOkButtonName(QString(" %1 ").arg(tr("Skip failed files")));
    if ((errDialog->exec() != QDialog::Accepted) || loadedFiles.empty()) {
      return;
    }
  }

  // Check if there is at least one DPI that's not OK.
  if (std::find_if(newFiles.begin(), newFiles.end(), [&](const ImageFileInfo& p) -> bool { return !p.isDpiOK(); })
      != newFiles.end()) {
    auto dpiDialog = std::make_unique<FixDpiDialog>(newFiles, this);
    dpiDialog->setWindowModality(Qt::WindowModal);
    if (dpiDialog->exec() != QDialog::Accepted) {
      return;
    }

    newFiles = dpiDialog->files();
  }

  // Actually insert the new pages.
  for (const ImageFileInfo& file : newFiles) {
    int imageNum = -1;  // Zero-based image number in a multi-page TIFF.
    for (const ImageMetadata& metadata : file.imageInfo()) {
      ++imageNum;

      const int numSubPages = ProjectPages::adviseNumberOfLogicalPages(metadata, OrthogonalRotation());
      const ImageInfo imageInfo(ImageId(file.fileInfo(), imageNum), metadata, numSubPages, false, false);
      insertImage(imageInfo, beforeOrAfter, existing);
    }
  }
}  // MainWindow::showInsertFileDialog

void MainWindow::showRemovePagesDialog(const std::set<PageId>& pages) {
  auto dialog = std::make_unique<QDialog>(this);
  Ui::RemovePagesDialog ui;
  ui.setupUi(dialog.get());
  ui.icon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxQuestion).pixmap(48, 48));

  ui.text->setText(ui.text->text().arg(pages.size()));

  QPushButton* removeBtn = ui.buttonBox->button(QDialogButtonBox::Ok);
  removeBtn->setText(tr("Remove"));

  dialog->setWindowModality(Qt::WindowModal);
  if (dialog->exec() == QDialog::Accepted) {
    removeFromProject(pages);
    eraseOutputFiles(pages);
  }
}

void MainWindow::forcePageSplitLayout(page_split::LayoutType layoutType) {
  const std::set<PageId> selectedPages = m_thumbSequence->selectedItems();
  if (selectedPages.empty()) {
    return;
  }

  // Apply the layout type to all selected pages using UpdateAction
  auto settings = m_stages->pageSplitFilter()->settings();
  for (const PageId& pageId : selectedPages) {
    page_split::Settings::UpdateAction updateAction;
    updateAction.setLayoutType(layoutType);
    settings->updatePage(pageId.imageId(), updateAction);
  }

  // Invalidate thumbnails and reload
  m_thumbSequence->invalidateAllThumbnails();
  reloadRequested();
}

void MainWindow::setColorModeForSelectedPages(output::ColorMode mode) {
  const std::set<PageId> pages = selectedPages();
  if (!pages.empty() && m_stages) {
    auto outputSettings = m_stages->outputFilter()->settings();
    auto finalizeSettings = m_stages->finalizeFilter()->settings();

    // Map output::ColorMode to finalize::ColorMode
    finalize::ColorMode finalizeMode;
    switch (mode) {
      case output::BLACK_AND_WHITE:
        finalizeMode = finalize::ColorMode::BlackAndWhite;
        break;
      case output::GRAYSCALE:
        finalizeMode = finalize::ColorMode::Grayscale;
        break;
      case output::COLOR:
      case output::MIXED:
      default:
        finalizeMode = finalize::ColorMode::Color;
        break;
    }

    for (const PageId& pageId : pages) {
      // Update Output settings
      output::ColorParams colorParams = outputSettings->getParams(pageId).colorParams();
      colorParams.setColorMode(mode);
      colorParams.setColorModeUserSet(true);
      outputSettings->setColorParams(pageId, colorParams);

      // Update Finalize settings
      finalizeSettings->setColorMode(pageId, finalizeMode);
    }
    for (const PageId& pageId : pages) {
      m_thumbSequence->invalidateThumbnail(pageId);
    }

    // reloadRequested() only re-dispatches the current page. When several
    // pages were converted, re-render all of them now through the same
    // selected-pages batch path the Output options panel uses
    // (batchProcessingRequested -> batchProcessPages). That path refuses to
    // start while another batch is running, so fall back to the current-page
    // reload in that case rather than queuing a second batch.
    if (pages.size() > 1 && !isBatchProcessingInProgress()) {
      batchProcessPages(pages);
    } else {
      reloadRequested();
    }
  }
}

/**
 * Note: insertImage(..., BEFORE, ImageId()) is legal and means inserting at the end.
 */
void MainWindow::insertImage(const ImageInfo& newImage, BeforeOrAfter beforeOrAfter, ImageId existing) {
  std::vector<PageInfo> pages(m_pages->insertImage(newImage, beforeOrAfter, existing, getCurrentView()));

  if (beforeOrAfter == BEFORE) {
    // The second one will be inserted first, then the first
    // one will be inserted BEFORE the second one.
    std::reverse(pages.begin(), pages.end());
  }

  for (const PageInfo& pageInfo : pages) {
    m_outFileNameGen.disambiguator()->registerFile(pageInfo.imageId().filePath());
    m_thumbSequence->insert(pageInfo, beforeOrAfter, existing);
    existing = pageInfo.imageId();
  }
}

void MainWindow::removeFromProject(const std::set<PageId>& pages) {
  m_interactiveQueue->cancelAndRemove(pages);
  if (m_batchQueue) {
    m_batchQueue->cancelAndRemove(pages);
  }

  m_pages->removePages(pages);


  const PageSequence itemsInOrder = m_thumbSequence->toPageSequence();
  std::set<PageId> newSelection;

  bool selectFirstNonDeleted = false;
  if (itemsInOrder.numPages() > 0) {
    // if first page was deleted select first not deleted page
    // otherwise select last not deleted page from beginning
    selectFirstNonDeleted = pages.find(itemsInOrder.pageAt(0).id()) != pages.end();

    PageId lastNonDeleted;
    for (const PageInfo& page : itemsInOrder) {
      const PageId& id = page.id();
      const bool wasDeleted = (pages.find(id) != pages.end());

      if (!wasDeleted) {
        if (selectFirstNonDeleted) {
          m_thumbSequence->setSelection(id);
          newSelection.insert(id);
          break;
        } else {
          lastNonDeleted = id;
        }
      } else if (!selectFirstNonDeleted && !lastNonDeleted.isNull()) {
        m_thumbSequence->setSelection(lastNonDeleted);
        newSelection.insert(lastNonDeleted);
        break;
      }
    }

    m_thumbSequence->removePages(pages);

    if (newSelection.empty()) {
      // fallback to old behaviour
      if (m_thumbSequence->selectionLeader().isNull()) {
        m_thumbSequence->setSelection(m_thumbSequence->firstPage().id());
      }
    }
  }

  updateMainArea();
}  // MainWindow::removeFromProject

void MainWindow::eraseOutputFiles(const std::set<PageId>& pages) {
  std::vector<PageId::SubPage> eraseVariations;
  eraseVariations.reserve(3);

  for (const PageId& pageId : pages) {
    eraseVariations.clear();
    switch (pageId.subPage()) {
      case PageId::SINGLE_PAGE:
        eraseVariations.push_back(PageId::SINGLE_PAGE);
        eraseVariations.push_back(PageId::LEFT_PAGE);
        eraseVariations.push_back(PageId::RIGHT_PAGE);
        break;
      case PageId::LEFT_PAGE:
        eraseVariations.push_back(PageId::SINGLE_PAGE);
        eraseVariations.push_back(PageId::LEFT_PAGE);
        break;
      case PageId::RIGHT_PAGE:
        eraseVariations.push_back(PageId::SINGLE_PAGE);
        eraseVariations.push_back(PageId::RIGHT_PAGE);
        break;
    }

    for (PageId::SubPage subpage : eraseVariations) {
      QFile::remove(m_outFileNameGen.filePathFor(PageId(pageId.imageId(), subpage)));
    }
  }
}

BackgroundTaskPtr MainWindow::createCompositeTask(const PageInfo& page,
                                                  const int lastFilterIdx,
                                                  const bool batch,
                                                  bool debug) {
  std::shared_ptr<fix_orientation::Task> fixOrientationTask;
  std::shared_ptr<page_split::Task> pageSplitTask;
  std::shared_ptr<deskew::Task> deskewTask;
  std::shared_ptr<page_box::Task> pageBoxTask;
  std::shared_ptr<select_content::Task> selectContentTask;
  std::shared_ptr<page_layout::Task> pageLayoutTask;
  std::shared_ptr<finalize::Task> finalizeTask;
  std::shared_ptr<output::Task> outputTask;
  std::shared_ptr<ocr::Task> ocrTask;
  std::shared_ptr<export_::Task> exportTask;

  if (batch) {
    debug = false;
  }

  if (lastFilterIdx >= m_stages->exportFilterIdx()) {
    // Chain: Output -> OCR -> Export
    outputTask = m_stages->outputFilter()->createTask(page.id(), m_thumbnailCache, m_outFileNameGen, batch, debug);
    ocrTask = m_stages->ocrFilter()->createTask(page.id(), outputTask, m_outFileNameGen, batch);
    exportTask = m_stages->exportFilter()->createTask(page.id(), ocrTask, batch);
    debug = false;
  } else if (lastFilterIdx >= m_stages->ocrFilterIdx()) {
    // Chain: Output -> OCR (no export)
    outputTask = m_stages->outputFilter()->createTask(page.id(), m_thumbnailCache, m_outFileNameGen, batch, debug);
    ocrTask = m_stages->ocrFilter()->createTask(page.id(), outputTask, m_outFileNameGen, batch);
    debug = false;
  } else if (lastFilterIdx >= m_stages->outputFilterIdx()) {
    outputTask = m_stages->outputFilter()->createTask(page.id(), m_thumbnailCache, m_outFileNameGen, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_stages->finalizeFilterIdx()) {
    // Pass the END of the chain to finalize (Export/OCR if selected, otherwise output)
    std::shared_ptr<AbstractOutputTask> nextTask;
    if (exportTask) {
      nextTask = exportTask;
    } else if (ocrTask) {
      nextTask = ocrTask;
    } else {
      nextTask = outputTask;
    }
    finalizeTask = m_stages->finalizeFilter()->createTask(page.id(), nextTask,
                                                          m_stages->outputFilter()->settings(), batch);
  }
  if (lastFilterIdx >= m_stages->pageLayoutFilterIdx()) {
    pageLayoutTask = m_stages->pageLayoutFilter()->createTask(page.id(), finalizeTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_stages->selectContentFilterIdx()) {
    selectContentTask = m_stages->selectContentFilter()->createTask(page.id(), pageLayoutTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_stages->pageBoxFilterIdx()) {
    pageBoxTask = m_stages->pageBoxFilter()->createTask(page.id(), selectContentTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_stages->deskewFilterIdx()) {
    deskewTask = m_stages->deskewFilter()->createTask(page.id(), pageBoxTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_stages->pageSplitFilterIdx()) {
    pageSplitTask = m_stages->pageSplitFilter()->createTask(page, deskewTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_stages->fixOrientationFilterIdx()) {
    fixOrientationTask = m_stages->fixOrientationFilter()->createTask(page.id(), pageSplitTask, batch);
    debug = false;
  }
  assert(fixOrientationTask);
  return std::make_shared<LoadFileTask>(batch ? BackgroundTask::BATCH : BackgroundTask::INTERACTIVE, page,
                                        m_thumbnailCache, m_pages, fixOrientationTask);
}  // MainWindow::createCompositeTask

std::shared_ptr<CompositeCacheDrivenTask> MainWindow::createCompositeCacheDrivenTask(const int lastFilterIdx) {
  std::shared_ptr<fix_orientation::CacheDrivenTask> fixOrientationTask;
  std::shared_ptr<page_split::CacheDrivenTask> pageSplitTask;
  std::shared_ptr<deskew::CacheDrivenTask> deskewTask;
  std::shared_ptr<page_box::CacheDrivenTask> pageBoxTask;
  std::shared_ptr<select_content::CacheDrivenTask> selectContentTask;
  std::shared_ptr<page_layout::CacheDrivenTask> pageLayoutTask;
  std::shared_ptr<finalize::CacheDrivenTask> finalizeTask;
  std::shared_ptr<output::CacheDrivenTask> outputTask;
  std::shared_ptr<ocr::CacheDrivenTask> ocrTask;
  std::shared_ptr<export_::CacheDrivenTask> exportTask;

  if (lastFilterIdx >= m_stages->exportFilterIdx()) {
    // Chain: Output -> OCR -> Export for thumbnails
    outputTask = m_stages->outputFilter()->createCacheDrivenTask(m_outFileNameGen);
    ocrTask = m_stages->ocrFilter()->createCacheDrivenTask(outputTask, m_outFileNameGen,
                                                           m_stages->outputFilter()->settings());
    exportTask = m_stages->exportFilter()->createCacheDrivenTask(ocrTask);
  } else if (lastFilterIdx >= m_stages->ocrFilterIdx()) {
    // Chain: Output -> OCR for thumbnails
    outputTask = m_stages->outputFilter()->createCacheDrivenTask(m_outFileNameGen);
    ocrTask = m_stages->ocrFilter()->createCacheDrivenTask(outputTask, m_outFileNameGen,
                                                           m_stages->outputFilter()->settings());
  } else if (lastFilterIdx >= m_stages->outputFilterIdx()) {
    outputTask = m_stages->outputFilter()->createCacheDrivenTask(m_outFileNameGen);
  }
  if (lastFilterIdx >= m_stages->finalizeFilterIdx()) {
    // Pass the END of the chain to finalize (Export/OCR if selected, otherwise output)
    std::shared_ptr<AbstractCacheDrivenOutputTask> nextTask;
    if (exportTask) {
      nextTask = exportTask;
    } else if (ocrTask) {
      nextTask = ocrTask;
    } else {
      nextTask = outputTask;
    }
    finalizeTask = m_stages->finalizeFilter()->createCacheDrivenTask(nextTask);
  }
  if (lastFilterIdx >= m_stages->pageLayoutFilterIdx()) {
    pageLayoutTask = m_stages->pageLayoutFilter()->createCacheDrivenTask(finalizeTask);
  }
  if (lastFilterIdx >= m_stages->selectContentFilterIdx()) {
    selectContentTask = m_stages->selectContentFilter()->createCacheDrivenTask(pageLayoutTask);
  }
  if (lastFilterIdx >= m_stages->pageBoxFilterIdx()) {
    pageBoxTask = m_stages->pageBoxFilter()->createCacheDrivenTask(selectContentTask);
  }
  if (lastFilterIdx >= m_stages->deskewFilterIdx()) {
    deskewTask = m_stages->deskewFilter()->createCacheDrivenTask(pageBoxTask);
  }
  if (lastFilterIdx >= m_stages->pageSplitFilterIdx()) {
    pageSplitTask = m_stages->pageSplitFilter()->createCacheDrivenTask(deskewTask);
  }
  if (lastFilterIdx >= m_stages->fixOrientationFilterIdx()) {
    fixOrientationTask = m_stages->fixOrientationFilter()->createCacheDrivenTask(pageSplitTask);
  }

  assert(fixOrientationTask);
  return fixOrientationTask;
}  // MainWindow::createCompositeCacheDrivenTask

void MainWindow::updateDisambiguationRecords(const PageSequence& pages) {
  for (const PageInfo& page : pages) {
    m_outFileNameGen.disambiguator()->registerFile(page.imageId().filePath());
  }
}

PageSelectionAccessor MainWindow::newPageSelectionAccessor() {
  auto provider = std::make_shared<PageSelectionProviderImpl>(this);
  return PageSelectionAccessor(provider);
}

void MainWindow::changeEvent(QEvent* event) {
  if (event != nullptr) {
    switch (event->type()) {
      case QEvent::LanguageChange:
        retranslateUi(this);
        updateWindowTitle();
        break;
      default:
        QWidget::changeEvent(event);
        break;
    }
  }
}

void MainWindow::toggleFullBleedForSelectedPages() {
  if (!m_stages) {
    return;
  }

  const std::set<PageId> pages = selectedPages();
  if (pages.empty()) {
    return;
  }

  auto settings = m_stages->pageLayoutFilter()->settings();
  const bool fullBleed = !settings->isPageFullBleed(*pages.begin());
  for (const PageId& pageId : pages) {
    settings->setPageFullBleed(pageId, fullBleed);
  }

  m_thumbSequence->invalidateAllThumbnails();
  reloadRequested();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
  // Full bleed is a page-layout setting that deliberately bypasses margins
  // and match-size aggregation.  Keep its unmodified "F" shortcut scoped to
  // the stage where the checkbox and its effect are visible; accepting it in
  // Finalize / Output made an ordinary key press silently remove standardized
  // margins from the selected pages.
  const bool isPageLayout = m_stages && m_curFilter == m_stages->pageLayoutFilterIdx();
  const bool commandModifierPressed
      = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
  if (isPageLayout && !commandModifierPressed && event->text().toLower() == "f") {
    toggleFullBleedForSelectedPages();
    event->accept();
    return;
  }

  // Color mode shortcuts for Finalize (stage 6) and Output (stage 7) filters
  // Use text() for keyboard layout independence (Dvorak, etc.)
  if (isFinalizeOrOutputFilter()) {
    const bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
    const QString text = event->text().toLower();

    // Shift+C/M/G/B: Toggle page filter buttons
    if (shiftPressed) {
      if (text == "c") {
        filterColorBtn->toggle();
        event->accept();
        return;
      } else if (text == "g") {
        filterGrayBtn->toggle();
        event->accept();
        return;
      } else if (text == "m") {
        filterMixedBtn->toggle();
        event->accept();
        return;
      } else if (text == "b") {
        filterBwBtn->toggle();
        event->accept();
        return;
      }
    } else {
      // c/g/b: Change color mode for selected pages
      output::ColorMode newMode;
      bool validKey = true;

      if (text == "c") {
        newMode = output::COLOR;
      } else if (text == "g") {
        newMode = output::GRAYSCALE;
      } else if (text == "b") {
        newMode = output::BLACK_AND_WHITE;
      } else {
        validKey = false;
      }

      if (validKey) {
        setColorModeForSelectedPages(newMode);
        event->accept();
        return;
      }

      // p: Toggle pass-through (Output stage only, but accept in both Finalize/Output to prevent fallthrough)
      if (text == "p") {
        if (m_curFilter == m_stages->outputFilterIdx()) {
          const std::set<PageId> pages = selectedPages();
          if (!pages.empty()) {
            auto outputSettings = m_stages->outputFilter()->settings();
            // Toggle based on first selected page's current state
            const PageId& firstPage = *pages.begin();
            const bool newState = !outputSettings->getOutputProcessingParams(firstPage).passThrough();
            for (const PageId& pageId : pages) {
              output::OutputProcessingParams opp = outputSettings->getOutputProcessingParams(pageId);
              opp.setPassThrough(newState);
              outputSettings->setOutputProcessingParams(pageId, opp);
              m_thumbSequence->invalidateThumbnail(pageId);
            }
            reloadRequested();
          }
        }
        // Accept 'p' in both Finalize and Output to prevent event fallthrough
        event->accept();
        return;
      }
    }
  }

  QMainWindow::keyPressEvent(event);
}

void MainWindow::setDockWidgetsVisible(bool state) {
  filterDockWidget->setVisible(state);
  thumbnailsDockWidget->setVisible(state);
}

void MainWindow::scaleThumbnails(int scaleFactor) {
  const double dx = 25.0 * scaleFactor;
  const double dy = 16.0 * scaleFactor;
  const double width = qBound(100.0, m_maxLogicalThumbSize.width() + dx, 1000.0);
  const double height = qBound(64.0, m_maxLogicalThumbSize.height() + dy, 640.0);
  m_maxLogicalThumbSize = QSizeF(width, height);
  if (!m_maxLogicalThumbSizeUpdater.isActive()) {
    m_maxLogicalThumbSizeUpdater.start(350);
  }
  ApplicationSettings::getInstance().setMaxLogicalThumbnailSize(m_maxLogicalThumbSize);
}

void MainWindow::updateMaxLogicalThumbSize() {
  m_thumbSequence->setMaxLogicalThumbSize(m_maxLogicalThumbSize);
  updateThumbViewMinWidth();
  resetThumbSequence(currentPageOrderProvider(), ThumbnailSequence::KEEP_SELECTION);
}

void MainWindow::setupIcons() {
  auto& iconProvider = IconProvider::getInstance();
  focusButton->setIcon(iconProvider.getIcon("eye"));
  prevPageBtn->setIcon(iconProvider.getIcon("triangle-up-arrow"));
  nextPageBtn->setIcon(iconProvider.getIcon("triangle-down-arrow"));
  filterSelectedBtn->setIcon(iconProvider.getIcon("check-mark"));
  gotoPageBtn->setIcon(iconProvider.getIcon("right-pointing"));
  selectionModeBtn->setIcon(iconProvider.getIcon("checkbox-styled"));
  thumbColumnViewBtn->setIcon(iconProvider.getIcon("column-view"));
  sortingOrderBtn->setIcon(iconProvider.getIcon("sorting-order"));
  deviationHighlightingBtn->setIcon(iconProvider.getIcon("six-spoked-asterisk"));
  diminishThumbnailsBtn->setIcon(iconProvider.getIcon("diminishing-glass"));
  magnifyThumbnailsBtn->setIcon(iconProvider.getIcon("magnifying-glass"));
}

void MainWindow::execGotoPageDialog() {
  if (isBatchProcessingInProgress() || !isProjectLoaded()) {
    return;
  }

  bool ok;
  const PageSequence pageSequence = m_thumbSequence->toPageSequence();
  const PageId& selectionLeader = m_thumbSequence->selectionLeader().id();
  int pageNumber
      = QInputDialog::getInt(this, tr("Go To Page"), tr("Enter the page number:"),
                             pageSequence.pageNo(selectionLeader) + 1, 1, (int) (pageSequence.numPages()), 1, &ok);
  if (ok) {
    const PageId& newSelectionLeader = pageSequence.pageAt(pageNumber - 1).id();
    if (selectionLeader != newSelectionLeader) {
      goToPage(newSelectionLeader);
    }
  }
}

void MainWindow::updateThumbnailViewMode() {
  const ThumbnailSequence::ViewMode viewMode
      = (ApplicationSettings::getInstance().isSingleColumnThumbnailDisplayEnabled()) ? ThumbnailSequence::SINGLE_COLUMN
                                                                                     : ThumbnailSequence::MULTI_COLUMN;
  if (m_thumbSequence->getViewMode() != viewMode) {
    m_thumbSequence->setViewMode(viewMode);
  }
}

void MainWindow::updateAutoSaveTimer() {
  if (m_autoSaveTimer.remainingTime() <= 0) {
    m_autoSaveTimer.start(60000);
  }
}

PageSequence MainWindow::currentPageSequence() {
  PageSequence pageSequence = m_pages->toPageSequence(getCurrentView());
  if (sortingOrderBtn->isChecked()) {
    std::reverse(pageSequence.begin(), pageSequence.end());
  }

  // Apply color mode filter if any filter button is unchecked
  const finalize::ThumbnailColorModeFilter colorModeFilter(
      filterBwBtn->isChecked(), filterGrayBtn->isChecked(), filterMixedBtn->isChecked(),
      filterColorBtn->isChecked());

  // If all filters are on, no filtering needed
  if (colorModeFilter.includesAll()) {
    return pageSequence;
  }

  // Get finalize settings to check color modes
  if (!m_stages) {
    return pageSequence;
  }
  auto finalizeSettings = m_stages->finalizeFilter()->settings();
  if (!finalizeSettings) {
    return pageSequence;
  }

  // Filter the page sequence based on color mode
  PageSequence filteredSequence;
  for (const PageInfo& pageInfo : pageSequence) {
    finalize::ColorMode colorMode = finalizeSettings->getColorMode(pageInfo.id());
    if (colorModeFilter.includes(colorMode)) {
      filteredSequence.append(pageInfo);
    }
  }

  return filteredSequence;
}

void MainWindow::reloadCurrentPage() {
  if (isBatchProcessingInProgress() || !isProjectLoaded())
    return;

  updateMainArea();
}

void MainWindow::showBatchProcessingSummary() {
  m_batchSummaries->showBatchProcessingSummary(m_autoTimingSummary, m_autoTimingBreakdown);
}

void MainWindow::forceTwoPageForImages(const std::vector<ImageId>& imageIds) {
  m_batchSummaries->forceTwoPageForImages(imageIds);
}

void MainWindow::forceSinglePageForImages(const std::vector<ImageId>& imageIds) {
  m_batchSummaries->forceSinglePageForImages(imageIds);
}

void MainWindow::showPageBoxSummary() {
  m_batchSummaries->showPageBoxSummary();
}

void MainWindow::showContentCoverageSummary() {
  m_batchSummaries->showContentCoverageSummary();
}

void MainWindow::preserveLayoutForPages(const std::vector<PageId>& pageIds) {
  m_batchSummaries->preserveLayoutForPages(pageIds);
}

void MainWindow::showPageSizeWarning() {
  m_batchSummaries->showPageSizeWarning();
}

void MainWindow::disableAlignmentForPages(const std::vector<PageId>& pageIds) {
  m_batchSummaries->disableAlignmentForPages(pageIds);
}

StageSequence* MainWindow::stages() const {
  return m_stages.get();
}

ProjectPages* MainWindow::pages() const {
  return m_pages.get();
}

ThumbnailSequence* MainWindow::thumbSequence() const {
  return m_thumbSequence.get();
}

PageView MainWindow::currentView() const {
  return getCurrentView();
}

int MainWindow::currentFilterIndex() const {
  return m_curFilter;
}

void MainWindow::jumpToPage(const PageId& pageId) {
  goToPage(pageId);
}

void MainWindow::selectFilterListRow(int row) {
  filterList->selectRow(row);
}

