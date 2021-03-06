/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2014 <tsujan2000@gmail.com>
 *
 * FeatherPad is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FeatherPad is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @license GPL-3.0+ <https://spdx.org/licenses/GPL-3.0+.html>
 */

#include "singleton.h"
#include "ui_fp.h"
#include "ui_about.h"
#include "encoding.h"
#include "filedialog.h"
#include "messagebox.h"
#include "pref.h"
#include "session.h"
#include "loading.h"
#include "warningbar.h"
#include "svgicons.h"

#include <QFontDialog>
#include <QPrintDialog>
#include <QToolTip>
#include <QDesktopWidget>
#include <QScrollBar>
#include <fstream> // std::ofstream
#include <QPrinter>
#include <QClipboard>
#include <QProcess>
#include <QTextDocumentWriter>
#include <QTextCodec>

#include "x11.h"

namespace FeatherPad {

void BusyMaker::waiting() {
    QTimer::singleShot (timeout, this, SLOT (makeBusy()));
}

void BusyMaker::makeBusy() {
    if (QGuiApplication::overrideCursor() == nullptr)
        QGuiApplication::setOverrideCursor (Qt::WaitCursor);
    emit finished();
}


FPwin::FPwin (QWidget *parent):QMainWindow (parent), dummyWidget (nullptr), ui (new Ui::FPwin)
{
    ui->setupUi (this);

    loadingProcesses_ = 0;
    rightClicked_ = -1;
    busyThread_ = nullptr;

    autoSaver_ = nullptr;
    autoSaverRemainingTime_ = -1;

    sidePane_ = nullptr;

    /* "Jump to" bar */
    ui->spinBox->hide();
    ui->label->hide();
    ui->checkBox->hide();

    /* status bar */
    QLabel *statusLabel = new QLabel();
    statusLabel->setObjectName ("statusLabel");
    statusLabel->setIndent (2);
    statusLabel->setMinimumWidth (100);
    statusLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
    QToolButton *wordButton = new QToolButton();
    wordButton->setObjectName ("wordButton");
    wordButton->setFocusPolicy (Qt::NoFocus);
    wordButton->setAutoRaise (true);
    wordButton->setToolButtonStyle (Qt::ToolButtonIconOnly);
    wordButton->setIconSize (QSize (16, 16));
    wordButton->setIcon (symbolicIcon::icon (":icons/view-refresh.svg"));
    wordButton->setToolTip (tr ("Calculate number of words\n(For huge texts, this may be CPU-intensive.)"));
    connect (wordButton, &QAbstractButton::clicked, [=]{updateWordInfo();});
    ui->statusBar->addWidget (statusLabel);
    ui->statusBar->addWidget (wordButton);

    /* text unlocking */
    ui->actionEdit->setVisible (false);

    ui->actionRun->setVisible (false);

    /* replace dock */
    ui->dockReplace->setTabOrder (ui->lineEditFind, ui->lineEditReplace);
    ui->dockReplace->setTabOrder (ui->lineEditReplace, ui->toolButtonNext);
    /* tooltips are set here for easier translation */
    ui->toolButtonNext->setToolTip (tr ("Next") + " (" + tr ("F7") + ")");
    ui->toolButtonPrv->setToolTip (tr ("Previous") + " (" + tr ("F8") + ")");
    ui->toolButtonAll->setToolTip (tr ("Replace all") + " (" + tr ("F9") + ")");
    ui->dockReplace->setVisible (false);

    applyConfigOnStarting();

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->mainToolBar->insertWidget (ui->actionMenu, spacer);
    QMenu *menu = new QMenu (ui->mainToolBar);
    menu->addMenu (ui->menuFile);
    menu->addMenu (ui->menuEdit);
    menu->addMenu (ui->menuOptions);
    menu->addMenu (ui->menuSearch);
    menu->addMenu (ui->menuHelp);
    ui->actionMenu->setMenu(menu);
    QList<QToolButton*> tbList = ui->mainToolBar->findChildren<QToolButton*>();
    if (!tbList.isEmpty())
        tbList.at (tbList.count() - 1)->setPopupMode (QToolButton::InstantPopup);

    newTab();

    aGroup_ = new QActionGroup (this);
    ui->actionUTF_8->setActionGroup (aGroup_);
    ui->actionUTF_16->setActionGroup (aGroup_);
    ui->actionWindows_Arabic->setActionGroup (aGroup_);
    ui->actionISO_8859_1->setActionGroup (aGroup_);
    ui->actionISO_8859_15->setActionGroup (aGroup_);
    ui->actionWindows_1252->setActionGroup (aGroup_);
    ui->actionCyrillic_CP1251->setActionGroup (aGroup_);
    ui->actionCyrillic_KOI8_U->setActionGroup (aGroup_);
    ui->actionCyrillic_ISO_8859_5->setActionGroup (aGroup_);
    ui->actionChineese_BIG5->setActionGroup (aGroup_);
    ui->actionChinese_GB18030->setActionGroup (aGroup_);
    ui->actionJapanese_ISO_2022_JP->setActionGroup (aGroup_);
    ui->actionJapanese_ISO_2022_JP_2->setActionGroup (aGroup_);
    ui->actionJapanese_ISO_2022_KR->setActionGroup (aGroup_);
    ui->actionJapanese_CP932->setActionGroup (aGroup_);
    ui->actionJapanese_EUC_JP->setActionGroup (aGroup_);
    ui->actionKorean_CP949->setActionGroup (aGroup_);
    ui->actionKorean_CP1361->setActionGroup (aGroup_);
    ui->actionKorean_EUC_KR->setActionGroup (aGroup_);
    ui->actionOther->setActionGroup (aGroup_);

    ui->actionUTF_8->setChecked (true);
    ui->actionOther->setDisabled (true);

    connect (ui->actionNew, &QAction::triggered, this, &FPwin::newTab);
    connect (ui->tabWidget->tabBar(), &TabBar::addEmptyTab, this, &FPwin::newTab);
    connect (ui->actionDetachTab, &QAction::triggered, this, &FPwin::detachTab);
    connect (ui->actionRightTab, &QAction::triggered, this, &FPwin::nextTab);
    connect (ui->actionLeftTab, &QAction::triggered, this, &FPwin::previousTab);
    if (sidePane_)
    {
        QString txt = ui->actionFirstTab->text();
        ui->actionFirstTab->setText (ui->actionLastTab->text());
        ui->actionLastTab->setText (txt);
        connect (ui->actionFirstTab, &QAction::triggered, this, &FPwin::lastTab);
        connect (ui->actionLastTab, &QAction::triggered, this, &FPwin::firstTab);
    }
    else
    {
        connect (ui->actionLastTab, &QAction::triggered, this, &FPwin::lastTab);
        connect (ui->actionFirstTab, &QAction::triggered, this, &FPwin::firstTab);
    }
    connect (ui->actionClose, &QAction::triggered, this, &FPwin::closeTab);
    connect (ui->tabWidget, &QTabWidget::tabCloseRequested, this, &FPwin::closeTabAtIndex);
    connect (ui->actionOpen, &QAction::triggered, this, &FPwin::fileOpen);
    connect (ui->actionReload, &QAction::triggered, this, &FPwin::reload);
    connect (aGroup_, &QActionGroup::triggered, this, &FPwin::enforceEncoding);
    connect (ui->actionSave, &QAction::triggered, [=]{saveFile (false);});
    connect (ui->actionSaveAs, &QAction::triggered, this, [=]{saveFile (false);});
    connect (ui->actionSaveCodec, &QAction::triggered, this, [=]{saveFile (false);});

    connect (ui->actionCut, &QAction::triggered, this, &FPwin::cutText);
    connect (ui->actionCopy, &QAction::triggered, this, &FPwin::copyText);
    connect (ui->actionPaste, &QAction::triggered, this, &FPwin::pasteText);
    connect (ui->actionDate, &QAction::triggered, this, &FPwin::insertDate);
    connect (ui->actionDelete, &QAction::triggered, this, &FPwin::deleteText);
    connect (ui->actionSelectAll, &QAction::triggered, this, &FPwin::selectAllText);

    connect (ui->actionEdit, &QAction::triggered, this, &FPwin::makeEditable);

    connect (ui->actionSession, &QAction::triggered, this, &FPwin::manageSessions);

    connect (ui->actionRun, &QAction::triggered, this, &FPwin::executeProcess);

    connect (ui->actionUndo, &QAction::triggered, this, &FPwin::undoing);
    connect (ui->actionRedo, &QAction::triggered, this, &FPwin::redoing);

    connect (ui->tabWidget, &TabWidget::currentTabChanged, this, &FPwin::tabSwitch);
    connect (ui->tabWidget->tabBar(), &TabBar::tabDetached, this, &FPwin::detachTab);
    ui->tabWidget->tabBar()->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (ui->tabWidget->tabBar(), &QWidget::customContextMenuRequested, this, &FPwin::tabContextMenu);
    connect (ui->actionCopyName, &QAction::triggered, this, &FPwin::copyTabFileName);
    connect (ui->actionCopyPath, &QAction::triggered, this, &FPwin::copyTabFilePath);
    connect (ui->actionCloseAll, &QAction::triggered, this, &FPwin::closeAllTabs);
    connect (ui->actionCloseRight, &QAction::triggered, this, &FPwin::closeNextTabs);
    connect (ui->actionCloseLeft, &QAction::triggered, this, &FPwin::closePreviousTabs);
    connect (ui->actionCloseOther, &QAction::triggered, this, &FPwin::closeOtherTabs);

    connect (ui->actionFont, &QAction::triggered, this, &FPwin::fontDialog);

    connect (ui->actionFind, &QAction::triggered, this, &FPwin::showHideSearch);
    connect (ui->actionJump, &QAction::triggered, this, &FPwin::jumpTo);
    connect (ui->spinBox, &QAbstractSpinBox::editingFinished, this, &FPwin::goTo);

    connect (ui->actionLineNumbers, &QAction::toggled, this, &FPwin::showLN);
    connect (ui->actionWrap, &QAction::triggered, this, &FPwin::toggleWrapping);
    connect (ui->actionSyntax, &QAction::triggered, this, &FPwin::toggleSyntaxHighlighting);
    connect (ui->actionIndent, &QAction::triggered, this, &FPwin::toggleIndent);

    connect (ui->actionPreferences, &QAction::triggered, this, &FPwin::prefDialog);

    connect (ui->actionReplace, &QAction::triggered, this, &FPwin::replaceDock);
    connect (ui->toolButtonNext, &QAbstractButton::clicked, this, &FPwin::replace);
    connect (ui->toolButtonPrv, &QAbstractButton::clicked, this, &FPwin::replace);
    connect (ui->toolButtonAll, &QAbstractButton::clicked, this, &FPwin::replaceAll);
    connect (ui->dockReplace, &QDockWidget::visibilityChanged, this, &FPwin::closeReplaceDock);
    connect (ui->dockReplace, &QDockWidget::topLevelChanged, this, &FPwin::resizeDock);

    connect (ui->actionDoc, &QAction::triggered, this, &FPwin::docProp);
    connect (ui->actionPrint, &QAction::triggered, this, &FPwin::filePrint);

    connect (ui->actionAbout, &QAction::triggered, this, &FPwin::aboutDialog);
    connect (ui->actionHelp, &QAction::triggered, this, &FPwin::helpDoc);

    connect (this, &FPwin::finishedLoading, [this] {
        if (sidePane_)
            sidePane_->listWidget()->scrollToItem (sidePane_->listWidget()->currentItem());
    });
    ui->actionSidePane->setAutoRepeat (false); // don't let UI change too rapidly
    connect (ui->actionSidePane, &QAction::triggered, [this] {toggleSidePane();});

    /***************************************************************************
     *****     KDE (KAcceleratorManager) has a nasty "feature" that        *****
     *****   "smartly" gives mnemonics to tab and tool button texts so     *****
     *****   that, sometimes, the same mnemonics are disabled in the GUI   *****
     *****     and, as a result, their corresponding action shortcuts      *****
     *****     become disabled too. As a workaround, we don't set text     *****
     *****     for tool buttons on the search bar and replacement dock.    *****
     ***** The toolbar buttons and menu items aren't affected by this bug. *****
     ***************************************************************************/
    ui->toolButtonNext->setShortcut (QKeySequence (tr ("F7")));
    ui->toolButtonPrv->setShortcut (QKeySequence (tr ("F8")));
    ui->toolButtonAll->setShortcut (QKeySequence (tr ("F9")));

    QShortcut *zoomin = new QShortcut (QKeySequence (tr ("Ctrl+=")), this);
    QShortcut *zoominPlus = new QShortcut (QKeySequence (tr ("Ctrl++")), this);
    QShortcut *zoomout = new QShortcut (QKeySequence (tr ("Ctrl+-")), this);
    QShortcut *zoomzero = new QShortcut (QKeySequence (tr ("Ctrl+0")), this);
    connect (zoomin, &QShortcut::activated, this, &FPwin::zoomIn);
    connect (zoominPlus, &QShortcut::activated, this, &FPwin::zoomIn);
    connect (zoomout, &QShortcut::activated, this, &FPwin::zoomOut);
    connect (zoomzero, &QShortcut::activated, this, &FPwin::zoomZero);

    QShortcut *fullscreen = new QShortcut (QKeySequence (tr ("F11")), this);
    QShortcut *defaultsize = new QShortcut (QKeySequence (tr ("Ctrl+Shift+W")), this);
    connect (fullscreen, &QShortcut::activated, [this] {setWindowState (windowState() ^ Qt::WindowFullScreen);});
    connect (defaultsize, &QShortcut::activated, this, &FPwin::defaultSize);

    /* this workaround, for the RTL bug in QPlainTextEdit, isn't needed
       because a better workaround is included in textedit.cpp */
    /*QShortcut *align = new QShortcut (QKeySequence (tr ("Ctrl+Shift+A", "Alignment")), this);
    connect (align, &QShortcut::activated, this, &FPwin::align);*/

    /* exiting a process */
    QShortcut *kill = new QShortcut (QKeySequence (tr ("Ctrl+Alt+E")), this);
    connect (kill, &QShortcut::activated, this, &FPwin::exitProcess);

    dummyWidget = new QWidget();
    setAcceptDrops (true);
    setAttribute (Qt::WA_AlwaysShowToolTips);
    setAttribute (Qt::WA_DeleteOnClose, false); // we delete windows in singleton
}
/*************************/
FPwin::~FPwin()
{
    startAutoSaving (false);
    delete dummyWidget; dummyWidget = nullptr;
    delete aGroup_; aGroup_ = nullptr;
    delete ui; ui = nullptr;
}
/*************************/
void FPwin::closeEvent (QCloseEvent *event)
{
    bool keep = closeTabs (-1, -1);
    if (keep)
        event->ignore();
    else
    {
        FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
        Config& config = singleton->getConfig();
        if (config.getRemSize() && windowState() == Qt::WindowNoState)
            config.setWinSize (size());
        if (sidePane_ && config.getRemSplitterPos())
        {
            QList<int> sizes = ui->splitter->sizes();
            config.setSplitterPos (qRound (100.0 * (qreal)sizes.at (0) / (qreal)(sizes.at (0) + sizes.at (1))));
        }
        singleton->removeWin (this);
        event->accept();
    }
}
/*************************/
void FPwin::toggleSidePane()
{
    if (!sidePane_)
    {
        ui->tabWidget->tabBar()->hide();
        ui->tabWidget->tabBar()->hideSingle (false); // prevent tabs from reappearing
        sidePane_ = new SidePane();
        ui->splitter->insertWidget (0, sidePane_);
        sidePane_->listWidget()->setFocus();
        int mult = size().width() / 100; // for more precision
        int sp = static_cast<FPsingleton*>(qApp)->getConfig().getSplitterPos();
        QList<int> sizes;
        sizes << sp * mult << (100 - sp) * mult;
        ui->splitter->setSizes (sizes);
        connect (sidePane_->listWidget(), &QWidget::customContextMenuRequested, this, &FPwin::listContextMenu);
        connect (sidePane_->listWidget(), &QListWidget::currentItemChanged, this, &FPwin::changeTab);
        connect (sidePane_->listWidget(), &ListWidget::closItem, [this](QListWidgetItem* item) {
            if (!sideItems_.isEmpty())
                closeTabAtIndex (ui->tabWidget->indexOf (sideItems_.value (item)));
        });

        if (ui->tabWidget->count() > 0)
        {
            updateShortcuts (true);
            int curIndex = ui->tabWidget->currentIndex();
            ListWidget *lw = sidePane_->listWidget();
            for (int i = 0; i < ui->tabWidget->count(); ++i)
            {
                TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->widget (i));
                /* tab text can't be used because, on the one hand, it may be elided
                   and, on the other hand, KDE's auto-mnemonics may interfere */
                QString fname = tabPage->textEdit()->getFileName();
                bool isLink (false);
                if (fname.isEmpty())
                {
                    if (tabPage->textEdit()->getProg() == "help")
                        fname = "** " + tr ("Help") + " **";
                    else
                        fname = tr ("Untitled");
                }
                else
                {
                    isLink = QFileInfo (fname).isSymLink();
                    fname = fname.section ('/', -1);
                }
                if (tabPage->textEdit()->document()->isModified())
                    fname.append ("*");
                fname.replace ("\n", " ");
                QListWidgetItem *lwi = new QListWidgetItem (isLink ? QIcon (":icons/link.svg") : QIcon(),
                                                            fname, lw);
                lwi->setToolTip (ui->tabWidget->tabToolTip (i));
                sideItems_.insert (lwi, tabPage);
                lw->addItem (lwi);
                if (i == curIndex)
                    lw->setCurrentItem (lwi);
            }
            sidePane_->listWidget()->scrollTo (sidePane_->listWidget()->currentIndex());
            updateShortcuts (false);
        }
    }
    else
    {
        if (!sidePane_->listWidget()->hasFocus())
            sidePane_->listWidget()->setFocus();
        else
        {
            sideItems_.clear();
            delete sidePane_;
            sidePane_ = nullptr;
            bool hideSingleTab = static_cast<FPsingleton*>(qApp)->
                                 getConfig().getHideSingleTab();
            ui->tabWidget->tabBar()->hideSingle (hideSingleTab);
            if (!hideSingleTab || ui->tabWidget->count() > 1)
                ui->tabWidget->tabBar()->show();
            /* return focus to the document */
            if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
                tabPage->textEdit()->setFocus();
        }
    }
}
/*************************/
void FPwin::applyConfigOnStarting()
{
    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();

    if (config.getRemSize())
    {
        resize (config.getWinSize());
        if (config.getIsMaxed())
            setWindowState (Qt::WindowMaximized);
        if (config.getIsFull() && config.getIsMaxed())
            setWindowState (windowState() ^ Qt::WindowFullScreen);
        else if (config.getIsFull())
            setWindowState (Qt::WindowFullScreen);
    }
    else
    {
        QSize startSize = config.getStartSize();
        QSize ag = QApplication::desktop()->availableGeometry().size();
        if (startSize.width() > ag.width() || startSize.height() > ag.height())
        {
            startSize = startSize.boundedTo (ag);
            config.setStartSize (startSize);
        }
        else if (startSize.isEmpty())
        {
            startSize = QSize (700, 500);
            config.setStartSize (startSize);
        }
        resize (startSize);
    }

    ui->mainToolBar->setVisible (!config.getNoToolbar());
    ui->menuBar->setVisible (!config.getNoMenubar());
    ui->actionMenu->setVisible (config.getNoMenubar());

    ui->actionDoc->setVisible (!config.getShowStatusbar());

    ui->actionWrap->setChecked (config.getWrapByDefault());

    ui->actionIndent->setChecked (config.getIndentByDefault());

    ui->actionLineNumbers->setChecked (config.getLineByDefault());
    ui->actionLineNumbers->setDisabled (config.getLineByDefault());

    ui->actionSyntax->setChecked (config.getSyntaxByDefault());

    if (!config.getShowStatusbar())
        ui->statusBar->hide();
    else
    {
        if (config.getShowCursorPos())
            addCursorPosLabel();
    }
    if (config.getShowLangSelector() && config.getSyntaxByDefault())
    {
        setupLangButton (true,
                         config.getShowWhiteSpace()
                         || config.getShowEndings()
                         || config.getVLineDistance() > 0);
    }

    if (config.getTabPosition() != 0)
        ui->tabWidget->setTabPosition ((QTabWidget::TabPosition) config.getTabPosition());

    if (!config.getSidePaneMode()) // hideSingle() shouldn't be set with the side-pane
        ui->tabWidget->tabBar()->hideSingle (config.getHideSingleTab());
    else
        toggleSidePane();

    if (config.getRecentOpened())
        ui->menuOpenRecently->setTitle (tr ("&Recently Opened"));
    int recentNumber = config.getCurRecentFilesNumber();
    QAction* recentAction = nullptr;
    for (int i = 0; i < recentNumber; ++i)
    {
        recentAction = new QAction (this);
        recentAction->setVisible (false);
        connect (recentAction, &QAction::triggered, this, &FPwin::newTabFromRecent);
        ui->menuOpenRecently->addAction (recentAction);
    }
    ui->menuOpenRecently->addAction (ui->actionClearRecent);
    connect (ui->menuOpenRecently, &QMenu::aboutToShow, this, &FPwin::updateRecenMenu);
    connect (ui->actionClearRecent, &QAction::triggered, this, &FPwin::clearRecentMenu);

    if (config.getIconless())
    {
        iconMode_ = NONE;
        ui->toolButtonNext->setText (tr ("Next"));
        ui->toolButtonPrv->setText (tr ("Previous"));
        ui->toolButtonAll->setText (tr ("All"));
    }
    else
    {
        bool rtl (QApplication::layoutDirection() == Qt::RightToLeft);
        if (config.getSysIcon())
        {
            iconMode_ = SYSTEM;

            ui->actionNew->setIcon (QIcon::fromTheme ("document-new"));
            ui->actionOpen->setIcon (QIcon::fromTheme ("document-open"));
            ui->menuOpenRecently->setIcon (QIcon::fromTheme ("document-open-recent"));
            ui->actionClearRecent->setIcon (QIcon::fromTheme ("edit-clear"));
            ui->actionSave->setIcon (QIcon::fromTheme ("document-save"));
            ui->actionSaveAs->setIcon (QIcon::fromTheme ("document-save-as"));
            ui->actionSaveCodec->setIcon (QIcon::fromTheme ("document-save-as"));
            ui->actionPrint->setIcon (QIcon::fromTheme ("document-print"));
            ui->actionDoc->setIcon (QIcon::fromTheme ("document-properties"));
            ui->actionUndo->setIcon (QIcon::fromTheme ("edit-undo"));
            ui->actionRedo->setIcon (QIcon::fromTheme ("edit-redo"));
            ui->actionCut->setIcon (QIcon::fromTheme ("edit-cut"));
            ui->actionCopy->setIcon (QIcon::fromTheme ("edit-copy"));
            ui->actionPaste->setIcon (QIcon::fromTheme ("edit-paste"));
            ui->actionDate->setIcon (QIcon::fromTheme ("clock"));
            ui->actionDelete->setIcon (QIcon::fromTheme ("edit-delete"));
            ui->actionSelectAll->setIcon (QIcon::fromTheme ("edit-select-all"));
            ui->actionReload->setIcon (QIcon::fromTheme ("view-refresh"));
            ui->actionFind->setIcon (QIcon::fromTheme ("edit-find"));
            ui->actionReplace->setIcon (QIcon::fromTheme ("edit-find-replace"));
            ui->actionClose->setIcon (QIcon::fromTheme ("window-close"));
            ui->actionQuit->setIcon (QIcon::fromTheme ("application-exit"));
            ui->actionFont->setIcon (QIcon::fromTheme ("preferences-desktop-font"));
            ui->actionPreferences->setIcon (QIcon::fromTheme ("preferences-system"));
            ui->actionHelp->setIcon (QIcon::fromTheme ("help-contents"));
            ui->actionAbout->setIcon (QIcon::fromTheme ("help-about"));
            ui->actionJump->setIcon (QIcon::fromTheme ("go-jump"));
            ui->actionEdit->setIcon (QIcon::fromTheme ("document-edit"));
            ui->actionRun->setIcon (QIcon::fromTheme ("system-run"));
            ui->actionCopyName->setIcon (QIcon::fromTheme ("edit-copy"));
            ui->actionCopyPath->setIcon (QIcon::fromTheme ("edit-copy"));

            /* these icons may not exist in some themes... */
            QIcon icn = QIcon::fromTheme ("tab-close-other");
            if (icn.isNull())
                icn = symbolicIcon::icon (":icons/tab-close-other.svg");
            ui->actionCloseOther->setIcon (icn);
            icn = QIcon::fromTheme ("application-menu");
            if (icn.isNull())
                icn = symbolicIcon::icon (":icons/application-menu.svg");
            ui->actionMenu->setIcon (icn);
            /* ... and the following buttons don't have text, so we don't risk */
            icn = QIcon::fromTheme ("go-down");
            if (icn.isNull())
                icn = QIcon (":icons/go-down.svg");
             ui->toolButtonNext->setIcon (icn);
            icn = QIcon::fromTheme ("go-up");
            if (icn.isNull())
                icn = QIcon (":icons/go-up.svg");
            ui->toolButtonPrv->setIcon (icn);
            icn = QIcon::fromTheme ("arrow-down-double");
            if (icn.isNull())
                icn = symbolicIcon::icon (":icons/arrow-down-double.svg");
            ui->toolButtonAll->setIcon (icn);
            if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton"))
            {
                icn = QIcon::fromTheme ("view-refresh");
                if (!icn.isNull())
                    wordButton->setIcon (icn);
            }

            if (rtl)
            {
                ui->actionCloseRight->setIcon (QIcon::fromTheme ("go-previous"));
                ui->actionCloseLeft->setIcon (QIcon::fromTheme ("go-next"));
                ui->actionRightTab->setIcon (QIcon::fromTheme ("go-previous"));
                ui->actionLeftTab->setIcon (QIcon::fromTheme ("go-next"));

                /* shortcuts should be reversed for rtl */
                ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Left")));
                ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Right")));
            }
            else
            {
                ui->actionCloseRight->setIcon (QIcon::fromTheme ("go-next"));
                ui->actionCloseLeft->setIcon (QIcon::fromTheme ("go-previous"));
                ui->actionRightTab->setIcon (QIcon::fromTheme ("go-next"));
                ui->actionLeftTab->setIcon (QIcon::fromTheme ("go-previous"));
            }

            icn = QIcon::fromTheme ("featherpad");
            if (icn.isNull())
                icn = QIcon (":icons/featherpad.svg");
            setWindowIcon (icn);
        }
        else // own icons
        {
            iconMode_ = OWN;

            ui->actionNew->setIcon (symbolicIcon::icon (":icons/document-new.svg"));
            ui->actionOpen->setIcon (symbolicIcon::icon (":icons/document-open.svg"));
            ui->menuOpenRecently->setIcon (symbolicIcon::icon (":icons/document-open-recent.svg"));
            ui->actionClearRecent->setIcon (symbolicIcon::icon (":icons/edit-clear.svg"));
            ui->actionSave->setIcon (symbolicIcon::icon (":icons/document-save.svg"));
            ui->actionSaveAs->setIcon (symbolicIcon::icon (":icons/document-save-as.svg"));
            ui->actionSaveCodec->setIcon (symbolicIcon::icon (":icons/document-save-as.svg"));
            ui->actionPrint->setIcon (symbolicIcon::icon (":icons/document-print.svg"));
            ui->actionDoc->setIcon (symbolicIcon::icon (":icons/document-properties.svg"));
            ui->actionUndo->setIcon (symbolicIcon::icon (":icons/edit-undo.svg"));
            ui->actionRedo->setIcon (symbolicIcon::icon (":icons/edit-redo.svg"));
            ui->actionCut->setIcon (symbolicIcon::icon (":icons/edit-cut.svg"));
            ui->actionCopy->setIcon (symbolicIcon::icon (":icons/edit-copy.svg"));
            ui->actionPaste->setIcon (symbolicIcon::icon (":icons/edit-paste.svg"));
            ui->actionDate->setIcon (symbolicIcon::icon (":icons/document-open-recent.svg"));
            ui->actionDelete->setIcon (QIcon (":icons/edit-delete.svg"));
            ui->actionSelectAll->setIcon (symbolicIcon::icon (":icons/edit-select-all.svg"));
            ui->actionReload->setIcon (symbolicIcon::icon (":icons/view-refresh.svg"));
            ui->actionFind->setIcon (symbolicIcon::icon (":icons/edit-find.svg"));
            ui->actionReplace->setIcon (symbolicIcon::icon (":icons/edit-find-replace.svg"));
            ui->actionClose->setIcon (QIcon (":icons/window-close.svg"));
            ui->actionQuit->setIcon (QIcon (":icons/application-exit.svg"));
            ui->actionFont->setIcon (symbolicIcon::icon (":icons/preferences-desktop-font.svg"));
            ui->actionPreferences->setIcon (symbolicIcon::icon (":icons/preferences-system.svg"));
            ui->actionHelp->setIcon (symbolicIcon::icon (":icons/help-contents.svg"));
            ui->actionAbout->setIcon (symbolicIcon::icon (":icons/help-about.svg"));
            ui->actionJump->setIcon (symbolicIcon::icon (":icons/go-jump.svg"));
            ui->actionEdit->setIcon (symbolicIcon::icon (":icons/document-edit.svg"));
            ui->actionRun->setIcon (symbolicIcon::icon (":icons/system-run.svg"));
            ui->actionCopyName->setIcon (symbolicIcon::icon (":icons/edit-copy.svg"));
            ui->actionCopyPath->setIcon (symbolicIcon::icon (":icons/edit-copy.svg"));

            ui->actionCloseOther->setIcon (symbolicIcon::icon (":icons/tab-close-other.svg"));
            ui->actionMenu->setIcon (symbolicIcon::icon (":icons/application-menu.svg"));

            ui->toolButtonNext->setIcon (symbolicIcon::icon (":icons/go-down.svg"));
            ui->toolButtonPrv->setIcon (symbolicIcon::icon (":icons/go-up.svg"));
            ui->toolButtonAll->setIcon (symbolicIcon::icon (":icons/arrow-down-double.svg"));

            if (rtl)
            {
                ui->actionCloseRight->setIcon (symbolicIcon::icon (":icons/go-previous.svg"));
                ui->actionCloseLeft->setIcon (symbolicIcon::icon (":icons/go-next.svg"));
                ui->actionRightTab->setIcon (symbolicIcon::icon (":icons/go-previous.svg"));
                ui->actionLeftTab->setIcon (symbolicIcon::icon (":icons/go-next.svg"));

                ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Left")));
                ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Right")));
            }
            else
            {
                ui->actionCloseRight->setIcon (symbolicIcon::icon (":icons/go-next.svg"));
                ui->actionCloseLeft->setIcon (symbolicIcon::icon (":icons/go-previous.svg"));
                ui->actionRightTab->setIcon (symbolicIcon::icon (":icons/go-next.svg"));
                ui->actionLeftTab->setIcon (symbolicIcon::icon (":icons/go-previous.svg"));
            }

            setWindowIcon (QIcon (":icons/featherpad.svg"));
        }
    }

    if (!config.hasReservedShortcuts())
    { // this is here, and not in "singleton.cpp", just to simplify translation
        QStringList reserved;
                    /* QPLainTextEdit */
        reserved << tr ("Ctrl+Shift+Z") << tr ("Ctrl+Z") << tr ("Ctrl+X") << tr ("Ctrl+C") << tr ("Ctrl+V") << tr ("Ctrl+A")
                 << tr ("Shift+Ins") << tr ("Shift+Del") << tr ("Ctrl+Ins") << tr ("Ctrl+Left") << tr ("Ctrl+Right")
                 << tr ("Ctrl+Up") << tr ("Ctrl+Down") << tr ("Ctrl+Home") << tr ("Ctrl+End")

                    /* search and replacement */
                 << tr ("F3") << tr ("F4") << tr ("F5") << tr ("F6")
                 << tr ("F7") << tr ("F8") << tr ("F9")
                 << tr ("F11") << tr ("Ctrl+Shift+W")

                 << tr ("Ctrl+=") << tr ("Ctrl++") << tr ("Ctrl+-") << tr ("Ctrl+0") // zooming
                 << tr ("Ctrl+Alt+E") // exiting a process
                 << tr ("Shift+Enter") << tr ("Ctrl+Tab") << tr ("Ctrl+Meta+Tab") // text tabulation
                 << tr ("Alt+Right") << tr ("Alt+Left") << tr ("Alt+Down")  << tr ("Alt+Up") // tab switching
                 << tr ("Ctrl+Shift+J") // select text on jumping (not an action)
                 << tr ("Ctrl+K"); // used by LineEdit as well as QPlainTextEdit
        config.setReservedShortcuts (reserved);
        config.readShortcuts();
    }

    QHash<QString, QString> ca = config.customShortcutActions();
    QHash<QString, QString>::const_iterator it = ca.constBegin();
    while (it != ca.constEnd())
    {
        if (QAction *action = findChild<QAction*>(it.key()))
            action->setShortcut (it.value());
        ++it;
    }

    if (config.getAutoSave())
        startAutoSaving (true, config.getAutoSaveInterval());
}
/*************************/
void FPwin::addCursorPosLabel()
{
    if (ui->statusBar->findChild<QLabel *>("posLabel"))
        return;
    QLabel *posLabel = new QLabel();
    posLabel->setObjectName ("posLabel");
    posLabel->setText ("<b>" + tr ("Position:") + "</b>");
    posLabel->setIndent (2);
    posLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
    ui->statusBar->addPermanentWidget (posLabel);
}
/*************************/
void FPwin::setupLangButton (bool add, bool normalAsUrl)
{
    static QStringList langList;
    if (langList.isEmpty())
    {
        langList << "c" << "changelog" << "cmake" << "config" << "cpp"
                 << "css" << "deb" << "desktop" << "diff" << "gtkrc"
                 << "html" << "javascript" << "log" << "lua" << "m3u"
                 << "markdown" << "makefile" << "perl" << "php" << "python"
                 << "qmake" << "qml" << "ruby" << "scss" << "sh"
                 << "troff" << "theme" << "xml";
        if (!normalAsUrl)
            langList.append ("url");
        langList.sort();
    }

    if (!add)
    { // remove the language button (normalAsUrl plays no role)
        langs.clear();
        langList.clear();
        if (QToolButton *langButton = ui->statusBar->findChild<QToolButton *>("langButton"))
            delete langButton;

        for (int i = 0; i < ui->tabWidget->count(); ++i)
        {
            TextEdit *textEdit = qobject_cast<TabPage*>(ui->tabWidget->widget (i))->textEdit();
            if (!textEdit->getLang().isEmpty())
            {
                textEdit->setLang (QString()); // remove the enforced syntax
                if (ui->actionSyntax->isChecked())
                {
                    syntaxHighlighting (textEdit, false);
                    syntaxHighlighting (textEdit);
                }
            }
            textEdit->setNormalAsUrl (normalAsUrl);
            handleNormalAsUrl (textEdit);
        }
    }
    else
    {
        QToolButton *langButton = ui->statusBar->findChild<QToolButton *>("langButton");
        if (langButton)
        { // just add or remove the url action
            if (normalAsUrl && langs.contains ("url"))
            {
                if (QAction *urlAction = langs.take ("url"))
                {
                    if (QMenu *menu = langButton->findChild<QMenu *>())
                        menu->removeAction (urlAction);
                    delete urlAction;
                    if (!langList.isEmpty())
                        langList.removeAll ("url");
                }
            }
            else if (!normalAsUrl && !langs.contains ("url"))
            {
                QMenu *menu = langButton->findChild<QMenu *>();
                QActionGroup *aGroup = langButton->findChild<QActionGroup *>();
                if (menu && aGroup)
                {
                    QAction *urlAction = new QAction ("url", menu);
                    QList<QAction*> allActions = menu->actions();
                    menu->insertAction (allActions.size() <= 1
                                            ? nullptr
                                            /* before the separator and "Normal" */
                                            : allActions.at (allActions.size() - 2), urlAction);
                    urlAction->setCheckable (true);
                    urlAction->setActionGroup (aGroup);
                    langs.insert ("url", urlAction);
                    if (!langList.isEmpty())
                    {
                        langList.append ("url");
                        langList.sort();
                    }
                }
            }
        }
        else
        { // add the language button
            QString normal = tr ("Normal");
            langButton = new QToolButton();
            langButton->setObjectName ("langButton");
            langButton->setFocusPolicy (Qt::NoFocus);
            langButton->setAutoRaise (true);
            langButton->setToolButtonStyle (Qt::ToolButtonTextOnly);
            langButton->setText (normal);
            langButton->setPopupMode (QToolButton::InstantPopup);

            QMenu *menu = new QMenu (langButton);
            QActionGroup *aGroup = new QActionGroup (langButton);
            QAction *action;
            for (int i = 0; i < langList.count(); ++i)
            {
                QString lang = langList.at (i);
                action = menu->addAction (lang);
                action->setCheckable (true);
                action->setActionGroup (aGroup);
                langs.insert (lang, action);
            }
            menu->addSeparator();
            action = menu->addAction (normal);
            action->setCheckable (true);
            action->setActionGroup (aGroup);
            langs.insert (normal, action);

            langButton->setMenu (menu);

            ui->statusBar->insertPermanentWidget (2, langButton);
            connect (aGroup, &QActionGroup::triggered, this, &FPwin::setLang);
        }

        for (int i = 0; i < ui->tabWidget->count(); ++i)
        { // in case this is called from outside c-tor
            TextEdit *textEdit = qobject_cast<TabPage*>(ui->tabWidget->widget (i))->textEdit();
            textEdit->setNormalAsUrl (normalAsUrl);
            handleNormalAsUrl (textEdit);
        }
    }

    /* correct the language button and statusbar message (if this is called from outside c-tor) */
    if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
    {
        TextEdit *textEdit = tabPage->textEdit();
        showLang (textEdit);
        /* the statusbar message should be changed only for url texts */
        if (ui->statusBar->isVisible()
            && ((normalAsUrl && textEdit->getProg().isEmpty()) || textEdit->getProg() == "url"))
        {
            statusMsgWithLineCount (textEdit->document()->blockCount());
            if (textEdit->getWordNumber() == -1)
            {
                if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton"))
                    wordButton->setVisible (true);
            }
            else
            {
                if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton"))
                    wordButton->setVisible (false);
                QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
                statusLabel->setText (QString ("%1 <i>%2</i>")
                                      .arg (statusLabel->text())
                                      .arg (textEdit->getWordNumber()));
            }
        }
    }
}
/*************************/
void FPwin::handleNormalAsUrl (TextEdit *textEdit)
{
    if (!ui->actionSyntax->isChecked() || !textEdit->getProg().isEmpty())
        return;
    if (textEdit->getNormalAsUrl())
    {
        if (!textEdit->getHighlighter())
            syntaxHighlighting (textEdit);
        else if (textEdit->getLang() == "url")
            textEdit->setLang (QString()); // "url" may have been enforced
    }
    else if (!textEdit->getNormalAsUrl() && textEdit->getHighlighter() && textEdit->getLang().isEmpty())
        syntaxHighlighting (textEdit, false);
}
/*************************/
// We want all dialogs to be window-modal as far as possible. However there is a problem:
// If a dialog is opened in a FeatherPad window and is closed after another dialog is
// opened in another window, the second dialog will be seen as a child of the first window.
// This could cause a crash if the dialog is closed after closing the first window.
// As a workaround, we keep window-modality but don't let the user open two window-modal dialogs.
bool FPwin::hasAnotherDialog()
{
    closeWarningBar();
    bool res = false;
    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        FPwin *win = singleton->Wins.at (i);
        if (win != this)
        {
            QList<QDialog*> dialogs = win->findChildren<QDialog*>();
            for (int j = 0; j < dialogs.count(); ++j)
            {
                if (dialogs.at (j)->objectName() != "processDialog"
                    && dialogs.at (j)->objectName() != "sessionDialog")
                {
                    res = true;
                    break;
                }
            }
            if (res) break;
        }
    }
    if (res)
    {
        showWarningBar("<center><b><big>" + tr ("Another FeatherPad window has a modal dialog!") + "</big></b></center>"
                       + "<center><i>" +tr ("Please attend to that window or just close its dialog!") + "</i></center>");
    }
    return res;
}
/*************************/
void FPwin::deleteTabPage (int tabIndex)
{
    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (tabIndex));
    if (sidePane_ && !sideItems_.isEmpty())
    {
        if (QListWidgetItem *wi = sideItems_.key (tabPage))
        {
            sideItems_.remove (wi);
            delete sidePane_->listWidget()->takeItem (sidePane_->listWidget()->row (wi));
        }
    }
    TextEdit *textEdit = tabPage->textEdit();
    if (textEdit->getSaveCursor())
    {
        QString fileName = textEdit->getFileName();
        if (!fileName.isEmpty())
        {
            Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
            config.saveCursorPos (fileName, textEdit->textCursor().position());
        }
    }
    /* because deleting the syntax highlighter changes the text,
       it is better to disconnect contentsChange() here to prevent a crash */
    disconnect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::hlight);
    disconnect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
    syntaxHighlighting (textEdit, false);
    ui->tabWidget->removeTab (tabIndex);
    delete tabPage; tabPage = nullptr;
}
/*************************/
// Here, "first" is the index/row, to whose right/bottom all tabs/rows are to be closed.
// Similarly, "last" is the index/row, to whose left/top all tabs/rows are to be closed.
// A negative value means including the start for "first" and the end for "last".
// If both "first" and "last" are negative, all tabs will be closed.
// The case, when they're both greater than -1, is covered but not used anywhere.
// Tabs/rows are always closed from right/bottom to left/top.
bool FPwin::closeTabs (int first, int last)
{
    if (!isReady()) return true;

    pauseAutoSaving (true);

    bool hasSideList (sidePane_ && !sideItems_.isEmpty());
    TabPage *curPage = nullptr;
    QListWidgetItem *curItem = nullptr;
    if (hasSideList)
    {
        int cur = sidePane_->listWidget()->currentRow();
        if (!(first < cur && (cur < last || last == -1)))
            curItem = sidePane_->listWidget()->currentItem();
    }
    else
    {
        int cur = ui->tabWidget->currentIndex();
        if (!(first < cur && (cur < last || last == -1)))
            curPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget());
    }
    bool keep = false;
    int index, count;
    DOCSTATE state = SAVED;
    while (state == SAVED && ui->tabWidget->count() > 0)
    {
        if (QGuiApplication::overrideCursor() == nullptr)
            waitToMakeBusy();

        if (last == 0) break; // no tab on the left
        if (last < 0) // close from the end
            index = ui->tabWidget->count() - 1; // the last tab/row
        else // if (last > 0)
            index = last - 1;

        if (first >= index)
            break;
        int tabIndex = hasSideList ? ui->tabWidget->indexOf (sideItems_.value (sidePane_->listWidget()->item (index)))
                                   : index;
        if (first == index - 1) // only one tab to be closed
            state = savePrompt (tabIndex, false);
        else
            state = savePrompt (tabIndex, true); // with a "No to all" button
        switch (state) {
        case SAVED: // close this tab and go to the next one on the left
            keep = false;
            deleteTabPage (tabIndex);

            if (last > -1) // also last > 0
                --last; // a left tab is removed

            /* final changes */
            count = ui->tabWidget->count();
            if (count == 0)
            {
                ui->actionReload->setDisabled (true);
                ui->actionSave->setDisabled (true);
                enableWidgets (false);
            }
            else if (count == 1)
            {
                ui->actionDetachTab->setDisabled (true);
                ui->actionRightTab->setDisabled (true);
                ui->actionLeftTab->setDisabled (true);
                ui->actionLastTab->setDisabled (true);
                ui->actionFirstTab->setDisabled (true);
            }
            break;
        case UNDECIDED: // stop quitting (cancel or can't save)
            keep = true;
            break;
        case DISCARDED: // no to all: close all tabs (and quit)
            keep = false;
            while (index > first)
            {
                if (last == 0) break;
                deleteTabPage (tabIndex);

                if (last < 0)
                    index = ui->tabWidget->count() - 1;
                else // if (last > 0)
                {
                    --last; // a left tab is removed
                    index = last - 1;
                }
                tabIndex = hasSideList ? ui->tabWidget->indexOf (sideItems_.value (sidePane_->listWidget()->item (index)))
                                       : index;

                count = ui->tabWidget->count();
                if (count == 0)
                {
                    ui->actionReload->setDisabled (true);
                    ui->actionSave->setDisabled (true);
                    enableWidgets (false);
                }
                else if (count == 1)
                {
                    ui->actionDetachTab->setDisabled (true);
                    ui->actionRightTab->setDisabled (true);
                    ui->actionLeftTab->setDisabled (true);
                    ui->actionLastTab->setDisabled (true);
                    ui->actionFirstTab->setDisabled (true);
                }
            }
            break;
        default:
            break;
        }
    }

    unbusy();
    if (!keep)
    { // restore the current page/item
        if (curPage)
            ui->tabWidget->setCurrentWidget (curPage);
        else if (curItem)
            sidePane_->listWidget()->setCurrentItem (curItem);
    }

    pauseAutoSaving (false);

    return keep;
}
/*************************/
void FPwin::copyTabFileName()
{
    if (rightClicked_ < 0) return;
    TabPage *tabPage;
    if (sidePane_)
        tabPage = sideItems_.value (sidePane_->listWidget()->item (rightClicked_));
    else
        tabPage = qobject_cast<TabPage*>(ui->tabWidget->widget (rightClicked_));
    QString fname = tabPage->textEdit()->getFileName();
    QApplication::clipboard()->setText (fname.section ('/', -1));
}
/*************************/
void FPwin::copyTabFilePath()
{
    if (rightClicked_ < 0) return;
    TabPage *tabPage;
    if (sidePane_)
        tabPage = sideItems_.value (sidePane_->listWidget()->item (rightClicked_));
    else
        tabPage = qobject_cast<TabPage*>(ui->tabWidget->widget (rightClicked_));
    QString str = tabPage->textEdit()->getFileName();
    str.chop (str.section ('/', -1).count());
    QApplication::clipboard()->setText (str);
}
/*************************/
void FPwin::closeAllTabs()
{
    closeTabs (-1, -1);
}
/*************************/
void FPwin::closeNextTabs()
{
    closeTabs (rightClicked_, -1);
}
/*************************/
void FPwin::closePreviousTabs()
{
    closeTabs (-1, rightClicked_);
}
/*************************/
void FPwin::closeOtherTabs()
{
    if (!closeTabs (rightClicked_, -1))
        closeTabs (-1, rightClicked_);
}
/*************************/
void FPwin::dragEnterEvent (QDragEnterEvent *event)
{
    if (findChildren<QDialog *>().count() == 0
        && (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat ("application/featherpad-tab")))
    {
        event->acceptProposedAction();
    }
}
/*************************/
void FPwin::dropEvent (QDropEvent *event)
{
    if (event->mimeData()->hasFormat ("application/featherpad-tab"))
        dropTab (event->mimeData()->data("application/featherpad-tab"));
    else
    {
        const QList<QUrl> urlList = event->mimeData()->urls();
        bool multiple (urlList.count() > 1 || isLoading());
        for (const QUrl &url : urlList)
            newTabFromName (url.adjusted (QUrl::NormalizePathSegments) // KDE may give a double slash
                               .toLocalFile(),
                            false,
                            multiple);
    }

    event->acceptProposedAction();
}
/*************************/
// This method checks if there's any text that isn't saved under a tab and,
// if there is, it activates the tab and shows an appropriate prompt dialog.
// "tabIndex" is always the tab index and not the item row (in the side-pane).
FPwin::DOCSTATE FPwin::savePrompt (int tabIndex, bool noToAll)
{
    DOCSTATE state = SAVED;
    TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->widget (tabIndex));
    TextEdit *textEdit = tabPage->textEdit();
    QString fname = textEdit->getFileName();
    bool isRemoved (!fname.isEmpty() && (!QFile::exists (fname) || !QFileInfo (fname).isFile()));
    if (textEdit->document()->isModified() || isRemoved)
    {
        unbusy(); // made busy at closeTabs()
        if (hasAnotherDialog()) return UNDECIDED; // cancel

        if (tabIndex != ui->tabWidget->currentIndex())
        { // switch to the page that needs attention
            if (sidePane_ && !sideItems_.isEmpty())
                sidePane_->listWidget()->setCurrentItem (sideItems_.key (tabPage)); // sets the current widget at changeTab()
            else
                ui->tabWidget->setCurrentIndex (tabIndex);
        }

        updateShortcuts (true);

        MessageBox msgBox (this);
        msgBox.setIcon (QMessageBox::Question);
        msgBox.setText ("<center><b><big>" + tr ("Save changes?") + "</big></b></center>");
        if (isRemoved)
            msgBox.setInformativeText ("<center><i>" + tr ("The file has been removed.") + "</i></center>");
        else
            msgBox.setInformativeText ("<center><i>" + tr ("The document has been modified.") + "</i></center>");
        if (noToAll && ui->tabWidget->count() > 1)
            msgBox.setStandardButtons (QMessageBox::Save
                                       | QMessageBox::Discard
                                       | QMessageBox::Cancel
                                       | QMessageBox::NoToAll);
        else
            msgBox.setStandardButtons (QMessageBox::Save
                                       | QMessageBox::Discard
                                       | QMessageBox::Cancel);
        msgBox.changeButtonText (QMessageBox::Save, tr ("Save"));
        msgBox.changeButtonText (QMessageBox::Discard, tr ("Discard changes"));
        msgBox.changeButtonText (QMessageBox::Cancel, tr ("Cancel"));
        if (noToAll)
            msgBox.changeButtonText (QMessageBox::NoToAll, tr ("No to all"));
        msgBox.setDefaultButton (QMessageBox::Save);
        msgBox.setWindowModality (Qt::WindowModal);
        /* enforce a central position */
        /*msgBox.show();
        msgBox.move (x() + width()/2 - msgBox.width()/2,
                     y() + height()/2 - msgBox.height()/ 2);*/
        switch (msgBox.exec()) {
        case QMessageBox::Save:
            if (!saveFile (true))
                state = UNDECIDED;
            break;
        case QMessageBox::Discard:
            break;
        case QMessageBox::Cancel:
            state = UNDECIDED;
            break;
        case QMessageBox::NoToAll:
            state = DISCARDED;
            break;
        default:
            state = UNDECIDED;
            break;
        }

        updateShortcuts (false);
    }
    return state;
}
/*************************/
// Enable or disable some widgets.
void FPwin::enableWidgets (bool enable) const
{
    if (!enable && ui->dockReplace->isVisible())
        ui->dockReplace->setVisible (false);
    if (!enable && ui->spinBox->isVisible())
    {
        ui->spinBox->setVisible (false);
        ui->label->setVisible (false);
        ui->checkBox->setVisible (false);
    }
    if ((!enable && ui->statusBar->isVisible())
        || (enable
            && static_cast<FPsingleton*>(qApp)->getConfig()
               .getShowStatusbar())) // starting from no tab
    {
        ui->statusBar->setVisible (enable);
    }

    ui->actionSelectAll->setEnabled (enable);
    ui->actionFind->setEnabled (enable);
    ui->actionJump->setEnabled (enable);
    ui->actionReplace->setEnabled (enable);
    ui->actionClose->setEnabled (enable);
    ui->actionSaveAs->setEnabled (enable);
    ui->menuEncoding->setEnabled (enable);
    ui->actionSaveCodec->setEnabled (enable);
    ui->actionFont->setEnabled (enable);
    ui->actionDoc->setEnabled (enable);
    ui->actionPrint->setEnabled (enable);

    if (!enable)
    {
        ui->actionUndo->setEnabled (false);
        ui->actionRedo->setEnabled (false);

        ui->actionEdit->setVisible (false);
        ui->actionRun->setVisible (false);

        ui->actionCut->setEnabled (false);
        ui->actionCopy->setEnabled (false);
        ui->actionPaste->setEnabled (false);
        ui->actionDate->setEnabled (false);
        ui->actionDelete->setEnabled (false);
    }
}
/*************************/
void FPwin::updateCustomizableShortcuts (bool disable)
{
    if (disable)
    {
        ui->actionLineNumbers->setShortcut (QKeySequence());
        ui->actionWrap->setShortcut (QKeySequence());
        ui->actionIndent->setShortcut (QKeySequence());
        ui->actionSyntax->setShortcut (QKeySequence());

        ui->actionNew->setShortcut (QKeySequence());
        ui->actionOpen->setShortcut (QKeySequence());
        ui->actionSave->setShortcut (QKeySequence());
        ui->actionFind->setShortcut (QKeySequence());
        ui->actionReplace->setShortcut (QKeySequence());
        ui->actionSaveAs->setShortcut (QKeySequence());
        ui->actionPrint->setShortcut (QKeySequence());
        ui->actionDoc->setShortcut (QKeySequence());
        ui->actionClose->setShortcut (QKeySequence());
        ui->actionQuit->setShortcut (QKeySequence());
        ui->actionPreferences->setShortcut (QKeySequence());
        ui->actionHelp->setShortcut (QKeySequence());
        ui->actionEdit->setShortcut (QKeySequence());
        ui->actionDetachTab->setShortcut (QKeySequence());
        ui->actionReload->setShortcut (QKeySequence());

        /* the shortcuts of these 3 actions don't need to be unset
           but they may need to be reset with Preferences dialog */
        ui->actionJump->setShortcut (QKeySequence());
        ui->actionRun->setShortcut (QKeySequence());
        ui->actionSession->setShortcut (QKeySequence());

        ui->actionSidePane->setShortcut (QKeySequence());

        ui->actionUndo->setShortcut (QKeySequence());
        ui->actionRedo->setShortcut (QKeySequence());
        ui->actionDate->setShortcut (QKeySequence());
    }
    else
    {
        QHash<QString, QString> ca = static_cast<FPsingleton*>(qApp)->
                                     getConfig().customShortcutActions();
        QList<QString> keys = ca.keys();

        ui->actionLineNumbers->setShortcut (keys.contains ("actionLineNumbers") ? ca.value ("actionLineNumbers") : QKeySequence (tr ("Ctrl+L")));
        ui->actionWrap->setShortcut (keys.contains ("actionWrap") ? ca.value ("actionWrap") : QKeySequence (tr ("Ctrl+W")));
        ui->actionIndent->setShortcut (keys.contains ("actionIndent") ? ca.value ("actionIndent") : QKeySequence (tr ("Ctrl+I")));
        ui->actionSyntax->setShortcut (keys.contains ("actionSyntax") ? ca.value ("actionSyntax") : QKeySequence (tr ("Ctrl+Shift+H")));

        ui->actionNew->setShortcut (keys.contains ("actionNew") ? ca.value ("actionNew") : QKeySequence (tr ("Ctrl+N")));
        ui->actionOpen->setShortcut (keys.contains ("actionOpen") ? ca.value ("actionOpen") : QKeySequence (tr ("Ctrl+O")));
        ui->actionSave->setShortcut (keys.contains ("actionSave") ? ca.value ("actionSave") : QKeySequence (tr ("Ctrl+S")));
        ui->actionFind->setShortcut (keys.contains ("actionFind") ? ca.value ("actionFind") : QKeySequence (tr ("Ctrl+F")));
        ui->actionReplace->setShortcut (keys.contains ("actionReplace") ? ca.value ("actionReplace") : QKeySequence (tr ("Ctrl+R")));
        ui->actionSaveAs->setShortcut (keys.contains ("actionSaveAs") ? ca.value ("actionSaveAs") : QKeySequence (tr ("Ctrl+Shift+S")));
        ui->actionPrint->setShortcut (keys.contains ("actionPrint") ? ca.value ("actionPrint") : QKeySequence (tr ("Ctrl+P")));
        ui->actionDoc->setShortcut (keys.contains ("actionDoc") ? ca.value ("actionDoc") : QKeySequence (tr ("Ctrl+Shift+D")));
        ui->actionClose->setShortcut (keys.contains ("actionClose") ? ca.value ("actionClose") : QKeySequence (tr ("Ctrl+Shift+Q")));
        ui->actionQuit->setShortcut (keys.contains ("actionQuit") ? ca.value ("actionQuit") : QKeySequence (tr ("Ctrl+Q")));
        ui->actionPreferences->setShortcut (keys.contains ("actionPreferences") ? ca.value ("actionPreferences") : QKeySequence (tr ("Ctrl+Shift+P")));
        ui->actionHelp->setShortcut (keys.contains ("actionHelp") ? ca.value ("actionHelp") : QKeySequence (tr ("Ctrl+H")));
        ui->actionEdit->setShortcut (keys.contains ("actionEdit") ? ca.value ("actionEdit") : QKeySequence (tr ("Ctrl+E")));
        ui->actionDetachTab->setShortcut (keys.contains ("actionDetachTab") ? ca.value ("actionDetachTab") : QKeySequence (tr ("Ctrl+T")));
        ui->actionReload->setShortcut (keys.contains ("actionReload") ? ca.value ("actionReload") : QKeySequence (tr ("Ctrl+Shift+R")));

        ui->actionJump->setShortcut (keys.contains ("actionJump") ? ca.value ("actionJump") : QKeySequence (tr ("Ctrl+J")));
        ui->actionRun->setShortcut (keys.contains ("actionRun") ? ca.value ("actionRun") : QKeySequence (tr ("Ctrl+E")));
        ui->actionSession->setShortcut (keys.contains ("actionSession") ? ca.value ("actionSession") : QKeySequence (tr ("Ctrl+M")));

        ui->actionSidePane->setShortcut (keys.contains ("actionSidePane") ? ca.value ("actionSidePane") : QKeySequence (tr ("Ctrl+Alt+P")));

        ui->actionUndo->setShortcut (keys.contains ("actionUndo") ? ca.value ("actionUndo") : QKeySequence (tr ("Ctrl+Z")));
        ui->actionRedo->setShortcut (keys.contains ("actionRedo") ? ca.value ("actionRedo") : QKeySequence (tr ("Ctrl+Shift+Z")));
        ui->actionDate->setShortcut (keys.contains ("actionDate") ? ca.value ("actionDate") : QKeySequence (tr ("Ctrl+Shift+V")));
    }
}
/*************************/
// When a window-modal dialog is shown, Qt doesn't disable the main window shortcuts.
// This is definitely a bug in Qt. As a workaround, we use this function to disable
// all shortcuts on showing a dialog and to enable them again on hiding it.
// The searchbar shortcuts of the current tab page are handled separately.
//
// This function also updates shortcuts after they're customized in the Preferences dialog.
void FPwin::updateShortcuts (bool disable, bool page)
{
    if (disable)
    {
        ui->actionCut->setShortcut (QKeySequence());
        ui->actionCopy->setShortcut (QKeySequence());
        ui->actionPaste->setShortcut (QKeySequence());
        ui->actionSelectAll->setShortcut (QKeySequence());

        ui->toolButtonNext->setShortcut (QKeySequence());
        ui->toolButtonPrv->setShortcut (QKeySequence());
        ui->toolButtonAll->setShortcut (QKeySequence());

        ui->actionRightTab->setShortcut (QKeySequence());
        ui->actionLeftTab->setShortcut (QKeySequence());
        ui->actionLastTab->setShortcut (QKeySequence());
        ui->actionFirstTab->setShortcut (QKeySequence());
    }
    else
    {
        ui->actionCut->setShortcut (QKeySequence (tr ("Ctrl+X")));
        ui->actionCopy->setShortcut (QKeySequence (tr ("Ctrl+C")));
        ui->actionPaste->setShortcut (QKeySequence (tr ("Ctrl+V")));
        ui->actionSelectAll->setShortcut (QKeySequence (tr ("Ctrl+A")));

        ui->toolButtonNext->setShortcut (QKeySequence (tr ("F7")));
        ui->toolButtonPrv->setShortcut (QKeySequence (tr ("F8")));
        ui->toolButtonAll->setShortcut (QKeySequence (tr ("F9")));

        if (QApplication::layoutDirection() == Qt::RightToLeft)
        {
            ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Left")));
            ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Right")));
        }
        else
        {
            ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Right")));
            ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Left")));
        }
        ui->actionLastTab->setShortcut (QKeySequence (tr ("Alt+Up")));
        ui->actionFirstTab->setShortcut (QKeySequence (tr ("Alt+Down")));
    }
    updateCustomizableShortcuts (disable);

    if (page) // disable/enable searchbar shortcuts of the current page too
    {
        if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
            tabPage->updateShortcuts (disable);
    }
}
/*************************/
void FPwin::newTab()
{
    createEmptyTab (!isLoading());
}
/*************************/
TabPage* FPwin::createEmptyTab (bool setCurrent, bool allowNormalHighlighter)
{
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();

    static const QStringList searchShortcuts = {tr ("F3"), tr ("F4"), tr ("F5"), tr ("F6")};
    TabPage *tabPage = new TabPage (iconMode_,
                                    config.getDarkColScheme() ? config.getDarkBgColorValue()
                                                              : config.getLightBgColorValue(),
                                    searchShortcuts,
                                    nullptr);
    TextEdit *textEdit = tabPage->textEdit();
    textEdit->setAutoBracket (config.getAutoBracket());
    textEdit->setScrollJumpWorkaround (config.getScrollJumpWorkaround());
    textEdit->setEditorFont (config.getFont());
    textEdit->setInertialScrolling (config.getInertialScrolling());
    textEdit->setDateFormat (config.getDateFormat());

    /* the (url) syntax highlighter will be created at tabSwitch() */
    if (config.getShowWhiteSpace()
        || config.getShowEndings()
        || config.getVLineDistance() > 0)
    {
        textEdit->setNormalAsUrl (true);
        if (allowNormalHighlighter)
            syntaxHighlighting (textEdit);
    }

    int index = ui->tabWidget->currentIndex();
    if (index == -1) enableWidgets (true);

    /* hide the searchbar consistently */
    if ((index == -1 && config.getHideSearchbar())
        || (index > -1 && !qobject_cast< TabPage *>(ui->tabWidget->widget (index))->isSearchBarVisible()))
    {
        tabPage->setSearchBarVisible (false);
    }

    ui->tabWidget->insertTab (index + 1, tabPage, tr ("Untitled"));

    /* set all preliminary properties */
    if (index >= 0)
    {
        ui->actionDetachTab->setEnabled (true);
        ui->actionRightTab->setEnabled (true);
        ui->actionLeftTab->setEnabled (true);
        ui->actionLastTab->setEnabled (true);
        ui->actionFirstTab->setEnabled (true);
    }
    ui->tabWidget->setTabToolTip (index + 1, tr ("Unsaved"));
    if (!ui->actionWrap->isChecked())
        textEdit->setLineWrapMode (QPlainTextEdit::NoWrap);
    if (!ui->actionIndent->isChecked())
        textEdit->setAutoIndentation (false);
    if (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible())
        textEdit->showLineNumbers (true);
    if (ui->spinBox->isVisible())
        connect (textEdit->document(), &QTextDocument::blockCountChanged, this, &FPwin::setMax);
    if (ui->statusBar->isVisible()
        || config.getShowStatusbar()) // when the main window is being created, isVisible() isn't set yet
    {
        int showCurPos = config.getShowCursorPos();
        if (setCurrent)
        {
            if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton"))
                wordButton->setVisible (false);
            QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
            statusLabel->setText ("<b>" + tr ("Encoding") + ":</b> <i>UTF-8</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>"
                                        + tr ("Lines") + ":</b> <i>1</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>"
                                        + tr ("Sel. Chars") + ":</b> <i>0</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>"
                                        + tr ("Words") + ":</b> <i>0</i>");
            if (showCurPos)
                showCursorPos();
        }
        connect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
        connect (textEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
        if (showCurPos)
            connect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::showCursorPos);
    }
    connect (textEdit->document(), &QTextDocument::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);
    connect (textEdit, &TextEdit::fileDropped, this, &FPwin::newTabFromName);
    connect (textEdit, &TextEdit::zoomedOut, this, &FPwin::reformat);

    connect (tabPage, &TabPage::find, this, &FPwin::find);
    connect (tabPage, &TabPage::searchFlagChanged, this, &FPwin::searchFlagChanged);

    /* I don't know why, under KDE, when text is selected
       for the first time, it isn't copied to the selection
       clipboard. Perhaps it has something to do with Klipper.
       I neither know why this s a workaround: */
    QApplication::clipboard()->text (QClipboard::Selection);

    if (sidePane_)
    {
        ListWidget *lw = sidePane_->listWidget();
        QListWidgetItem *lwi = new QListWidgetItem (tr ("Untitled"), lw);
        lwi->setToolTip (tr ("Unsaved"));
        sideItems_.insert (lwi, tabPage);
        lw->addItem (lwi);
        if (setCurrent
            || index == -1) // for tabs, it's done automatically
        {
            lw->setCurrentItem (lwi);
        }
    }

    if (setCurrent)
    {
        ui->tabWidget->setCurrentWidget (tabPage);
        textEdit->setFocus();
    }

    /* this isn't enough for unshading under all WMs */
    /*if (isMinimized())
        setWindowState (windowState() & (~Qt::WindowMinimized | Qt::WindowActive));*/
    if (static_cast<FPsingleton*>(qApp)->isX11() && isWindowShaded (winId()))
        unshadeWindow (winId());
    if (setCurrent)
    {
        activateWindow();
        raise();
    }

    return tabPage;
}
/*************************/
void FPwin::updateRecenMenu()
{
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    QStringList recentFiles = config.getRecentFiles();
    int recentNumber = config.getCurRecentFilesNumber();

    QList<QAction *> actions = ui->menuOpenRecently->actions();
    int recentSize = recentFiles.count();
    QFontMetrics metrics (ui->menuOpenRecently->font());
    int w = 150 * metrics.width (' ');
    for (int i = 0; i < recentNumber; ++i)
    {
        if (i < recentSize)
        {
            actions.at (i)->setText (metrics.elidedText (recentFiles.at (i), Qt::ElideMiddle, w));
            actions.at (i)->setData (recentFiles.at (i));
            actions.at (i)->setVisible (true);
        }
        else
        {
            actions.at (i)->setText (QString());
            actions.at (i)->setData (QVariant());
            actions.at (i)->setVisible (false);
        }
    }
    ui->actionClearRecent->setEnabled (recentSize != 0);
}
/*************************/
void FPwin::clearRecentMenu()
{
    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
    config.clearRecentFiles();
    updateRecenMenu();
}
/*************************/
void FPwin::reformat (TextEdit *textEdit)
{
    formatTextRect (textEdit->rect()); // in "syntax.cpp"
    if (!textEdit->getSearchedText().isEmpty())
        hlight(); // in "find.cpp"
}
/*************************/
void FPwin::zoomIn()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
        tabPage->textEdit()->zooming (1.f);
}
/*************************/
void FPwin::zoomOut()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
    {
        TextEdit *textEdit = tabPage->textEdit();
        textEdit->zooming (-1.f);
    }
}
/*************************/
void FPwin::zoomZero()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
    {
        TextEdit *textEdit = tabPage->textEdit();
        textEdit->zooming (0.f);
    }
}
/*************************/
void FPwin::defaultSize()
{
    QSize s = static_cast<FPsingleton*>(qApp)->getConfig().getStartSize();
    if (size() == s) return;
    if (isMaximized() || isFullScreen())
        showNormal();
    /*if (isMaximized() && isFullScreen())
        showMaximized();
    if (isMaximized())
        showNormal();*/
    /* instead of hiding, reparent with the dummy
       widget to guarantee resizing under all DEs */
    /*Qt::WindowFlags flags = windowFlags();
    setParent (dummyWidget, Qt::SubWindow);*/
    hide();
    resize (s);
    /*if (parent() != nullptr)
        setParent (nullptr, flags);*/
    QTimer::singleShot (0, this, SLOT (show()));
}
/*************************/
/*void FPwin::align()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QTextOption opt = textEdit->document()->defaultTextOption();
    if (opt.alignment() == (Qt::AlignLeft))
    {
        opt = QTextOption (Qt::AlignRight);
        opt.setTextDirection (Qt::LayoutDirectionAuto);
        textEdit->document()->setDefaultTextOption (opt);
    }
    else if (opt.alignment() == (Qt::AlignRight))
    {
        opt = QTextOption (Qt::AlignLeft);
        opt.setTextDirection (Qt::LayoutDirectionAuto);
        textEdit->document()->setDefaultTextOption (opt);
    }
}*/
/*************************/
void FPwin::executeProcess()
{
    QList<QDialog*> dialogs = findChildren<QDialog*>();
    for (int i = 0; i < dialogs.count(); ++i)
    {
        if (dialogs.at (i)->objectName() != "processDialog"
            && dialogs.at (i)->objectName() != "sessionDialog")
        {
            return; // shortcut may work when there's a modal dialog
        }
    }
    closeWarningBar();

    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    if (!config.getExecuteScripts()) return;

    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
    {
        if (tabPage->findChild<QProcess *>(QString(), Qt::FindDirectChildrenOnly))
        {
            showWarningBar ("<center><b><big>" + tr ("Another process is running in this tab!") + "</big></b></center>"
                            + "<center><i>" + tr ("Only one process is allowed per tab.") + "</i></center>");
            return;
        }

        QString fName = tabPage->textEdit()->getFileName();
        if (!isScriptLang (tabPage->textEdit()->getProg())  || !QFileInfo (fName).isExecutable())
            return;

        QProcess *process = new QProcess (tabPage);
        process->setObjectName (fName); // to put it into the message dialog
        connect(process, &QProcess::readyReadStandardOutput,this, &FPwin::displayOutput);
        connect(process, &QProcess::readyReadStandardError,this, &FPwin::displayError);
        QString command = config.getExecuteCommand();
        if (!command.isEmpty())
            command +=  " ";
        fName.replace ("\"", "\"\"\""); // literal quotes in the command are shown by triple quotes
        process->start (command + "\"" + fName + "\"");
        connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                [=](int /*exitCode*/, QProcess::ExitStatus /*exitStatus*/){ process->deleteLater(); });
    }
}
/*************************/
bool FPwin::isScriptLang (QString lang)
{
    return (lang == "sh" || lang == "python"
            || lang == "ruby" || lang == "lua"
            || lang == "perl");
}
/*************************/
void FPwin::exitProcess()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
    {
        if (QProcess *process = tabPage->findChild<QProcess *>(QString(), Qt::FindDirectChildrenOnly))
            process->kill();
    }
}
/*************************/
void FPwin::displayMessage (bool error)
{
    QProcess *process = static_cast<QProcess*>(QObject::sender());
    if (!process) return; // impossible
    QByteArray msg;
    if (error)
    {
        process->setReadChannel(QProcess::StandardError);
        msg = process->readAllStandardError();
    }
    else
    {
        process->setReadChannel(QProcess::StandardOutput);
        msg = process->readAllStandardOutput();
    }
    if (msg.isEmpty()) return;

    QPointer<QDialog> msgDlg = nullptr;
    QList<QDialog*> dialogs = findChildren<QDialog*>();
    for (int i = 0; i < dialogs.count(); ++i)
    {
        if (dialogs.at (i)->parent() == process->parent())
        {
            msgDlg = dialogs.at (i);
            break;
        }
    }
    if (msgDlg)
    { // append to the existing message
        if (QPlainTextEdit *tEdit = msgDlg->findChild<QPlainTextEdit*>())
        {
            tEdit->setPlainText (tEdit->toPlainText() + "\n" + msg.constData());
            QTextCursor cur = tEdit->textCursor();
            cur.movePosition (QTextCursor::End);
            tEdit->setTextCursor (cur);
        }
    }
    else
    {
        msgDlg = new QDialog (qobject_cast<QWidget*>(process->parent()));
        msgDlg->setObjectName ("processDialog");
        msgDlg->setWindowTitle (tr ("Script Output"));
        msgDlg->setSizeGripEnabled (true);
        QGridLayout *grid = new QGridLayout;
        QLabel *label = new QLabel (msgDlg);
        label->setText ("<center><b>" + tr ("Script File") + ": </b></center><i>" + process->objectName() + "</i>");
        label->setTextInteractionFlags (Qt::TextSelectableByMouse);
        label->setWordWrap (true);
        label->setMargin (5);
        grid->addWidget (label, 0, 0, 1, 2);
        QPlainTextEdit *tEdit = new QPlainTextEdit (msgDlg);
        tEdit->setTextInteractionFlags (Qt::TextSelectableByMouse);
        tEdit->ensureCursorVisible();
        grid->addWidget (tEdit, 1, 0, 1, 2);
        QPushButton *closeButton = new QPushButton (QIcon::fromTheme ("edit-delete"), tr ("Close"));
        connect (closeButton, &QAbstractButton::clicked, msgDlg, &QDialog::reject);
        grid->addWidget (closeButton, 2, 1, Qt::AlignRight);
        QPushButton *clearButton = new QPushButton (QIcon::fromTheme ("edit-clear"), tr ("Clear"));
        connect (clearButton, &QAbstractButton::clicked, tEdit, &QPlainTextEdit::clear);
        grid->addWidget (clearButton, 2, 0, Qt::AlignLeft);
        msgDlg->setLayout (grid);
        tEdit->setPlainText (msg.constData());
        QTextCursor cur = tEdit->textCursor();
        cur.movePosition (QTextCursor::End);
        tEdit->setTextCursor (cur);
        msgDlg->setAttribute (Qt::WA_DeleteOnClose);
        msgDlg->show();
        msgDlg->raise();
        msgDlg->activateWindow();
    }
}
/*************************/
void FPwin::displayOutput()
{
    displayMessage (false);
}
/*************************/
void FPwin::displayError()
{
    displayMessage (true);
}
/*************************/
// This closes either the current page or the right-clicked side-pane item but
// never the right-clicked tab because the tab context menu has no closing item.
void FPwin::closeTab()
{
    if (!isReady()) return;

    pauseAutoSaving (true);

    QListWidgetItem *curItem = nullptr;
    int index = -1;
    if (sidePane_ && rightClicked_ >= 0) // close the right-clicked item
    {
        index = ui->tabWidget->indexOf (sideItems_.value (sidePane_->listWidget()->item (rightClicked_)));
        if (index != ui->tabWidget->currentIndex())
            curItem = sidePane_->listWidget()->currentItem();
    }
    else // close the current page
    {
        index = ui->tabWidget->currentIndex();
        if (index == -1)  // not needed
        {
            pauseAutoSaving (false);
            return;
        }
    }

    if (savePrompt (index, false) != SAVED)
    {
        pauseAutoSaving (false);
        return;
    }

    deleteTabPage (index);
    int count = ui->tabWidget->count();
    if (count == 0)
    {
        ui->actionReload->setDisabled (true);
        ui->actionSave->setDisabled (true);
        enableWidgets (false);
    }
    else // set focus to text-edit
    {
        if (count == 1)
        {
            ui->actionDetachTab->setDisabled (true);
            ui->actionRightTab->setDisabled (true);
            ui->actionLeftTab->setDisabled (true);
            ui->actionLastTab->setDisabled (true);
            ui->actionFirstTab->setDisabled (true);
        }

        if (curItem) // restore the current item
            sidePane_->listWidget()->setCurrentItem (curItem);

        if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
            tabPage->textEdit()->setFocus();
    }

    pauseAutoSaving (false);
}
/*************************/
void FPwin::closeTabAtIndex (int index)
{
    pauseAutoSaving (true);

    TabPage *curPage = nullptr;
    if (index != ui->tabWidget->currentIndex())
        curPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget());
    if (savePrompt (index, false) != SAVED)
    {
        pauseAutoSaving (false);
        return;
    }
    closeWarningBar();

    deleteTabPage (index);
    int count = ui->tabWidget->count();
    if (count == 0)
    {
        ui->actionReload->setDisabled (true);
        ui->actionSave->setDisabled (true);
        enableWidgets (false);
    }
    else
    {
        if (count == 1)
        {
            ui->actionDetachTab->setDisabled (true);
            ui->actionRightTab->setDisabled (true);
            ui->actionLeftTab->setDisabled (true);
            ui->actionLastTab->setDisabled (true);
            ui->actionFirstTab->setDisabled (true);
        }

        if (curPage) // restore the current page
            ui->tabWidget->setCurrentWidget (curPage);

        if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
            tabPage->textEdit()->setFocus();
    }

    pauseAutoSaving (false);
}
/*************************/
void FPwin::setTitle (const QString& fileName, int tabIndex)
{
    int index = tabIndex;
    if (index < 0)
        index = ui->tabWidget->currentIndex(); // is never -1

    bool isLink (false);
    QString shownName;
    if (fileName.isEmpty())
        shownName = tr ("Untitled");
    else
    {
        isLink = QFileInfo (fileName).isSymLink();
        shownName = fileName.section ('/', -1);
        shownName.replace ("\n", " "); // no multi-line tab text
    }

    if (tabIndex < 0)
        setWindowTitle (shownName);

    shownName.replace ("&", "&&"); // single ampersand is for mnemonic
    ui->tabWidget->setTabText (index, shownName);
    if (isLink)
        ui->tabWidget->setTabIcon (index, QIcon (":icons/link.svg"));
    else
        ui->tabWidget->setTabIcon (index, QIcon());

    if (sidePane_ && !sideItems_.isEmpty())
    {
        if (QListWidgetItem *wi = sideItems_.key (qobject_cast<TabPage*>(ui->tabWidget->widget (index))))
        {
            wi->setText (shownName);
            if (isLink)
                wi->setIcon (QIcon (":icons/link.svg"));
            else
                wi->setIcon (QIcon());
        }
    }
}
/*************************/
void FPwin::asterisk (bool modified)
{
    int index = ui->tabWidget->currentIndex();

    QString fname = qobject_cast< TabPage *>(ui->tabWidget->widget (index))
                    ->textEdit()->getFileName();
    QString shownName;
    if (fname.isEmpty())
        shownName = tr ("Untitled");
    else
        shownName = fname.section ('/', -1);
    if (modified)
        shownName.prepend ("*");
    shownName.replace ("\n", " ");

    setWindowTitle (shownName);

    shownName.replace ("&", "&&");
    ui->tabWidget->setTabText (index, shownName);

    if (sidePane_)
    {
        if (modified)
        {
            shownName.remove (0, 1);
            shownName.append ("*");
        }
        sidePane_->listWidget()->currentItem()->setText (shownName);
    }
}
/*************************/
void FPwin::waitToMakeBusy()
{
    if (busyThread_ != nullptr) return;

    busyThread_ = new QThread;
    BusyMaker *makeBusy = new BusyMaker();
    makeBusy->moveToThread (busyThread_);
    connect (busyThread_, &QThread::started, makeBusy, &BusyMaker::waiting);
    connect (busyThread_, &QThread::finished, busyThread_, &QObject::deleteLater);
    connect (busyThread_, &QThread::finished, makeBusy, &QObject::deleteLater);
    connect (makeBusy, &BusyMaker::finished, busyThread_, &QThread::quit);
    busyThread_->start();
}
/*************************/
void FPwin::unbusy()
{
    if (busyThread_ && !busyThread_->isFinished())
    {
        busyThread_->quit();
        busyThread_->wait();
    }
    if (QGuiApplication::overrideCursor() != nullptr)
        QGuiApplication::restoreOverrideCursor();
}
/*************************/
void FPwin::loadText (const QString& fileName, bool enforceEncod, bool reload,
                      bool saveCursor, bool enforceUneditable, bool multiple)
{
    if (loadingProcesses_ == 0)
        closeWarningBar();
    ++ loadingProcesses_;
    QString charset;
    if (enforceEncod)
        charset = checkToEncoding();
    Loading *thread = new Loading (fileName, charset, reload, saveCursor, enforceUneditable, multiple);
    connect (thread, &Loading::completed, this, &FPwin::addText);
    connect (thread, &Loading::finished, thread, &QObject::deleteLater);
    thread->start();

    if (QGuiApplication::overrideCursor() == nullptr)
        waitToMakeBusy();
    ui->tabWidget->tabBar()->lockTabs (true);
    updateShortcuts (true, false);
}
/*************************/
// When multiple files are being loaded, we don't change the current tab.
void FPwin::addText (const QString& text, const QString& fileName, const QString& charset,
                     bool enforceEncod, bool reload, bool saveCursor,
                     bool uneditable,
                     bool multiple)
{
    if (fileName.isEmpty() || charset.isEmpty())
    {
        if (!fileName.isEmpty() && charset.isEmpty()) // means a very large file
            connect (this, &FPwin::finishedLoading, this, &FPwin::onOpeningHugeFiles, Qt::UniqueConnection);
        else
            connect (this, &FPwin::finishedLoading, this, &FPwin::onPermissionDenied, Qt::UniqueConnection);
        -- loadingProcesses_; // can never become negative
        if (!isLoading())
        {
            unbusy();
            ui->tabWidget->tabBar()->lockTabs (false);
            updateShortcuts (false, false);
            emit finishedLoading();
        }
        return;
    }

    if (enforceEncod || reload)
        multiple = false; // respect the logic

    /* only for the side-pane mode */
    static bool scrollToFirstItem (false);
    static TabPage *firstPage = nullptr;

    TextEdit *textEdit;
    TabPage *tabPage;
    if (ui->tabWidget->currentIndex() == -1)
        tabPage = createEmptyTab (!multiple, false);
    else
        tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget());
    textEdit = tabPage->textEdit();

    bool openInCurrentTab (true);
    if (!reload
        && !enforceEncod
        && (!textEdit->document()->isEmpty()
            || textEdit->document()->isModified()
            || !textEdit->getFileName().isEmpty()))
    {
        tabPage = createEmptyTab (!multiple, false);
        textEdit = tabPage->textEdit();
        openInCurrentTab = false;
    }
    else
    {
        if (sidePane_ && !reload && !enforceEncod) // an unused empty tab
            scrollToFirstItem = true;
        /*if (isMinimized())
            setWindowState (windowState() & (~Qt::WindowMinimized | Qt::WindowActive));*/
        if (static_cast<FPsingleton*>(qApp)->isX11() && isWindowShaded (winId()))
            unshadeWindow (winId());
        activateWindow();
        raise();
    }
    textEdit->setSaveCursor (saveCursor);

    /* uninstall the syntax highlgihter to reinstall it below (when the text is reloaded,
       its encoding is enforced, or a new tab with normal as url was opened here) */
    if (textEdit->getHighlighter())
    {
        textEdit->setGreenSel (QList<QTextEdit::ExtraSelection>()); // they'll have no meaning later
        syntaxHighlighting (textEdit, false);
    }

    QFileInfo fInfo (fileName);

    if (scrollToFirstItem
        && (!firstPage
            || firstPage->textEdit()->getFileName().section ('/', -1).
               compare (fInfo.fileName(), Qt::CaseInsensitive) > 0))
    {
        firstPage = tabPage;
    }

    /* this workaround, for the RTL bug in QPlainTextEdit, isn't needed
       because a better workaround is included in textedit.cpp */
    /*QTextOption opt = textEdit->document()->defaultTextOption();
    if (text.isRightToLeft())
    {
        if (opt.alignment() == (Qt::AlignLeft))
        {
            opt = QTextOption (Qt::AlignRight);
            opt.setTextDirection (Qt::LayoutDirectionAuto);
            textEdit->document()->setDefaultTextOption (opt);
        }
    }
    else if (opt.alignment() == (Qt::AlignRight))
    {
        opt = QTextOption (Qt::AlignLeft);
        opt.setTextDirection (Qt::LayoutDirectionAuto);
        textEdit->document()->setDefaultTextOption (opt);
    }*/

    /* we want to restore the cursor later */
    int pos = 0, anchor = 0;
    int scrollbarValue = -1;
    if (reload)
    {
        pos = textEdit->textCursor().position();
        anchor = textEdit->textCursor().anchor();
        if (QScrollBar *scrollbar = textEdit->verticalScrollBar())
        {
            if (scrollbar->isVisible())
                scrollbarValue = scrollbar->value();
        }
    }

    /* set the text */
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    textEdit->setPlainText (text);
    connect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);

    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();

    /* now, restore the cursor */
    if (reload)
    {
        QTextCursor cur = textEdit->textCursor();
        cur.movePosition (QTextCursor::End, QTextCursor::MoveAnchor);
        int curPos = cur.position();
        if (anchor <= curPos && pos <= curPos)
        {
            cur.setPosition (anchor);
            cur.setPosition (pos, QTextCursor::KeepAnchor);
        }
        textEdit->setTextCursor (cur);
    }
    else if (saveCursor)
    {
        QHash<QString, QVariant> cursorPos = config.savedCursorPos();
        if (cursorPos.contains (fileName))
        {
            QTextCursor cur = textEdit->textCursor();
            cur.movePosition (QTextCursor::End, QTextCursor::MoveAnchor);
            int pos = qMin (qMax (cursorPos.value (fileName, 0).toInt(), 0), cur.position());
            cur.setPosition (pos);
            textEdit->setTextCursor (cur);
        }
    }

    textEdit->setFileName (fileName);
    textEdit->setSize (fInfo.size());
    textEdit->setLastModified (fInfo.lastModified());
    lastFile_ = fileName;
    if (config.getRecentOpened())
        config.addRecentFile (lastFile_);
    textEdit->setEncoding (charset);
    textEdit->setWordNumber (-1);
    if (uneditable)
    {
        connect (this, &FPwin::finishedLoading, this, &FPwin::onOpeningUneditable, Qt::UniqueConnection);
        textEdit->makeUneditable (uneditable);
    }
    setProgLang (textEdit);
    if (ui->actionSyntax->isChecked())
        syntaxHighlighting (textEdit);
    setTitle (fileName, (multiple && !openInCurrentTab) ?
                        /* the index may have changed because syntaxHighlighting() waits for
                           all events to be processed (but it won't change from here on) */
                        ui->tabWidget->indexOf (tabPage) : -1);
    QString tip (fInfo.absolutePath() + "/");
    QFontMetrics metrics (QToolTip::font());
    int w = QApplication::desktop()->screenGeometry().width();
    if (w > 200 * metrics.width (' ')) w = 200 * metrics.width (' ');
    QString elidedTip = metrics.elidedText (tip, Qt::ElideMiddle, w);
    ui->tabWidget->setTabToolTip (ui->tabWidget->indexOf (tabPage), elidedTip);
    if (!sideItems_.isEmpty())
    {
        if (QListWidgetItem *wi = sideItems_.key (tabPage))
            wi->setToolTip (elidedTip);
    }

    if (uneditable || alreadyOpen (tabPage))
    {
        textEdit->setReadOnly (true);
        if (!textEdit->hasDarkScheme())
        {
            if (uneditable) // as with Help
                textEdit->viewport()->setStyleSheet (".QWidget {"
                                                     "color: black;"
                                                     "background-color: rgb(225, 238, 255);}");
            else
                textEdit->viewport()->setStyleSheet (".QWidget {"
                                                     "color: black;"
                                                     "background-color: rgb(236, 236, 208);}");
        }
        else
        {
            if (uneditable)
                textEdit->viewport()->setStyleSheet (".QWidget {"
                                                     "color: white;"
                                                     "background-color: rgb(0, 60, 110);}");
            else
                textEdit->viewport()->setStyleSheet (".QWidget {"
                                                     "color: white;"
                                                     "background-color: rgb(60, 0, 0);}");
        }
        if (!multiple || openInCurrentTab)
        {
            if (!uneditable)
                ui->actionEdit->setVisible (true);
            else
                ui->actionSaveAs->setDisabled (true);
            ui->actionCut->setDisabled (true);
            ui->actionPaste->setDisabled (true);
            ui->actionDate->setDisabled (true);
            ui->actionDelete->setDisabled (true);
        }
        disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
        disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    }
    else if (textEdit->isReadOnly())
        QTimer::singleShot (0, this, SLOT (makeEditable()));

    if (!multiple || openInCurrentTab)
    {
        if (ui->statusBar->isVisible())
        {
            statusMsgWithLineCount (textEdit->document()->blockCount());
            if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton"))
                wordButton->setVisible (true);
            if (text.isEmpty())
                updateWordInfo();
        }
        if (config.getShowLangSelector() && config.getSyntaxByDefault())
            showLang (textEdit);
        encodingToCheck (charset);
        ui->actionReload->setEnabled (true);
        textEdit->setFocus(); // the text may have been opened in this (empty) tab

        if (openInCurrentTab)
        {
            if (isScriptLang (textEdit->getProg()) && fInfo.isExecutable())
                ui->actionRun->setVisible (config.getExecuteScripts());
            else
                ui->actionRun->setVisible (false);
        }
    }

    /* a file is completely loaded */
    -- loadingProcesses_;
    if (!isLoading())
    {
        unbusy();
        ui->tabWidget->tabBar()->lockTabs (false);
        updateShortcuts (false, false);
        if (reload && scrollbarValue > -1)
        { // restore the scrollbar position
            lambdaConnection_ = QObject::connect (this, &FPwin::finishedLoading, textEdit,
                                                  [this, textEdit, scrollbarValue]() {
                if (QScrollBar *scrollbar = textEdit->verticalScrollBar())
                {
                    if (scrollbar->isVisible())
                        scrollbar->setValue (scrollbarValue);
                }
                disconnectLambda();
            });
        }
        /* select the first item (sidePane_ exists) */
        else if (firstPage && !sideItems_.isEmpty())
        {
            if (QListWidgetItem *wi = sideItems_.key (firstPage))
                sidePane_->listWidget()->setCurrentItem (wi);
        }
        /* reset the static variables */
        scrollToFirstItem = false;
        firstPage = nullptr;

        emit finishedLoading();
    }
}
/*************************/
void FPwin::disconnectLambda()
{
    QObject::disconnect (lambdaConnection_);
}
/*************************/
void FPwin::onOpeningHugeFiles()
{
    disconnect (this, &FPwin::finishedLoading, this, &FPwin::onOpeningHugeFiles);
    QTimer::singleShot (100, this, [=]() { // TabWidget has a 50-ms timer
        showWarningBar ("<center><b><big>" + tr ("Huge file(s) not opened!") + "</big></b></center>\n"
                        + "<center>" + tr ("FeatherPad does not open files larger than 100 MiB.") + "</center>");
    });
}
/*************************/
void FPwin::onPermissionDenied()
{
    disconnect (this, &FPwin::finishedLoading, this, &FPwin::onPermissionDenied);
    QTimer::singleShot (100, this, [=]() {
        showWarningBar ("<center><b><big>" + tr ("Some file(s) could not be opened!") + "</big></b></center>\n"
                        + "<center>" + tr ("You may not have the permission to read.") + "</center>");
    });
}
/*************************/
void FPwin::onOpeningUneditable()
{
    disconnect (this, &FPwin::finishedLoading, this, &FPwin::onOpeningUneditable);
    QTimer::singleShot (100, this, [=]() {
        this->showWarningBar ("<center><b><big>" + tr ("Uneditable file(s)!") + "</big></b></center>\n"
                              + "<center>" + tr ("Non-text files or files with huge lines cannot be edited.") + "</center>");
    });
}
/*************************/
void FPwin::showWarningBar (const QString& message)
{
    /* don't close and show the same warning bar */
    if (QLayoutItem *item = ui->verticalLayout->itemAt (ui->verticalLayout->count() - 1))
    {
        if (WarningBar *wb = qobject_cast<WarningBar*>(item->widget()))
        {
            if (wb->getMessage() == message)
                return;
            ui->verticalLayout->removeWidget (item->widget());
            delete wb;
        }
    }

    WarningBar *bar = new WarningBar (message, iconMode_);
    ui->verticalLayout->insertWidget (ui->verticalLayout->count(), bar); // at end
    connect (bar, &WarningBar::closeButtonPressed, [=] {
        ui->verticalLayout->removeWidget(bar);
        bar->deleteLater();
    });
    /* close the bar when the text is scrolled */
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
    {
        connect (tabPage->textEdit(), &QPlainTextEdit::updateRequest, bar, [=](const QRect&, int dy) {
            if (dy != 0)
                closeWarningBar();
        });
    }
}
/*************************/
void FPwin::showCrashWarning()
{
    QTimer::singleShot (0, this, [=]() {
        this->showWarningBar ("<center><b><big>" + tr ("A previous crash detected!") + "</big></b></center>"
                              + "<center><i>" +tr ("Preferably, close all FeatherPad windows and start again!") + "</i></center>");
    });
}
/*************************/
void FPwin::closeWarningBar()
{
    if (QLayoutItem *item = ui->verticalLayout->itemAt (ui->verticalLayout->count() - 1))
    {
        if (WarningBar *wb = qobject_cast<WarningBar*>(item->widget()))
        {
            ui->verticalLayout->removeWidget (item->widget());
            delete wb; // delete it immediately because a modal dialog might pop up
        }
    }
}
/*************************/
void FPwin::newTabFromName (const QString& fileName, bool saveCursor,
                            bool multiple)
{
    if (!fileName.isEmpty()
        /* although loadText() takes care of folders, we don't want to open
           (a symlink to) /dev/null and then, get a prompt dialog on closing */
        && QFileInfo (fileName).isFile())
    {
        loadText (fileName, false, false,
                  saveCursor, false, multiple);
    }
}
/*************************/
void FPwin::newTabFromRecent()
{
    QAction *action = qobject_cast<QAction*>(QObject::sender());
    if (!action) return;
    loadText (action->data().toString(), false, false);
}
/*************************/
void FPwin::fileOpen()
{
    if (isLoading()) return;

    /* find a suitable directory */
    QString fname;
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
        fname = tabPage->textEdit()->getFileName();

    QString path;
    if (!fname.isEmpty())
    {
        if (QFile::exists (fname))
            path = fname;
        else
        {
            QDir dir = QFileInfo (fname).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            path = dir.path();
        }
    }
    else
    {
        /* I like the last opened file to be remembered */
        fname = lastFile_;
        if (!fname.isEmpty())
        {
            QDir dir = QFileInfo (fname).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            path = dir.path();
        }
        else
        {
            QDir dir = QDir::home();
            path = dir.path();
        }
    }

    if (hasAnotherDialog()) return;
    updateShortcuts (true);
    QString filter = tr ("All Files (*)");
    if (!fname.isEmpty()
        && QFileInfo (fname).fileName().contains ('.'))
    {
        /* if relevant, do filtering to make opening of similar files easier */
        filter = tr ("All Files (*);;.%1 Files (*.%1)").arg (fname.section ('.', -1, -1));
    }
    FileDialog dialog (this, static_cast<FPsingleton*>(qApp)->getConfig().getNativeDialog());
    dialog.setAcceptMode (QFileDialog::AcceptOpen);
    dialog.setWindowTitle (tr ("Open file..."));
    dialog.setFileMode (QFileDialog::ExistingFiles);
    dialog.setNameFilter (filter);
    /*dialog.setLabelText (QFileDialog::Accept, tr ("Open"));
    dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
    if (QFileInfo (path).isDir())
        dialog.setDirectory (path);
    else
    {
        dialog.selectFile (path);
        dialog.autoScroll();
    }
    if (dialog.exec())
    {
        const QStringList files = dialog.selectedFiles();
        bool multiple (files.count() > 1 || isLoading());
        for (const QString &file : files)
            newTabFromName (file, false, multiple);
    }
    updateShortcuts (false);
}
/*************************/
// Check if the file is already opened for editing somewhere else.
bool FPwin::alreadyOpen (TabPage *tabPage) const
{
    bool res = false;

    QString fileName = tabPage->textEdit()->getFileName();
    QFileInfo info (fileName);
    QString target = info.isSymLink() ? info.symLinkTarget() // consider symlinks too
                                      : fileName;
    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        FPwin *thisOne = singleton->Wins.at (i);
        for (int j = 0; j < thisOne->ui->tabWidget->count(); ++j)
        {
            TabPage *thisTabPage = qobject_cast<TabPage*>(thisOne->ui->tabWidget->widget (j));
            if (thisOne == this && thisTabPage == tabPage)
                continue;
            TextEdit *thisTextEdit = thisTabPage->textEdit();
            if (thisTextEdit->isReadOnly())
                continue;
            QFileInfo thisInfo (thisTextEdit->getFileName());
            QString thisTarget = thisInfo.isSymLink() ? thisInfo.symLinkTarget()
                                                      : thisTextEdit->getFileName();
            if (thisTarget == target)
            {
                res = true;
                break;
            }
        }
        if (res) break;
    }
    return res;
}
/*************************/
void FPwin::enforceEncoding (QAction*)
{
    /* here, we don't need to check if some files are loading
       because encoding has no keyboard shortcut or tool button */

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QString fname = textEdit->getFileName();
    if (!fname.isEmpty())
    {
        if (savePrompt (index, false) != SAVED)
        { // back to the previous encoding
            encodingToCheck (textEdit->getEncoding());
            return;
        }
        textEdit->setLang (QString()); // remove the enforced syntax
        loadText (fname, true, true,
                  false, textEdit->isUneditable(), false);
    }
    else
    {
        /* just change the statusbar text; the doc
           might be saved later with the new encoding */
        textEdit->setEncoding (checkToEncoding());
        if (ui->statusBar->isVisible())
        {
            QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
            QString str = statusLabel->text();
            QString encodStr = tr ("Encoding");
            // the next info is about lines; there's no syntax info
            QString lineStr = "</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
            int i = str.indexOf (encodStr);
            int j = str.indexOf (lineStr);
            int offset = encodStr.count() + 9; // size of ":</b> <i>"
            str.replace (i + offset, j - i - offset, checkToEncoding());
            statusLabel->setText (str);
        }
    }
}
/*************************/
void FPwin::reload()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (savePrompt (index, false) != SAVED) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    textEdit->setLang (QString()); // remove the enforced syntax
    QString fname = textEdit->getFileName();
    if (!fname.isEmpty())
    {
        loadText (fname, false, true,
                  textEdit->getSaveCursor());
    }
}
/*************************/
static inline int trailingSpaces (const QString &str)
{
    int i = 0;
    while (i < str.length())
    {
        if (!str.at (str.length() - 1 - i).isSpace())
            return i;
        ++i;
    }
    return i;
}
/*************************/
// This is for both "Save" and "Save As"
bool FPwin::saveFile (bool keepSyntax)
{
    if (!isReady()) return false;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return false;

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();
    QString fname = textEdit->getFileName();
    if (fname.isEmpty()) fname = lastFile_;
    QString filter = tr ("All Files (*)");
    if (!fname.isEmpty()
        && QFileInfo (fname).fileName().contains ('.'))
    {
        /* if relevant, do filtering to prevent disastrous overwritings */
        filter = tr (".%1 Files (*.%1);;All Files (*)").arg (fname.section ('.', -1, -1));
    }

    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();

    if (fname.isEmpty()
        || !QFile::exists (fname)
        || textEdit->getFileName().isEmpty())
    {
        bool restorable = false;
        if (fname.isEmpty())
        {
            QDir dir = QDir::home();
            fname = dir.filePath (tr ("Untitled"));
        }
        else if (!QFile::exists (fname))
        {
            QDir dir = QFileInfo (fname).absoluteDir();
            if (!dir.exists())
            {
                dir = QDir::home();
                if (textEdit->getFileName().isEmpty())
                    filter = tr ("All Files (*)");
            }
            /* if the removed file is opened in this tab and its
               containing folder still exists, it's restorable */
            else if (!textEdit->getFileName().isEmpty())
                restorable = true;

            /* add the file name */
            if (!textEdit->getFileName().isEmpty())
                fname = dir.filePath (QFileInfo (fname).fileName());
            else
                fname = dir.filePath (tr ("Untitled"));
        }
        else
            fname = QFileInfo (fname).absoluteDir().filePath (tr ("Untitled"));

        /* use Save-As for Save or saving */
        if (!restorable
            && QObject::sender() != ui->actionSaveAs
            && QObject::sender() != ui->actionSaveCodec)
        {
            if (hasAnotherDialog()) return false;
            updateShortcuts (true);
            FileDialog dialog (this, config.getNativeDialog());
            dialog.setAcceptMode (QFileDialog::AcceptSave);
            dialog.setWindowTitle (tr ("Save as..."));
            dialog.setFileMode (QFileDialog::AnyFile);
            dialog.setNameFilter (filter);
            dialog.selectFile (fname);
            dialog.autoScroll();
            /*dialog.setLabelText (QFileDialog::Accept, tr ("Save"));
            dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
            if (dialog.exec())
            {
                fname = dialog.selectedFiles().at (0);
                if (fname.isEmpty() || QFileInfo (fname).isDir())
                {
                    updateShortcuts (false);
                    return false;
                }
            }
            else
            {
                updateShortcuts (false);
                return false;
            }
            updateShortcuts (false);
        }
    }

    if (QObject::sender() == ui->actionSaveAs)
    {
        if (hasAnotherDialog()) return false;
        updateShortcuts (true);
        FileDialog dialog (this, config.getNativeDialog());
        dialog.setAcceptMode (QFileDialog::AcceptSave);
        dialog.setWindowTitle (tr ("Save as..."));
        dialog.setFileMode (QFileDialog::AnyFile);
        dialog.setNameFilter (filter);
        dialog.selectFile (fname);
        dialog.autoScroll();
        /*dialog.setLabelText (QFileDialog::Accept, tr ("Save"));
        dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
        if (dialog.exec())
        {
            fname = dialog.selectedFiles().at (0);
            if (fname.isEmpty() || QFileInfo (fname).isDir())
            {
                updateShortcuts (false);
                return false;
            }
        }
        else
        {
            updateShortcuts (false);
            return false;
        }
        updateShortcuts (false);
    }
    else if (QObject::sender() == ui->actionSaveCodec)
    {
        if (hasAnotherDialog()) return false;
        updateShortcuts (true);
        FileDialog dialog (this, config.getNativeDialog());
        dialog.setAcceptMode (QFileDialog::AcceptSave);
        dialog.setWindowTitle (tr ("Keep encoding and save as..."));
        dialog.setFileMode (QFileDialog::AnyFile);
        dialog.setNameFilter (filter);
        dialog.selectFile (fname);
        dialog.autoScroll();
        /*dialog.setLabelText (QFileDialog::Accept, tr ("Save"));
        dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
        if (dialog.exec())
        {
            fname = dialog.selectedFiles().at (0);
            if (fname.isEmpty() || QFileInfo (fname).isDir())
            {
                updateShortcuts (false);
                return false;
            }
        }
        else
        {
            updateShortcuts (false);
            return false;
        }
        updateShortcuts (false);
    }

    if (config.getRemoveTrailingSpaces())
    {
        /* using text blocks directly is the fastest
           and lightest way of removing trailing spaces */
        if (QGuiApplication::overrideCursor() == nullptr)
            waitToMakeBusy();
        QTextBlock block = textEdit->document()->firstBlock();
        QTextCursor tmpCur = textEdit->textCursor();
        tmpCur.beginEditBlock();
        while (block.isValid())
        {
            if (const int num = trailingSpaces (block.text()))
            {
                tmpCur.setPosition (block.position() + block.text().length());
                if (num > 1 && textEdit->getProg() == "markdown") // md sees two trailing spaces as a new line
                    tmpCur.movePosition (QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, num - 2);
                else
                    tmpCur.movePosition (QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, num);
                tmpCur.removeSelectedText();
            }
            block = block.next();
        }
        tmpCur.endEditBlock();
        unbusy();
    }

    if (config.getAppendEmptyLine()
        && !textEdit->document()->lastBlock().text().isEmpty())
    {
        QTextCursor tmpCur = textEdit->textCursor();
        tmpCur.beginEditBlock();
        tmpCur.movePosition (QTextCursor::End);
        tmpCur.insertBlock();
        tmpCur.endEditBlock();
    }

    /* now, try to write */
    QTextDocumentWriter writer (fname, "plaintext");
    bool success = false;
    if (QObject::sender() == ui->actionSaveCodec)
    {
        QString encoding  = checkToEncoding();

        if (hasAnotherDialog()) return false;
        updateShortcuts (true);
        MessageBox msgBox (this);
        msgBox.setIcon (QMessageBox::Question);
        msgBox.addButton (QMessageBox::Yes);
        msgBox.addButton (QMessageBox::No);
        msgBox.addButton (QMessageBox::Cancel);
        msgBox.changeButtonText (QMessageBox::Yes, tr ("Yes"));
        msgBox.changeButtonText (QMessageBox::No, tr ("No"));
        msgBox.changeButtonText (QMessageBox::Cancel, tr ("Cancel"));
        msgBox.setText ("<center>" + tr ("Do you want to use <b>MS Windows</b> end-of-lines?") + "</center>");
        msgBox.setInformativeText ("<center><i>" + tr ("This may be good for readability under MS Windows.") + "</i></center>");
        msgBox.setWindowModality (Qt::WindowModal);
        QString contents;
        int ln;
        QTextCodec *codec;
        QByteArray encodedString;
        const char *txt;
        switch (msgBox.exec()) {
        case QMessageBox::Yes:
            contents = textEdit->document()->toPlainText();
            contents.replace ("\n", "\r\n");
            ln = contents.length(); // for fwrite();
            codec = QTextCodec::codecForName (checkToEncoding().toUtf8());
            encodedString = codec->fromUnicode (contents);
            txt = encodedString.constData();
            if (encoding != "UTF-16")
            {
                std::ofstream file;
                file.open (fname.toUtf8().constData());
                if (file.is_open())
                {
                    file << txt;
                    file.close();
                    success = true;
                }
            }
            else
            {
                FILE * file;
                file = fopen (fname.toUtf8().constData(), "wb");
                if (file != nullptr)
                {
                    /* this worked correctly as I far as I tested */
                    fwrite (txt , 2 , ln + 1 , file);
                    fclose (file);
                    success = true;
                }
            }
            break;
        case QMessageBox::No:
            writer.setCodec (QTextCodec::codecForName (encoding.toUtf8()));
            break;
        default:
            updateShortcuts (false);
            return false;
            break;
        }
        updateShortcuts (false);
    }
    if (!success)
        success = writer.write (textEdit->document());

    if (success)
    {
        QFileInfo fInfo (fname);

        textEdit->document()->setModified (false);
        textEdit->setFileName (fname);
        textEdit->setSize (fInfo.size());
        textEdit->setLastModified (fInfo.lastModified());
        ui->actionReload->setDisabled (false);
        setTitle (fname);
        QString tip (fInfo.absolutePath() + "/");
        QFontMetrics metrics (QToolTip::font());
        int w = QApplication::desktop()->screenGeometry().width();
        if (w > 200 * metrics.width (' ')) w = 200 * metrics.width (' ');
        QString elidedTip = metrics.elidedText (tip, Qt::ElideMiddle, w);
        ui->tabWidget->setTabToolTip (index, elidedTip);
        if (!sideItems_.isEmpty())
        {
            if (QListWidgetItem *wi = sideItems_.key (tabPage))
                wi->setToolTip (elidedTip);
        }
        lastFile_ = fname;
        config.addRecentFile (lastFile_);
        if (!keepSyntax)
        { // uninstall and reinstall the syntax highlgihter if the programming language is changed
            QString prevLan = textEdit->getProg();
            setProgLang (textEdit);
            if (prevLan != textEdit->getProg())
            {
                if (config.getShowLangSelector() && config.getSyntaxByDefault())
                {
                    if (textEdit->getLang() == textEdit->getProg())
                        textEdit->setLang (QString()); // not enforced because it's the real syntax
                    showLang (textEdit);
                }

                if (ui->statusBar->isVisible()
                    && textEdit->getWordNumber() != -1)
                { // we want to change the statusbar text below
                    disconnect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
                }

                if (textEdit->getLang().isEmpty())
                { // restart the syntax highlighting only when the language isn't forced
                    syntaxHighlighting (textEdit, false);
                    if (ui->actionSyntax->isChecked())
                        syntaxHighlighting (textEdit);
                }

                if (ui->statusBar->isVisible())
                { // correct the statusbar text just by replacing the old syntax info
                    QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
                    QString str = statusLabel->text();
                    QString syntaxStr = tr ("Syntax");
                    int i = str.indexOf (syntaxStr);
                    if (i == -1) // there was no language before saving (prevLan.isEmpty())
                    {
                        QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                        int j = str.indexOf (lineStr);
                        syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax") + QString (":</b> <i>%1</i>")
                                                                                    .arg (textEdit->getProg());
                        str.insert (j, syntaxStr);
                    }
                    else
                    {
                        if (textEdit->getProg().isEmpty()) // there's no language after saving
                        {
                            syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax");
                            QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                            int j = str.indexOf (syntaxStr);
                            int k = str.indexOf (lineStr);
                            str.remove (j, k - j);
                        }
                        else // the language is changed by saving
                        {
                            QString lineStr = "</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                            int j = str.indexOf (lineStr);
                            int offset = syntaxStr.count() + 9; // size of ":</b> <i>"
                            str.replace (i + offset, j - i - offset, textEdit->getProg());
                        }
                    }
                    statusLabel->setText (str);
                    if (textEdit->getWordNumber() != -1)
                        connect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
                }
            }
        }
    }
    else
    {
        QString str = writer.device()->errorString();
        showWarningBar ("<center><b><big>" + tr ("Cannot be saved!") + "</big></b></center>\n"
                        + "<center><i>" + QString ("<center><i>%1.</i></center>").arg (str) + "<i/></center>");
    }

    if (success && textEdit->isReadOnly() && !alreadyOpen (tabPage))
         QTimer::singleShot (0, this, SLOT (makeEditable()));

    return success;
}
/*************************/
void FPwin::cutText()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
        tabPage->textEdit()->cut();
}
/*************************/
void FPwin::copyText()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
        tabPage->textEdit()->copy();
}
/*************************/
void FPwin::pasteText()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
        tabPage->textEdit()->paste();
}
/*************************/
void FPwin::insertDate()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
    {
        Config config = static_cast<FPsingleton*>(qApp)->getConfig();
        QString format  = config.getDateFormat();
        tabPage->textEdit()->insertPlainText (QDateTime::currentDateTime().toString (format.isEmpty()
                                                  ? "MMM dd, yyyy, hh:mm:ss"
                                                  : format));
    }
}
/*************************/
void FPwin::deleteText()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
    {
        TextEdit *textEdit = tabPage->textEdit();
        if (!textEdit->isReadOnly())
            textEdit->insertPlainText ("");
    }
}
/*************************/
void FPwin::selectAllText()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
        tabPage->textEdit()->selectAll();
}
/*************************/
void FPwin::makeEditable()
{
    if (!isReady()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    bool textIsSelected = textEdit->textCursor().hasSelection();

    textEdit->setReadOnly (false);
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    if (!textEdit->hasDarkScheme())
    {
        textEdit->viewport()->setStyleSheet (QString (".QWidget {"
                                                      "color: black;"
                                                      "background-color: rgb(%1, %1, %1);}")
                                             .arg (config.getLightBgColorValue()));
    }
    else
    {
        textEdit->viewport()->setStyleSheet (QString (".QWidget {"
                                                      "color: white;"
                                                      "background-color: rgb(%1, %1, %1);}")
                                             .arg (config.getDarkBgColorValue()));
    }
    ui->actionEdit->setVisible (false);

    ui->actionPaste->setEnabled (true);
    ui->actionDate->setEnabled (true);
    ui->actionCopy->setEnabled (textIsSelected);
    ui->actionCut->setEnabled (textIsSelected);
    ui->actionDelete->setEnabled (textIsSelected);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
}
/*************************/
void FPwin::undoing()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    textEdit->removeGreenHighlights(); // always remove replacing highlights before undoing
    textEdit->undo();
}
/*************************/
void FPwin::redoing()
{
    if (TabPage *tabPage = qobject_cast<TabPage*>(ui->tabWidget->currentWidget()))
        tabPage->textEdit()->redo();
}
/*************************/
void FPwin::changeTab (QListWidgetItem *current, QListWidgetItem* /*previous*/)
{
    if (!sidePane_ || sideItems_.isEmpty()) return;
    ui->tabWidget->setCurrentWidget (sideItems_.value (current));
}
/*************************/
// Change the window title and the search entry when switching tabs and...
void FPwin::tabSwitch (int index)
{
    if (index == -1)
    {
        setWindowTitle ("FeatherPad[*]");
        setWindowModified (false);
        return;
    }

    closeWarningBar();

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();
    if (!tabPage->isSearchBarVisible())
        textEdit->setFocus();
    QString fname = textEdit->getFileName();
    bool modified (textEdit->document()->isModified());

    QFileInfo info;
    QString shownName;
    if (fname.isEmpty())
    {
        if (textEdit->getProg() == "help")
            shownName = "** " + tr ("Help") + " **";
        else
            shownName = tr ("Untitled");
    }
    else
    {
        info.setFile (fname);
        shownName = fname.section ('/', -1);
        if (!QFile::exists (fname))
            showWarningBar ("<center><b><big>" + tr ("The file has been removed.") + "</big></b></center>");
        else if (textEdit->getLastModified() != info.lastModified())
            showWarningBar ("<center><b><big>" + tr ("This file has been modified elsewhere or in another way!") + "</big></b></center>\n"
                            + "<center>" + tr ("Please be careful about reloading or saving this document!") + "</center>");
    }
    if (modified)
        shownName.prepend ("*");
    shownName.replace ("\n", " ");
    setWindowTitle (shownName);

    /* although the window size, wrapping state or replacing text may have changed or
       the replace dock may have been closed, hlight() will be called automatically */
    //if (!textEdit->getSearchedText().isEmpty()) hlight();

    /* correct the encoding menu */
    encodingToCheck (textEdit->getEncoding());

    /* correct the states of some buttons */
    ui->actionUndo->setEnabled (textEdit->document()->isUndoAvailable());
    ui->actionRedo->setEnabled (textEdit->document()->isRedoAvailable());
    ui->actionSave->setEnabled (modified);
    ui->actionReload->setEnabled (!fname.isEmpty());
    bool readOnly = textEdit->isReadOnly();
    if (fname.isEmpty()
        && !modified
        && !textEdit->document()->isEmpty()) // 'Help' is an exception
    {
        ui->actionEdit->setVisible (false);
        ui->actionSaveAs->setEnabled (true);
    }
    else
    {
        ui->actionEdit->setVisible (readOnly && !textEdit->isUneditable());
        ui->actionSaveAs->setEnabled (!textEdit->isUneditable());
    }
    ui->actionPaste->setEnabled (!readOnly);
    ui->actionDate->setEnabled (!readOnly);
    bool textIsSelected = textEdit->textCursor().hasSelection();
    ui->actionCopy->setEnabled (textIsSelected);
    ui->actionCut->setEnabled (!readOnly && textIsSelected);
    ui->actionDelete->setEnabled (!readOnly && textIsSelected);

    Config config = static_cast<FPsingleton*>(qApp)->getConfig();

    if (isScriptLang (textEdit->getProg()) && info.isExecutable())
        ui->actionRun->setVisible (config.getExecuteScripts());
    else
        ui->actionRun->setVisible (false);

    /* handle the spinbox */
    if (ui->spinBox->isVisible())
        ui->spinBox->setMaximum (textEdit->document()->blockCount());

    /* handle the statusbar */
    if (ui->statusBar->isVisible())
    {
        statusMsgWithLineCount (textEdit->document()->blockCount());
        QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton");
        if (textEdit->getWordNumber() == -1)
        {
            if (wordButton)
                wordButton->setVisible (true);
            if (textEdit->document()->isEmpty()) // make an exception
                updateWordInfo();
        }
        else
        {
            if (wordButton)
                wordButton->setVisible (false);
            QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
            statusLabel->setText (QString ("%1 <i>%2</i>")
                                  .arg (statusLabel->text())
                                  .arg (textEdit->getWordNumber()));
        }
        showCursorPos();
    }
    if (config.getShowLangSelector() && config.getSyntaxByDefault())
        showLang (textEdit);

    /* al last, set the title of Replacment dock */
    if (ui->dockReplace->isVisible())
    {
        QString title = textEdit->getReplaceTitle();
        if (!title.isEmpty())
            ui->dockReplace->setWindowTitle (title);
        else
            ui->dockReplace->setWindowTitle (tr ("Rep&lacement"));
    }
    else
        textEdit->setReplaceTitle (QString());
}
/*************************/
void FPwin::fontDialog()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (hasAnotherDialog()) return;
    updateShortcuts (true);

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();

    QFont currentFont = textEdit->getDefaultFont();
    QFontDialog fd (currentFont, this);
    //fd.setOption (QFontDialog::DontUseNativeDialog);
    fd.setWindowModality (Qt::WindowModal);
    fd.move (x() + width()/2 - fd.width()/2,
             y() + height()/2 - fd.height()/ 2);
    if (fd.exec())
    {
        QFont newFont = fd.selectedFont();
        FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
        for (int i = 0; i < singleton->Wins.count(); ++i)
        {
            FPwin *thisWin = singleton->Wins.at (i);
            for (int j = 0; j < thisWin->ui->tabWidget->count(); ++j)
            {
                TextEdit *thisTextEdit = qobject_cast< TabPage *>(thisWin->ui->tabWidget->widget (j))->textEdit();
                thisTextEdit->setEditorFont (newFont);
            }
        }

        Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
        if (config.getRemFont())
            config.setFont (newFont);

        /* the font can become larger... */
        textEdit->adjustScrollbars();
        /* ... or smaller */
        reformat (textEdit);
    }
    updateShortcuts (false);
}
/*************************/
void FPwin::changeEvent (QEvent *event)
{
    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
    if (config.getRemSize() && event->type() == QEvent::WindowStateChange)
    {
        if (windowState() == Qt::WindowFullScreen)
        {
            config.setIsFull (true);
            config.setIsMaxed (false);
        }
        else if (windowState() == (Qt::WindowFullScreen ^ Qt::WindowMaximized))
        {
            config.setIsFull (true);
            config.setIsMaxed (true);
        }
        else
        {
            config.setIsFull (false);
            if (windowState() == Qt::WindowMaximized)
                config.setIsMaxed (true);
            else
                config.setIsMaxed (false);
        }
    }
    QWidget::changeEvent (event);
}
/*************************/
bool FPwin::event (QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
    {
        if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
        {
            TextEdit *textEdit = tabPage->textEdit();
            QString fname = textEdit->getFileName();
            if (!fname.isEmpty())
            {
                if (!QFile::exists (fname))
                    showWarningBar ("<center><b><big>" + tr ("The file has been removed.") + "</big></b></center>");
                else if (textEdit->getLastModified() != QFileInfo (fname).lastModified())
                    showWarningBar ("<center><b><big>" + tr ("This file has been modified elsewhere or in another way!") + "</big></b></center>\n"
                                    + "<center>" + tr ("Please be careful about reloading or saving this document!") + "</center>");
            }
        }
    }
    return QMainWindow::event (event);
}
/*************************/
void FPwin::showHideSearch()
{
    if (!isReady()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    bool isFocused = tabPage->isSearchBarVisible() && tabPage->searchBarHasFocus();

    if (!isFocused)
        tabPage->focusSearchBar();
    else
    {
        ui->dockReplace->setVisible (false); // searchbar is needed by replace dock
        /* return focus to the document,... */
        tabPage->textEdit()->setFocus();
    }

    int count = ui->tabWidget->count();
    for (int indx = 0; indx < count; ++indx)
    {
        TabPage *page = qobject_cast< TabPage *>(ui->tabWidget->widget (indx));
        if (isFocused)
        {
            /* ... remove all yellow and green highlights... */
            TextEdit *textEdit = page->textEdit();
            textEdit->setSearchedText (QString());
            QList<QTextEdit::ExtraSelection> es;
            textEdit->setGreenSel (es); // not needed
            if (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible())
                es.prepend (textEdit->currentLineSelection());
            es.append (textEdit->getRedSel());
            textEdit->setExtraSelections (es);
            /* ... and empty all search entries */
            page->clearSearchEntry();
        }
        page->setSearchBarVisible (!isFocused);
    }
}
/*************************/
void FPwin::jumpTo()
{
    if (!isReady()) return;

    bool visibility = ui->spinBox->isVisible();

    for (int i = 0; i < ui->tabWidget->count(); ++i)
    {
        TextEdit *thisTextEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit();
        if (!ui->actionLineNumbers->isChecked())
            thisTextEdit->showLineNumbers (!visibility);

        if (!visibility)
        {
            /* setMaximum() isn't a slot */
            connect (thisTextEdit->document(),
                     &QTextDocument::blockCountChanged,
                     this,
                     &FPwin::setMax);
        }
        else
            disconnect (thisTextEdit->document(),
                        &QTextDocument::blockCountChanged,
                        this,
                        &FPwin::setMax);
    }

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget());
    if (tabPage)
    {
        if (!visibility && ui->tabWidget->count() > 0)
            ui->spinBox->setMaximum (tabPage->textEdit()
                                     ->document()
                                     ->blockCount());
    }
    ui->spinBox->setVisible (!visibility);
    ui->label->setVisible (!visibility);
    ui->checkBox->setVisible (!visibility);
    if (!visibility)
    {
        ui->spinBox->setFocus();
        ui->spinBox->selectAll();
    }
    else if (tabPage)/* return focus to doc */
        tabPage->textEdit()->setFocus();
}
/*************************/
void FPwin::setMax (const int max)
{
    ui->spinBox->setMaximum (max);
}
/*************************/
void FPwin::goTo()
{
    /* workaround for not being able to use returnPressed()
       because of protectedness of spinbox's QLineEdit */
    if (!ui->spinBox->hasFocus()) return;

    if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
    {
        TextEdit *textEdit = tabPage->textEdit();
        QTextBlock block = textEdit->document()->findBlockByNumber (ui->spinBox->value() - 1);
        int pos = block.position();
        QTextCursor start = textEdit->textCursor();
        if (ui->checkBox->isChecked())
            start.setPosition (pos, QTextCursor::KeepAnchor);
        else
            start.setPosition (pos);
        textEdit->setTextCursor (start);
    }
}
/*************************/
void FPwin::showLN (bool checked)
{
    int count = ui->tabWidget->count();
    if (count == 0) return;

    if (checked)
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->showLineNumbers (true);
    }
    else if (!ui->spinBox->isVisible()) // also the spinBox affects line numbers visibility
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->showLineNumbers (false);
    }
}
/*************************/
void FPwin::toggleWrapping()
{
    int count = ui->tabWidget->count();
    if (count == 0) return;

    if (ui->actionWrap->isChecked())
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setLineWrapMode (QPlainTextEdit::WidgetWidth);
    }
    else
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setLineWrapMode (QPlainTextEdit::NoWrap);
    }
}
/*************************/
void FPwin::toggleIndent()
{
    int count = ui->tabWidget->count();
    if (count == 0) return;

    if (ui->actionIndent->isChecked())
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setAutoIndentation (true);
    }
    else
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setAutoIndentation (false);
    }
}
/*************************/
void FPwin::encodingToCheck (const QString& encoding)
{
    if (encoding != "UTF-8")
        ui->actionOther->setDisabled (true);

    if (encoding == "UTF-8")
        ui->actionUTF_8->setChecked (true);
    else if (encoding == "UTF-16")
        ui->actionUTF_16->setChecked (true);
    else if (encoding == "CP1256")
        ui->actionWindows_Arabic->setChecked (true);
    else if (encoding == "ISO-8859-1")
        ui->actionISO_8859_1->setChecked (true);
    else if (encoding == "ISO-8859-15")
        ui->actionISO_8859_15->setChecked (true);
    else if (encoding == "CP1252")
        ui->actionWindows_1252->setChecked (true);
    else if (encoding == "CP1251")
        ui->actionCyrillic_CP1251->setChecked (true);
    else if (encoding == "KOI8-U")
        ui->actionCyrillic_KOI8_U->setChecked (true);
    else if (encoding == "ISO-8859-5")
        ui->actionCyrillic_ISO_8859_5->setChecked (true);
    else if (encoding == "BIG5")
        ui->actionChineese_BIG5->setChecked (true);
    else if (encoding == "B18030")
        ui->actionChinese_GB18030->setChecked (true);
    else if (encoding == "ISO-2022-JP")
        ui->actionJapanese_ISO_2022_JP->setChecked (true);
    else if (encoding == "ISO-2022-JP-2")
        ui->actionJapanese_ISO_2022_JP_2->setChecked (true);
    else if (encoding == "ISO-2022-KR")
        ui->actionJapanese_ISO_2022_KR->setChecked (true);
    else if (encoding == "CP932")
        ui->actionJapanese_CP932->setChecked (true);
    else if (encoding == "EUC-JP")
        ui->actionJapanese_EUC_JP->setChecked (true);
    else if (encoding == "CP949")
        ui->actionKorean_CP949->setChecked (true);
    else if (encoding == "CP1361")
        ui->actionKorean_CP1361->setChecked (true);
    else if (encoding == "EUC-KR")
        ui->actionKorean_EUC_KR->setChecked (true);
    else
    {
        ui->actionOther->setDisabled (false);
        ui->actionOther->setChecked (true);
    }
}
/*************************/
const QString FPwin::checkToEncoding() const
{
    QString encoding;

    if (ui->actionUTF_8->isChecked())
        encoding = "UTF-8";
    else if (ui->actionUTF_16->isChecked())
        encoding = "UTF-16";
    else if (ui->actionWindows_Arabic->isChecked())
        encoding = "CP1256";
    else if (ui->actionISO_8859_1->isChecked())
        encoding = "ISO-8859-1";
    else if (ui->actionISO_8859_15->isChecked())
        encoding = "ISO-8859-15";
    else if (ui->actionWindows_1252->isChecked())
        encoding = "CP1252";
    else if (ui->actionCyrillic_CP1251->isChecked())
        encoding = "CP1251";
    else if (ui->actionCyrillic_KOI8_U->isChecked())
        encoding = "KOI8-U";
    else if (ui->actionCyrillic_ISO_8859_5->isChecked())
        encoding = "ISO-8859-5";
    else if (ui->actionChineese_BIG5->isChecked())
        encoding = "BIG5";
    else if (ui->actionChinese_GB18030->isChecked())
        encoding = "B18030";
    else if (ui->actionJapanese_ISO_2022_JP->isChecked())
        encoding = "ISO-2022-JP";
    else if (ui->actionJapanese_ISO_2022_JP_2->isChecked())
        encoding = "ISO-2022-JP-2";
    else if (ui->actionJapanese_ISO_2022_KR->isChecked())
        encoding = "ISO-2022-KR";
    else if (ui->actionJapanese_CP932->isChecked())
        encoding = "CP932";
    else if (ui->actionJapanese_EUC_JP->isChecked())
        encoding = "EUC-JP";
    else if (ui->actionKorean_CP949->isChecked())
        encoding = "CP949";
    else if (ui->actionKorean_CP1361->isChecked())
        encoding = "CP1361";
    else if (ui->actionKorean_EUC_KR->isChecked())
        encoding = "EUC-KR";
    else
        encoding = "UTF-8";

    return encoding;
}
/*************************/
void FPwin::docProp()
{
    bool showCurPos = static_cast<FPsingleton*>(qApp)->getConfig().getShowCursorPos();
    if (ui->statusBar->isVisible())
    {
        for (int i = 0; i < ui->tabWidget->count(); ++i)
        {
            TextEdit *thisTextEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit();
            disconnect (thisTextEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
            disconnect (thisTextEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
            if (showCurPos)
                disconnect (thisTextEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::showCursorPos);
            /* don't delete the cursor position label because the statusbar might be shown later */
        }
        ui->statusBar->setVisible (false);
        return;
    }

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    statusMsgWithLineCount (qobject_cast< TabPage *>(ui->tabWidget->widget (index))
                            ->textEdit()->document()->blockCount());
    for (int i = 0; i < ui->tabWidget->count(); ++i)
    {
        TextEdit *thisTextEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit();
        connect (thisTextEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
        connect (thisTextEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
        if (showCurPos)
            connect (thisTextEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::showCursorPos);
    }

    ui->statusBar->setVisible (true);
    if (showCurPos)
    {
        addCursorPosLabel();
        showCursorPos();
    }
    if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton"))
        wordButton->setVisible (true);
    updateWordInfo();
}
/*************************/
// Set the status bar text according to the block count.
void FPwin::statusMsgWithLineCount (const int lines)
{
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->currentWidget())->textEdit();
    /* ensure that the signal comes from the active tab if this is about a tab a signal */
    if (qobject_cast<TextEdit*>(QObject::sender()) && QObject::sender() != textEdit)
        return;

    QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");

    /* the order: Encoding -> Syntax -> Lines -> Sel. Chars -> Words */
    QString encodStr = "<b>" + tr ("Encoding") + QString (":</b> <i>%1</i>").arg (textEdit->getEncoding());
    QString syntaxStr;
    if (!textEdit->getProg().isEmpty() && textEdit->getProg() != "help")
        syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax") + QString (":</b> <i>%1</i>").arg (textEdit->getProg());
    QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines") + QString (":</b> <i>%1</i>").arg (lines);
    QString selStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Sel. Chars")
                     + QString (":</b> <i>%1</i>").arg (textEdit->textCursor().selectedText().size());
    QString wordStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Words") + ":</b>";

    statusLabel->setText (encodStr + syntaxStr + lineStr + selStr + wordStr);
}
/*************************/
// Change the status bar text when the selection changes.
void FPwin::statusMsg()
{
    QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
    int sel = qobject_cast< TabPage *>(ui->tabWidget->currentWidget())->textEdit()
              ->textCursor().selectedText().size();
    QString str = statusLabel->text();
    QString selStr = tr ("Sel. Chars");
    QString wordStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Words");
    int i = str.indexOf (selStr) + selStr.count();
    int j = str.indexOf (wordStr);
    if (sel == 0)
    {
        QString prevSel = str.mid (i + 9, j - i - 13); // j - i - 13 --> j - (i + 9[":</b> <i>]") - 4["</i>"]
        if (prevSel.toInt() == 0) return;
    }
    QString charN;
    charN.setNum (sel);
    str.replace (i + 9, j - i - 13, charN);
    statusLabel->setText (str);
}
/*************************/
void FPwin::showCursorPos()
{
    QLabel *posLabel = ui->statusBar->findChild<QLabel *>("posLabel");
    if (!posLabel) return;

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget());
    if (!tabPage) return;

    int pos = tabPage->textEdit()->textCursor().positionInBlock();
    QString charN;
    charN.setNum (pos); charN = "<i> " + charN + "</i>";
    QString str = posLabel->text();
    QString scursorStr = "<b>" + tr ("Position:") + "</b>";
    int i = scursorStr.count();
    str.replace (i, str.count() - i, charN);
    posLabel->setText (str);
}
/*************************/
void FPwin::showLang (TextEdit *textEdit)
{
    QToolButton *langButton = ui->statusBar->findChild<QToolButton *>("langButton");
    if (!langButton) return;

    langButton->setEnabled (textEdit->getProg() != "help");

    QString lang = textEdit->getLang().isEmpty() ? textEdit->getProg()
                                                 : textEdit->getLang();
    if (lang.isEmpty() || lang == "normal" || lang == "help")
        lang = tr ("Normal");
    langButton->setText (lang);
    if (QAction *action = langs.value (lang))
        action->setChecked (true);
}
/*************************/
void FPwin::setLang (QAction *action)
{
    QToolButton *langButton = ui->statusBar->findChild<QToolButton *>("langButton");
    if (!langButton) return;

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget());
    if (!tabPage) return;

    TextEdit *textEdit = tabPage->textEdit();
    QString lang = action->text();
    lang.remove ('&'); // because of KAcceleratorManager
    langButton->setText (lang);
    if (lang == tr ("Normal"))
    {
        lang = "normal";
        if (textEdit->getProg().isEmpty())
            textEdit->setLang (QString());
        else
            textEdit->setLang ("normal");
    }
    else if (textEdit->getProg() == lang)
        textEdit->setLang (QString()); // not enforced because it's the real syntax
    else
        textEdit->setLang (lang);
    if (ui->actionSyntax->isChecked())
    {
        syntaxHighlighting (textEdit, false);
        syntaxHighlighting (textEdit, true, lang);
    }
}
/*************************/
void FPwin::updateWordInfo (int /*position*/, int charsRemoved, int charsAdded)
{
    QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton");
    if (!wordButton) return;
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    /* ensure that the signal comes from the active tab (when the info is going to be removed) */
    if (qobject_cast<QTextDocument*>(QObject::sender()) && QObject::sender() != textEdit->document())
        return;

    if (wordButton->isVisible())
    {
        QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
        int words = textEdit->getWordNumber();
        if (words == -1)
        {
            words = textEdit->toPlainText()
                    .split (QRegularExpression ("(\\s|\\n|\\r)+"), QString::SkipEmptyParts)
                    .count();
            textEdit->setWordNumber (words);
        }

        wordButton->setVisible (false);
        statusLabel->setText (QString ("%1 <i>%2</i>")
                              .arg (statusLabel->text())
                              .arg (words));
        connect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
    }
    else if (charsRemoved > 0 || charsAdded > 0) // not if only the format is changed
    {
        disconnect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
        textEdit->setWordNumber (-1);
        wordButton->setVisible (true);
        statusMsgWithLineCount (textEdit->document()->blockCount());
    }
}
/*************************/
void FPwin::filePrint()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (hasAnotherDialog()) return;
    updateShortcuts (true);

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QPrinter printer (QPrinter::HighResolution);

    /* choose an appropriate name and directory */
    QString fileName = textEdit->getFileName();
    if (fileName.isEmpty())
    {
        QDir dir = QDir::home();
        fileName= dir.filePath (tr ("Untitled"));
    }
    if (printer.outputFormat() == QPrinter::PdfFormat)
        printer.setOutputFileName (fileName.append (".pdf"));
    /*else if (printer.outputFormat() == QPrinter::PostScriptFormat)
        printer.setOutputFileName (fileName.append (".ps"));*/

    QPrintDialog dlg (&printer, this);
    dlg.setWindowModality (Qt::WindowModal);
    if (textEdit->textCursor().hasSelection())
        dlg.setOption (QAbstractPrintDialog::PrintSelection);
    dlg.setWindowTitle (tr ("Print Document"));
    if (dlg.exec() == QDialog::Accepted)
        textEdit->print (&printer);

    updateShortcuts (false);
}
/*************************/
void FPwin::nextTab()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (sidePane_)
    {
        int curRow = sidePane_->listWidget()->currentRow();
        if (curRow == sidePane_->listWidget()->count() - 1)
        {
            if (static_cast<FPsingleton*>(qApp)->getConfig().getTabWrapAround())
                sidePane_->listWidget()->setCurrentRow (0);
        }
        else
            sidePane_->listWidget()->setCurrentRow (curRow + 1);
    }
    else
    {
        if (QWidget *widget = ui->tabWidget->widget (index + 1))
            ui->tabWidget->setCurrentWidget (widget);
        else if (static_cast<FPsingleton*>(qApp)->getConfig().getTabWrapAround())
            ui->tabWidget->setCurrentIndex (0);
    }
}
/*************************/
void FPwin::previousTab()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (sidePane_)
    {
        int curRow = sidePane_->listWidget()->currentRow();
        if (curRow == 0)
        {
            if (static_cast<FPsingleton*>(qApp)->getConfig().getTabWrapAround())
                sidePane_->listWidget()->setCurrentRow (sidePane_->listWidget()->count() - 1);
        }
        else
            sidePane_->listWidget()->setCurrentRow (curRow - 1);
    }
    else
    {
        if (QWidget *widget = ui->tabWidget->widget (index - 1))
            ui->tabWidget->setCurrentWidget (widget);
        else if (static_cast<FPsingleton*>(qApp)->getConfig().getTabWrapAround())
        {
            int count = ui->tabWidget->count();
            if (count > 0)
                ui->tabWidget->setCurrentIndex (count - 1);
        }
    }
}
/*************************/
void FPwin::lastTab()
{
    if (isLoading()) return;

    if (sidePane_)
    {
        int count = sidePane_->listWidget()->count();
        if (count > 0)
            sidePane_->listWidget()->setCurrentRow (count - 1);
    }
    else
    {
        int count = ui->tabWidget->count();
        if (count > 0)
            ui->tabWidget->setCurrentIndex (count - 1);
    }
}
/*************************/
void FPwin::firstTab()
{
    if (isLoading()) return;

    if (sidePane_)
    {
        if (sidePane_->listWidget()->count() > 0)
            sidePane_->listWidget()->setCurrentRow (0);
    }
    else if (ui->tabWidget->count() > 0)
        ui->tabWidget->setCurrentIndex (0);
}
/*************************/
void FPwin::detachTab()
{
    if (!isReady()) return;

    int index = -1;
    if (sidePane_ && rightClicked_ >= 0)
        index = ui->tabWidget->indexOf (sideItems_.value (sidePane_->listWidget()->item (rightClicked_)));
    else
        index = ui->tabWidget->currentIndex();
    if (index == -1 || ui->tabWidget->count() == 1)
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }

    /*****************************************************
     *****          Get all necessary info.          *****
     ***** Then, remove the tab but keep its widget. *****
     *****************************************************/

    QString tooltip = ui->tabWidget->tabToolTip (index);
    QString tabText = ui->tabWidget->tabText (index);
    QString title = windowTitle();
    bool hl = true;
    bool spin = false;
    bool ln = false;
    bool status = false;
    bool statusCurPos = false;
    if (!ui->actionSyntax->isChecked())
        hl = false;
    if (ui->spinBox->isVisible())
        spin = true;
    if (ui->actionLineNumbers->isChecked())
        ln = true;
    if (ui->statusBar->isVisible())
    {
        status = true;
        if (ui->statusBar->findChild<QLabel *>("posLabel"))
            statusCurPos = true;
    }

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();

    disconnect (textEdit, &TextEdit::updateRect, this ,&FPwin::hlighting);
    disconnect (textEdit, &QPlainTextEdit::textChanged, this ,&FPwin::hlight);
    if (status)
    {
        disconnect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
        disconnect (textEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
        if (statusCurPos)
            disconnect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::showCursorPos);
    }
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);
    disconnect (textEdit, &TextEdit::zoomedOut, this, &FPwin::reformat);
    disconnect (textEdit, &TextEdit::fileDropped, this, &FPwin::newTabFromName);
    disconnect (textEdit, &TextEdit::updateBracketMatching, this, &FPwin::matchBrackets);
    disconnect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::formatOnBlockChange);
    disconnect (textEdit, &TextEdit::updateRect, this, &FPwin::formatVisibleText);
    disconnect (textEdit, &TextEdit::resized, this, &FPwin::formatOnResizing);

    disconnect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
    disconnect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::formatOnTextChange);
    disconnect (textEdit->document(), &QTextDocument::blockCountChanged, this, &FPwin::setMax);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    disconnect (textEdit->document(), &QTextDocument::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);

    disconnect (tabPage, &TabPage::find, this, &FPwin::find);
    disconnect (tabPage, &TabPage::searchFlagChanged, this, &FPwin::searchFlagChanged);

    /* for tabbar to be updated peoperly with tab reordering during a
       fast drag-and-drop, mouse should be released before tab removal */
    ui->tabWidget->tabBar()->releaseMouse();

    ui->tabWidget->removeTab (index);
    if (ui->tabWidget->count() == 1)
    {
        ui->actionDetachTab->setDisabled (true);
        ui->actionRightTab->setDisabled (true);
        ui->actionLeftTab->setDisabled (true);
        ui->actionLastTab->setDisabled (true);
        ui->actionFirstTab->setDisabled (true);
    }
    if (sidePane_ && !sideItems_.isEmpty())
    {
        if (QListWidgetItem *wi = sideItems_.key (tabPage))
        {
            sideItems_.remove (wi);
            delete sidePane_->listWidget()->takeItem (sidePane_->listWidget()->row (wi));
        }
    }

    /*******************************************************************
     ***** create a new window and replace its tab by this widget. *****
     *******************************************************************/

    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    FPwin * dropTarget = singleton->newWin ("");
    dropTarget->closeTabAtIndex (0);

    /* first, set the new info... */
    dropTarget->lastFile_ = textEdit->getFileName();
    textEdit->setGreenSel (QList<QTextEdit::ExtraSelection>());
    textEdit->setRedSel (QList<QTextEdit::ExtraSelection>());
    /* ... then insert the detached widget... */
    dropTarget->enableWidgets (true); // the tab will be inserted and switched to below
    bool isLink = dropTarget->lastFile_.isEmpty() ? false
                                                  : QFileInfo (dropTarget->lastFile_).isSymLink();
    dropTarget->ui->tabWidget->insertTab (0, tabPage,
                                          isLink ? QIcon (":icons/link.svg") : QIcon(),
                                          tabText);
    if (dropTarget->sidePane_)
    {
        ListWidget *lw = dropTarget->sidePane_->listWidget();
        if (textEdit->document()->isModified())
        {
            tabText.remove (0, 1);
            tabText.append ("*");
        }
        QListWidgetItem *lwi = new QListWidgetItem (isLink ? QIcon (":icons/link.svg") : QIcon(),
                                                    tabText, lw);
        lw->setToolTip (tooltip);
        dropTarget->sideItems_.insert (lwi, tabPage);
        lw->addItem (lwi);
        lw->setCurrentItem (lwi);
    }
    /* ... and remove all yellow and green highlights
       (the yellow ones will be recreated later if needed) */
    QList<QTextEdit::ExtraSelection> es;
    if (ln || spin)
        es.prepend (textEdit->currentLineSelection());
    textEdit->setExtraSelections (es);

    /* at last, set all properties correctly */
    dropTarget->setWindowTitle (title);
    dropTarget->ui->tabWidget->setTabToolTip (0, tooltip);
    /* reload buttons, syntax highlighting, jump bar, line numbers */
    dropTarget->encodingToCheck (textEdit->getEncoding());
    if (!textEdit->getFileName().isEmpty())
        dropTarget->ui->actionReload->setEnabled (true);
    if (!hl)
        dropTarget->ui->actionSyntax->setChecked (false);
    else
        dropTarget->syntaxHighlighting (textEdit, true, textEdit->getLang());
    if (spin)
    {
        dropTarget->ui->spinBox->setVisible (true);
        dropTarget->ui->label->setVisible (true);
        dropTarget->ui->spinBox->setMaximum (textEdit->document()->blockCount());
        connect (textEdit->document(), &QTextDocument::blockCountChanged, dropTarget, &FPwin::setMax);
    }
    if (ln)
        dropTarget->ui->actionLineNumbers->setChecked (true);
    /* searching */
    if (!textEdit->getSearchedText().isEmpty())
    {
        connect (textEdit, &QPlainTextEdit::textChanged, dropTarget, &FPwin::hlight);
        connect (textEdit, &TextEdit::updateRect, dropTarget, &FPwin::hlighting);
        /* restore yellow highlights, which will automatically
           set the current line highlight if needed because the
           spin button and line number menuitem are set above */
        dropTarget->hlight();
    }
    /* status bar */
    if (status)
    {
        dropTarget->ui->statusBar->setVisible (true);
        dropTarget->statusMsgWithLineCount (textEdit->document()->blockCount());
        if (textEdit->getWordNumber() == -1)
        {
            if (QToolButton *wordButton = dropTarget->ui->statusBar->findChild<QToolButton *>("wordButton"))
                wordButton->setVisible (true);
        }
        else
        {
            if (QToolButton *wordButton = dropTarget->ui->statusBar->findChild<QToolButton *>("wordButton"))
                wordButton->setVisible (false);
            QLabel *statusLabel = dropTarget->ui->statusBar->findChild<QLabel *>("statusLabel");
            statusLabel->setText (QString ("%1 <i>%2</i>")
                                  .arg (statusLabel->text())
                                  .arg (textEdit->getWordNumber()));
            connect (textEdit->document(), &QTextDocument::contentsChange, dropTarget, &FPwin::updateWordInfo);
        }
        connect (textEdit, &QPlainTextEdit::blockCountChanged, dropTarget, &FPwin::statusMsgWithLineCount);
        connect (textEdit, &QPlainTextEdit::selectionChanged, dropTarget, &FPwin::statusMsg);
        if (statusCurPos)
        {
            dropTarget->addCursorPosLabel();
            dropTarget->showCursorPos();
            connect (textEdit, &QPlainTextEdit::cursorPositionChanged, dropTarget, &FPwin::showCursorPos);
        }
    }
    if (textEdit->lineWrapMode() == QPlainTextEdit::NoWrap)
        dropTarget->ui->actionWrap->setChecked (false);
    /* auto indentation */
    if (textEdit->getAutoIndentation() == false)
        dropTarget->ui->actionIndent->setChecked (false);
    /* the remaining signals */
    connect (textEdit->document(), &QTextDocument::undoAvailable, dropTarget->ui->actionUndo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::redoAvailable, dropTarget->ui->actionRedo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, dropTarget->ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, dropTarget, &FPwin::asterisk);
    connect (textEdit, &QPlainTextEdit::copyAvailable, dropTarget->ui->actionCopy, &QAction::setEnabled);

    connect (tabPage, &TabPage::find, dropTarget, &FPwin::find);
    connect (tabPage, &TabPage::searchFlagChanged, dropTarget, &FPwin::searchFlagChanged);

    if (!textEdit->isReadOnly())
    {
        connect (textEdit, &QPlainTextEdit::copyAvailable, dropTarget->ui->actionCut, &QAction::setEnabled);
        connect (textEdit, &QPlainTextEdit::copyAvailable, dropTarget->ui->actionDelete, &QAction::setEnabled);
    }
    connect (textEdit, &TextEdit::fileDropped, dropTarget, &FPwin::newTabFromName);
    connect (textEdit, &TextEdit::zoomedOut, dropTarget, &FPwin::reformat);

    textEdit->setFocus();

    dropTarget->activateWindow();
    dropTarget->raise();
}
/*************************/
void FPwin::dropTab (const QString& str)
{
    QStringList list = str.split ("+", QString::SkipEmptyParts);
    if (list.count() != 2)
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }
    int index = list.at (1).toInt();
    if (index <= -1) // impossible
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }

    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    FPwin *dragSource = nullptr;
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        if (singleton->Wins.at (i)->winId() == (uint) list.at (0).toInt())
        {
            dragSource = singleton->Wins.at (i);
            break;
        }
    }
    if (dragSource == this
        || dragSource == nullptr) // impossible
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }

    closeWarningBar();
    dragSource->closeWarningBar();

    QString tooltip = dragSource->ui->tabWidget->tabToolTip (index);
    QString tabText = dragSource->ui->tabWidget->tabText (index);
    bool spin = false;
    bool ln = false;
    if (dragSource->ui->spinBox->isVisible())
        spin = true;
    if (dragSource->ui->actionLineNumbers->isChecked())
        ln = true;

    TabPage *tabPage = qobject_cast< TabPage *>(dragSource->ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();

    disconnect (textEdit, &TextEdit::updateRect, dragSource ,&FPwin::hlighting);
    disconnect (textEdit, &QPlainTextEdit::textChanged, dragSource ,&FPwin::hlight);
    if (dragSource->ui->statusBar->isVisible())
    {
        disconnect (textEdit, &QPlainTextEdit::blockCountChanged, dragSource, &FPwin::statusMsgWithLineCount);
        disconnect (textEdit, &QPlainTextEdit::selectionChanged, dragSource, &FPwin::statusMsg);
        if (dragSource->ui->statusBar->findChild<QLabel *>("posLabel"))
            disconnect (textEdit, &QPlainTextEdit::cursorPositionChanged, dragSource, &FPwin::showCursorPos);
    }
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, dragSource->ui->actionCut, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, dragSource->ui->actionDelete, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, dragSource->ui->actionCopy, &QAction::setEnabled);
    disconnect (textEdit, &TextEdit::zoomedOut, dragSource, &FPwin::reformat);
    disconnect (textEdit, &TextEdit::fileDropped, dragSource, &FPwin::newTabFromName);
    disconnect (textEdit, &TextEdit::updateBracketMatching, dragSource, &FPwin::matchBrackets);
    disconnect (textEdit, &QPlainTextEdit::blockCountChanged, dragSource, &FPwin::formatOnBlockChange);
    disconnect (textEdit, &TextEdit::updateRect, dragSource, &FPwin::formatVisibleText);
    disconnect (textEdit, &TextEdit::resized, dragSource, &FPwin::formatOnResizing);

    disconnect (textEdit->document(), &QTextDocument::contentsChange, dragSource, &FPwin::updateWordInfo);
    disconnect (textEdit->document(), &QTextDocument::contentsChange, dragSource, &FPwin::formatOnTextChange);
    disconnect (textEdit->document(), &QTextDocument::blockCountChanged, dragSource, &FPwin::setMax);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, dragSource, &FPwin::asterisk);
    disconnect (textEdit->document(), &QTextDocument::undoAvailable, dragSource->ui->actionUndo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::redoAvailable, dragSource->ui->actionRedo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, dragSource->ui->actionSave, &QAction::setEnabled);

    disconnect (tabPage, &TabPage::find, dragSource, &FPwin::find);
    disconnect (tabPage, &TabPage::searchFlagChanged, dragSource, &FPwin::searchFlagChanged);

    /* it's important to release mouse before tab removal because otherwise, the source
       tabbar might not be updated properly with tab reordering during a fast drag-and-drop */
    dragSource->ui->tabWidget->tabBar()->releaseMouse();

    dragSource->ui->tabWidget->removeTab (index); // there can't be a side-pane here
    int count = dragSource->ui->tabWidget->count();
    if (count == 1)
    {
        dragSource->ui->actionDetachTab->setDisabled (true);
        dragSource->ui->actionRightTab->setDisabled (true);
        dragSource->ui->actionLeftTab->setDisabled (true);
        dragSource->ui->actionLastTab->setDisabled (true);
        dragSource->ui->actionFirstTab->setDisabled (true);
    }

    /***************************************************************************
     ***** The tab is dropped into this window; so insert it as a new tab. *****
     ***************************************************************************/

    int insertIndex = ui->tabWidget->currentIndex() + 1;

    /* first, set the new info... */
    lastFile_ = textEdit->getFileName();
    textEdit->setGreenSel (QList<QTextEdit::ExtraSelection>());
    textEdit->setRedSel (QList<QTextEdit::ExtraSelection>());
    /* ... then insert the detached widget,
       considering whether the searchbar should be shown... */
    if (!textEdit->getSearchedText().isEmpty())
    {
        if (insertIndex == 0 // the window has no tab yet
            || !qobject_cast< TabPage *>(ui->tabWidget->widget (insertIndex - 1))->isSearchBarVisible())
        {
            for (int i = 0; i < ui->tabWidget->count(); ++i)
                qobject_cast< TabPage *>(ui->tabWidget->widget (i))->setSearchBarVisible (true);
        }
    }
    else if (insertIndex > 0)
    {
        tabPage->setSearchBarVisible (qobject_cast< TabPage *>(ui->tabWidget->widget (insertIndex - 1))
                                      ->isSearchBarVisible());
    }
    if (ui->tabWidget->count() == 0) // the tab will be inserted and switched to below
        enableWidgets (true);
    else if (ui->tabWidget->count() == 1)
    { // tab detach and switch actions
        ui->actionDetachTab->setEnabled (true);
        ui->actionRightTab->setEnabled (true);
        ui->actionLeftTab->setEnabled (true);
        ui->actionLastTab->setEnabled (true);
        ui->actionFirstTab->setEnabled (true);
    }
    bool isLink = lastFile_.isEmpty() ? false : QFileInfo (lastFile_).isSymLink();
    ui->tabWidget->insertTab (insertIndex, tabPage,
                              isLink ? QIcon (":icons/link.svg") : QIcon(),
                              tabText);
    if (sidePane_)
    {
        ListWidget *lw = sidePane_->listWidget();
        if (textEdit->document()->isModified())
        {
            tabText.remove (0, 1);
            tabText.append ("*");
        }
        QListWidgetItem *lwi = new QListWidgetItem (isLink ? QIcon (":icons/link.svg") : QIcon(),
                                                    tabText, lw);
        lw->setToolTip (tooltip);
        sideItems_.insert (lwi, tabPage);
        lw->addItem (lwi);
        lw->setCurrentItem (lwi);
    }
    ui->tabWidget->setCurrentIndex (insertIndex);
    /* ... and remove all yellow and green highlights
       (the yellow ones will be recreated later if needed) */
    QList<QTextEdit::ExtraSelection> es;
    if ((ln || spin)
        && (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible()))
    {
        es.prepend (textEdit->currentLineSelection());
    }
    textEdit->setExtraSelections (es);

    /* at last, set all properties correctly */
    ui->tabWidget->setTabToolTip (insertIndex, tooltip);
    /* reload buttons, syntax highlighting, jump bar, line numbers */
    if (ui->actionSyntax->isChecked())
        syntaxHighlighting (textEdit, true, textEdit->getLang());
    else if (!ui->actionSyntax->isChecked() && textEdit->getHighlighter())
    { // there's no connction to the drag target yet
        textEdit->setDrawIndetLines (false);
        Highlighter *highlighter = qobject_cast< Highlighter *>(textEdit->getHighlighter());
        textEdit->setHighlighter (nullptr);
        delete highlighter; highlighter = nullptr;
    }
    if (ui->spinBox->isVisible())
        connect (textEdit->document(), &QTextDocument::blockCountChanged, this, &FPwin::setMax);
    if (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible())
        textEdit->showLineNumbers (true);
    else
        textEdit->showLineNumbers (false);
    /* searching */
    if (!textEdit->getSearchedText().isEmpty())
    {
        connect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::hlight);
        connect (textEdit, &TextEdit::updateRect, this, &FPwin::hlighting);
        /* restore yellow highlights, which will automatically
           set the current line highlight if needed because the
           spin button and line number menuitem are set above */
        hlight();
    }
    /* status bar */
    if (ui->statusBar->isVisible())
    {
        connect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
        connect (textEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
        if (ui->statusBar->findChild<QLabel *>("posLabel"))
        {
            showCursorPos();
            connect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::showCursorPos);
        }
        if (textEdit->getWordNumber() != -1)
            connect (textEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
    }
    if (ui->actionWrap->isChecked() && textEdit->lineWrapMode() == QPlainTextEdit::NoWrap)
        textEdit->setLineWrapMode (QPlainTextEdit::WidgetWidth);
    else if (!ui->actionWrap->isChecked() && textEdit->lineWrapMode() == QPlainTextEdit::WidgetWidth)
        textEdit->setLineWrapMode (QPlainTextEdit::NoWrap);
    /* auto indentation */
    if (ui->actionIndent->isChecked() && textEdit->getAutoIndentation() == false)
        textEdit->setAutoIndentation (true);
    else if (!ui->actionIndent->isChecked() && textEdit->getAutoIndentation() == true)
        textEdit->setAutoIndentation (false);
    /* the remaining signals */
    connect (textEdit->document(), &QTextDocument::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);

    connect (tabPage, &TabPage::find, this, &FPwin::find);
    connect (tabPage, &TabPage::searchFlagChanged, this, &FPwin::searchFlagChanged);

    if (!textEdit->isReadOnly())
    {
        connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
        connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    }
    connect (textEdit, &TextEdit::fileDropped, this, &FPwin::newTabFromName);
    connect (textEdit, &TextEdit::zoomedOut, this, &FPwin::reformat);

    textEdit->setFocus();

    activateWindow();
    raise();

    if (count == 0)
        QTimer::singleShot (0, dragSource, SLOT (close()));
}
/*************************/
void FPwin::tabContextMenu (const QPoint& p)
{
    int tabNum = ui->tabWidget->count();
    QTabBar *tbar = ui->tabWidget->tabBar();
    rightClicked_ = tbar->tabAt (p);
    if (rightClicked_ < 0) return;

    QString fname = qobject_cast< TabPage *>(ui->tabWidget->widget (rightClicked_))
                    ->textEdit()->getFileName();
    QMenu menu;
    bool showMenu = false;
    if (tabNum > 1)
    {
        showMenu = true;
        if (rightClicked_ < tabNum - 1)
            menu.addAction (ui->actionCloseRight);
        if (rightClicked_ > 0)
            menu.addAction (ui->actionCloseLeft);
        menu.addSeparator();
        if (rightClicked_ < tabNum - 1 && rightClicked_ > 0)
            menu.addAction (ui->actionCloseOther);
        menu.addAction (ui->actionCloseAll);
        if (!fname.isEmpty())
            menu.addSeparator();
    }
    if (!fname.isEmpty())
    {
        showMenu = true;
        menu.addAction (ui->actionCopyName);
        menu.addAction (ui->actionCopyPath);
        QFileInfo info (fname);
        if (info.isSymLink())
        {
            menu.addSeparator();
            QAction *action = menu.addAction (QIcon (":icons/link.svg"), tr ("Copy Target Path"));
            connect (action, &QAction::triggered, action, [info] {
                QApplication::clipboard()->setText (info.symLinkTarget());
            });
            action = menu.addAction (QIcon (":icons/link.svg"), tr ("Open Target Here"));
            connect (action, &QAction::triggered, action, [this, info] {
                QString targetName = info.symLinkTarget();
                for (int i = 0; i < ui->tabWidget->count(); ++i)
                {
                    TabPage *thisTabPage = qobject_cast<TabPage*>(ui->tabWidget->widget (i));
                    if (targetName == thisTabPage->textEdit()->getFileName())
                    {
                        ui->tabWidget->setCurrentWidget (thisTabPage);
                        return;
                    }
                }
                newTabFromName (targetName, false);
            });
        }
    }
    if (showMenu) // we don't want an empty menu
        menu.exec (tbar->mapToGlobal (p));
    rightClicked_ = -1; // reset
}
/*************************/
void FPwin::listContextMenu (const QPoint& p)
{
    if (!sidePane_ || sideItems_.isEmpty())
        return;

    ListWidget *lw = sidePane_->listWidget();
    QModelIndex index = lw->indexAt (p);
    if (!index.isValid()) return;
    QListWidgetItem *item = lw->getItemFromIndex (index);
    rightClicked_ = lw->row (item);
    QString fname = sideItems_.value (item)->textEdit()->getFileName();

    QMenu menu;
    menu.addAction (ui->actionClose);
    if (lw->count() > 1)
    {
        menu.addSeparator();
        if (rightClicked_ < lw->count() - 1)
            menu.addAction (ui->actionCloseRight);
        if (rightClicked_ > 0)
            menu.addAction (ui->actionCloseLeft);
        if (rightClicked_ < lw->count() - 1 && rightClicked_ > 0)
        {
            menu.addSeparator();
            menu.addAction (ui->actionCloseOther);
        }
        menu.addAction (ui->actionCloseAll);
        menu.addSeparator();
        menu.addAction (ui->actionDetachTab);
    }
    if (!fname.isEmpty())
    {
        menu.addSeparator();
        menu.addAction (ui->actionCopyName);
        menu.addAction (ui->actionCopyPath);
        QFileInfo info (fname);
        if (info.isSymLink())
        {
            menu.addSeparator();
            QAction *action = menu.addAction (QIcon (":icons/link.svg"), tr ("Copy Target Path"));
            connect (action, &QAction::triggered, action, [info] {
                QApplication::clipboard()->setText (info.symLinkTarget());
            });
            action = menu.addAction (QIcon (":icons/link.svg"), tr ("Open Target Here"));
            connect (action, &QAction::triggered, action, [this, info] {
                QString targetName = info.symLinkTarget();
                for (int i = 0; i < ui->tabWidget->count(); ++i)
                {
                    TabPage *thisTabPage = qobject_cast<TabPage*>(ui->tabWidget->widget (i));
                    if (targetName == thisTabPage->textEdit()->getFileName())
                    {
                        if (QListWidgetItem *wi = sideItems_.key (thisTabPage))
                            sidePane_->listWidget()->setCurrentItem (wi); // sets the current widget at changeTab()
                        return;
                    }
                }
                newTabFromName (targetName, false);
            });
        }
    }
    menu.exec (lw->mapToGlobal (p));
    rightClicked_ = -1; // reset
}
/*************************/
void FPwin::prefDialog()
{
    if (isLoading()) return;

    if (hasAnotherDialog()) return;

    static QHash<QString, QString> defaultShortcuts; // FIXME: use C++11 initializer
    if (defaultShortcuts.isEmpty())
    {
        defaultShortcuts.insert ("actionNew", tr ("Ctrl+N"));
        defaultShortcuts.insert ("actionOpen", tr ("Ctrl+O"));
        defaultShortcuts.insert ("actionSave", tr ("Ctrl+S"));
        defaultShortcuts.insert ("actionReload", tr ("Ctrl+Shift+R"));
        defaultShortcuts.insert ("actionFind", tr ("Ctrl+F"));
        defaultShortcuts.insert ("actionReplace", tr ("Ctrl+R"));
        defaultShortcuts.insert ("actionSaveAs", tr ("Ctrl+Shift+S"));
        defaultShortcuts.insert ("actionPrint", tr ("Ctrl+P"));
        defaultShortcuts.insert ("actionDoc", tr ("Ctrl+Shift+D"));
        defaultShortcuts.insert ("actionClose", tr ("Ctrl+Shift+Q"));
        defaultShortcuts.insert ("actionQuit", tr ("Ctrl+Q"));
        defaultShortcuts.insert ("actionLineNumbers", tr ("Ctrl+L"));
        defaultShortcuts.insert ("actionWrap", tr ("Ctrl+W"));
        defaultShortcuts.insert ("actionIndent", tr ("Ctrl+I"));
        defaultShortcuts.insert ("actionSyntax", tr ("Ctrl+Shift+H"));
        defaultShortcuts.insert ("actionPreferences", tr ("Ctrl+Shift+P"));
        defaultShortcuts.insert ("actionHelp", tr ("Ctrl+H"));
        defaultShortcuts.insert ("actionJump", tr ("Ctrl+J"));
        defaultShortcuts.insert ("actionEdit", tr ("Ctrl+Shift+E"));
        defaultShortcuts.insert ("actionDetachTab", tr ("Ctrl+T"));
        defaultShortcuts.insert ("actionRun", tr ("Ctrl+E"));
        defaultShortcuts.insert ("actionSession", tr ("Ctrl+M"));
        defaultShortcuts.insert ("actionSidePane", tr ("Ctrl+Alt+P"));

        defaultShortcuts.insert ("actionUndo", tr ("Ctrl+Z"));
        defaultShortcuts.insert ("actionRedo", tr ("Ctrl+Shift+Z"));
        defaultShortcuts.insert ("actionDate", tr ("Ctrl+Shift+V"));
    }

    updateShortcuts (true);
    PrefDialog dlg (defaultShortcuts, this);
    /*dlg.show();
    move (x() + width()/2 - dlg.width()/2,
          y() + height()/2 - dlg.height()/ 2);*/
    dlg.exec();
    updateShortcuts (false);
}
/*************************/
void FPwin::manageSessions()
{
    if (!isReady()) return;

    /* first see whether the Sessions dialog is already open... */
    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        QList<QDialog*> dialogs  = singleton->Wins.at (i)->findChildren<QDialog*>();
        for (int j = 0; j < dialogs.count(); ++j)
        {
            if (dialogs.at (j)->objectName() ==  "sessionDialog")
            {
                dialogs.at (j)->raise();
                dialogs.at (j)->activateWindow();
                return;
            }
        }
    }
    /* ... and if not, create a non-modal Sessions dialog */
    SessionDialog *dlg = new SessionDialog (this);
    dlg->setAttribute (Qt::WA_DeleteOnClose);
    dlg->show();
    /*move (x() + width()/2 - dlg.width()/2,
          y() + height()/2 - dlg.height()/ 2);*/
    dlg->raise();
    dlg->activateWindow();
}
/*************************/
// Pauses or resumes auto-saving.
void FPwin::pauseAutoSaving (bool pause)
{
    if (!autoSaver_) return;
    if (pause)
    {
        autoSaverPause_.start();
        autoSaverRemainingTime_ = autoSaver_->remainingTime();
    }
    else if (autoSaverPause_.isValid())
    {
        if (autoSaverPause_.hasExpired (autoSaverRemainingTime_))
        {
            autoSaverPause_.invalidate();
            autoSave();
        }
        else
            autoSaverPause_.invalidate();
    }
}
/*************************/
void FPwin::startAutoSaving (bool start, int interval)
{
    if (start)
    {
        if (!autoSaver_)
        {
            autoSaver_ = new QTimer (this);
            connect (autoSaver_, &QTimer::timeout, this, &FPwin::autoSave);
        }
        autoSaver_->setInterval (interval * 1000 * 60);
        autoSaver_->start();
    }
    else if (autoSaver_)
    {
        if (autoSaver_->isActive())
            autoSaver_->stop();
        delete autoSaver_; autoSaver_ = nullptr;
    }
}
/*************************/
void FPwin::autoSave()
{
    /* since there are important differences between this
       and saveFile(), we can't use the latter here.
       We especially don't show any prompt or warning here. */
    if (autoSaverPause_.isValid()) return;
    QTimer::singleShot (0, this, [=]() {
        if (!autoSaver_ || !autoSaver_->isActive())
            return;
        int index = ui->tabWidget->currentIndex();
        if (index == -1) return;

        Config& config = static_cast<FPsingleton*>(qApp)->getConfig();

        for (int indx = 0; indx < ui->tabWidget->count(); ++indx)
        {
            TabPage *thisTabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (indx));
            TextEdit *thisTextEdit = thisTabPage->textEdit();
            if (thisTextEdit->isUneditable() || !thisTextEdit->document()->isModified())
                continue;
            QString fname = thisTextEdit->getFileName();
            if (fname.isEmpty() || !QFile::exists (fname))
                continue;
            /* make changes to the document if needed */
            if (config.getRemoveTrailingSpaces())
            {
                if (QGuiApplication::overrideCursor() == nullptr)
                    waitToMakeBusy();
                QTextBlock block = thisTextEdit->document()->firstBlock();
                QTextCursor tmpCur = thisTextEdit->textCursor();
                tmpCur.beginEditBlock();
                while (block.isValid())
                {
                    if (const int num = trailingSpaces (block.text()))
                    {
                        tmpCur.setPosition (block.position() + block.text().length());
                        if (num > 1 && thisTextEdit->getProg() == "markdown")
                            tmpCur.movePosition (QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, num - 2);
                        else
                            tmpCur.movePosition (QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, num);
                        tmpCur.removeSelectedText();
                    }
                    block = block.next();
                }
                tmpCur.endEditBlock();
                unbusy();
            }
            if (config.getAppendEmptyLine()
                && !thisTextEdit->document()->lastBlock().text().isEmpty())
            {
                QTextCursor tmpCur = thisTextEdit->textCursor();
                tmpCur.beginEditBlock();
                tmpCur.movePosition (QTextCursor::End);
                tmpCur.insertBlock();
                tmpCur.endEditBlock();
            }

            QTextDocumentWriter writer (fname, "plaintext");
            if (writer.write (thisTextEdit->document()))
            {
                thisTextEdit->document()->setModified (false);
                QFileInfo fInfo (fname);
                thisTextEdit->setSize (fInfo.size());
                thisTextEdit->setLastModified (fInfo.lastModified());
                setTitle (fname, (indx == index ? -1 : indx));
                config.addRecentFile (fname); // recently saved also means recently opened
                /* uninstall and reinstall the syntax highlgihter if the programming language is changed */
                QString prevLan = thisTextEdit->getProg();
                setProgLang (thisTextEdit);
                if (prevLan != thisTextEdit->getProg())
                {
                    if (config.getShowLangSelector() && config.getSyntaxByDefault())
                    {
                        if (thisTextEdit->getLang() == thisTextEdit->getProg())
                            thisTextEdit->setLang (QString()); // not enforced because it's the real syntax
                        showLang (thisTextEdit);
                    }

                    if (indx == index && ui->statusBar->isVisible()
                        && thisTextEdit->getWordNumber() != -1)
                    { // we want to change the statusbar text below
                        disconnect (thisTextEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
                    }

                    if (thisTextEdit->getLang().isEmpty())
                    { // restart the syntax highlighting only when the language isn't forced
                        syntaxHighlighting (thisTextEdit, false);
                        if (ui->actionSyntax->isChecked())
                            syntaxHighlighting (thisTextEdit);
                    }

                    if (indx == index && ui->statusBar->isVisible())
                    { // correct the statusbar text just by replacing the old syntax info
                        QLabel *statusLabel = ui->statusBar->findChild<QLabel *>("statusLabel");
                        QString str = statusLabel->text();
                        QString syntaxStr = tr ("Syntax");
                        int i = str.indexOf (syntaxStr);
                        if (i == -1) // there was no language before saving (prevLan.isEmpty())
                        {
                            QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                            int j = str.indexOf (lineStr);
                            syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax") + QString (":</b> <i>%1</i>")
                                                                                        .arg (thisTextEdit->getProg());
                            str.insert (j, syntaxStr);
                        }
                        else
                        {
                            if (thisTextEdit->getProg().isEmpty()) // there's no language after saving
                            {
                                syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax");
                                QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                                int j = str.indexOf (syntaxStr);
                                int k = str.indexOf (lineStr);
                                str.remove (j, k - j);
                            }
                            else // the language is changed by saving
                            {
                                QString lineStr = "</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                                int j = str.indexOf (lineStr);
                                int offset = syntaxStr.count() + 9; // size of ":</b> <i>"
                                str.replace (i + offset, j - i - offset, thisTextEdit->getProg());
                            }
                        }
                        statusLabel->setText (str);
                        if (thisTextEdit->getWordNumber() != -1)
                            connect (thisTextEdit->document(), &QTextDocument::contentsChange, this, &FPwin::updateWordInfo);
                    }
                }
            }
        }
    });
}
/*************************/
void FPwin::aboutDialog()
{
    if (isLoading()) return;

    if (hasAnotherDialog()) return;
    updateShortcuts (true);

    class AboutDialog : public QDialog {
    public:
        explicit AboutDialog (QWidget* parent = nullptr, Qt::WindowFlags f = 0) : QDialog (parent, f) {
            aboutUi.setupUi (this);
            aboutUi.textLabel->setOpenExternalLinks (true);
        }
        void setTabTexts (const QString& first, const QString& sec) {
            aboutUi.tabWidget->setTabText (0, first);
            aboutUi.tabWidget->setTabText (1, sec);
        }
        void setMainIcon (const QIcon& icn) {
            aboutUi.iconLabel->setPixmap (icn.pixmap (64, 64));
        }
        void settMainTitle (const QString& title) {
            aboutUi.titleLabel->setText (title);
        }
        void setMainText (const QString& txt) {
            aboutUi.textLabel->setText (txt);
        }
    private:
        Ui::AboutDialog aboutUi;
    };

    AboutDialog dialog (this);
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    QIcon FPIcon;
    if (config.getSysIcon())
    {
        FPIcon = QIcon::fromTheme ("featherpad");
        if (FPIcon.isNull())
            FPIcon = QIcon (":icons/featherpad.svg");
    }
    else
        FPIcon = QIcon (":icons/featherpad.svg");
    dialog.setMainIcon (FPIcon);
    dialog.settMainTitle (QString ("<center><b><big>%1 %2</big></b></center><br>").arg (qApp->applicationName()).arg (qApp->applicationVersion()));
    dialog.setMainText ("<center> " + tr ("A lightweight, tabbed, plain-text editor") + " </center>\n<center> "
                        + tr ("based on Qt5") + " </center><br><center> "
                        + tr ("Author")+": <a href='mailto:tsujan2000@gmail.com?Subject=My%20Subject'>Pedram Pourang ("
                        + tr ("aka.") + " Tsu Jan)</a> </center><p></p>");
    dialog.setTabTexts (tr ("About FeatherPad"), tr ("Translators"));
    dialog.setWindowTitle (tr ("About FeatherPad"));
    dialog.setWindowModality (Qt::WindowModal);
    dialog.exec();
    updateShortcuts (false);
}
/*************************/
void FPwin::helpDoc()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1)
        newTab();
    else
    {
        for (int i = 0; i < ui->tabWidget->count(); ++i)
        {
            TabPage *thisTabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (i));
            TextEdit *thisTextEdit = thisTabPage->textEdit();
            if (thisTextEdit->getFileName().isEmpty()
                && !thisTextEdit->document()->isModified()
                && !thisTextEdit->document()->isEmpty())
            {
                if (sidePane_ && !sideItems_.isEmpty())
                {
                    if (QListWidgetItem *wi = sideItems_.key (thisTabPage))
                        sidePane_->listWidget()->setCurrentItem (wi); // sets the current widget at changeTab()
                }
                else
                    ui->tabWidget->setCurrentWidget (thisTabPage);
                return;
            }
        }
    }

    QFile helpFile (DATADIR "/featherpad/help");

    if (!helpFile.exists()) return;
    if (!helpFile.open (QFile::ReadOnly)) return;

    TextEdit *textEdit = qobject_cast<TabPage*>(ui->tabWidget->currentWidget())->textEdit();
    if (!textEdit->document()->isEmpty()
        || textEdit->document()->isModified()
        || !textEdit->getFileName().isEmpty()) // an empty file is just opened
    {
        createEmptyTab (!isLoading(), false);
        textEdit = qobject_cast<TabPage*>(ui->tabWidget->currentWidget())->textEdit();
    }
    else if (textEdit->getHighlighter()) // in case normal is highlighted as urrl
        syntaxHighlighting (textEdit, false);

    QByteArray data = helpFile.readAll();
    helpFile.close();
    QTextCodec *codec = QTextCodec::codecForName ("UTF-8");
    QString str = codec->toUnicode (data);
    textEdit->setPlainText (str);

    textEdit->setReadOnly (true);
    if (!textEdit->hasDarkScheme())
        textEdit->viewport()->setStyleSheet (".QWidget {"
                                             "color: black;"
                                             "background-color: rgb(225, 238, 255);}");
    else
        textEdit->viewport()->setStyleSheet (".QWidget {"
                                             "color: white;"
                                             "background-color: rgb(0, 60, 110);}");
    ui->actionCut->setDisabled (true);
    ui->actionPaste->setDisabled (true);
    ui->actionDate->setDisabled (true);
    ui->actionDelete->setDisabled (true);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);

    index = ui->tabWidget->currentIndex();
    textEdit->setEncoding ("UTF-8");
    textEdit->setWordNumber (-1);
    textEdit->setProg ("help"); // just for marking
    if (ui->statusBar->isVisible())
    {
        statusMsgWithLineCount (textEdit->document()->blockCount());
        if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>("wordButton"))
            wordButton->setVisible (true);
    }
    if (QToolButton *langButton = ui->statusBar->findChild<QToolButton *>("langButton"))
        langButton->setEnabled (false);
    encodingToCheck ("UTF-8");
    QString title = "** " + tr ("Help") + " **";
    ui->tabWidget->setTabText (index, title);
    setWindowTitle (title + "[*]");
    setWindowModified (false);
    ui->tabWidget->setTabToolTip (index, title);
    if (sidePane_)
    {
        QListWidgetItem *cur = sidePane_->listWidget()->currentItem();
        cur->setText (title);
        cur->setToolTip (title);
    }
}

}
