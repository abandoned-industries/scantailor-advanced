// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_MAINWINDOW_H_
#define SCANTAILOR_APP_MAINWINDOW_H_

#include <QMainWindow>
#include <QElapsedTimer>
#include <QObjectCleanupHandler>
#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <functional>
#include <array>
#include <memory>
#include <set>
#include <vector>

#include "AbstractCommand.h"
#include "BackgroundTask.h"
#include "BeforeOrAfter.h"
#include "FilterResult.h"
#include "FilterUiInterface.h"
#include "ImageFileInfo.h"
#include "NonCopyable.h"
#include "OutputFileNameGenerator.h"
#include "PageId.h"
#include "PageRange.h"
#include "PageView.h"
#include "SelectedPage.h"
#include "StatusBarPanel.h"
#include "ThumbnailSequence.h"
#include "filters/output/ColorParams.h"
#include "filters/page_split/LayoutType.h"
#include "ui_MainWindow.h"

class AbstractFilter;
class AbstractRelinker;
class ThumbnailPixmapCache;
class ProjectPages;
class PageSequence;
class StageSequence;
class PageOrderProvider;
class PageSelectionAccessor;
class FilterOptionsWidget;
class ProcessingIndicationWidget;
class ImageInfo;
class ImageViewBase;
class PageInfo;
class QLabel;
class QStackedLayout;
class WorkerThreadPool;
class ProjectReader;
class DebugImages;
class ContentBoxPropagator;
class PageOrientationPropagator;
class ProjectCreationContext;
class ProjectOpeningContext;
class CompositeCacheDrivenTask;
class TabbedDebugImages;
class ProcessingTaskQueue;
class FixDpiDialog;
class OutOfMemoryDialog;
class QLineF;
class QRectF;
class QLayout;
class BatchProcessingSummaryDialog;
struct BookMetadata;

class MainWindow : public QMainWindow, private FilterUiInterface, private Ui::MainWindow {
  DECLARE_NON_COPYABLE(MainWindow)

  Q_OBJECT
 public:
  explicit MainWindow(bool restoreGeometry = true);

  ~MainWindow() override;

  PageSequence allPages() const;

  std::set<PageId> selectedPages() const;

  std::vector<PageRange> selectedRanges() const;

 signals:
  void projectClosed();
  void newProjectRequested();
  void quitRequested();

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override;

  void closeEvent(QCloseEvent* event) override;

  void timerEvent(QTimerEvent* event) override;

  void changeEvent(QEvent* event) override;

  void keyPressEvent(QKeyEvent* event) override;

 public slots:

  void openProject(const QString& projectFile);

  void newProject();

  void importPdf();

  void openProject();

  void importPdfFile(const QString& pdfPath);

  bool importPdfFileToProject(const QString& pdfPath, const QString& projectDirectory);

  void startBenchmarkAutoProcess();

  void createProjectFromFiles(const QString& inputDir,
                              const QString& outputDir,
                              const std::vector<ImageFileInfo>& files,
                              bool rtl,
                              bool fixDpi);

 private:
  enum MainAreaAction { UPDATE_MAIN_AREA, CLEAR_MAIN_AREA };

 private slots:

  void autoSaveProject();

  void goFirstPage();

  void goLastPage();

  void goNextPage();

  void goPrevPage();

  void goNextSelectedPage();

  void goPrevSelectedPage();

  void execGotoPageDialog();

  void goToPage(const PageId& pageId,
                ThumbnailSequence::SelectionAction selectionAction = ThumbnailSequence::RESET_SELECTION);

  void currentPageChanged(const PageInfo& pageInfo, const QRectF& thumbRect, ThumbnailSequence::SelectionFlags flags);

  void pageContextMenuRequested(const PageInfo& pageInfo, const QPoint& screenPos, bool selected);

  void pastLastPageContextMenuRequested(const QPoint& screenPos);

  void thumbViewFocusToggled(bool checked);

  void thumbViewScrolled();

  void filterSelectionChanged(const QItemSelection& selected);

  void switchFilter1();

  void switchFilter2();

  void switchFilter3();

  void switchFilter4();

  void switchFilter5();

  void switchFilter6();

  void switchFilter7();

  void switchFilter8();

  void pageOrderingChanged(int idx);

  void reloadRequested();

  void outputDirectoryChanged(const QString& newDir);

  void outputFormatSettingChanged(int format);
  void tiffCompressionSettingChanged(int compression);
  void jpegQualitySettingChanged(int quality);

  void startBatchProcessing();

  void startAutoMode();

  void startBatchProcessingFrom(const PageInfo& startPage);

  void stopBatchProcessing(MainAreaAction mainArea = UPDATE_MAIN_AREA, bool resetAutoMode = true);

  void invalidateThumbnail(const PageId& pageId) override;

  void invalidateThumbnail(const PageInfo& pageInfo);

