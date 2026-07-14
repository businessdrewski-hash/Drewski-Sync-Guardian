// SPDX-License-Identifier: GPL-2.0-or-later
// Sync Guardian v0.2.0 - OBS companion plugin for DistroAV/NDI monitoring and recovery.

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>
#include <util/platform.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("sync-guardian", "en-US")

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "0.2.0"
#endif

namespace {

constexpr const char *kDockId = "sync_guardian_dock";
constexpr const char *kDistroAvSourceId = "ndi_source";
constexpr const char *kPropSource = "ndi_source_name";
constexpr const char *kPropLatency = "latency";
constexpr const char *kPropFrameSync = "ndi_framesync";
constexpr const char *kPropSync = "ndi_sync";
constexpr uint64_t kNsPerMs = 1'000'000ULL;
constexpr uint64_t kNsPerSecond = 1'000'000'000ULL;
constexpr uint64_t kJumpThresholdNs = 50'000'000ULL;
constexpr uint64_t kJumpCooldownNs = 500'000'000ULL;
constexpr uint64_t kOffsetWindowNs = 5ULL * kNsPerSecond;
constexpr uint64_t kBaselineWindowNs = 30ULL * kNsPerSecond;
constexpr uint64_t kDiagnosticHistoryNs = 60ULL * kNsPerSecond;
constexpr uint64_t kIncidentPreNs = 30ULL * kNsPerSecond;
constexpr uint64_t kIncidentPostNs = 30ULL * kNsPerSecond;
constexpr uint64_t kIncidentCooldownNs = 5ULL * 60ULL * kNsPerSecond;
constexpr uint64_t kResetLimitWindowNs = 60ULL * 60ULL * kNsPerSecond;
constexpr uint64_t kObserveRepeatNs = 30ULL * kNsPerSecond;
constexpr uint64_t kJumpEvidenceWindowNs = 5ULL * kNsPerSecond;
constexpr uint64_t kStallConfirmNs = 500ULL * kNsPerMs;
constexpr int kRefreshIntervalMs = 250;

static int64_t signedDelta(uint64_t newer, uint64_t older)
{
	return static_cast<int64_t>(newer) - static_cast<int64_t>(older);
}

static double nsToMs(int64_t value)
{
	return static_cast<double>(value) / static_cast<double>(kNsPerMs);
}

static double median(std::vector<double> values)
{
	if (values.empty())
		return std::numeric_limits<double>::quiet_NaN();
	const size_t middle = values.size() / 2;
	std::nth_element(values.begin(), values.begin() + static_cast<ptrdiff_t>(middle), values.end());
	double result = values[middle];
	if ((values.size() % 2) == 0) {
		const auto lower = std::max_element(values.begin(), values.begin() + static_cast<ptrdiff_t>(middle));
		result = (*lower + result) * 0.5;
	}
	return result;
}

static QString latencyName(long long value)
{
	switch (value) {
	case 0:
		return QStringLiteral("Normal");
	case 1:
		return QStringLiteral("Low");
	case 2:
		return QStringLiteral("Lowest");
	default:
		return QStringLiteral("Unknown (%1)").arg(value);
	}
}

static QString syncModeName(long long value)
{
	switch (value) {
	case 1:
		return QStringLiteral("NDI Timestamp");
	case 2:
		return QStringLiteral("NDI Source Timecode");
	default:
		return QStringLiteral("Unknown (%1)").arg(value);
	}
}

enum class AutomationMode : int {
	Observe = 0,
	Ask = 1,
	Automatic = 2,
};

enum class RecoveryTarget : int {
	None = 0,
	Video,
	DesktopAudio,
	Mic,
	BothAudio,
	EntireGroup,
};

enum class IssueKind : int {
	None = 0,
	VideoStall,
	DesktopAudioStall,
	MicStall,
	PersistentDrift,
};

static QString targetName(RecoveryTarget target)
{
	switch (target) {
	case RecoveryTarget::Video:
		return QStringLiteral("NDI Video");
	case RecoveryTarget::DesktopAudio:
		return QStringLiteral("NDI Desktop Audio");
	case RecoveryTarget::Mic:
		return QStringLiteral("NDI Mic");
	case RecoveryTarget::BothAudio:
		return QStringLiteral("Both NDI audio sources");
	case RecoveryTarget::EntireGroup:
		return QStringLiteral("Entire NDI sync group");
	default:
		return QStringLiteral("None");
	}
}

static QString modeName(AutomationMode mode)
{
	switch (mode) {
	case AutomationMode::Ask:
		return QStringLiteral("Ask before reset");
	case AutomationMode::Automatic:
		return QStringLiteral("Fully automatic");
	default:
		return QStringLiteral("Observe only");
	}
}

struct OffsetSample {
	uint64_t wallNs = 0;
	double valueMs = std::numeric_limits<double>::quiet_NaN();
};

struct DiagnosticSample {
	uint64_t wallNs = 0;
	QString time;
	double rawOffsetMs = std::numeric_limits<double>::quiet_NaN();
	double filteredOffsetMs = std::numeric_limits<double>::quiet_NaN();
	double driftMs = std::numeric_limits<double>::quiet_NaN();
	double driftRateMsPerMinute = std::numeric_limits<double>::quiet_NaN();
	double videoAgeMs = std::numeric_limits<double>::quiet_NaN();
	double desktopAgeMs = std::numeric_limits<double>::quiet_NaN();
	double micAgeMs = std::numeric_limits<double>::quiet_NaN();
	int confidence = 0;
};

struct SourceState {
	QString role;
	QString sourceName;
	obs_weak_source_t *weak = nullptr;
	obs_data_t *snapshot = nullptr;
	bool monitorAudio = false;

	std::atomic<uint64_t> lastAudioTimestampNs{0};
	std::atomic<uint64_t> lastAudioWallNs{0};
	std::atomic<uint64_t> lastAudioJumpWallNs{0};
	std::atomic<int64_t> lastAudioJumpErrorNs{0};
	std::atomic<uint64_t> audioJumpCount{0};

	uint64_t lastVideoTimestampNs = 0;
	uint64_t lastVideoWallNs = 0;
	uint64_t lastVideoJumpWallNs = 0;
	int64_t lastVideoJumpErrorNs = 0;
	uint64_t videoJumpCount = 0;

	uint64_t resetCount = 0;
	uint64_t recoveryCount = 0;
	std::shared_ptr<std::atomic_bool> resetInProgress = std::make_shared<std::atomic_bool>(false);
	bool wasFresh = false;
	bool hasEverBeenFresh = false;
	bool enabled = false;
	bool active = false;
	bool showing = false;
};

struct ResetTicket {
	obs_weak_source_t *weak = nullptr;
	obs_data_t *original = nullptr;
	std::shared_ptr<std::atomic_bool> resetFlag;
	bool restored = false;

	void restore()
	{
		if (restored || !weak || !original)
			return;
		obs_source_t *source = obs_weak_source_get_source(weak);
		if (source) {
			obs_source_update(source, original);
			obs_source_release(source);
		}
		restored = true;
		if (resetFlag)
			resetFlag->store(false);
	}

	~ResetTicket()
	{
		restore();
		if (original)
			obs_data_release(original);
		if (weak)
			obs_weak_source_release(weak);
	}
};

struct RecoveryAttempt {
	bool active = false;
	bool automated = false;
	bool escalationUsed = false;
	RecoveryTarget target = RecoveryTarget::None;
	IssueKind issue = IssueKind::None;
	QString reason;
	uint64_t verifyAtNs = 0;
	double preDriftMs = std::numeric_limits<double>::quiet_NaN();
};

struct IncidentCapture {
	bool active = false;
	QString path;
	uint64_t finalizeAtNs = 0;
	QJsonObject root;
	QJsonArray samples;
};

class SyncGuardian {
public:
	SyncGuardian()
	{
		lifetimeContext_ = new QObject();
		states_[0].role = QStringLiteral("NDI Video");
		states_[0].monitorAudio = false;
		states_[1].role = QStringLiteral("Desktop Audio");
		states_[1].monitorAudio = true;
		states_[2].role = QStringLiteral("Mic");
		states_[2].monitorAudio = true;

		buildUi();
		refreshSourceLists();
		loadConfiguration();
		bindAllSources();
		registerHotkeys();

		const uint64_t now = os_gettime_ns();
		monitoringGraceUntilNs_ = now + static_cast<uint64_t>(startupGraceSec_->value()) * kNsPerSecond;

		refreshTimer_ = new QTimer(lifetimeContext_);
		refreshTimer_->setInterval(kRefreshIntervalMs);
		QObject::connect(refreshTimer_, &QTimer::timeout, [this]() { refreshDiagnostics(); });
		refreshTimer_->start();

		obs_frontend_add_event_callback(frontendEvent, this);
		appendEvent(QStringLiteral("Sync Guardian %1 loaded in %2 mode")
				    .arg(QStringLiteral(PLUGIN_VERSION), modeName(currentMode())),
			    false);
	}

	~SyncGuardian()
	{
		obs_frontend_remove_event_callback(frontendEvent, this);
		unregisterHotkeys();

		for (const auto &ticket : pendingResets_)
			ticket->restore();
		pendingResets_.clear();
		finalizeIncident(true);

		delete lifetimeContext_;
		lifetimeContext_ = nullptr;

		for (auto &state : states_) {
			detachSource(state);
			if (state.snapshot) {
				obs_data_release(state.snapshot);
				state.snapshot = nullptr;
			}
		}
		obs_frontend_remove_dock(kDockId);
	}

private:
	QObject *lifetimeContext_ = nullptr;
	QWidget *panel_ = nullptr;
	std::array<QComboBox *, 3> sourceCombos_{};
	QSpinBox *pulseDurationMs_ = nullptr;
	QCheckBox *chapterMarkers_ = nullptr;
	QCheckBox *jsonLogging_ = nullptr;
	QCheckBox *incidentReports_ = nullptr;

