#include <obs-module.h>
#include <obs-source.h>
#include <obs-frontend-api.h>
#include <QString>
#include <QTimer>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include "widgets.h"
#include "platform.h"

OBS_DECLARE_MODULE()

std::unordered_map<QString,obs_source_t*> sources;
QString foregroundWindowTitle;
QTimer refreshInterval;
ScrollingList *sourcesWidget;
Platform *platform;

using SourcePtr=std::unique_ptr<obs_source_t,decltype(&obs_source_release)>;
using SourceListPtr=std::unique_ptr<obs_frontend_source_list,decltype(&obs_frontend_source_list_free)>;
using SceneItemPtr=std::unique_ptr<obs_sceneitem_t,decltype(&obs_sceneitem_release)>;

const char *SUBSYSTEM_NAME="[Don't Blink]";

void Log(const QString &message)
{
	if (!message.isNull()) blog(LOG_INFO,"%s%s%s",SUBSYSTEM_NAME," ",message.toLocal8Bit().constData());
}

void Warn(const QString &message)
{
	blog(LOG_WARNING,"%s%s%s",SUBSYSTEM_NAME," ",message.toLocal8Bit().data());
}

QStringList SourceNames()
{
	QStringList list;
	for (const std::pair<QString,obs_source_t*> &pair : sources) list.append(pair.first);
	return list;
}

bool AvailableSource(obs_scene_t *scene,obs_sceneitem_t *item,void *data)
{
	obs_source_t *source=obs_sceneitem_get_source(item);
	sources[obs_source_get_name(source)]=source;
	return true;
}

void UpdateAvailableSources()
{
	obs_scene_t *scene=obs_scene_from_source(SourcePtr(obs_frontend_get_current_scene(),&obs_source_release).get()); // get current scene
	obs_scene_enum_items(scene,&AvailableSource,nullptr);
}

void ForegroundWindowChanged(const QString &title)
{
	obs_scene_t *scene=obs_scene_from_source(SourcePtr(obs_frontend_get_current_scene(),&obs_source_release).get()); // passing NULL into obs_scene_from_source() does not crash
	if (!scene) return;

	// active new source
	if (QString newSourceName=sourcesWidget->Source(title); !newSourceName.isNull()) obs_sceneitem_set_visible(obs_scene_find_source(scene,newSourceName.toLocal8Bit().data()),true);

	// deactive previous source
	if (!foregroundWindowTitle.isNull())
	{
		if (QString oldSourceName=sourcesWidget->Source(foregroundWindowTitle); !oldSourceName.isNull()) obs_sceneitem_set_visible(obs_scene_find_source(scene,oldSourceName.toLocal8Bit().data()),false);
	}
	foregroundWindowTitle=title;
}

void AvailableWindowsUpdated(const std::unordered_set<QString> &titles)
{
	QStringList sourceNames=SourceNames();
	sourcesWidget->AddEntries(titles,sourceNames);
}

void HandleEvent(obs_frontend_event event,void *data)
{
	switch (event)
	{
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		UpdateAvailableSources();
		platform->UpdateAvailableWindows();
		refreshInterval.start();
		break;
	}
}

void BuildUI()
{
	QMainWindow *window=static_cast<QMainWindow*>(obs_frontend_get_main_window());
	QDockWidget *dock=new QDockWidget("Don't Blink",window);
	QWidget *content=new QWidget(dock);
	QGridLayout *layout=new QGridLayout(content);
	sourcesWidget=new ScrollingList(content);
	layout->addWidget(sourcesWidget);
	QPushButton *refresh=new QPushButton("Refresh",content);
	refresh->connect(refresh,&QPushButton::clicked,platform,&Platform::UpdateAvailableWindows);
	layout->addWidget(refresh);
	content->setLayout(layout);
	dock->setWidget(content);
	dock->setObjectName("dont_blink");
	window->addDockWidget(Qt::BottomDockWidgetArea,dock);
	obs_frontend_add_dock(dock);
}

bool obs_module_load()
{
	obs_frontend_add_event_callback(HandleEvent,nullptr);

	BuildUI();

	try
	{
		platform=Platform::Create();
	}
	catch (const std::runtime_error &exception)
	{
		Log(QString("Error initializing platform (%1)").arg(exception.what()));
		return false;
	}
	platform->connect(platform,&Platform::Log,platform,&Log);
	platform->connect(platform,&Platform::AvailableWindowsUpdated,platform,&AvailableWindowsUpdated);
	platform->connect(platform,&Platform::ForegroundWindowChanged,platform,&ForegroundWindowChanged);
	Log("OS-specific logic initialized");

	refreshInterval.setInterval(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(1)));
	refreshInterval.connect(&refreshInterval,&QTimer::timeout,platform,&Platform::UpdateAvailableWindows);

	return true;
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(HandleEvent,nullptr);

	refreshInterval.stop();

	delete platform;
}