  void invalidateAllThumbnails() override;

  void batchProcessPages(const std::set<PageId>& pages);

  void showRelinkingDialog();

  void filterResult(const BackgroundTaskPtr& task, const FilterResultPtr& result);

  void debugToggled(bool enabled);

  void fixDpiDialogRequested();

  void fixedDpiSubmitted();

  bool saveProjectTriggered();

  bool saveProjectAsTriggered();

  void exportToPdf();

  void exportToPdfFromFilter();

  void newProjectCreated(ProjectCreationContext* context);

  void projectOpened(ProjectOpeningContext* context);

  void closeProject();

  void openSettingsDialog();

  void openDefaultParamsDialog();

  void onSettingsChanged();

  void showAboutDialog();

  void revealZoteroPlugin();

  void handleOutOfMemorySituation();

  void reloadCurrentPage();

 public slots:
  void quitApp();

 private:
  class PageSelectionProviderImpl;

  enum SavePromptResult { SAVE, DONT_SAVE, CANCEL };

  using FilterPtr = std::shared_ptr<AbstractFilter>;

  static void removeWidgetsFromLayout(QLayout* layout);

  void setOptionsWidget(FilterOptionsWidget* widget, Ownership ownership) override;

  void setImageWidget(QWidget* widget,
                      Ownership ownership,
                      DebugImages* debugImages = nullptr,
                      bool overlay = false) override;

  std::shared_ptr<AbstractCommand<void>> relinkingDialogRequester() override;

  void switchToNewProject(const std::shared_ptr<ProjectPages>& pages,
                          const QString& outDir,
                          const QString& projectFilePath = QString(),
                          const ProjectReader* projectReader = nullptr);

  void updateThumbViewMinWidth();

  void setupThumbView();

  void showNewOpenProjectPanel();

  SavePromptResult promptProjectSave();

  static bool compareFiles(const QString& fpath1, const QString& fpath2);

  std::shared_ptr<const PageOrderProvider> currentPageOrderProvider() const;

  void updateSortOptions();

  void resetThumbSequence(const std::shared_ptr<const PageOrderProvider>& pageOrderProvider,
                          ThumbnailSequence::SelectionAction selectionAction = ThumbnailSequence::RESET_SELECTION);

  void removeFilterOptionsWidget();

  void removeImageWidget();

  void updateProjectActions();

  bool isBatchProcessingInProgress() const;

  bool isProjectLoaded() const;

  bool isBelowSelectContent() const;

  bool isBelowSelectContent(int filterIdx) const;

  bool isBelowFixOrientation(int filterIdx) const;

  bool isOutputFilter() const;

  bool isOutputFilter(int filterIdx) const;

  bool isFinalizeOrOutputFilter() const;

  void toggleFullBleedForSelectedPages();

  PageView getCurrentView() const;

  void updateMainArea();

  bool checkReadyForOutput(const PageId* ignore = nullptr) const;

  void loadPageInteractive(const PageInfo& page);

  void updateWindowTitle();

  bool closeProjectInteractive();

  void closeProjectWithoutSaving();

  void cleanupTempOutputFiles();

  bool showTempCleanupWarning();

  bool saveProjectWithFeedback(const QString& projectFile);

  bool saveProjectToFolder(const QString& folderPath);

  QString suggestProjectName() const;

  QString defaultPdfExportPath(const BookMetadata& metadata) const;

  void showInsertFileDialog(BeforeOrAfter beforeOrAfter, const ImageId& existig);

  void showRemovePagesDialog(const std::set<PageId>& pages);

  void forcePageSplitLayout(page_split::LayoutType layoutType);

  void setColorModeForSelectedPages(output::ColorMode mode);

  void insertImage(const ImageInfo& newImage, BeforeOrAfter beforeOrAfter, ImageId existing);

  void removeFromProject(const std::set<PageId>& pages);

  void eraseOutputFiles(const std::set<PageId>& pages);

  BackgroundTaskPtr createCompositeTask(const PageInfo& page, int lastFilterIdx, bool batch, bool debug);

  std::shared_ptr<CompositeCacheDrivenTask> createCompositeCacheDrivenTask(int lastFilterIdx);

  void createBatchProcessingWidget();

  void updateDisambiguationRecords(const PageSequence& pages);

  void performRelinking(const std::shared_ptr<AbstractRelinker>& relinker);

  PageSelectionAccessor newPageSelectionAccessor();

  void setDockWidgetsVisible(bool state);

  void scaleThumbnails(int scaleFactor);

  void updateMaxLogicalThumbSize();

  void updateThumbnailViewMode();

  void updateAutoSaveTimer();

  PageSequence currentPageSequence();

  void setupIcons();

  void showBatchProcessingSummary();