	QComboBox *automationMode_ = nullptr;
	QCheckBox *onlyWhenOutputActive_ = nullptr;
	QCheckBox *requireActiveSources_ = nullptr;
	QCheckBox *enableFreezeDetection_ = nullptr;
	QCheckBox *enableDriftDetection_ = nullptr;
	QCheckBox *autoEscalate_ = nullptr;
	QSpinBox *videoStallMs_ = nullptr;
	QSpinBox *audioStallMs_ = nullptr;
	QSpinBox *driftThresholdMs_ = nullptr;
	QSpinBox *driftPersistenceMs_ = nullptr;
	QSpinBox *cooldownSec_ = nullptr;
	QSpinBox *maxAutoResetsPerHour_ = nullptr;
	QSpinBox *startupGraceSec_ = nullptr;
	QSpinBox *verifyDelaySec_ = nullptr;

	QLabel *automationStatusLabel_ = nullptr;
	QLabel *healthLabel_ = nullptr;
	QLabel *avOffsetLabel_ = nullptr;
	QLabel *driftLabel_ = nullptr;
	QLabel *micOffsetLabel_ = nullptr;
	QLabel *obsStatsLabel_ = nullptr;
	QTableWidget *statusTable_ = nullptr;
	QTextEdit *eventLog_ = nullptr;
	QTimer *refreshTimer_ = nullptr;

	std::array<SourceState, 3> states_{};
	std::vector<std::shared_ptr<ResetTicket>> pendingResets_;
	std::deque<OffsetSample> offsetSamples_;
	std::deque<DiagnosticSample> diagnosticHistory_;
	std::deque<uint64_t> automatedResetTimes_;
	RecoveryAttempt recovery_;
	IncidentCapture incident_;

	double baselineOffsetMs_ = std::numeric_limits<double>::quiet_NaN();
	double filteredOffsetMs_ = std::numeric_limits<double>::quiet_NaN();
	double driftRateMsPerMinute_ = std::numeric_limits<double>::quiet_NaN();
	bool restoringSnapshot_ = false;
	bool loadingConfig_ = true;
	bool promptActive_ = false;
	uint64_t moduleStartNs_ = os_gettime_ns();
	uint64_t monitoringGraceUntilNs_ = 0;
	uint64_t detectionSuppressedUntilNs_ = 0;
	uint64_t lastAutomatedResetNs_ = 0;
	uint64_t videoStallSinceNs_ = 0;
	uint64_t desktopStallSinceNs_ = 0;
	uint64_t micStallSinceNs_ = 0;
	uint64_t driftSinceNs_ = 0;
	uint64_t lastObservedIssueNs_ = 0;
	uint64_t lastIncidentStartNs_ = 0;
	QString lastObservedIssueKey_;
	int currentConfidence_ = 0;
	std::array<uint64_t, 3> loggedJumpCounts_{0, 0, 0};

	std::array<obs_hotkey_id, 8> hotkeys_{OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID,
					      OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID,
					      OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID,
					      OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID};

	void buildUi()
	{
		panel_ = new QWidget();
		panel_->setObjectName(QStringLiteral("SyncGuardianPanel"));
		auto *root = new QVBoxLayout(panel_);
		root->setContentsMargins(8, 8, 8, 8);

		auto *sourceBox = new QGroupBox(QStringLiteral("NDI source mapping"), panel_);
		auto *sourceForm = new QFormLayout(sourceBox);
		for (size_t i = 0; i < sourceCombos_.size(); ++i) {
			sourceCombos_[i] = new QComboBox(sourceBox);
			sourceCombos_[i]->setSizeAdjustPolicy(QComboBox::AdjustToContents);
			sourceForm->addRow(states_[i].role + QStringLiteral(":"), sourceCombos_[i]);
			QObject::connect(sourceCombos_[i], QOverload<int>::of(&QComboBox::currentIndexChanged),
					 [this, i](int) {
						bindSource(i, sourceCombos_[i]->currentData().toString());
						clearCalibration(QStringLiteral("source mapping changed"), false);
						saveConfig();
					 });
		}
		root->addWidget(sourceBox);

		auto *automationBox = new QGroupBox(QStringLiteral("Automatic detection and recovery"), panel_);
		auto *automationForm = new QFormLayout(automationBox);
		automationMode_ = new QComboBox(automationBox);
		automationMode_->addItem(QStringLiteral("Observe only"), static_cast<int>(AutomationMode::Observe));
		automationMode_->addItem(QStringLiteral("Ask before resetting"), static_cast<int>(AutomationMode::Ask));
		automationMode_->addItem(QStringLiteral("Fully automatic"), static_cast<int>(AutomationMode::Automatic));
		automationForm->addRow(QStringLiteral("Operating mode:"), automationMode_);

		onlyWhenOutputActive_ = new QCheckBox(QStringLiteral("Only act while streaming or recording"), automationBox);
		onlyWhenOutputActive_->setChecked(true);
		requireActiveSources_ = new QCheckBox(QStringLiteral("Require mapped sources to be active (video also showing)"), automationBox);
		requireActiveSources_->setChecked(true);
		enableFreezeDetection_ = new QCheckBox(QStringLiteral("Detect packet stalls and frozen video"), automationBox);
		enableFreezeDetection_->setChecked(true);
		enableDriftDetection_ = new QCheckBox(QStringLiteral("Detect persistent A/V drift from calibrated baseline"), automationBox);
		enableDriftDetection_->setChecked(true);
		autoEscalate_ = new QCheckBox(QStringLiteral("Escalate a failed source reset to a full-group rebuild"), automationBox);
		autoEscalate_->setChecked(true);
		automationForm->addRow(onlyWhenOutputActive_);
		automationForm->addRow(requireActiveSources_);
		automationForm->addRow(enableFreezeDetection_);
		automationForm->addRow(enableDriftDetection_);
		automationForm->addRow(autoEscalate_);

		videoStallMs_ = createSpin(automationBox, 250, 10000, 1000, QStringLiteral(" ms"));
		audioStallMs_ = createSpin(automationBox, 250, 10000, 1000, QStringLiteral(" ms"));
		driftThresholdMs_ = createSpin(automationBox, 50, 2000, 200, QStringLiteral(" ms"));
		driftPersistenceMs_ = createSpin(automationBox, 1000, 60000, 10000, QStringLiteral(" ms"));
		cooldownSec_ = createSpin(automationBox, 10, 3600, 180, QStringLiteral(" sec"));
		maxAutoResetsPerHour_ = createSpin(automationBox, 1, 20, 3, QStringLiteral(" / hour"));
		startupGraceSec_ = createSpin(automationBox, 5, 300, 30, QStringLiteral(" sec"));
		verifyDelaySec_ = createSpin(automationBox, 2, 30, 5, QStringLiteral(" sec"));
		automationForm->addRow(QStringLiteral("Video stall threshold:"), videoStallMs_);
		automationForm->addRow(QStringLiteral("Audio stall threshold:"), audioStallMs_);
		automationForm->addRow(QStringLiteral("Persistent drift threshold:"), driftThresholdMs_);
		automationForm->addRow(QStringLiteral("Drift must persist for:"), driftPersistenceMs_);
		automationForm->addRow(QStringLiteral("Automatic reset cooldown:"), cooldownSec_);
		automationForm->addRow(QStringLiteral("Maximum automatic resets:"), maxAutoResetsPerHour_);
		automationForm->addRow(QStringLiteral("Startup/scene-change grace:"), startupGraceSec_);
		automationForm->addRow(QStringLiteral("Recovery verification delay:"), verifyDelaySec_);

		auto *calibrationButtons = new QWidget(automationBox);
		auto *calibrationLayout = new QGridLayout(calibrationButtons);
		calibrationLayout->setContentsMargins(0, 0, 0, 0);
		auto *setBaseline = new QPushButton(QStringLiteral("Set Current Baseline"), calibrationButtons);
		auto *autoCalibrate = new QPushButton(QStringLiteral("Restart Auto Calibration"), calibrationButtons);
		calibrationLayout->addWidget(setBaseline, 0, 0);
		calibrationLayout->addWidget(autoCalibrate, 0, 1);
		automationForm->addRow(QStringLiteral("Calibration:"), calibrationButtons);
		root->addWidget(automationBox);

		auto *controlBox = new QGroupBox(QStringLiteral("Manual recovery"), panel_);
		auto *controlGrid = new QGridLayout(controlBox);
		auto *resetVideo = new QPushButton(QStringLiteral("Reset Video Only"), controlBox);
		auto *resetDesktop = new QPushButton(QStringLiteral("Reset Desktop Audio Only"), controlBox);
		auto *resetMic = new QPushButton(QStringLiteral("Reset Mic Only"), controlBox);
		auto *resetBothAudio = new QPushButton(QStringLiteral("Reset Both Audio Sources"), controlBox);
		auto *rebuildGroup = new QPushButton(QStringLiteral("Rebuild Entire Sync Group"), controlBox);
		auto *captureSnapshot = new QPushButton(QStringLiteral("Capture Known-Good State"), controlBox);
		auto *restoreSnapshot = new QPushButton(QStringLiteral("Restore Last Known-Good State"), controlBox);
		auto *markEventButton = new QPushButton(QStringLiteral("Mark Sync Event"), controlBox);
		auto *refreshSources = new QPushButton(QStringLiteral("Refresh Source List"), controlBox);

		controlGrid->addWidget(resetVideo, 0, 0);
		controlGrid->addWidget(resetDesktop, 0, 1);
		controlGrid->addWidget(resetMic, 1, 0);
		controlGrid->addWidget(resetBothAudio, 1, 1);
		controlGrid->addWidget(rebuildGroup, 2, 0, 1, 2);
		controlGrid->addWidget(captureSnapshot, 3, 0);
		controlGrid->addWidget(restoreSnapshot, 3, 1);
		controlGrid->addWidget(markEventButton, 4, 0);
		controlGrid->addWidget(refreshSources, 4, 1);

		pulseDurationMs_ = createSpin(controlBox, 50, 1500, 180, QStringLiteral(" ms reset pulse"));
		controlGrid->addWidget(pulseDurationMs_, 5, 0, 1, 2);
		chapterMarkers_ = new QCheckBox(QStringLiteral("Add recording chapter on actions"), controlBox);
		chapterMarkers_->setChecked(true);
		jsonLogging_ = new QCheckBox(QStringLiteral("Append events to sync-guardian-events.jsonl"), controlBox);
		jsonLogging_->setChecked(true);
		incidentReports_ = new QCheckBox(QStringLiteral("Capture 30 seconds before/after detected incidents"), controlBox);
		incidentReports_->setChecked(true);
		controlGrid->addWidget(chapterMarkers_, 6, 0, 1, 2);
		controlGrid->addWidget(jsonLogging_, 7, 0, 1, 2);
		controlGrid->addWidget(incidentReports_, 8, 0, 1, 2);
		root->addWidget(controlBox);

		QObject::connect(resetVideo, &QPushButton::clicked,
				 [this]() { manualReset(RecoveryTarget::Video, QStringLiteral("Reset Video Only")); });
		QObject::connect(resetDesktop, &QPushButton::clicked,
				 [this]() { manualReset(RecoveryTarget::DesktopAudio, QStringLiteral("Reset Desktop Audio Only")); });
		QObject::connect(resetMic, &QPushButton::clicked,
				 [this]() { manualReset(RecoveryTarget::Mic, QStringLiteral("Reset Mic Only")); });
		QObject::connect(resetBothAudio, &QPushButton::clicked,
				 [this]() { manualReset(RecoveryTarget::BothAudio, QStringLiteral("Reset Both Audio Sources")); });
		QObject::connect(rebuildGroup, &QPushButton::clicked,
				 [this]() { manualReset(RecoveryTarget::EntireGroup, QStringLiteral("Rebuild Entire Sync Group")); });
		QObject::connect(captureSnapshot, &QPushButton::clicked, [this]() { captureSnapshotState(); });
		QObject::connect(restoreSnapshot, &QPushButton::clicked, [this]() { restoreSnapshotState(); });
		QObject::connect(setBaseline, &QPushButton::clicked, [this]() {
			const double current = std::isfinite(filteredOffsetMs_) ? filteredOffsetMs_ : calculateAvOffsetMs();
			if (std::isfinite(current)) {
				baselineOffsetMs_ = current;
				appendEvent(QStringLiteral("A/V baseline manually set to %1 ms").arg(current, 0, 'f', 2));
			} else {
				appendEvent(QStringLiteral("A/V baseline unavailable: both video and desktop audio must be fresh"), false);
			}
		});
		QObject::connect(autoCalibrate, &QPushButton::clicked,
				 [this]() { clearCalibration(QStringLiteral("manual auto-calibration restart"), true); });
		QObject::connect(markEventButton, &QPushButton::clicked,
				 [this]() { appendEvent(QStringLiteral("Manual sync event marker")); });
		QObject::connect(refreshSources, &QPushButton::clicked, [this]() {
			refreshSourceLists();
			bindAllSources();
			clearCalibration(QStringLiteral("source list refreshed"), false);
			appendEvent(QStringLiteral("NDI source list refreshed"), false);
		});

		auto *diagnosticsBox = new QGroupBox(QStringLiteral("Live diagnostics"), panel_);
		auto *diagnosticsLayout = new QVBoxLayout(diagnosticsBox);
		automationStatusLabel_ = new QLabel(QStringLiteral("Automation: starting"), diagnosticsBox);
		healthLabel_ = new QLabel(QStringLiteral("Detection confidence: 0/100"), diagnosticsBox);
		avOffsetLabel_ = new QLabel(QStringLiteral("Video − Desktop Audio timestamp: —"), diagnosticsBox);
		driftLabel_ = new QLabel(QStringLiteral("Drift from baseline: —"), diagnosticsBox);
		micOffsetLabel_ = new QLabel(QStringLiteral("Mic − Desktop Audio timestamp: —"), diagnosticsBox);
		obsStatsLabel_ = new QLabel(QStringLiteral("OBS: —"), diagnosticsBox);
		diagnosticsLayout->addWidget(automationStatusLabel_);
		diagnosticsLayout->addWidget(healthLabel_);
		diagnosticsLayout->addWidget(avOffsetLabel_);
		diagnosticsLayout->addWidget(driftLabel_);
		diagnosticsLayout->addWidget(micOffsetLabel_);
		diagnosticsLayout->addWidget(obsStatsLabel_);

		statusTable_ = new QTableWidget(3, 8, diagnosticsBox);
		statusTable_->setHorizontalHeaderLabels({QStringLiteral("Role"), QStringLiteral("OBS source"),
							 QStringLiteral("State"), QStringLiteral("Packet age"),
							 QStringLiteral("Timestamp jumps"), QStringLiteral("Resets"),
							 QStringLiteral("Recoveries"), QStringLiteral("DistroAV settings")});
		statusTable_->verticalHeader()->setVisible(false);
		statusTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
		statusTable_->setSelectionMode(QAbstractItemView::NoSelection);
		statusTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
		statusTable_->horizontalHeader()->setStretchLastSection(true);
		for (int row = 0; row < 3; ++row) {
			for (int col = 0; col < 8; ++col)
				statusTable_->setItem(row, col, new QTableWidgetItem());
		}
		diagnosticsLayout->addWidget(statusTable_);
		root->addWidget(diagnosticsBox);

		eventLog_ = new QTextEdit(panel_);
		eventLog_->setReadOnly(true);
		eventLog_->setMaximumHeight(175);
		root->addWidget(eventLog_);

		connectSettingsSignals();
		if (!obs_frontend_add_dock_by_id(kDockId, "Sync Guardian", panel_))
			blog(LOG_ERROR, "[sync-guardian] Failed to add dock; dock id may already be registered");
	}

