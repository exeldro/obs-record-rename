#include "record-rename.hpp"
#include "version.h"
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QMenu>
#include <QTimer>
#include <string>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>

static bool rename_record_enabled = true;
static bool rename_replay_enabled = true;

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("record-rename", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("RecordRename");
}

void ask_rename_file_UI(void *param)
{
	std::string path = (char *)param;
	bfree(param);
	const auto main_window = static_cast<QWidget *>(obs_frontend_get_main_window());
	size_t extension_pos = -1;
	size_t slash_pos = -1;
	for (size_t pos = path.length(); pos > 0; pos--) {
		auto c = path[pos - 1];
		if (c == '.' && extension_pos == (size_t)-1) {
			extension_pos = pos;
		} else if (c == '/' || c == '\\') {
			slash_pos = pos;
			break;
		}
	}
	std::string filename;
	std::string extension;
	std::string folder;
	if (extension_pos != (size_t)-1) {
		filename = path.substr(0, extension_pos - 1);
		extension = path.substr(extension_pos - 1);
	} else {
		filename = path;
	}
	if (slash_pos != (size_t)-1) {
		folder = filename.substr(0, slash_pos);
		filename = filename.substr(slash_pos);
	}
	std::string orig_filename = filename;
	std::string new_path = folder + filename + extension;
	while (os_file_exists(new_path.c_str())) {
		std::string title = obs_module_text("RenameFile");
		if (orig_filename != filename) {
			title += ": ";
			title += obs_module_text("FileExists");
		}
		if (!RenameFileDialog::AskForName(main_window, title, filename))
			return;
		new_path = folder + filename + extension;
	}
	os_rename(path.c_str(), new_path.c_str());
}

void ask_rename_file(std::string path)
{
	bool autoRemux = config_get_bool(obs_frontend_get_profile_config(), "Video", "AutoRemux");
	if (autoRemux) {
		blog(LOG_INFO, "[Record Rename] AutoRemux is enabled, skipping rename.");
		return;
	}
	if (!os_file_exists(path.c_str())) {
		blog(LOG_ERROR, "[Record Rename] File not found: %s", path.c_str());
		return;
	}
	obs_queue_task(OBS_TASK_UI, ask_rename_file_UI, (void *)bstrdup(path.c_str()), false);
}

void replay_saved(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	if (!rename_replay_enabled)
		return;
	obs_output_t *output = (obs_output_t *)data;
	calldata_t cd = {0};
	proc_handler_t *ph = obs_output_get_proc_handler(output);
	proc_handler_call(ph, "get_last_replay", &cd);
	const char *path = calldata_string(&cd, "path");
	if (path)
		ask_rename_file(path);
	calldata_free(&cd);
}

void record_stop(void *data, calldata_t *calldata)
{
	UNUSED_PARAMETER(calldata);
	if (!rename_record_enabled)
		return;
	obs_output_t *output = (obs_output_t *)data;
	obs_data_t *settings = obs_output_get_settings(output);
	const char *path = obs_data_get_string(settings, "path");
	if (path && strlen(path) && os_file_exists(path)) {
		ask_rename_file(path);
	} else {
		const char *url = obs_data_get_string(settings, "url");
		if (url && strlen(url) && os_file_exists(url)) {
			ask_rename_file(url);
		}
	}
	obs_data_release(settings);
}

bool loadOutput(void *data, obs_output_t *output)
{
	UNUSED_PARAMETER(data);
	auto r = obs_frontend_get_recording_output();
	obs_output_release(r);
	if (r == output)
		return true;
	r = obs_frontend_get_replay_buffer_output();
	obs_output_release(r);
	if (r == output)
		return true;

	if (strcmp("replay_buffer", obs_output_get_id(output)) == 0) {
		auto sh = obs_output_get_signal_handler(output);
		signal_handler_connect(sh, "saved", replay_saved, output);
	} else {
		auto sh = obs_output_get_signal_handler(output);
		signal_handler_connect(sh, "stop", record_stop, output);
	}
	return true;
}

void loadOutputs()
{
	obs_enum_outputs(loadOutput, nullptr);
}

void frontend_event(obs_frontend_event event, void *param)
{
	UNUSED_PARAMETER(param);
	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		if (rename_record_enabled)
			ask_rename_file(obs_frontend_get_last_recording());
		break;
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		if (rename_replay_enabled)
			ask_rename_file(obs_frontend_get_last_replay());
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
		config_t *config = obs_frontend_get_profile_config();
		if (config) {
			config_set_default_bool(config, "RecordRename", "RenameRecord", true);
			config_set_default_bool(config, "RecordRename", "RenameReplay", true);
			rename_record_enabled = config_get_bool(config, "RecordRename", "RenameRecord");
			rename_replay_enabled = config_get_bool(config, "RecordRename", "RenameReplay");
		}
		loadOutputs();
		break;
	}
	default:
		break;
	}
}

void save_config()
{
	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		config_set_bool(config, "RecordRename", "RenameRecord", rename_record_enabled);
		config_set_bool(config, "RecordRename", "RenameReplay", rename_replay_enabled);
	}
	config_save(config);
	blog(LOG_INFO, "[Record Rename] Config saved: %s %s", rename_record_enabled ? "true" : "false",
	     rename_replay_enabled ? "true" : "false");
}

static QTimer *timer;

bool obs_module_load()
{
	blog(LOG_INFO, "[Record Rename] loaded version %s", PROJECT_VERSION);

	obs_frontend_add_event_callback(frontend_event, nullptr);
	timer = new QTimer();
	timer->setInterval(2000);
	QObject::connect(timer, &QTimer::timeout, []() { loadOutputs(); });
	timer->start();

	QAction *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(obs_module_text("RecordRename")));
	QMenu *menu = new QMenu();
	auto recordAction = menu->addAction(QString::fromUtf8(obs_module_text("Record")), [] {
		rename_record_enabled = !rename_record_enabled;
		save_config();
	});
	recordAction->setCheckable(true);
	auto replayAction = menu->addAction(QString::fromUtf8(obs_module_text("ReplayBuffer")), [] {
		rename_replay_enabled = !rename_replay_enabled;
		save_config();
	});
	replayAction->setCheckable(true);
	action->setMenu(menu);
	QObject::connect(menu, &QMenu::aboutToShow, [recordAction, replayAction] {
		recordAction->setChecked(rename_record_enabled);
		replayAction->setChecked(rename_replay_enabled);
	});
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	if (timer) {
		timer->stop();
		delete timer;
		timer = nullptr;
	}
}

RenameFileDialog::RenameFileDialog(QWidget *parent, std::string title) : QDialog(parent)
{
	setWindowTitle(QString::fromUtf8(title));
	setModal(true);
	setWindowModality(Qt::WindowModality::WindowModal);
	setMinimumWidth(300);
	setMinimumHeight(100);
	QVBoxLayout *layout = new QVBoxLayout;
	setLayout(layout);

	userText = new QLineEdit(this);
	layout->addWidget(userText);

	QDialogButtonBox *buttonbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttonbox);
	buttonbox->setCenterButtons(true);
	connect(buttonbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttonbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool RenameFileDialog::AskForName(QWidget *parent, std::string title, std::string &name)
{
	RenameFileDialog dialog(parent, title);
	dialog.userText->setMaxLength(170);
	dialog.userText->setText(QString::fromUtf8(name.c_str()));
	dialog.userText->selectAll();
	dialog.userText->setFocus();

	if (dialog.exec() != DialogCode::Accepted) {
		return false;
	}
	name = dialog.userText->text().toUtf8().constData();
	return true;
}