  void autoModeAdvance();
  void startBatchTimer();
  void recordCurrentAutoStageTime();
  void finishAutoProcess(bool completed);
  void reportManualBatchCompletion(qint64 elapsedMilliseconds, size_t pageCount);
  QString autoProcessBreakdown() const;
  void applyAutoProcessColorSettings();
  void resetAutoProcessColorPolicy();
  void autoAcceptPageSplit();
  void autoSetDeskewZero();
  void autoAcceptContentOutliers();
  void autoAcceptPageSizeOutliers();

  void jumpToPageFromSummary(const ImageId& imageId);

  void forceTwoPageForImages(const std::vector<ImageId>& imageIds);

  void forceSinglePageForImages(const std::vector<ImageId>& imageIds);

  void showPageBoxSummary();

  void showContentCoverageSummary();

  void jumpToPageFromContentSummary(const PageId& pageId);

  void preserveLayoutForPages(const std::vector<PageId>& pageIds);

  void showPageSizeWarning();

  void jumpToPageFromPageSizeWarning(const PageId& pageId);

  void goToPageSplitFromWarning();

  void disableAlignmentForPages(const std::vector<PageId>& pageIds);

  QSizeF m_maxLogicalThumbSize;
  std::shared_ptr<ProjectPages> m_pages;
  std::shared_ptr<StageSequence> m_stages;
  QString m_projectFile;
  QString m_projectFolderPath;       // Base path when saved to project folder structure
  bool m_projectSavedToFolder = false;  // True when project is saved to folder structure
  QString m_defaultOutDir;  // Original (temp) output directory
  QString m_sourceDocumentPath;  // Original source used for export location and naming
  OutputFileNameGenerator m_outFileNameGen;
  std::shared_ptr<ThumbnailPixmapCache> m_thumbnailCache;
  std::unique_ptr<ThumbnailSequence> m_thumbSequence;
  std::unique_ptr<WorkerThreadPool> m_workerThreadPool;
  std::unique_ptr<ProcessingTaskQueue> m_batchQueue;
  std::unique_ptr<ProcessingTaskQueue> m_interactiveQueue;
  QStackedLayout* m_imageFrameLayout;
  QStackedLayout* m_optionsFrameLayout;
  int m_filterDockBaseMinimumWidth = 0;
  QPointer<FilterOptionsWidget> m_optionsWidget;
  QPointer<FixDpiDialog> m_fixDpiDialog;
  std::unique_ptr<TabbedDebugImages> m_tabbedDebugImages;
  std::unique_ptr<ContentBoxPropagator> m_contentBoxPropagator;
  std::unique_ptr<PageOrientationPropagator> m_pageOrientationPropagator;
  std::unique_ptr<QWidget> m_batchProcessingWidget;
  QLabel* m_batchProgressLabel = nullptr;
  std::unique_ptr<ProcessingIndicationWidget> m_processingIndicationWidget;
  std::function<bool()> m_checkBeepWhenFinished;
  SelectedPage m_selectedPage;
  QObjectCleanupHandler m_optionsWidgetCleanup;
  QObjectCleanupHandler m_imageWidgetCleanup;
  std::unique_ptr<OutOfMemoryDialog> m_outOfMemoryDialog;
  int m_curFilter;
  int m_ignoreSelectionChanges;
  int m_ignorePageOrderingChanges;
  bool m_restoreGeometry;
  bool m_debug;
  bool m_closing;
  bool m_closeEventPending;
  bool m_quitting;  // True when user explicitly wants to quit (Cmd+Q or Quit menu)
  bool m_twoPassBatchInProgress;  // True when running first pass (Page Layout) before Output
  int m_twoPassTargetFilter;      // The filter to run after first pass completes

  enum AutoModeStage {
    AUTO_NONE = -1,
    AUTO_PAGE_SPLIT = 0,
    AUTO_DESKEW = 1,
    AUTO_PAGE_BOX = 2,
    AUTO_SELECT_CONTENT = 3,
    AUTO_PAGE_LAYOUT = 4,
    AUTO_OUTPUT = 5,
    AUTO_OCR = 6
  };
  AutoModeStage m_autoModeStage = AUTO_NONE;
  bool m_autoModeIncludeOcr = false;
  bool m_autoModeForceBlackAndWhite = false;
  bool m_autoModeRedetectColor = false;
  QElapsedTimer m_autoModeTimer;
  QElapsedTimer m_autoStageTimer;
  QElapsedTimer m_batchTimer;
  std::array<qint64, 7> m_autoStageElapsedMs{};
  QString m_autoTimingSummary;
  QString m_autoTimingBreakdown;
  QTimer m_autoSaveTimer;
  QLabel* m_timingStatusLabel;
  StatusBarPanel* m_statusBarPanel;
  QActionGroup* m_unitsMenuActionGroup;
  QTimer m_maxLogicalThumbSizeUpdater;
  QTimer m_sceneItemsPosUpdater;
};


#endif  // ifndef SCANTAILOR_APP_MAINWINDOW_H_