	QSpinBox *createSpin(QWidget *parent, int minimum, int maximum, int value, const QString &suffix)
	{
		auto *spin = new QSpinBox(parent);
		spin->setRange(minimum, maximum);
		spin->setValue(value);
		spin->setSuffix(suffix);
		return spin;
	}

	void connectSettingsSignals()
	{
		auto save = [this]() { saveConfig(); };
		QObject::connect(automationMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, save](int) {
			save();
			appendEvent(QStringLiteral("Automation mode changed to %1").arg(modeName(currentMode())), false);
		});
		const std::array<QCheckBox *, 8> checks = {onlyWhenOutputActive_, requireActiveSources_,
							 enableFreezeDetection_, enableDriftDetection_, autoEscalate_,
							 chapterMarkers_, jsonLogging_, incidentReports_};
		for (QCheckBox *check : checks)
			QObject::connect(check, &QCheckBox::toggled, save);
		const std::array<QSpinBox *, 9> spins = {videoStallMs_, audioStallMs_, driftThresholdMs_,
						       driftPersistenceMs_, cooldownSec_, maxAutoResetsPerHour_,
						       startupGraceSec_, verifyDelaySec_, pulseDurationMs_};
		for (QSpinBox *spin : spins)
			QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), save);
	}

	void refreshSourceLists()
	{
		QStringList names;
		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				auto *list = static_cast<QStringList *>(param);
				const char *id = obs_source_get_unversioned_id(source);
				if (id && strcmp(id, kDistroAvSourceId) == 0)
					list->append(QString::fromUtf8(obs_source_get_name(source)));
				return true;
			},
			&names);
		names.removeDuplicates();
		names.sort(Qt::CaseInsensitive);

		for (size_t i = 0; i < sourceCombos_.size(); ++i) {
			const QString current = sourceCombos_[i]->currentData().toString();
			QSignalBlocker blocker(sourceCombos_[i]);
			sourceCombos_[i]->clear();
			sourceCombos_[i]->addItem(QStringLiteral("(Not selected)"), QString());
			for (const QString &name : names)
				sourceCombos_[i]->addItem(name, name);
			const int index = sourceCombos_[i]->findData(current);
			if (index >= 0)
				sourceCombos_[i]->setCurrentIndex(index);
		}
	}

	obs_data_t *loadConfig() const
	{
		char *path = obs_module_config_path("sync-guardian.json");
		if (!path)
			return nullptr;
		obs_data_t *data = obs_data_create_from_json_file_safe(path, ".bak");
		bfree(path);
		return data;
	}

	void loadConfiguration()
	{
		loadingConfig_ = true;
		obs_data_t *config = loadConfig();
		const std::array<QString, 3> defaults = {QStringLiteral("NDI Video"), QStringLiteral("NDI Desktop Audio"),
							  QStringLiteral("NDI MIC only")};
		const std::array<const char *, 3> sourceKeys = {"video_source", "desktop_source", "mic_source"};

		for (size_t i = 0; i < sourceCombos_.size(); ++i) {
			QString desired;
			if (config)
				desired = QString::fromUtf8(obs_data_get_string(config, sourceKeys[i]));
			if (desired.isEmpty())
				desired = defaults[i];
			QSignalBlocker blocker(sourceCombos_[i]);
			const int index = sourceCombos_[i]->findData(desired);
			if (index >= 0)
				sourceCombos_[i]->setCurrentIndex(index);
		}

		if (config) {
			if (obs_data_has_user_value(config, "automation_mode"))
				setComboDataBlocked(automationMode_, static_cast<int>(obs_data_get_int(config, "automation_mode")));
			loadCheckIfPresent(config, "only_when_output_active", onlyWhenOutputActive_);
			loadCheckIfPresent(config, "require_active_sources", requireActiveSources_);
			loadCheckIfPresent(config, "enable_freeze_detection", enableFreezeDetection_);
			loadCheckIfPresent(config, "enable_drift_detection", enableDriftDetection_);
			loadCheckIfPresent(config, "auto_escalate", autoEscalate_);
			loadCheckIfPresent(config, "chapter_markers", chapterMarkers_);
			loadCheckIfPresent(config, "json_logging", jsonLogging_);
			loadCheckIfPresent(config, "incident_reports", incidentReports_);
			setSpinBlocked(videoStallMs_, obs_data_get_int(config, "video_stall_ms"));
			setSpinBlocked(audioStallMs_, obs_data_get_int(config, "audio_stall_ms"));
			setSpinBlocked(driftThresholdMs_, obs_data_get_int(config, "drift_threshold_ms"));
			setSpinBlocked(driftPersistenceMs_, obs_data_get_int(config, "drift_persistence_ms"));
			setSpinBlocked(cooldownSec_, obs_data_get_int(config, "cooldown_sec"));
			setSpinBlocked(maxAutoResetsPerHour_, obs_data_get_int(config, "max_auto_resets_per_hour"));
			setSpinBlocked(startupGraceSec_, obs_data_get_int(config, "startup_grace_sec"));
			setSpinBlocked(verifyDelaySec_, obs_data_get_int(config, "verify_delay_sec"));
			setSpinBlocked(pulseDurationMs_, obs_data_get_int(config, "pulse_duration_ms"));
			obs_data_release(config);
		}
		loadingConfig_ = false;
	}

	void setComboDataBlocked(QComboBox *combo, int data)
	{
		QSignalBlocker blocker(combo);
		const int index = combo->findData(data);
		if (index >= 0)
			combo->setCurrentIndex(index);
	}

	void loadCheckIfPresent(obs_data_t *config, const char *key, QCheckBox *check)
	{
		if (obs_data_has_user_value(config, key))
			setCheckBlocked(check, obs_data_get_bool(config, key));
	}

	void setCheckBlocked(QCheckBox *check, bool value)
	{
		QSignalBlocker blocker(check);
		check->setChecked(value);
	}

	void setSpinBlocked(QSpinBox *spin, long long value)
	{
		if (value <= 0)
			return;
		QSignalBlocker blocker(spin);
		spin->setValue(static_cast<int>(value));
	}

	void saveConfig() const
	{
		if (!panel_ || loadingConfig_)
			return;
		obs_data_t *data = obs_data_create();
		obs_data_set_string(data, "video_source", sourceCombos_[0]->currentData().toString().toUtf8().constData());
		obs_data_set_string(data, "desktop_source", sourceCombos_[1]->currentData().toString().toUtf8().constData());
		obs_data_set_string(data, "mic_source", sourceCombos_[2]->currentData().toString().toUtf8().constData());
		obs_data_set_int(data, "automation_mode", static_cast<long long>(currentMode()));
		obs_data_set_bool(data, "only_when_output_active", onlyWhenOutputActive_->isChecked());
		obs_data_set_bool(data, "require_active_sources", requireActiveSources_->isChecked());
		obs_data_set_bool(data, "enable_freeze_detection", enableFreezeDetection_->isChecked());
		obs_data_set_bool(data, "enable_drift_detection", enableDriftDetection_->isChecked());
		obs_data_set_bool(data, "auto_escalate", autoEscalate_->isChecked());
		obs_data_set_bool(data, "chapter_markers", chapterMarkers_->isChecked());
		obs_data_set_bool(data, "json_logging", jsonLogging_->isChecked());
		obs_data_set_bool(data, "incident_reports", incidentReports_->isChecked());
		obs_data_set_int(data, "video_stall_ms", videoStallMs_->value());
		obs_data_set_int(data, "audio_stall_ms", audioStallMs_->value());
		obs_data_set_int(data, "drift_threshold_ms", driftThresholdMs_->value());
		obs_data_set_int(data, "drift_persistence_ms", driftPersistenceMs_->value());
		obs_data_set_int(data, "cooldown_sec", cooldownSec_->value());
		obs_data_set_int(data, "max_auto_resets_per_hour", maxAutoResetsPerHour_->value());
		obs_data_set_int(data, "startup_grace_sec", startupGraceSec_->value());
		obs_data_set_int(data, "verify_delay_sec", verifyDelaySec_->value());
		obs_data_set_int(data, "pulse_duration_ms", pulseDurationMs_->value());
		char *path = obs_module_config_path("sync-guardian.json");
		if (path) {
			obs_data_save_json_safe(data, path, ".tmp", ".bak");
			bfree(path);
		}
		obs_data_release(data);
	}

	AutomationMode currentMode() const
	{
		return static_cast<AutomationMode>(automationMode_->currentData().toInt());
	}

	void bindAllSources()
	{
		for (size_t i = 0; i < sourceCombos_.size(); ++i)
			bindSource(i, sourceCombos_[i]->currentData().toString());
	}

	void bindSource(size_t index, const QString &name)
	{
		SourceState &state = states_[index];
		if (state.sourceName == name && state.weak)
			return;
		detachSource(state);
		state.sourceName = name;
		state.wasFresh = false;
		state.hasEverBeenFresh = false;
		state.lastAudioTimestampNs.store(0);
		state.lastAudioWallNs.store(0);
		state.lastVideoTimestampNs = 0;
		state.lastVideoWallNs = 0;
		state.enabled = false;
		state.active = false;
		state.showing = false;
		if (name.isEmpty())
			return;

		obs_source_t *source = obs_get_source_by_name(name.toUtf8().constData());
		if (!source)
			return;
		state.weak = obs_source_get_weak_source(source);
		if (state.monitorAudio)
			obs_source_add_audio_capture_callback(source, audioCapture, &state);
		obs_source_release(source);
	}

	void detachSource(SourceState &state)
	{
		if (!state.weak)
			return;
		obs_source_t *source = obs_weak_source_get_source(state.weak);
		if (source) {
			if (state.monitorAudio)
				obs_source_remove_audio_capture_callback(source, audioCapture, &state);
			obs_source_release(source);
		}
		obs_weak_source_release(state.weak);
		state.weak = nullptr;
	}

	static void audioCapture(void *param, obs_source_t *, const struct audio_data *audio, bool)
	{
		auto *state = static_cast<SourceState *>(param);
		if (!state || !audio)
			return;
		const uint64_t now = os_gettime_ns();
		const uint64_t timestamp = audio->timestamp;
		const uint64_t previousTimestamp = state->lastAudioTimestampNs.exchange(timestamp);
		const uint64_t previousWall = state->lastAudioWallNs.exchange(now);
		if (!previousTimestamp || !previousWall || timestamp == 0)
			return;

		const int64_t timestampDelta = signedDelta(timestamp, previousTimestamp);
		const int64_t wallDelta = signedDelta(now, previousWall);
		const int64_t error = timestampDelta - wallDelta;
		if (std::llabs(error) < static_cast<int64_t>(kJumpThresholdNs))
			return;
		const uint64_t previousJump = state->lastAudioJumpWallNs.load();
		if (previousJump && now - previousJump < kJumpCooldownNs)
			return;
		state->lastAudioJumpWallNs.store(now);
		state->lastAudioJumpErrorNs.store(error);
		state->audioJumpCount.fetch_add(1);
	}

	obs_source_t *sourceForState(const SourceState &state) const
	{
		if (state.weak) {
			obs_source_t *source = obs_weak_source_get_source(state.weak);
			if (source)
				return source;
		}
		if (state.sourceName.isEmpty())
			return nullptr;
		return obs_get_source_by_name(state.sourceName.toUtf8().constData());
	}

	bool pulseReset(SourceState &state)
	{
		if (state.resetInProgress->load())
			return false;
		obs_source_t *source = sourceForState(state);
		if (!source)
			return false;
		const char *id = obs_source_get_unversioned_id(source);
		if (!id || strcmp(id, kDistroAvSourceId) != 0) {
			obs_source_release(source);
			return false;
		}

		auto ticket = std::make_shared<ResetTicket>();
		ticket->original = obs_source_get_settings(source);
		ticket->weak = obs_source_get_weak_source(source);
		ticket->resetFlag = state.resetInProgress;
		state.resetInProgress->store(true);

		obs_data_t *pulse = obs_data_create();
		obs_data_apply(pulse, ticket->original);
		const bool originalFrameSync = obs_data_get_bool(ticket->original, kPropFrameSync);
		obs_data_set_bool(pulse, kPropFrameSync, !originalFrameSync);
		obs_source_update(source, pulse);
		obs_data_release(pulse);
		obs_source_release(source);
		state.resetCount++;

		const int delay = pulseDurationMs_ ? pulseDurationMs_->value() : 180;
		pendingResets_.push_back(ticket);
		QTimer::singleShot(delay, lifetimeContext_, [this, ticket]() {
			ticket->restore();
			pendingResets_.erase(std::remove(pendingResets_.begin(), pendingResets_.end(), ticket),
						 pendingResets_.end());
		});
		return true;
	}

	bool resetTarget(RecoveryTarget target)
	{
		bool resetAny = false;
		switch (target) {
		case RecoveryTarget::Video:
			resetAny = pulseReset(states_[0]);
			break;
		case RecoveryTarget::DesktopAudio:
			resetAny = pulseReset(states_[1]);
			break;
		case RecoveryTarget::Mic:
			resetAny = pulseReset(states_[2]);
			break;
		case RecoveryTarget::BothAudio:
			resetAny = pulseReset(states_[1]) || resetAny;
			resetAny = pulseReset(states_[2]) || resetAny;
			break;
		case RecoveryTarget::EntireGroup:
			for (auto &state : states_)
				resetAny = pulseReset(state) || resetAny;
			break;
		default:
			break;
		}
		return resetAny;
	}

	void manualReset(RecoveryTarget target, const QString &action)
	{
		if (recovery_.active || anyResetInProgress()) {
			appendEvent(action + QStringLiteral(" blocked: another recovery is active"), false);
			return;
		}
		if (!resetTarget(target)) {
			appendEvent(action + QStringLiteral(" failed: no selected DistroAV source"), false);
			return;
		}
		const uint64_t now = os_gettime_ns();
		detectionSuppressedUntilNs_ = now + static_cast<uint64_t>(verifyDelaySec_->value()) * kNsPerSecond;
		appendEvent(action);
	}

	bool anyResetInProgress() const
	{
		for (const auto &state : states_) {
			if (state.resetInProgress->load())
				return true;
		}
		return false;
	}

	void captureSnapshotState()
	{
		if (anyResetInProgress() || recovery_.active) {
			appendEvent(QStringLiteral("Capture Known-Good State blocked: recovery is active"), false);
			return;
		}
		int captured = 0;
		for (auto &state : states_) {
			if (state.snapshot) {
				obs_data_release(state.snapshot);
				state.snapshot = nullptr;
			}
			obs_source_t *source = sourceForState(state);
			if (!source)
				continue;
			state.snapshot = obs_source_get_settings(source);
			obs_source_release(source);
			captured++;
		}
		if (std::isfinite(filteredOffsetMs_))
			baselineOffsetMs_ = filteredOffsetMs_;
		appendEvent(QStringLiteral("Captured known-good state for %1 source(s)").arg(captured));
	}

	void restoreSnapshotState()
	{
		if (restoringSnapshot_)
			return;
		if (anyResetInProgress() || recovery_.active) {
			appendEvent(QStringLiteral("Restore Last Known-Good State blocked: recovery is active"), false);
			return;
		}
		restoringSnapshot_ = true;
		int restored = 0;
		for (auto &state : states_) {
			if (!state.snapshot)
				continue;
			obs_source_t *source = sourceForState(state);
			if (!source)
				continue;
			obs_source_update(source, state.snapshot);
			obs_source_release(source);
			restored++;
		}
		if (restored == 0) {
			appendEvent(QStringLiteral("Restore Last Known-Good State failed: no snapshot"), false);
			restoringSnapshot_ = false;
			return;
		}
		appendEvent(QStringLiteral("Restored last known-good settings for %1 source(s)").arg(restored));
		QTimer::singleShot(100, lifetimeContext_, [this]() {
			manualReset(RecoveryTarget::EntireGroup, QStringLiteral("Rebuilt sync group after snapshot restore"));
			restoringSnapshot_ = false;
		});
	}

	void clearCalibration(const QString &reason, bool log)
	{
		baselineOffsetMs_ = std::numeric_limits<double>::quiet_NaN();
		filteredOffsetMs_ = std::numeric_limits<double>::quiet_NaN();
		driftRateMsPerMinute_ = std::numeric_limits<double>::quiet_NaN();
		offsetSamples_.clear();
		driftSinceNs_ = 0;
		monitoringGraceUntilNs_ = os_gettime_ns() + static_cast<uint64_t>(startupGraceSec_->value()) * kNsPerSecond;
		if (log)
			appendEvent(QStringLiteral("A/V auto calibration restarted: %1").arg(reason), false);
	}

	void refreshDiagnostics()
	{
		const uint64_t now = os_gettime_ns();
		for (size_t i = 0; i < states_.size(); ++i)
			refreshSourceRow(i, now);
		logNewTimestampJumps();

		const double rawOffset = calculateAvOffsetMs();
		updateOffsetFilter(now, rawOffset);
		updateCalibration(now);
		const double micOffset = calculateMicOffsetMs();
		const double drift = currentDriftMs();

		if (std::isfinite(filteredOffsetMs_))
			avOffsetLabel_->setText(QStringLiteral("Video − Desktop Audio timestamp: %1 ms filtered (%2 ms raw)")
							.arg(filteredOffsetMs_, 0, 'f', 2)
							.arg(rawOffset, 0, 'f', 2));
		else
			avOffsetLabel_->setText(QStringLiteral("Video − Desktop Audio timestamp: —"));

		if (std::isfinite(drift)) {
			driftLabel_->setText(QStringLiteral("Drift from baseline: %1 ms | rate %2 ms/min | baseline %3 ms")
						    .arg(drift, 0, 'f', 2)
						    .arg(driftRateMsPerMinute_, 0, 'f', 2)
						    .arg(baselineOffsetMs_, 0, 'f', 2));
		} else {
			driftLabel_->setText(QStringLiteral("Drift from baseline: calibrating — %1 seconds of stable data required")
						    .arg(static_cast<int>(kBaselineWindowNs / kNsPerSecond)));
		}
		micOffsetLabel_->setText(std::isfinite(micOffset)
						 ? QStringLiteral("Mic − Desktop Audio timestamp: %1 ms").arg(micOffset, 0, 'f', 2)
						 : QStringLiteral("Mic − Desktop Audio timestamp: —"));

		updateObsStats();
		evaluateAutomation(now);
		addDiagnosticSample(now, rawOffset);
		updateIncidentCapture(now);
	}

	void refreshSourceRow(size_t index, uint64_t now)
	{
		SourceState &state = states_[index];
		obs_source_t *source = sourceForState(state);
		QString stateText = QStringLiteral("Missing");
		QString packetAgeText = QStringLiteral("—");
		QString settingsText = QStringLiteral("—");
		bool fresh = false;

		if (source) {
			if (index == 0)
				pollVideoFrame(state, source, now);

			state.active = obs_source_active(source);
			state.showing = obs_source_showing(source);
			state.enabled = obs_source_enabled(source);
			stateText = QStringLiteral("%1 / %2 / %3%4")
					    .arg(state.enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled"),
						 state.active ? QStringLiteral("Active") : QStringLiteral("Inactive"),
						 state.showing ? QStringLiteral("Showing") : QStringLiteral("Hidden"),
						 state.resetInProgress->load() ? QStringLiteral(" / Resetting") : QString());

			const uint64_t packetWall = packetWallNs(index);
			if (packetWall && now >= packetWall) {
				const double ageMs = static_cast<double>(now - packetWall) / static_cast<double>(kNsPerMs);
				packetAgeText = QStringLiteral("%1 ms").arg(ageMs, 0, 'f', 1);
				const int threshold = index == 0 ? videoStallMs_->value() : audioStallMs_->value();
				fresh = ageMs < static_cast<double>(threshold);
			}

			obs_data_t *settings = obs_source_get_settings(source);
			const QString ndiTarget = QString::fromUtf8(obs_data_get_string(settings, kPropSource));
			const long long latency = obs_data_get_int(settings, kPropLatency);
			const bool frameSync = obs_data_get_bool(settings, kPropFrameSync);
			const long long syncMode = obs_data_get_int(settings, kPropSync);
			settingsText = QStringLiteral("%1 | %2 | FrameSync %3 | %4")
					       .arg(ndiTarget.isEmpty() ? QStringLiteral("No NDI target") : ndiTarget,
						    latencyName(latency), frameSync ? QStringLiteral("On") : QStringLiteral("Off"),
						    syncModeName(syncMode));
			obs_data_release(settings);
			obs_source_release(source);
		} else {
			state.active = false;
			state.showing = false;
			state.enabled = false;
		}

		if (fresh && !state.wasFresh && state.hasEverBeenFresh)
			state.recoveryCount++;
		if (fresh)
			state.hasEverBeenFresh = true;
		state.wasFresh = fresh;

		const uint64_t jumps = index == 0 ? state.videoJumpCount : state.audioJumpCount.load();
		setCell(index, 0, state.role);
		setCell(index, 1, state.sourceName.isEmpty() ? QStringLiteral("(Not selected)") : state.sourceName);
		setCell(index, 2, stateText);
		setCell(index, 3, packetAgeText);
		setCell(index, 4, QString::number(static_cast<unsigned long long>(jumps)));
		setCell(index, 5, QString::number(static_cast<unsigned long long>(state.resetCount)));
		setCell(index, 6, QString::number(static_cast<unsigned long long>(state.recoveryCount)));
		setCell(index, 7, settingsText);
	}

	void pollVideoFrame(SourceState &state, obs_source_t *source, uint64_t now)
	{
		obs_source_frame *frame = obs_source_get_frame(source);
		if (!frame)
			return;
		const uint64_t timestamp = frame->timestamp;
		obs_source_release_frame(source, frame);
		if (!timestamp || timestamp == state.lastVideoTimestampNs)
			return;

		if (state.lastVideoTimestampNs && state.lastVideoWallNs) {
			const int64_t timestampDelta = signedDelta(timestamp, state.lastVideoTimestampNs);
			const int64_t wallDelta = signedDelta(now, state.lastVideoWallNs);
			const int64_t error = timestampDelta - wallDelta;
			if (std::llabs(error) >= static_cast<int64_t>(kJumpThresholdNs) &&
			    (!state.lastVideoJumpWallNs || now - state.lastVideoJumpWallNs >= kJumpCooldownNs)) {
				state.lastVideoJumpWallNs = now;
				state.lastVideoJumpErrorNs = error;
				state.videoJumpCount++;
			}
		}
		state.lastVideoTimestampNs = timestamp;
		state.lastVideoWallNs = now;
	}

	uint64_t packetWallNs(size_t index) const
	{
		return index == 0 ? states_[0].lastVideoWallNs : states_[index].lastAudioWallNs.load();
	}

	double packetAgeMs(size_t index, uint64_t now) const
	{
		const uint64_t wall = packetWallNs(index);
		if (!wall || now < wall)
			return std::numeric_limits<double>::infinity();
		return static_cast<double>(now - wall) / static_cast<double>(kNsPerMs);
	}

	double calculateAvOffsetMs() const
	{
		const uint64_t videoTs = states_[0].lastVideoTimestampNs;
		const uint64_t videoWall = states_[0].lastVideoWallNs;
		const uint64_t audioTs = states_[1].lastAudioTimestampNs.load();
		const uint64_t audioWall = states_[1].lastAudioWallNs.load();
		if (!videoTs || !videoWall || !audioTs || !audioWall)
			return std::numeric_limits<double>::quiet_NaN();
		const uint64_t now = os_gettime_ns();
		const int64_t videoProjected = static_cast<int64_t>(videoTs) + signedDelta(now, videoWall);
		const int64_t audioProjected = static_cast<int64_t>(audioTs) + signedDelta(now, audioWall);
		return nsToMs(videoProjected - audioProjected);
	}

	double calculateMicOffsetMs() const
	{
		const uint64_t desktopTs = states_[1].lastAudioTimestampNs.load();
		const uint64_t desktopWall = states_[1].lastAudioWallNs.load();
		const uint64_t micTs = states_[2].lastAudioTimestampNs.load();
		const uint64_t micWall = states_[2].lastAudioWallNs.load();
		if (!desktopTs || !desktopWall || !micTs || !micWall)
			return std::numeric_limits<double>::quiet_NaN();
		const uint64_t now = os_gettime_ns();
		const int64_t desktopProjected = static_cast<int64_t>(desktopTs) + signedDelta(now, desktopWall);
		const int64_t micProjected = static_cast<int64_t>(micTs) + signedDelta(now, micWall);
		return nsToMs(micProjected - desktopProjected);
	}

	void updateOffsetFilter(uint64_t now, double rawOffset)
	{
		const bool fresh = packetAgeMs(0, now) < videoStallMs_->value() &&
				   packetAgeMs(1, now) < audioStallMs_->value();
		if (std::isfinite(rawOffset) && fresh && !anyResetInProgress())
			offsetSamples_.push_back({now, rawOffset});
		while (!offsetSamples_.empty() && now - offsetSamples_.front().wallNs > kBaselineWindowNs)
			offsetSamples_.pop_front();

		std::vector<double> recent;
		for (const auto &sample : offsetSamples_) {
			if (now - sample.wallNs <= kOffsetWindowNs)
				recent.push_back(sample.valueMs);
		}
		filteredOffsetMs_ = median(std::move(recent));

		std::vector<double> early;
		std::vector<double> late;
		for (const auto &sample : offsetSamples_) {
			if (now - sample.wallNs > 15ULL * kNsPerSecond)
				early.push_back(sample.valueMs);
			else if (now - sample.wallNs <= 5ULL * kNsPerSecond)
				late.push_back(sample.valueMs);
		}
		const double earlyMedian = median(std::move(early));
		const double lateMedian = median(std::move(late));
		if (std::isfinite(earlyMedian) && std::isfinite(lateMedian))
			driftRateMsPerMinute_ = (lateMedian - earlyMedian) * 4.0;
		else
			driftRateMsPerMinute_ = std::numeric_limits<double>::quiet_NaN();
	}

	void updateCalibration(uint64_t now)
	{
		if (std::isfinite(baselineOffsetMs_) || now < monitoringGraceUntilNs_ || offsetSamples_.empty())
			return;
		if (now - offsetSamples_.front().wallNs < kBaselineWindowNs - static_cast<uint64_t>(kRefreshIntervalMs) * kNsPerMs)
			return;

		std::vector<double> values;
		values.reserve(offsetSamples_.size());
		for (const auto &sample : offsetSamples_)
			values.push_back(sample.valueMs);
		if (values.size() < 60)
			return;
		const auto minmax = std::minmax_element(values.begin(), values.end());
		const double range = *minmax.second - *minmax.first;
		const double allowedRange = std::max(25.0, static_cast<double>(driftThresholdMs_->value()) * 0.25);
		if (range > allowedRange)
			return;
		baselineOffsetMs_ = median(std::move(values));
		appendEvent(QStringLiteral("Automatic A/V baseline calibrated at %1 ms after 30 stable seconds")
				    .arg(baselineOffsetMs_, 0, 'f', 2),
			    false);
	}

	double currentDriftMs() const
	{
		if (!std::isfinite(baselineOffsetMs_) || !std::isfinite(filteredOffsetMs_))
			return std::numeric_limits<double>::quiet_NaN();
		return filteredOffsetMs_ - baselineOffsetMs_;
	}

	void updateObsStats()
	{
		const uint32_t totalFrames = obs_get_total_frames();
		const uint32_t laggedFrames = obs_get_lagged_frames();
		const double lagPercent = totalFrames ? (100.0 * laggedFrames / totalFrames) : 0.0;
		QString outputStats = QStringLiteral("stream output inactive");
		obs_output_t *streamOutput = obs_frontend_get_streaming_output();
		if (streamOutput) {
			if (obs_output_active(streamOutput)) {
				const int total = obs_output_get_total_frames(streamOutput);
				const int dropped = obs_output_get_frames_dropped(streamOutput);
				const double droppedPercent = total ? (100.0 * dropped / total) : 0.0;
				outputStats = QStringLiteral("stream dropped %1/%2 (%3%)")
						      .arg(dropped)
						      .arg(total)
						      .arg(droppedPercent, 0, 'f', 3);
			}
			obs_output_release(streamOutput);
		}
		obsStatsLabel_->setText(QStringLiteral("OBS: %1 FPS | render lag %2/%3 (%4%) | %5")
						.arg(obs_get_active_fps(), 0, 'f', 2)
						.arg(laggedFrames)
						.arg(totalFrames)
						.arg(lagPercent, 0, 'f', 4)
						.arg(outputStats));
	}

	bool outputActive() const
	{
		return obs_frontend_streaming_active() || obs_frontend_recording_active();
	}

	bool sourceEligibilitySatisfied() const
	{
		if (!requireActiveSources_->isChecked())
			return true;
		return states_[0].enabled && states_[0].active && states_[0].showing &&
		       states_[1].enabled && states_[1].active;
	}

	void evaluateAutomation(uint64_t now)
	{
		if (recovery_.active) {
			if (now >= recovery_.verifyAtNs)
				verifyRecovery(now);
			else
				automationStatusLabel_->setText(QStringLiteral("Automation: verifying recovery in %1 sec")
								.arg(static_cast<unsigned long long>((recovery_.verifyAtNs - now) / kNsPerSecond)));
			return;
		}

		if (anyResetInProgress()) {
			automationStatusLabel_->setText(QStringLiteral("Automation: reset pulse active"));
			return;
		}
		if (now < monitoringGraceUntilNs_) {
			automationStatusLabel_->setText(QStringLiteral("Automation: startup/calibration grace (%1 sec remaining)")
							.arg(static_cast<unsigned long long>((monitoringGraceUntilNs_ - now) / kNsPerSecond)));
			currentConfidence_ = 0;
			healthLabel_->setText(QStringLiteral("Detection confidence: 0/100"));
			return;
		}
		if (now < detectionSuppressedUntilNs_) {
			automationStatusLabel_->setText(QStringLiteral("Automation: temporarily suppressed (%1 sec remaining)")
							.arg(static_cast<unsigned long long>((detectionSuppressedUntilNs_ - now) / kNsPerSecond)));
			return;
		}
		if (onlyWhenOutputActive_->isChecked() && !outputActive()) {
			automationStatusLabel_->setText(QStringLiteral("Automation: waiting for streaming or recording"));
			resetConditionTimers();
			currentConfidence_ = 0;
			healthLabel_->setText(QStringLiteral("Detection confidence: 0/100"));
			return;
		}
		if (!sourceEligibilitySatisfied()) {
			automationStatusLabel_->setText(QStringLiteral("Automation: waiting for mapped sources to become active/showing"));
			resetConditionTimers();
			currentConfidence_ = 0;
			healthLabel_->setText(QStringLiteral("Detection confidence: 0/100"));
			return;
		}

		const double videoAge = packetAgeMs(0, now);
		const double desktopAge = packetAgeMs(1, now);
		const double micAge = packetAgeMs(2, now);
		const bool videoFresh = videoAge < videoStallMs_->value();
		const bool desktopFresh = desktopAge < audioStallMs_->value();
		const bool micExpected = !states_[2].sourceName.isEmpty() && states_[2].enabled && states_[2].active;

		const bool videoStalled = enableFreezeDetection_->isChecked() && desktopFresh &&
					  videoAge >= videoStallMs_->value();
		const bool desktopStalled = enableFreezeDetection_->isChecked() && videoFresh &&
					    desktopAge >= audioStallMs_->value();
		const bool micStalled = enableFreezeDetection_->isChecked() && micExpected && (videoFresh || desktopFresh) &&
					micAge >= audioStallMs_->value();
		updateConditionTimer(videoStalled, videoStallSinceNs_, now);
		updateConditionTimer(desktopStalled, desktopStallSinceNs_, now);
		updateConditionTimer(micStalled, micStallSinceNs_, now);

		const double drift = currentDriftMs();
		const bool driftExceeded = enableDriftDetection_->isChecked() && std::isfinite(drift) &&
					   std::abs(drift) >= driftThresholdMs_->value() && videoFresh && desktopFresh;
		updateConditionTimer(driftExceeded, driftSinceNs_, now);

		IssueKind issue = IssueKind::None;
		RecoveryTarget target = RecoveryTarget::None;
		QString reason;
		int confidence = 0;

		if (videoStallSinceNs_ && now - videoStallSinceNs_ >= kStallConfirmNs) {
			issue = IssueKind::VideoStall;
			target = RecoveryTarget::Video;
			confidence = 95;
			reason = QStringLiteral("NDI video timestamp has not advanced for %1 ms while desktop audio remains fresh")
					 .arg(videoAge, 0, 'f', 0);
		} else if (desktopStallSinceNs_ && now - desktopStallSinceNs_ >= kStallConfirmNs) {
			issue = IssueKind::DesktopAudioStall;
			target = RecoveryTarget::DesktopAudio;
			confidence = 95;
			reason = QStringLiteral("NDI desktop audio has not delivered a block for %1 ms while video remains fresh")
					 .arg(desktopAge, 0, 'f', 0);
		} else if (micStallSinceNs_ && now - micStallSinceNs_ >= kStallConfirmNs) {
			issue = IssueKind::MicStall;
			target = RecoveryTarget::Mic;
			confidence = 90;
			reason = QStringLiteral("NDI microphone has not delivered a block for %1 ms while companion streams remain fresh")
					 .arg(micAge, 0, 'f', 0);
		} else if (driftSinceNs_ && now - driftSinceNs_ >= static_cast<uint64_t>(driftPersistenceMs_->value()) * kNsPerMs) {
			issue = IssueKind::PersistentDrift;
			target = drift < 0.0 ? RecoveryTarget::Video : RecoveryTarget::DesktopAudio;
			confidence = 75;
			reason = QStringLiteral("Filtered A/V offset moved %1 ms from baseline for %2 ms; likely %3 lag")
					 .arg(drift, 0, 'f', 1)
					 .arg(driftPersistenceMs_->value())
					 .arg(drift < 0.0 ? QStringLiteral("video") : QStringLiteral("desktop-audio"));
		}

		if (issue != IssueKind::None) {
			if (recentJumpEvidence(now))
				confidence = std::min(100, confidence + 15);
			if (std::isfinite(driftRateMsPerMinute_) && std::isfinite(drift) &&
			    std::abs(driftRateMsPerMinute_) >= 30.0 && driftRateMsPerMinute_ * drift > 0.0)
				confidence = std::min(100, confidence + 10);
		}
		currentConfidence_ = confidence;
		healthLabel_->setText(QStringLiteral("Detection confidence: %1/100%2")
					  .arg(confidence)
					  .arg(recentJumpEvidence(now) ? QStringLiteral(" | recent timestamp jump") : QString()));

		if (issue == IssueKind::None) {
			automationStatusLabel_->setText(QStringLiteral("Automation: %1 | healthy")
							.arg(modeName(currentMode())));
			lastObservedIssueKey_.clear();
			return;
		}

		automationStatusLabel_->setText(QStringLiteral("Automation: %1 | issue detected: %2")
						.arg(modeName(currentMode()), reason));
		handleDetectedIssue(now, issue, target, reason, confidence);
	}

	void updateConditionTimer(bool condition, uint64_t &timer, uint64_t now)
	{
		if (condition) {
			if (!timer)
				timer = now;
		} else {
			timer = 0;
		}
	}

	void resetConditionTimers()
	{
		videoStallSinceNs_ = 0;
		desktopStallSinceNs_ = 0;
		micStallSinceNs_ = 0;
		driftSinceNs_ = 0;
	}

	bool recentJumpEvidence(uint64_t now) const
	{
		const uint64_t videoJump = states_[0].lastVideoJumpWallNs;
		const uint64_t desktopJump = states_[1].lastAudioJumpWallNs.load();
		return (videoJump && now - videoJump <= kJumpEvidenceWindowNs) ||
		       (desktopJump && now - desktopJump <= kJumpEvidenceWindowNs);
	}

	void handleDetectedIssue(uint64_t now, IssueKind issue, RecoveryTarget target, const QString &reason, int confidence)
	{
		const QString key = QStringLiteral("%1:%2").arg(static_cast<int>(issue)).arg(static_cast<int>(target));
		startIncident(reason, target, confidence);

		if (currentMode() == AutomationMode::Observe) {
			if (key != lastObservedIssueKey_ || !lastObservedIssueNs_ || now - lastObservedIssueNs_ >= kObserveRepeatNs) {
				appendEvent(QStringLiteral("Observe-only detection (%1/100): %2").arg(confidence).arg(reason), false);
				lastObservedIssueKey_ = key;
				lastObservedIssueNs_ = now;
			}
			return;
		}

		QString blockedReason;
		if (!automaticResetAllowed(now, blockedReason)) {
			if (key != lastObservedIssueKey_ || !lastObservedIssueNs_ || now - lastObservedIssueNs_ >= kObserveRepeatNs) {
				appendEvent(QStringLiteral("Automatic recovery blocked: %1. Detected: %2")
						    .arg(blockedReason, reason),
					    false);
				lastObservedIssueKey_ = key;
				lastObservedIssueNs_ = now;
			}
			return;
		}

		if (currentMode() == AutomationMode::Ask) {
			if (promptActive_)
				return;
			promptActive_ = true;
			const QMessageBox::StandardButton answer = QMessageBox::question(
				panel_, QStringLiteral("Sync Guardian detected an NDI sync problem"),
				QStringLiteral("%1\n\nConfidence: %2/100\nSuggested action: reset %3.\n\nPerform the reset now?")
					.arg(reason)
					.arg(confidence)
					.arg(targetName(target)),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
			promptActive_ = false;
			if (answer != QMessageBox::Yes) {
				detectionSuppressedUntilNs_ = now + 60ULL * kNsPerSecond;
				appendEvent(QStringLiteral("User declined suggested reset of %1: %2").arg(targetName(target), reason), false);
				return;
			}
		}

		beginAutomatedRecovery(os_gettime_ns(), issue, target, reason);
	}

	bool automaticResetAllowed(uint64_t now, QString &blockedReason)
	{
		while (!automatedResetTimes_.empty() && now - automatedResetTimes_.front() > kResetLimitWindowNs)
			automatedResetTimes_.pop_front();
		if (automatedResetTimes_.size() >= static_cast<size_t>(maxAutoResetsPerHour_->value())) {
			blockedReason = QStringLiteral("maximum of %1 automatic resets per hour reached")
						.arg(maxAutoResetsPerHour_->value());
			return false;
		}
		const uint64_t cooldownNs = static_cast<uint64_t>(cooldownSec_->value()) * kNsPerSecond;
		if (lastAutomatedResetNs_ && now - lastAutomatedResetNs_ < cooldownNs) {
			blockedReason = QStringLiteral("%1-second reset cooldown is active")
						.arg(cooldownSec_->value());
			return false;
		}
		return true;
	}

	void beginAutomatedRecovery(uint64_t now, IssueKind issue, RecoveryTarget target, const QString &reason)
	{
		if (recovery_.active || anyResetInProgress())
			return;
		if (!resetTarget(target)) {
			appendEvent(QStringLiteral("Automated reset failed to start for %1: mapped source unavailable")
					    .arg(targetName(target)),
				    false);
			return;
		}

		automatedResetTimes_.push_back(now);
		lastAutomatedResetNs_ = now;
		recovery_.active = true;
		recovery_.automated = true;
		recovery_.escalationUsed = false;
		recovery_.target = target;
		recovery_.issue = issue;
		recovery_.reason = reason;
		recovery_.preDriftMs = currentDriftMs();
		recovery_.verifyAtNs = now + static_cast<uint64_t>(pulseDurationMs_->value()) * kNsPerMs +
				       static_cast<uint64_t>(verifyDelaySec_->value()) * kNsPerSecond;
		detectionSuppressedUntilNs_ = recovery_.verifyAtNs;
		resetConditionTimers();
		appendEvent(QStringLiteral("Automated recovery started: reset %1 because %2")
				    .arg(targetName(target), reason));
	}

	void verifyRecovery(uint64_t now)
	{
		const bool recovered = recoverySucceeded(now);
		if (recovered) {
			appendEvent(QStringLiteral("Recovery verified: %1 is fresh and the triggering condition cleared")
					    .arg(targetName(recovery_.target)));
			recovery_ = RecoveryAttempt{};
			detectionSuppressedUntilNs_ = now + 2ULL * kNsPerSecond;
			return;
		}

		while (!automatedResetTimes_.empty() && now - automatedResetTimes_.front() > kResetLimitWindowNs)
			automatedResetTimes_.pop_front();
		const bool escalationWithinLimit = automatedResetTimes_.size() < static_cast<size_t>(maxAutoResetsPerHour_->value());
		if (autoEscalate_->isChecked() && escalationWithinLimit && !recovery_.escalationUsed &&
		    recovery_.target != RecoveryTarget::EntireGroup) {
			if (resetTarget(RecoveryTarget::EntireGroup)) {
				automatedResetTimes_.push_back(now);
				lastAutomatedResetNs_ = now;
				recovery_.escalationUsed = true;
				recovery_.target = RecoveryTarget::EntireGroup;
				recovery_.verifyAtNs = now + static_cast<uint64_t>(pulseDurationMs_->value()) * kNsPerMs +
						       static_cast<uint64_t>(verifyDelaySec_->value()) * kNsPerSecond;
				detectionSuppressedUntilNs_ = recovery_.verifyAtNs;
				appendEvent(QStringLiteral("Initial recovery did not verify; escalated to full NDI sync-group rebuild"));
				return;
			}
		}

		appendEvent(QStringLiteral("Recovery failed verification after %1. Automatic actions are now in cooldown; manual review required")
				    .arg(targetName(recovery_.target)));
		recovery_ = RecoveryAttempt{};
		detectionSuppressedUntilNs_ = now + static_cast<uint64_t>(cooldownSec_->value()) * kNsPerSecond;
	}

	bool recoverySucceeded(uint64_t now) const
	{
		const bool videoFresh = packetAgeMs(0, now) < videoStallMs_->value() * 0.75;
		const bool desktopFresh = packetAgeMs(1, now) < audioStallMs_->value() * 0.75;
		const bool micFresh = packetAgeMs(2, now) < audioStallMs_->value() * 0.75;
		switch (recovery_.issue) {
		case IssueKind::VideoStall:
			return videoFresh;
		case IssueKind::DesktopAudioStall:
			return desktopFresh;
		case IssueKind::MicStall:
			return micFresh;
		case IssueKind::PersistentDrift: {
			const double drift = currentDriftMs();
			if (!videoFresh || !desktopFresh || !std::isfinite(drift))
				return false;
			const bool belowThreshold = std::abs(drift) < driftThresholdMs_->value() * 0.75;
			const bool materiallyImproved = std::isfinite(recovery_.preDriftMs) &&
						      std::abs(drift) < std::abs(recovery_.preDriftMs) * 0.5;
			return belowThreshold || materiallyImproved;
		}
		default:
			return videoFresh && desktopFresh;
		}
	}

	void addDiagnosticSample(uint64_t now, double rawOffset)
	{
		DiagnosticSample sample;
		sample.wallNs = now;
		sample.time = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
		sample.rawOffsetMs = rawOffset;
		sample.filteredOffsetMs = filteredOffsetMs_;
		sample.driftMs = currentDriftMs();
		sample.driftRateMsPerMinute = driftRateMsPerMinute_;
		sample.videoAgeMs = packetAgeMs(0, now);
		sample.desktopAgeMs = packetAgeMs(1, now);
		sample.micAgeMs = packetAgeMs(2, now);
		sample.confidence = currentConfidence_;
		diagnosticHistory_.push_back(sample);
		while (!diagnosticHistory_.empty() && now - diagnosticHistory_.front().wallNs > kDiagnosticHistoryNs)
			diagnosticHistory_.pop_front();
		if (incident_.active)
			incident_.samples.append(diagnosticSampleJson(sample));
	}

	QJsonObject diagnosticSampleJson(const DiagnosticSample &sample) const
	{
		QJsonObject object;
		object.insert(QStringLiteral("time"), sample.time);
		object.insert(QStringLiteral("monotonic_ns"), static_cast<double>(sample.wallNs));
		insertFinite(object, QStringLiteral("raw_av_offset_ms"), sample.rawOffsetMs);
		insertFinite(object, QStringLiteral("filtered_av_offset_ms"), sample.filteredOffsetMs);
		insertFinite(object, QStringLiteral("drift_from_baseline_ms"), sample.driftMs);
		insertFinite(object, QStringLiteral("drift_rate_ms_per_minute"), sample.driftRateMsPerMinute);
		insertFinite(object, QStringLiteral("video_packet_age_ms"), sample.videoAgeMs);
		insertFinite(object, QStringLiteral("desktop_packet_age_ms"), sample.desktopAgeMs);
		insertFinite(object, QStringLiteral("mic_packet_age_ms"), sample.micAgeMs);
		object.insert(QStringLiteral("confidence"), sample.confidence);
		return object;
	}

	static void insertFinite(QJsonObject &object, const QString &key, double value)
	{
		if (std::isfinite(value))
			object.insert(key, value);
	}

	void startIncident(const QString &reason, RecoveryTarget target, int confidence)
	{
		const uint64_t monotonicNow = os_gettime_ns();
		if (!incidentReports_->isChecked() || incident_.active ||
		    (lastIncidentStartNs_ && monotonicNow - lastIncidentStartNs_ < kIncidentCooldownNs))
			return;
		lastIncidentStartNs_ = monotonicNow;
		const QDateTime nowDate = QDateTime::currentDateTime();
		const QString fileName = QStringLiteral("sync-guardian-incident-%1.json")
					 .arg(nowDate.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
		char *basePath = obs_module_config_path("incidents");
		if (!basePath)
			return;
		QDir directory(QString::fromUtf8(basePath));
		bfree(basePath);
		if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
			return;

		incident_.active = true;
		incident_.path = directory.filePath(fileName);
		incident_.finalizeAtNs = monotonicNow + kIncidentPostNs;
		incident_.root = QJsonObject{};
		incident_.samples = QJsonArray{};
		incident_.root.insert(QStringLiteral("plugin_version"), QStringLiteral(PLUGIN_VERSION));
		incident_.root.insert(QStringLiteral("detected_at"), nowDate.toString(Qt::ISODateWithMs));
		incident_.root.insert(QStringLiteral("reason"), reason);
		incident_.root.insert(QStringLiteral("suggested_target"), targetName(target));
		incident_.root.insert(QStringLiteral("confidence"), confidence);
		incident_.root.insert(QStringLiteral("mode"), modeName(currentMode()));
		insertFinite(incident_.root, QStringLiteral("baseline_offset_ms"), baselineOffsetMs_);
		for (const auto &sample : diagnosticHistory_) {
			if (monotonicNow - sample.wallNs <= kIncidentPreNs)
				incident_.samples.append(diagnosticSampleJson(sample));
		}
		appendEvent(QStringLiteral("Incident capture started: 30 seconds of post-event diagnostics will be written to %1")
				    .arg(fileName),
			    false);
	}

	void updateIncidentCapture(uint64_t now)
	{
		if (incident_.active && now >= incident_.finalizeAtNs)
			finalizeIncident(false);
	}

	void finalizeIncident(bool interrupted)
	{
		if (!incident_.active)
			return;
		incident_.root.insert(QStringLiteral("finalized_at"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
		incident_.root.insert(QStringLiteral("interrupted_by_plugin_unload"), interrupted);
		incident_.root.insert(QStringLiteral("samples"), incident_.samples);
		QFile file(incident_.path);
		if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			file.write(QJsonDocument(incident_.root).toJson(QJsonDocument::Indented));
		incident_ = IncidentCapture{};
	}

	void logNewTimestampJumps()
	{
		for (size_t index = 0; index < states_.size(); ++index) {
			const uint64_t count = index == 0 ? states_[index].videoJumpCount : states_[index].audioJumpCount.load();
			if (count <= loggedJumpCounts_[index])
				continue;
			const int64_t errorNs = index == 0 ? states_[index].lastVideoJumpErrorNs
						       : states_[index].lastAudioJumpErrorNs.load();
			const uint64_t newEvents = count - loggedJumpCounts_[index];
			loggedJumpCounts_[index] = count;
			appendEvent(QStringLiteral("%1 timestamp discontinuity detected: %2 ms timeline error%3")
					    .arg(states_[index].role)
					    .arg(nsToMs(errorNs), 0, 'f', 2)
					    .arg(newEvents > 1 ? QStringLiteral(" (multiple callbacks since last poll)") : QString()),
				    false);
		}
	}

	void setCell(size_t row, int column, const QString &text)
	{
		if (QTableWidgetItem *item = statusTable_->item(static_cast<int>(row), column))
			item->setText(text);
	}

	void appendEvent(const QString &event, bool addChapter = true)
	{
		const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
		const QString line = QStringLiteral("[%1] %2").arg(timestamp, event);
		if (eventLog_)
			eventLog_->append(line.toHtmlEscaped());
		blog(LOG_INFO, "[sync-guardian] %s", event.toUtf8().constData());

		if (addChapter && chapterMarkers_ && chapterMarkers_->isChecked()) {
			const QByteArray chapter = QStringLiteral("Sync Guardian - %1").arg(event).toUtf8();
			obs_frontend_recording_add_chapter(chapter.constData());
		}

		if (jsonLogging_ && jsonLogging_->isChecked()) {
			QJsonObject object;
			object.insert(QStringLiteral("time"), timestamp);
			object.insert(QStringLiteral("event"), event);
			object.insert(QStringLiteral("mode"), modeName(currentMode()));
			insertFinite(object, QStringLiteral("raw_video_minus_desktop_ms"), calculateAvOffsetMs());
			insertFinite(object, QStringLiteral("filtered_video_minus_desktop_ms"), filteredOffsetMs_);
			insertFinite(object, QStringLiteral("baseline_ms"), baselineOffsetMs_);
			insertFinite(object, QStringLiteral("drift_ms"), currentDriftMs());
			insertFinite(object, QStringLiteral("drift_rate_ms_per_minute"), driftRateMsPerMinute_);
			insertFinite(object, QStringLiteral("mic_minus_desktop_ms"), calculateMicOffsetMs());
			object.insert(QStringLiteral("confidence"), currentConfidence_);
			object.insert(QStringLiteral("video_resets"), static_cast<double>(states_[0].resetCount));
			object.insert(QStringLiteral("desktop_resets"), static_cast<double>(states_[1].resetCount));
			object.insert(QStringLiteral("mic_resets"), static_cast<double>(states_[2].resetCount));
			char *path = obs_module_config_path("sync-guardian-events.jsonl");
			if (path) {
				QFile file(QString::fromUtf8(path));
				if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
					file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
					file.write("\n");
				}
				bfree(path);
			}
		}
	}

	void registerHotkeys()
	{
		hotkeys_[0] = obs_hotkey_register_frontend("sync_guardian.reset_video", "Sync Guardian: Reset Video Only",
						      hotkeyCallback, this);
		hotkeys_[1] = obs_hotkey_register_frontend("sync_guardian.reset_desktop", "Sync Guardian: Reset Desktop Audio Only",
						      hotkeyCallback, this);
		hotkeys_[2] = obs_hotkey_register_frontend("sync_guardian.reset_mic", "Sync Guardian: Reset Mic Only",
						      hotkeyCallback, this);
		hotkeys_[3] = obs_hotkey_register_frontend("sync_guardian.reset_audio", "Sync Guardian: Reset Both Audio Sources",
						      hotkeyCallback, this);
		hotkeys_[4] = obs_hotkey_register_frontend("sync_guardian.rebuild_group", "Sync Guardian: Rebuild Entire Sync Group",
						      hotkeyCallback, this);
		hotkeys_[5] = obs_hotkey_register_frontend("sync_guardian.restore_snapshot", "Sync Guardian: Restore Last Known-Good State",
						      hotkeyCallback, this);
		hotkeys_[6] = obs_hotkey_register_frontend("sync_guardian.mark_event", "Sync Guardian: Mark Sync Event",
						      hotkeyCallback, this);
		hotkeys_[7] = obs_hotkey_register_frontend("sync_guardian.recalibrate", "Sync Guardian: Restart A/V Auto Calibration",
						      hotkeyCallback, this);
	}

	void unregisterHotkeys()
	{
		for (obs_hotkey_id id : hotkeys_) {
			if (id != OBS_INVALID_HOTKEY_ID)
				obs_hotkey_unregister(id);
		}
	}

	static void hotkeyCallback(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
	{
		if (!pressed)
			return;
		auto *self = static_cast<SyncGuardian *>(data);
		QMetaObject::invokeMethod(self->lifetimeContext_, [self, id]() {
			if (id == self->hotkeys_[0])
				self->manualReset(RecoveryTarget::Video, QStringLiteral("Reset Video Only (hotkey)"));
			else if (id == self->hotkeys_[1])
				self->manualReset(RecoveryTarget::DesktopAudio, QStringLiteral("Reset Desktop Audio Only (hotkey)"));
			else if (id == self->hotkeys_[2])
				self->manualReset(RecoveryTarget::Mic, QStringLiteral("Reset Mic Only (hotkey)"));
			else if (id == self->hotkeys_[3])
				self->manualReset(RecoveryTarget::BothAudio, QStringLiteral("Reset Both Audio Sources (hotkey)"));
			else if (id == self->hotkeys_[4])
				self->manualReset(RecoveryTarget::EntireGroup, QStringLiteral("Rebuild Entire Sync Group (hotkey)"));
			else if (id == self->hotkeys_[5])
				self->restoreSnapshotState();
			else if (id == self->hotkeys_[6])
				self->appendEvent(QStringLiteral("Manual sync event marker (hotkey)"));
			else if (id == self->hotkeys_[7])
				self->clearCalibration(QStringLiteral("hotkey"), true);
		}, Qt::QueuedConnection);
	}

	static void frontendEvent(enum obs_frontend_event event, void *data)
	{
		auto *self = static_cast<SyncGuardian *>(data);
		if (!self || !self->panel_)
			return;
		if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
			QMetaObject::invokeMethod(self->lifetimeContext_, [self]() {
				self->refreshSourceLists();
				self->loadConfiguration();
				self->bindAllSources();
				self->clearCalibration(QStringLiteral("OBS scene collection/load event"), false);
			}, Qt::QueuedConnection);
		}
	}
};

SyncGuardian *g_guardian = nullptr;

} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Automatic DistroAV/NDI A/V timestamp monitoring, staged receiver recovery, and incident diagnostics for OBS Studio.";
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[sync-guardian] Loading version %s", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	g_guardian = new SyncGuardian();
}

void obs_module_unload(void)
{
	delete g_guardian;
	g_guardian = nullptr;
	blog(LOG_INFO, "[sync-guardian] Unloaded");
}
