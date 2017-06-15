/* -*- Mode: C++; indent-tabs-mode: nil; tab-width: 4 -*-
 * -*- coding: utf-8 -*-
 *
 * Copyright (C) 2011 ~ 2017 Deepin, Inc.
 *               2011 ~ 2017 Wang Yong
 *
 * Author:     Wang Yong <wangyong@deepin.com>
 * Maintainer: Wang Yong <wangyong@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "constant.h"
#include "dthememanager.h"
#include "main_window.h"
#include <DTitlebar>
#include <QApplication>
#include <QDebug>
#include <QDesktopWidget>
#include <QKeyEvent>
#include <QStyleFactory>
#include <iostream>
#include <signal.h>

using namespace std;

MainWindow::MainWindow(DMainWindow *parent) : DMainWindow(parent)
{
    installEventFilter(this);   // add event filter

    settings = new Settings();
    settings->init();

    if (this->titlebar()) {
        toolbar = new Toolbar();

        menu = new QMenu();
        menu->setStyle(QStyleFactory::create("dlight"));
        killAction = new QAction("结束应用程序", this);
        connect(killAction, &QAction::triggered, this, &MainWindow::showWindowKiller);
        lightThemeAction = new QAction("浅色主题", this);
        connect(lightThemeAction, &QAction::triggered, this, &MainWindow::switchToLightTheme);
        darkThemeAction = new QAction("深色主题", this);
        connect(darkThemeAction, &QAction::triggered, this, &MainWindow::switchToDarkTheme);
        menu->addAction(killAction);
        menu->addAction(lightThemeAction);
        menu->addAction(darkThemeAction);
        menu->addSeparator();

        initTheme();

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, this, &MainWindow::changeTheme);

        this->titlebar()->setCustomWidget(toolbar, Qt::AlignVCenter, false);
        this->titlebar()->setMenu(menu);

        layoutWidget = new QWidget();
        layout = new QHBoxLayout(layoutWidget);

        this->setCentralWidget(layoutWidget);

        int tab_index = settings->getOption("process_tab_index").toInt();

        processManager = new ProcessManager(tab_index, getColumnHideFlags());
        processManager->getProcessView()->installEventFilter(this);
        statusMonitor = new StatusMonitor(tab_index);

        connect(toolbar, &Toolbar::pressEsc, processManager, &ProcessManager::focusProcessView);
        connect(toolbar, &Toolbar::pressTab, processManager, &ProcessManager::focusProcessView);

        connect(processManager, &ProcessManager::activeTab, this, &MainWindow::switchTab);
        connect(processManager, &ProcessManager::columnToggleStatus, this, &MainWindow::recordVisibleColumn);

        connect(statusMonitor, &StatusMonitor::updateProcessStatus, processManager, &ProcessManager::updateStatus, Qt::QueuedConnection);
        connect(statusMonitor, &StatusMonitor::updateProcessNumber, processManager, &ProcessManager::updateProcessNumber, Qt::QueuedConnection);

        connect(toolbar, &Toolbar::search, processManager, &ProcessManager::handleSearch, Qt::QueuedConnection);

        statusMonitor->updateStatus();

        layout->addWidget(statusMonitor);
        layout->addWidget(processManager);

        killPid = -1;

        killProcessDialog = new DDialog(QString("结束进程"), QString("结束应用会有丢失数据的风险\n您确定要结束选中的应用吗？"), this);
        killProcessDialog->setIcon(QIcon(Utils::getQrcPath("deepin-system-monitor.svg")));
        killProcessDialog->addButton(QString("取消"), false, DDialog::ButtonNormal);
        killProcessDialog->addButton(QString("结束进程"), true, DDialog::ButtonNormal);
        connect(killProcessDialog, &DDialog::buttonClicked, this, &MainWindow::dialogButtonClicked);

        killer = NULL;
    }
}

MainWindow::~MainWindow()
{
    // We don't need clean pointers because application has exit here.
}

void MainWindow::changeTheme(QString theme)
{
    if (theme == "light") {
        backgroundColor = "#FFFFFF";
        
        setBorderColor("#d9d9d9");
    } else {
        backgroundColor = "#0E0E0E";
        
        setBorderColor("#101010");
    }

    initThemeAction();
}

QList<bool> MainWindow::getColumnHideFlags()
{
    QString processColumns = settings->getOption("process_columns").toString();

    QList<bool> toggleHideFlags;
    toggleHideFlags << processColumns.contains("name");
    toggleHideFlags << processColumns.contains("cpu");
    toggleHideFlags << processColumns.contains("memory");
    toggleHideFlags << processColumns.contains("disk_write");
    toggleHideFlags << processColumns.contains("disk_read");
    toggleHideFlags << processColumns.contains("download");
    toggleHideFlags << processColumns.contains("upload");
    toggleHideFlags << processColumns.contains("pid");

    return toggleHideFlags;
}

bool MainWindow::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        QRect rect = QApplication::desktop()->screenGeometry();

        // Just change status monitor width when screen width is more than 1024.
        if (rect.width() * 0.2 > Constant::STATUS_BAR_WIDTH) {
            if (windowState() == Qt::WindowMaximized) {
                statusMonitor->setFixedWidth(rect.width() * 0.2);
            } else {
                statusMonitor->setFixedWidth(Constant::STATUS_BAR_WIDTH);
            }
        }

    } else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_F) {
            if (keyEvent->modifiers() == Qt::ControlModifier) {
                toolbar->focusInput();
            }
        }
    }

    return false;
}

void MainWindow::initTheme()
{
    QString theme = settings->getOption("theme_style").toString();
    DThemeManager::instance()->setTheme(theme);

    changeTheme(theme);
}

void MainWindow::initThemeAction()
{
    if (settings->getOption("theme_style") == "light") {
        darkThemeAction->setVisible(true);
        lightThemeAction->setVisible(false);
    } else {
        darkThemeAction->setVisible(false);
        lightThemeAction->setVisible(true);
    }
}

void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    QPainterPath path;
    path.addRect(QRectF(rect()));
    painter.setOpacity(1);
    painter.fillPath(path, QColor(backgroundColor));
}

void MainWindow::createWindowKiller()
{
    killer = new InteractiveKill();
    killer->setFocus();
    connect(killer, &InteractiveKill::killWindow, this, &MainWindow::popupKillConfirmDialog, Qt::QueuedConnection);
}

void MainWindow::dialogButtonClicked(int index, QString)
{
    if (index == 1) {
        if (killPid != -1) {
            if (kill(killPid, SIGTERM) != 0) {
                cout << "Kill failed." << endl;
            }

            killPid = -1;
        }
    }
}

void MainWindow::popupKillConfirmDialog(int pid)
{
    killer->close();

    killPid = pid;
    killProcessDialog->show();
}

void MainWindow::recordVisibleColumn(int, bool, QList<bool> columnVisibles)
{
    QList<QString> visibleColumns;
    visibleColumns << "name";


    if (columnVisibles[1]) {
        visibleColumns << "cpu";
    }

    if (columnVisibles[2]) {
        visibleColumns << "memory";
    }

    if (columnVisibles[3]) {
        visibleColumns << "disk_write";
    }

    if (columnVisibles[4]) {
        visibleColumns << "disk_read";
    }

    if (columnVisibles[5]) {
        visibleColumns << "download";
    }

    if (columnVisibles[6]) {
        visibleColumns << "upload";
    }

    if (columnVisibles[7]) {
        visibleColumns << "pid";
    }

    QString processColumns = "";
    for (int i = 0; i < visibleColumns.length(); i++) {
        if (i != visibleColumns.length() - 1) {
            processColumns += QString("%1,").arg(visibleColumns[i]);
        } else {
            processColumns += visibleColumns[i];
        }
    }

    settings->setOption("process_columns", processColumns);
}

void MainWindow::showWindowKiller()
{
    QTimer::singleShot(200, this, SLOT(createWindowKiller()));
}

void MainWindow::switchTab(int index)
{
    if (index == 0) {
        statusMonitor->switchToOnlyGui();
    } else if (index == 1) {
        statusMonitor->switchToOnlyMe();
    } else {
        statusMonitor->switchToAllProcess();
    }

    settings->setOption("process_tab_index", index);
}

void MainWindow::switchToLightTheme()
{
    settings->setOption("theme_style", "light");

    DThemeManager::instance()->setTheme("light");

    repaint();
}

void MainWindow::switchToDarkTheme()
{
    settings->setOption("theme_style", "dark");

    DThemeManager::instance()->setTheme("dark");

    repaint();
}
