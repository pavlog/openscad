/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <iostream>
#include "openscad.h"
#include "GeometryCache.h"
#include "ModuleCache.h"
#include "MainWindow.h"
#include "OpenSCADApp.h"
#include "parsersettings.h"
#include "rendersettings.h"
#include "Preferences.h"
#include "printutils.h"
#include "node.h"
#include "polyset.h"
#include "csgnode.h"
#include "highlighter.h"
#include "builtin.h"
#include "memory.h"
#include "expression.h"
#include "progress.h"
#include "dxfdim.h"
#include "legacyeditor.h"
#include "settings.h"
#ifdef USE_SCINTILLA_EDITOR
#include "scintillaeditor.h"
#endif
#include "AboutDialog.h"
#include "FontListDialog.h"
#include "LibraryInfoDialog.h"
#ifdef ENABLE_OPENCSG
#include "CSGTreeEvaluator.h"
#include "OpenCSGRenderer.h"
#include <opencsg.h>
#endif
#include "ProgressWidget.h"
#include "ThrownTogetherRenderer.h"
#include "CSGTreeNormalizer.h"
#include "QGLView.h"
#ifdef Q_OS_MAC
#include "CocoaUtils.h"
#endif
#include "PlatformUtils.h"
#ifdef OPENSCAD_UPDATER
#include "AutoUpdater.h"
#endif

#include <QMenu>
#include <QTime>
#include <QMenuBar>
#include <QSplitter>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QTextStream>
#include <QStatusBar>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QTimer>
#include <QMessageBox>
#include <QDesktopServices>
#include <QSettings>
#include <QProgressDialog>
#include <QMutexLocker>
#include <QTemporaryFile>
#include <QDockWidget>
#include <QClipboard>
#include <QDesktopWidget>
#include <QDirIterator>

#if (QT_VERSION < QT_VERSION_CHECK(5, 1, 0))
// Set dummy for Qt versions that do not have QSaveFile
#define QT_FILE_SAVE_CLASS QFile
#define QT_FILE_SAVE_COMMIT true
#else
#include <QSaveFile>
#define QT_FILE_SAVE_CLASS QSaveFile
#define QT_FILE_SAVE_COMMIT if (saveOk) { saveOk = file.commit(); } else { file.cancelWriting(); }
#endif

#include <fstream>

#include <algorithm>
#include <boost/version.hpp>
#include <sys/stat.h>

#ifdef ENABLE_CGAL

#include "CGALCache.h"
#include "GeometryEvaluator.h"
#include "CGALRenderer.h"
#include "CGAL_Nef_polyhedron.h"
#include "cgal.h"
#include "cgalworker.h"
#include "cgalutils.h"

#endif // ENABLE_CGAL

#include "boosty.h"
#include "FontCache.h"



void MainWindow::windowLoaded()
{
	setCurrentOutput();

	// initialize plugins
	QString librarydir;
	QDir libdir(QApplication::instance()->applicationDirPath());
#ifdef Q_OS_MAC
	libdir.cd("../Resources");
	if (!libdir.exists("plugins")) libdir.cd("../../..");
#elif defined(Q_OS_UNIX)
	if (libdir.cd("../share/openscad/plugins"))
	{
		librarydir = libdir.path();
	}
	else if (libdir.cd("../../share/openscad/plugins"))
	{
		librarydir = libdir.path();
	}
	else if (libdir.cd("../../plugins"))
	{
			librarydir = libdir.path();
	}
	else
#endif
		if (libdir.cd("plugins"))
		{
			librarydir = libdir.path();
		}
	//
	PRINTB("Plugins folder: [%s]",librarydir.toStdString().c_str());
	QDirIterator it(librarydir, QStringList() << "*.plugin", QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
	{
		QString pluginFile = it.next();
		PRINTB("Plugin file found [%s]",pluginFile.toStdString().c_str());
		//
		std::shared_ptr<Plugin> plugin(new Plugin(this));
		QSettings settings(pluginFile, QSettings::IniFormat);
		QString program = settings.value("Plugin/Executable").toString();
		//PRINT(program.toStdString().c_str());
		QFileInfo pdir(pluginFile);
		QStringList arguments;
		QString argument = settings.value("Plugin/Arguments").toString();
		arguments.push_back(argument);
		plugin->m_pluginProcess = std::shared_ptr<QProcess>(new QProcess(this));
		connect(plugin->m_pluginProcess.get(), SIGNAL(readyReadStandardOutput()), plugin.get(), SLOT(readyReadStandardOutput()));
		plugin->m_pluginProcess->setWorkingDirectory(pdir.absoluteDir().absolutePath());
		plugin->m_pluginProcess->start(program, arguments);
		plugin->m_pluginProcess->waitForStarted();
		PRINTB("Plugin program started [%s] wd [%s] pid [%llu] status[%d]",program.toStdString().c_str() % pdir.absoluteDir().absolutePath().toStdString().c_str() % plugin->m_pluginProcess->pid() % ((int)settings.status()));
		//PRINTB("Plugin stderr [%s]",plugin->m_pluginProcess->errorString().toStdString().c_str());
		m_plugins.push_back(plugin);
	}
	clearCurrentOutput();
}

void Plugin::readyReadStandardOutput()
{
	//m_parent->setCurrentOutput();
	QString strData = m_pluginProcess->readAllStandardOutput();
	m_commandLine+=strData;
	//PRINTB("Plugin [%s]",m_commandLine.toStdString().c_str());
	if( m_commandLine[m_commandLine.size()-1]!=QChar('\n'))
	{
		return;
	}
	// process only if \n is last char
	QStringList pieces =  strData.split( "\n" );
	foreach(QString cmd,pieces)
	{
		// if command starts from # - just print in log
		if( cmd[0]=='#' )
		{
			//m_parent->consoleOutput(cmd);
			PRINTB("PluginLog: [%s]",cmd.toStdString().c_str());
		}
		else if( cmd.startsWith("AddMenuItem") )
		{
			//AddMenuItem,idMenu1(Menu1Title)\\idMenu2(Menu2Title)\\actionid(ActionTitle),after#editActionUnindent,shortcut
			//AddMenuItem,menu_Edit(&Edit)\\editActionReIndent(Re-Indent),after#editActionUnindent,Ctrl+Alt+I\n
			//m_parent->consoleOutput(cmd);
			PRINTB("AddMenuItem: [%s]",cmd.toStdString().c_str());
			QStringList params = cmd.split( "," );

			
			
			//m_parent->menuBar()->get
			//QMenuBar *mb = new QMenuBar();
			//QMenu *updateMenu = mb->addMenu("MMMM");
			//this->updateAction = new QAction("Check for Update..", this);
		// Add to application menu
		//this->updateAction->setMenuRole(QAction::ApplicationSpecificRole);
		//this->updateAction->setEnabled(true);
		//this->connect(this->updateAction, SIGNAL(triggered()), this, SLOT(checkForUpdates()));

		//this->updateMenu->addAction(this->updateAction);
//QAction *act = m_parent->menuBar()->addMenu("Edit2")->addMenu("ffffff")->addAction("someAction");
//QObject::connect(act,SIGNAL(triggered()),
//                 someObj,SLOT(actionReaction()));
        //QAction* pAction = new QAction(m_parent);
        //pAction->setObjectName("tTTT");
		//pAction->setText("RRRRR");
		//m_parent->menu_Edit->addAction(pAction);
		//	m_parent->menu_Edit->addAction(pAction);
		//QAction *act = m_parent->menuBar()->addMenu("SomeMenu")->addMenu("someSubmenu")->addAction("someAction");
		//m_parent->menuBar()->up`
QAction *action = new QAction("Edit",m_parent);
QAction *dummyaction = new QAction("Testing",m_parent);
QMenu *menu = new QMenu();
menu->addAction(dummyaction);

//bool val= connect(menu, SIGNAL( aboutToShow()), this, SLOT( Move()));
//val= connect(menu, SIGNAL( aboutToHide()), this, SLOT(Move()));

action->setMenu(menu);
QList<QMenu*> lst;
lst = m_parent->menuBar()->findChildren<QMenu*>();
foreach (QMenu* m, lst)
{
		//PRINTB("Menu [%s]",m->objectName().toUtf8().constData());
	if( m->objectName()=="menu_Edit" )
	{
		m->addAction("eeeee");
	}
    //foreach (QAction* a, m->actions())
    //{
     //   actions->addAction(a);
    //}
}


//if( m_parent->menubar )
{
//m_parent->menubar->addAction(action);
//m_parent->menubar->addMenu("DDDDD");
	//m_parent->menuBar()->addMenu("Edit2")->addAction("ddd");
		}
		}
	}
	m_commandLine.clear();
	//PRINTB("Plugin [%s]",QString(m_pluginProcess->readAllStandardOutput()).toUtf8().constData());
	//m_parent->clearCurrentOutput();
}

