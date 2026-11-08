/*---------------------------------------------------------*\
| OpenRGBWallpaperEnginePlugin.cpp                          |
|                                                           |
|   OpenRGB companion plugin for Wallpaper Engine           |
|   lighting synchronization via Razer Chroma & iCUE        |
\*---------------------------------------------------------*/

#define _CRT_SECURE_NO_WARNINGS

#include "OpenRGBWallpaperEnginePlugin.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

/*---------------------------------------------------------*\
| Chroma Shared Memory Constants                            |
|                                                           |
| Razer Chroma shared memory is a ring buffer of 10 slots. |
| Each slot is 2,968 bytes (0x0B98).                        |
| Offset 0x0000 holds the 32-bit atomic write index (0-9).  |
| The last completed slot is: (write_index + 9) % 10.       |
| In each slot:                                             |
|   - Offset 0x00: Header (magic, effect type)              |
|   - Offset 0x48: Color[6][22] (Background underglow)      |
|   - Offset 0x258: Key[6][22] (PER-KEY WALLPAPER CANVAS!)  |
\*---------------------------------------------------------*/
static const char*  CHROMA_SHM_GLOBAL   = "Global\\{74164FAD-E73C-4FA1-A9AA-70813315ED9C}";
static const char*  CHROMA_SHM_LOCAL    = "Local\\{74164FAD-E73C-4FA1-A9AA-70813315ED9C}";
static const size_t CHROMA_SHM_SIZE     = 29688; // 8 byte header + 10 slots x 2968
static const size_t CHROMA_SLOT_SIZE    = 2968;  // 0x0B98
static const int    CHROMA_ROWS         = 6;
static const int    CHROMA_COLS         = 22;
static const int    CHROMA_TOTAL_KEYS   = CHROMA_ROWS * CHROMA_COLS; // 132

/*---------------------------------------------------------*\
| Hardware Device Classification for Smart Spatial Layout   |
\*---------------------------------------------------------*/
enum DeviceSpatialRole
{
    ROLE_VERTICAL_RAM,       // DRAM, DIMM, Memory -> vertical orientation (Y axis)
    ROLE_HORIZONTAL_GPU,     // GPU, Graphics Card, Discrete VGA -> bottom horizontal (X axis, Y=bottom)
    ROLE_CENTER_COOLER,      // Liquid Cooler, AIO Pump, CPU Block -> center zone (X=center, Y=center)
    ROLE_LEFT_MOTHERBOARD,   // Motherboard, Mainboard, I/O Shroud -> left side / IO
    ROLE_GENERIC_STRIP       // Strips, Fans, generic -> horizontal panoramic sweep
};

static DeviceSpatialRole ClassifyDevice(const std::string& name_raw)
{
    std::string name = name_raw;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return (char)std::tolower(c); });

    if (name.find("dram") != std::string::npos ||
        name.find("dimm") != std::string::npos ||
        name.find("memory") != std::string::npos ||
        name.find("ram") != std::string::npos)
    {
        return ROLE_VERTICAL_RAM;
    }

    if (name.find("rtx") != std::string::npos ||
        name.find("geforce") != std::string::npos ||
        name.find("radeon") != std::string::npos ||
        name.find("arc") != std::string::npos ||
        name.find("gpu") != std::string::npos ||
        name.find("graphics") != std::string::npos ||
        name.find("vga") != std::string::npos)
    {
        return ROLE_HORIZONTAL_GPU;
    }

    if (name.find("cooler") != std::string::npos ||
        name.find("pump") != std::string::npos ||
        name.find("aio") != std::string::npos ||
        name.find("liquid") != std::string::npos ||
        name.find("hydro") != std::string::npos ||
        name.find("kraken") != std::string::npos ||
        name.find("nautilus") != std::string::npos ||
        name.find("capellix") != std::string::npos)
    {
        return ROLE_CENTER_COOLER;
    }

    if (name.find("motherboard") != std::string::npos ||
        name.find("mainboard") != std::string::npos ||
        name.find("mobo") != std::string::npos ||
        name.find("z790") != std::string::npos ||
        name.find("z890") != std::string::npos ||
        name.find("b650") != std::string::npos ||
        name.find("x670") != std::string::npos ||
        name.find("x870") != std::string::npos ||
        name.find("strix") != std::string::npos ||
        name.find("taichi") != std::string::npos ||
        name.find("tomahawk") != std::string::npos)
    {
        return ROLE_LEFT_MOTHERBOARD;
    }

    return ROLE_GENERIC_STRIP;
}

/*---------------------------------------------------------*\
| WallpaperEngineSyncWidget Implementation                  |
\*---------------------------------------------------------*/
WallpaperEngineSyncWidget::WallpaperEngineSyncWidget(OpenRGBWallpaperEnginePlugin* plugin_parent, QWidget* parent)
    : QWidget(parent), plugin(plugin_parent)
{
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(12);
    main_layout->setContentsMargins(16, 16, 16, 16);

    // Title / Header
    QLabel* header_label = new QLabel("Wallpaper Engine Lighting Sync", this);
    QFont title_font = header_label->font();
    title_font.setPointSize(14);
    title_font.setBold(true);
    header_label->setFont(title_font);
    main_layout->addWidget(header_label);

    QLabel* desc_label = new QLabel("Synchronizes your physical OpenRGB hardware with Wallpaper Engine's live RGB stream.", this);
    desc_label->setStyleSheet("color: #888888;");
    main_layout->addWidget(desc_label);

    // Group 1: Status & Live Preview
    QGroupBox* status_group = new QGroupBox("Wallpaper Engine Stream Status", this);
    QVBoxLayout* status_layout = new QVBoxLayout(status_group);

    QHBoxLayout* status_row = new QHBoxLayout();
    status_label = new QLabel("Checking connection...", status_group);
    status_label->setStyleSheet("font-weight: bold; font-size: 13px; color: #f44336;");
    status_row->addWidget(status_label);
    status_row->addStretch();

    fps_label = new QLabel("0.0 FPS", status_group);
    fps_label->setStyleSheet("color: #aaaaaa; font-family: monospace; font-size: 13px;");
    status_row->addWidget(fps_label);
    status_layout->addLayout(status_row);

    // Multi-color spectrum flow bar
    QLabel* spectrum_title = new QLabel("Live Multi-Color Stream Spectrum (Horizontal Screen Canvas):", status_group);
    spectrum_title->setStyleSheet("color: #aaaaaa; font-size: 11px; margin-top: 4px;");
    status_layout->addWidget(spectrum_title);

    spectrum_preview_bar = new QFrame(status_group);
    spectrum_preview_bar->setFixedHeight(22);
    spectrum_preview_bar->setFrameShape(QFrame::Box);
    spectrum_preview_bar->setStyleSheet("background-color: #1a1a1a; border: 1px solid #444444; border-radius: 4px;");
    status_layout->addWidget(spectrum_preview_bar);

    QHBoxLayout* preview_row = new QHBoxLayout();
    QLabel* preview_caption = new QLabel("Dominant Ambient Color:", status_group);
    preview_row->addWidget(preview_caption);

    color_preview_box = new QFrame(status_group);
    color_preview_box->setFixedSize(36, 20);
    color_preview_box->setFrameShape(QFrame::Box);
    color_preview_box->setStyleSheet("background-color: #000000; border: 1px solid #555555; border-radius: 4px;");
    preview_row->addWidget(color_preview_box);

    color_rgb_label = new QLabel("RGB: (0, 0, 0)", status_group);
    color_rgb_label->setStyleSheet("font-family: monospace; color: #888888;");
    preview_row->addWidget(color_rgb_label);
    preview_row->addStretch();
    status_layout->addLayout(preview_row);

    main_layout->addWidget(status_group);

    // Group 2: Sync Settings
    QGroupBox* settings_group = new QGroupBox("Synchronization Controls", this);
    QVBoxLayout* settings_layout = new QVBoxLayout(settings_group);

    enable_checkbox = new QCheckBox("Enable Wallpaper Engine Lighting Sync", settings_group);
    QFont check_font = enable_checkbox->font();
    check_font.setPointSize(11);
    check_font.setBold(true);
    enable_checkbox->setFont(check_font);
    enable_checkbox->setChecked(plugin->IsSyncEnabled());
    connect(enable_checkbox, &QCheckBox::toggled, this, &WallpaperEngineSyncWidget::OnSyncToggled);
    settings_layout->addWidget(enable_checkbox);

    // Brightness row
    QHBoxLayout* bright_row = new QHBoxLayout();
    QLabel* bright_caption = new QLabel("Brightness:", settings_group);
    bright_row->addWidget(bright_caption);

    brightness_slider = new QSlider(Qt::Horizontal, settings_group);
    brightness_slider->setRange(0, 100);
    brightness_slider->setValue(plugin->GetBrightness());
    connect(brightness_slider, &QSlider::valueChanged, this, &WallpaperEngineSyncWidget::OnBrightnessChanged);
    bright_row->addWidget(brightness_slider);

    brightness_label = new QLabel(QString::number(plugin->GetBrightness()) + "%", settings_group);
    brightness_label->setFixedWidth(45);
    bright_row->addWidget(brightness_label);
    settings_layout->addLayout(bright_row);

    // Flow & Smoothing row (Razer-style temporal ease)
    QHBoxLayout* smooth_row = new QHBoxLayout();
    QLabel* smooth_caption = new QLabel("Color Flow & Smoothing:", settings_group);
    smooth_row->addWidget(smooth_caption);

    smoothing_slider = new QSlider(Qt::Horizontal, settings_group);
    smoothing_slider->setRange(10, 95);
    smoothing_slider->setValue(plugin->GetSmoothingLevel());
    connect(smoothing_slider, &QSlider::valueChanged, this, &WallpaperEngineSyncWidget::OnSmoothingChanged);
    smooth_row->addWidget(smoothing_slider);

    smoothing_label = new QLabel(QString::number(plugin->GetSmoothingLevel()) + "%", settings_group);
    smoothing_label->setFixedWidth(45);
    smooth_row->addWidget(smoothing_label);
    settings_layout->addLayout(smooth_row);

    // Loop when paused checkbox (Default: freeze last frame, matching Razer Synapse)
    loop_when_paused_cb = new QCheckBox("Loop ambient colors when Wallpaper Engine pauses (default: freeze last frame)", settings_group);
    loop_when_paused_cb->setChecked(plugin->IsLoopWhenPausedEnabled());
    connect(loop_when_paused_cb, &QCheckBox::toggled, this, &WallpaperEngineSyncWidget::OnLoopWhenPausedToggled);
    settings_layout->addWidget(loop_when_paused_cb);

    main_layout->addWidget(settings_group);

    // Group 3: Device Selection
    QGroupBox* devices_group = new QGroupBox("Synced Devices (Check devices to synchronize)", this);
    QVBoxLayout* devices_layout = new QVBoxLayout(devices_group);

    device_tree = new QTreeWidget(devices_group);
    device_tree->setHeaderLabels(QStringList() << "Device Name (Check to Sync)" << "Case Location / Zone" << "LEDs / Info");
    device_tree->setColumnWidth(0, 310);
    device_tree->setColumnWidth(1, 210);
    device_tree->header()->setStretchLastSection(true);
    device_tree->setRootIsDecorated(false);
    device_tree->setSelectionMode(QAbstractItemView::NoSelection);
    connect(device_tree, &QTreeWidget::itemChanged, this, &WallpaperEngineSyncWidget::OnDeviceItemChanged);
    devices_layout->addWidget(device_tree);

    QHBoxLayout* dev_btn_row = new QHBoxLayout();
    select_all_btn = new QPushButton("Select All", devices_group);
    connect(select_all_btn, &QPushButton::clicked, this, &WallpaperEngineSyncWidget::OnSelectAllClicked);
    dev_btn_row->addWidget(select_all_btn);

    deselect_all_btn = new QPushButton("Deselect All", devices_group);
    connect(deselect_all_btn, &QPushButton::clicked, this, &WallpaperEngineSyncWidget::OnDeselectAllClicked);
    dev_btn_row->addWidget(deselect_all_btn);

    dev_btn_row->addStretch();

    refresh_btn = new QPushButton("Refresh Devices", devices_group);
    connect(refresh_btn, &QPushButton::clicked, this, &WallpaperEngineSyncWidget::OnRefreshClicked);
    dev_btn_row->addWidget(refresh_btn);

    devices_layout->addLayout(dev_btn_row);
    main_layout->addWidget(devices_group);

    QLabel* legal_lbl = new QLabel("Independent open-source plugin (API v5). Not affiliated with Wallpaper Engine, Razer, or Corsair. Use at your own risk.", this);
    legal_lbl->setStyleSheet("color: #666666; font-size: 11px;");
    legal_lbl->setAlignment(Qt::AlignCenter);
    main_layout->addWidget(legal_lbl);

    main_layout->addStretch();

    // Populate devices
    RefreshDeviceList();

    // Set up UI update timer (tick at ~15 Hz for smooth UI updates)
    ui_timer = new QTimer(this);
    connect(ui_timer, &QTimer::timeout, this, &WallpaperEngineSyncWidget::OnUiTimerTick);
    ui_timer->start(66);
}

WallpaperEngineSyncWidget::~WallpaperEngineSyncWidget()
{
    if (ui_timer)
    {
        ui_timer->stop();
    }
}

void WallpaperEngineSyncWidget::RefreshDeviceList()
{
    updating_list_ui = true;
    device_tree->clear();

    auto controllers = plugin->GetControllers();
    for (size_t i = 0; i < controllers.size(); ++i)
    {
        RGBControllerInterface* ctrl = controllers[i];
        if (!ctrl) continue;

        std::string name = ctrl->GetName();
        // Ignore any virtual / loopback wallpaper devices
        if (name.find("Wallpaper") != std::string::npos) continue;

        unsigned int leds = ctrl->GetLEDCount();
        std::string desc = ctrl->GetDescription();

        QTreeWidgetItem* item = new QTreeWidgetItem(device_tree);
        item->setText(0, QString::fromStdString(name));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

        bool is_checked = plugin->IsDeviceEnabled(name);
        item->setCheckState(0, is_checked ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, QString::fromStdString(name));

        item->setText(2, QString("%1 LEDs - %2").arg(leds).arg(QString::fromStdString(desc)));

        QComboBox* zone_combo = new QComboBox();
        zone_combo->addItem("Auto-Detect (Smart)", ZONE_AUTO);
        zone_combo->addItem("Top Fans / Radiator", ZONE_TOP_FANS);
        zone_combo->addItem("Bottom Fans / Underglow", ZONE_BOTTOM_FANS);
        zone_combo->addItem("Front Intake Fans", ZONE_FRONT_FANS);
        zone_combo->addItem("Rear Exhaust Fan", ZONE_REAR_FAN);
        zone_combo->addItem("Motherboard / I/O", ZONE_MOTHERBOARD);
        zone_combo->addItem("RAM (Vertical)", ZONE_RAM);
        zone_combo->addItem("GPU (Horizontal)", ZONE_GPU);
        zone_combo->addItem("AIO Cooler / Pump", ZONE_AIO_COOLER);
        zone_combo->addItem("Full Screen Panorama", ZONE_PANORAMA);
        zone_combo->addItem("Ambient Color Wash", ZONE_AMBIENT);

        CaseLocationZone current_zone = plugin->GetDeviceZone(name);
        int idx = zone_combo->findData(current_zone);
        if (idx >= 0)
        {
            zone_combo->setCurrentIndex(idx);
        }

        connect(zone_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, name, zone_combo](int index) {
            if (updating_list_ui) return;
            CaseLocationZone z = (CaseLocationZone)zone_combo->itemData(index).toInt();
            plugin->SetDeviceZone(name, z);
        });

        device_tree->setItemWidget(item, 1, zone_combo);
    }

    updating_list_ui = false;
}

void WallpaperEngineSyncWidget::OnSyncToggled(bool checked)
{
    plugin->SetSyncEnabled(checked);
}

void WallpaperEngineSyncWidget::OnBrightnessChanged(int value)
{
    brightness_label->setText(QString::number(value) + "%");
    plugin->SetBrightness(value);
}

void WallpaperEngineSyncWidget::OnSmoothingChanged(int value)
{
    smoothing_label->setText(QString::number(value) + "%");
    plugin->SetSmoothingLevel(value);
}

void WallpaperEngineSyncWidget::OnLoopWhenPausedToggled(bool checked)
{
    plugin->SetLoopWhenPaused(checked);
}

void WallpaperEngineSyncWidget::OnDeviceItemChanged(QTreeWidgetItem* item, int column)
{
    if (updating_list_ui || !item || column != 0) return;

    std::string dev_name = item->data(0, Qt::UserRole).toString().toStdString();
    bool checked = (item->checkState(0) == Qt::Checked);
    plugin->SetDeviceEnabled(dev_name, checked);
}

void WallpaperEngineSyncWidget::OnSelectAllClicked()
{
    plugin->SetAllDevicesEnabled(true);
    RefreshDeviceList();
}

void WallpaperEngineSyncWidget::OnDeselectAllClicked()
{
    plugin->SetAllDevicesEnabled(false);
    RefreshDeviceList();
}

void WallpaperEngineSyncWidget::OnRefreshClicked()
{
    RefreshDeviceList();
}

void WallpaperEngineSyncWidget::OnUiTimerTick()
{
    bool connected = plugin->IsConnected();
    if (connected)
    {
        float vel = plugin->GetStreamMotionVelocity();
        if (vel < 8.0f)
        {
            status_label->setText("Active - Calm Ambient Scene (Jitter Lock & Power Save)");
        }
        else if (vel < 22.0f)
        {
            status_label->setText("Active - Gentle Ambient Flow");
        }
        else
        {
            status_label->setText("Active - Dynamic Wave Motion (Full Speed Flow)");
        }
        status_label->setStyleSheet("font-weight: bold; font-size: 13px; color: #4CAF50;");
    }
    else
    {
        status_label->setText("Waiting for Wallpaper Engine / Razer Chroma stream...");
        status_label->setStyleSheet("font-weight: bold; font-size: 13px; color: #ff9800;");
    }

    float fps = plugin->GetFPS();
    fps_label->setText(QString("%1 FPS").arg(fps, 0, 'f', 1));

    // Update live horizontal spectrum bar
    std::vector<RGBColor> spectrum = plugin->GetSpectrumPreview();
    if (spectrum.size() >= 2)
    {
        QString grad = "qlineargradient(x1:0, y1:0, x2:1, y2:0";
        for (size_t i = 0; i < spectrum.size(); ++i)
        {
            float stop = (float)i / (float)(spectrum.size() - 1);
            RGBColor c = spectrum[i];
            grad += QString(", stop:%1 rgb(%2,%3,%4)")
                        .arg(stop, 0, 'f', 3)
                        .arg(RGBGetRValue(c))
                        .arg(RGBGetGValue(c))
                        .arg(RGBGetBValue(c));
        }
        grad += ")";
        spectrum_preview_bar->setStyleSheet(QString("background: %1; border: 1px solid #666666; border-radius: 4px;").arg(grad));
    }

    RGBColor col = plugin->GetCurrentColor();
    unsigned char r = RGBGetRValue(col);
    unsigned char g = RGBGetGValue(col);
    unsigned char b = RGBGetBValue(col);

    color_preview_box->setStyleSheet(
        QString("background-color: rgb(%1, %2, %3); border: 1px solid #777777; border-radius: 4px;")
            .arg(r).arg(g).arg(b));

    color_rgb_label->setText(QString("RGB: (%1, %2, %3)").arg(r).arg(g).arg(b));
}

/*---------------------------------------------------------*\
| OpenRGBWallpaperEnginePlugin Implementation               |
\*---------------------------------------------------------*/
OpenRGBWallpaperEnginePlugin::OpenRGBWallpaperEnginePlugin()
{
}

OpenRGBWallpaperEnginePlugin::~OpenRGBWallpaperEnginePlugin()
{
    Unload();
}

OpenRGBPluginInfo OpenRGBWallpaperEnginePlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;
    info.Name            = "Wallpaper Engine Sync";
    info.Description     = "Synchronizes OpenRGB devices with Wallpaper Engine via Chroma / iCUE stream";
    info.Version         = "1.0.0";
    info.Commit          = "1.0.0";
    info.URL             = "https://github.com/Emieldev/OpenRGB-Wallpaper-Engine-Sync-Plugin";
    info.Location        = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label           = "Wallpaper Engine";
    info.TabIconString   = "";
    info.ProtocolVersion = 0;
    return info;
}

unsigned int OpenRGBWallpaperEnginePlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void OpenRGBWallpaperEnginePlugin::Load(OpenRGBPluginAPIInterface* plugin_api_ptr)
{
    plugin_api = plugin_api_ptr;

    LoadPluginSettings();

    // Start decoupled worker threads
    threads_running.store(true);
    needs_mode_init.store(true);

    stream_thread   = std::thread(&OpenRGBWallpaperEnginePlugin::StreamThreadFunc, this);
    hardware_thread = std::thread(&OpenRGBWallpaperEnginePlugin::HardwareThreadFunc, this);
}

QWidget* OpenRGBWallpaperEnginePlugin::GetWidget()
{
    if (!widget)
    {
        widget = new WallpaperEngineSyncWidget(this);
    }
    return widget;
}

QMenu* OpenRGBWallpaperEnginePlugin::GetTrayMenu()
{
    return nullptr;
}

void OpenRGBWallpaperEnginePlugin::Unload()
{
    if (threads_running.load())
    {
        threads_running.store(false);
        if (stream_thread.joinable())
        {
            stream_thread.join();
        }
        if (hardware_thread.joinable())
        {
            hardware_thread.join();
        }
    }

    if (widget)
    {
        delete widget;
        widget = nullptr;
    }
}

void OpenRGBWallpaperEnginePlugin::OnProfileAboutToLoad() {}
void OpenRGBWallpaperEnginePlugin::OnProfileLoad(nlohmann::json) {}
nlohmann::json OpenRGBWallpaperEnginePlugin::OnProfileSave() { return nlohmann::json(); }
unsigned char* OpenRGBWallpaperEnginePlugin::OnSDKCommand(unsigned int, unsigned char*, unsigned int*) { return nullptr; }
void OpenRGBWallpaperEnginePlugin::ProfileManagerUpdated(unsigned int) {}

void OpenRGBWallpaperEnginePlugin::ResourceManagerUpdated(unsigned int)
{
    if (widget)
    {
        widget->RefreshDeviceList();
    }
    controller_cache_dirty.store(true);
    needs_mode_init.store(true);
}

void OpenRGBWallpaperEnginePlugin::SettingsManagerUpdated(unsigned int) {}

std::vector<RGBControllerInterface*> OpenRGBWallpaperEnginePlugin::GetControllers()
{
    if (plugin_api)
    {
        return plugin_api->GetRGBControllers();
    }
    return {};
}

void OpenRGBWallpaperEnginePlugin::SetSyncEnabled(bool enabled)
{
    sync_enabled.store(enabled);
    if (enabled)
    {
        needs_mode_init.store(true);
    }
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetBrightness(int b)
{
    brightness.store(b);
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetSmoothingLevel(int s)
{
    smoothing_level.store(s);
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetLoopWhenPaused(bool l)
{
    loop_when_paused.store(l);
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetAntiFlashEnabled(bool en)
{
    anti_flash_enabled.store(en);
    controller_cache_dirty.store(true);
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetFanBrightness(int b)
{
    fan_accent_brightness.store(b);
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetFanHarmony(int h)
{
    fan_accent_harmony.store(h);
    SavePluginSettings();
}

bool OpenRGBWallpaperEnginePlugin::IsDeviceAntiFlash(const std::string& name) const
{
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    // Auto-detect: GPU controllers, fans, hubs, and accent indicators
    if (lower_name.find("gpu") != std::string::npos ||
        lower_name.find("geforce") != std::string::npos ||
        lower_name.find("radeon") != std::string::npos ||
        lower_name.find("arc") != std::string::npos ||
        lower_name.find("rtx") != std::string::npos ||
        lower_name.find("fan") != std::string::npos ||
        lower_name.find("commander") != std::string::npos ||
        lower_name.find("hub") != std::string::npos ||
        lower_name.find("cooler") != std::string::npos)
    {
        return true;
    }

    return false;
}

CaseLocationZone OpenRGBWallpaperEnginePlugin::GetDeviceZone(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(settings_mutex));
    auto it = device_zones.find(name);
    if (it != device_zones.end())
    {
        return it->second;
    }
    return ZONE_AUTO;
}

void OpenRGBWallpaperEnginePlugin::SetDeviceZone(const std::string& name, CaseLocationZone zone)
{
    {
        std::lock_guard<std::mutex> lock(settings_mutex);
        device_zones[name] = zone;
    }
    controller_cache_dirty.store(true);
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetSyncMode(int m)
{
    sync_mode.store(m);
    SavePluginSettings();
}

bool OpenRGBWallpaperEnginePlugin::IsDeviceEnabled(const std::string& name)
{
    std::lock_guard<std::mutex> lock(settings_mutex);
    return enabled_devices.find(name) != enabled_devices.end();
}

void OpenRGBWallpaperEnginePlugin::SetDeviceEnabled(const std::string& name, bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(settings_mutex);
        if (enabled)
        {
            enabled_devices.insert(name);
        }
        else
        {
            enabled_devices.erase(name);
        }
    }
    controller_cache_dirty.store(true);
    needs_mode_init.store(true);
    SavePluginSettings();
}

void OpenRGBWallpaperEnginePlugin::SetAllDevicesEnabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(settings_mutex);
        enabled_devices.clear();
        if (enabled)
        {
            auto ctrls = GetControllers();
            for (RGBControllerInterface* ctrl : ctrls)
            {
                if (ctrl)
                {
                    std::string d_name = ctrl->GetName();
                    if (d_name.find("Wallpaper") == std::string::npos)
                    {
                        enabled_devices.insert(d_name);
                    }
                }
            }
        }
    }
    controller_cache_dirty.store(true);
    needs_mode_init.store(true);
    SavePluginSettings();
}

std::vector<RGBColor> OpenRGBWallpaperEnginePlugin::GetSpectrumPreview()
{
    std::lock_guard<std::mutex> lock(canvas_mutex);
    std::vector<RGBColor> preview(CHROMA_COLS, 0);

    for (int col = 0; col < CHROMA_COLS; ++col)
    {
        // Sample middle row (row 2)
        unsigned char r = (unsigned char)std::clamp((int)shared_canvas[2][col].r, 0, 255);
        unsigned char g = (unsigned char)std::clamp((int)shared_canvas[2][col].g, 0, 255);
        unsigned char b = (unsigned char)std::clamp((int)shared_canvas[2][col].b, 0, 255);
        preview[col] = ToRGBColor(r, g, b);
    }

    return preview;
}

void OpenRGBWallpaperEnginePlugin::SavePluginSettings()
{
    try
    {
        std::filesystem::path config_dir;
        if (plugin_api)
        {
            config_dir = plugin_api->GetConfigurationDirectory();
        }
        if (config_dir.empty())
        {
#ifdef _WIN32
            const char* appdata = std::getenv("APPDATA");
            if (appdata)
            {
                config_dir = std::filesystem::path(appdata) / "OpenRGB";
            }
#else
            const char* xdg = std::getenv("XDG_CONFIG_HOME");
            if (xdg && *xdg)
            {
                config_dir = std::filesystem::path(xdg) / "OpenRGB";
            }
            else
            {
                const char* home = std::getenv("HOME");
                if (home && *home)
                {
                    config_dir = std::filesystem::path(home) / ".config" / "OpenRGB";
                }
            }
#endif
        }

        std::filesystem::path settings_dir = config_dir / "plugins" / "settings";
        std::filesystem::create_directories(settings_dir);
        std::filesystem::path settings_file = settings_dir / "WallpaperEngineSync.json";

        nlohmann::json settings;
        settings["enabled"]               = sync_enabled.load();
        settings["brightness"]            = brightness.load();
        settings["smoothing"]             = smoothing_level.load();
        settings["anti_flash_enabled"]    = anti_flash_enabled.load();
        settings["fan_accent_brightness"] = fan_accent_brightness.load();
        settings["fan_accent_harmony"]    = fan_accent_harmony.load();
        settings["loop_when_paused"]      = loop_when_paused.load();
        settings["mode"]                  = sync_mode.load();

        std::vector<std::string> dev_list;
        nlohmann::json zones_json = nlohmann::json::object();
        {
            std::lock_guard<std::mutex> lock(settings_mutex);
            dev_list.assign(enabled_devices.begin(), enabled_devices.end());
            for (const auto& pair : device_zones)
            {
                zones_json[pair.first] = (int)pair.second;
            }
        }
        settings["devices"] = dev_list;
        settings["device_zones"] = zones_json;

        std::ofstream out(settings_file);
        if (out.is_open())
        {
            out << settings.dump(4);
        }
    }
    catch (...) {}
}

void OpenRGBWallpaperEnginePlugin::LoadPluginSettings()
{
    try
    {
        std::filesystem::path config_dir;
        if (plugin_api)
        {
            config_dir = plugin_api->GetConfigurationDirectory();
        }
        if (config_dir.empty())
        {
#ifdef _WIN32
            const char* appdata = std::getenv("APPDATA");
            if (appdata)
            {
                config_dir = std::filesystem::path(appdata) / "OpenRGB";
            }
#else
            const char* xdg = std::getenv("XDG_CONFIG_HOME");
            if (xdg && *xdg)
            {
                config_dir = std::filesystem::path(xdg) / "OpenRGB";
            }
            else
            {
                const char* home = std::getenv("HOME");
                if (home && *home)
                {
                    config_dir = std::filesystem::path(home) / ".config" / "OpenRGB";
                }
            }
#endif
        }

        std::filesystem::path settings_file = config_dir / "plugins" / "settings" / "WallpaperEngineSync.json";
        if (std::filesystem::exists(settings_file))
        {
            std::ifstream in(settings_file);
            if (in.is_open())
            {
                nlohmann::json settings;
                in >> settings;

                if (settings.contains("enabled"))
                {
                    sync_enabled.store(settings["enabled"].get<bool>());
                }
                if (settings.contains("brightness"))
                {
                    brightness.store(settings["brightness"].get<int>());
                }
                if (settings.contains("smoothing"))
                {
                    smoothing_level.store(settings["smoothing"].get<int>());
                }
                if (settings.contains("anti_flash_enabled"))
                {
                    anti_flash_enabled.store(settings["anti_flash_enabled"].get<bool>());
                }
                if (settings.contains("fan_accent_brightness"))
                {
                    fan_accent_brightness.store(settings["fan_accent_brightness"].get<int>());
                }
                else if (settings.contains("gpu_fan_brightness"))
                {
                    fan_accent_brightness.store(settings["gpu_fan_brightness"].get<int>());
                }
                if (settings.contains("fan_accent_harmony"))
                {
                    fan_accent_harmony.store(settings["fan_accent_harmony"].get<int>());
                }
                else if (settings.contains("gpu_fan_harmony"))
                {
                    fan_accent_harmony.store(settings["gpu_fan_harmony"].get<int>());
                }
                if (settings.contains("loop_when_paused"))
                {
                    loop_when_paused.store(settings["loop_when_paused"].get<bool>());
                }
                if (settings.contains("mode"))
                {
                    sync_mode.store(settings["mode"].get<int>());
                }
                if (settings.contains("devices") && settings["devices"].is_array())
                {
                    std::lock_guard<std::mutex> lock(settings_mutex);
                    enabled_devices.clear();
                    for (const auto& dev : settings["devices"])
                    {
                        enabled_devices.insert(dev.get<std::string>());
                    }
                }
                if (settings.contains("device_zones") && settings["device_zones"].is_object())
                {
                    std::lock_guard<std::mutex> lock(settings_mutex);
                    device_zones.clear();
                    for (auto it = settings["device_zones"].begin(); it != settings["device_zones"].end(); ++it)
                    {
                        device_zones[it.key()] = (CaseLocationZone)it.value().get<int>();
                    }
                }
                controller_cache_dirty.store(true);
                needs_mode_init.store(true);
            }
        }
    }
    catch (...) {}
}

/*---------------------------------------------------------*\
| Stream Thread: High-Speed Zero-Lag Chroma Stream Reader  |
\*---------------------------------------------------------*/
void OpenRGBWallpaperEnginePlugin::StreamThreadFunc()
{
#ifdef _WIN32
    HANDLE hMap = NULL;
#endif
    unsigned char* pBuf = NULL;

    auto last_fps_time = std::chrono::steady_clock::now();
    int frames_counted = 0;
    RGBColorFloat local_ambient_smooth = { 0.0f, 0.0f, 0.0f };
    RGBColorFloat prev_frame_canvas[CHROMA_ROWS][CHROMA_COLS] = {};
    bool has_prev_frame = false;
    float rolling_motion_vel = 0.0f;
    auto last_stream_frame_time = std::chrono::steady_clock::now();

    while (threads_running.load())
    {
        // 1. Maintain shared memory connection with multi-namespace autodetection
        if (!pBuf)
        {
#ifdef _WIN32
            // Autodetect across Global\, Local\, and Session\<id>\ namespaces
            hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, CHROMA_SHM_GLOBAL);
            if (!hMap)
            {
                hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, CHROMA_SHM_LOCAL);
            }
            if (!hMap)
            {
                DWORD session_id = 0;
                if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
                {
                    std::string session_path = "Session\\" + std::to_string(session_id) + "\\BaseNamedObjects\\{74164FAD-E73C-4FA1-A9AA-70813315ED9C}";
                    hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, session_path.c_str());
                }
            }

            if (hMap)
            {
                pBuf = (unsigned char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, CHROMA_SHM_SIZE);
                if (pBuf)
                {
                    shm_connected.store(true);
                }
                else
                {
                    CloseHandle(hMap);
                    hMap = NULL;
                    shm_connected.store(false);
                }
            }
            else
            {
                shm_connected.store(false);
            }
#else
            // Linux / POSIX memory mapping fallback (for Proton / Wine / linux-wallpaperengine)
            int shm_fd = shm_open("/{74164FAD-E73C-4FA1-A9AA-70813315ED9C}", O_RDONLY, 0666);
            if (shm_fd != -1)
            {
                pBuf = (unsigned char*)mmap(NULL, CHROMA_SHM_SIZE, PROT_READ, MAP_SHARED, shm_fd, 0);
                close(shm_fd);
                if (pBuf != MAP_FAILED && pBuf != NULL)
                {
                    shm_connected.store(true);
                }
                else
                {
                    pBuf = NULL;
                    shm_connected.store(false);
                }
            }
            else
            {
                shm_connected.store(false);
            }
#endif

            if (!pBuf)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        // 2. Measure incoming stream FPS
        frames_counted++;
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = now - last_fps_time;
        if (elapsed.count() >= 1.0f)
        {
            current_fps.store((float)frames_counted / elapsed.count());
            frames_counted = 0;
            last_fps_time = now;
        }

        // 3. Obtain current completed slot from atomic write counter at offset 0
        uint32_t write_idx = *(const uint32_t*)pBuf;
        uint32_t latest_slot = write_idx % 10;

        static uint32_t last_seen_write_idx = 0xFFFFFFFF;
        auto now_tick = std::chrono::steady_clock::now();
        if (write_idx == last_seen_write_idx)
        {
            // Frame hasn't changed: check idle timeout without burning CPU or acquiring mutexes
            auto idle_dur = std::chrono::duration_cast<std::chrono::milliseconds>(now_tick - last_stream_arrival).count();
            if (idle_dur > 450)
            {
                stream_idle.store(true);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        last_seen_write_idx = write_idx;
        last_stream_arrival = now_tick;
        stream_idle.store(false);

        const unsigned char* pSlotBase = pBuf + 8 + (latest_slot * CHROMA_SLOT_SIZE);

        // In Razer Chroma CUSTOM_KEY_EFFECT_TYPE:
        // Offset 0x48:  Color[6][22] (Background underglow, 528 bytes)
        // Offset 0x258: Key[6][22]   (PER-KEY MULTI-COLOR CANVAS, 528 bytes!)
        const unsigned char* pKeyMatrix = pSlotBase + 0x258;
        const unsigned char* pBgMatrix  = pSlotBase + 0x48;

        // Fast 64-bit word check for key data
        bool has_key_data = false;
        const uint64_t* pWords = (const uint64_t*)pKeyMatrix;
        for (int i = 0; i < (CHROMA_TOTAL_KEYS * 4) / 8; ++i)
        {
            if (pWords[i] != 0)
            {
                has_key_data = true;
                break;
            }
        }

        const unsigned char* pColorData = has_key_data ? pKeyMatrix : pBgMatrix;

        // 4. Extract 6x22 2D spatial canvas directly (Zero redundant intermediate copies)
        RGBColorFloat diffuse_canvas[CHROMA_ROWS][CHROMA_COLS];
        float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f;
        int active_keys = 0;

        for (int row = 0; row < CHROMA_ROWS; ++row)
        {
            for (int col = 0; col < CHROMA_COLS; ++col)
            {
                int key_idx = (row * CHROMA_COLS + col) * 4;

                // Razer Chroma COLORREF: Byte 0: Red, Byte 1: Green, Byte 2: Blue
                float r = (float)pColorData[key_idx + 0];
                float g = (float)pColorData[key_idx + 1];
                float b = (float)pColorData[key_idx + 2];

                diffuse_canvas[row][col] = { r, g, b };

                if (r > 0.0f || g > 0.0f || b > 0.0f)
                {
                    sum_r += r;
                    sum_g += g;
                    sum_b += b;
                    active_keys++;
                }
            }
        }

        // 5. Blank Frame Rejection & Scene Palette Hold (Zero Blackouts during wallpaper transitions)
        float avg_frame_brightness = (sum_r + sum_g + sum_b) / (float)(CHROMA_TOTAL_KEYS * 3);
        bool is_blank_frame = (active_keys < 4 || avg_frame_brightness < 1.2f);

        if (!is_blank_frame && active_keys > 0)
        {
            // Measure sustained stream motion velocity (RGB units / second)
            auto current_frame_time = std::chrono::steady_clock::now();
            std::chrono::duration<float> dt_frame = current_frame_time - last_stream_frame_time;
            float dt_sec = (std::max)(0.005f, dt_frame.count());
            last_stream_frame_time = current_frame_time;

            float frame_diff = 0.0f;
            if (has_prev_frame)
            {
                float sum_diff = 0.0f;
                for (int r = 0; r < CHROMA_ROWS; ++r)
                {
                    for (int c = 0; c < CHROMA_COLS; ++c)
                    {
                        sum_diff += std::abs(diffuse_canvas[r][c].r - prev_frame_canvas[r][c].r) +
                                    std::abs(diffuse_canvas[r][c].g - prev_frame_canvas[r][c].g) +
                                    std::abs(diffuse_canvas[r][c].b - prev_frame_canvas[r][c].b);
                    }
                }
                frame_diff = sum_diff / (float)(CHROMA_TOTAL_KEYS * 3);
            }

            float instant_vel = frame_diff / dt_sec;
            rolling_motion_vel = 0.85f * rolling_motion_vel + 0.15f * instant_vel;
            stream_motion_velocity.store(rolling_motion_vel);

            for (int r = 0; r < CHROMA_ROWS; ++r)
            {
                for (int c = 0; c < CHROMA_COLS; ++c)
                {
                    prev_frame_canvas[r][c] = diffuse_canvas[r][c];
                }
            }
            has_prev_frame = true;

            // Commit crisp, unattenuated diffuse canvas directly to shared double-buffer
            {
                std::lock_guard<std::mutex> lock(canvas_mutex);
                std::memcpy(shared_canvas, diffuse_canvas, sizeof(shared_canvas));

                // Ambient preview smoothing for UI widget
                float target_amb_r = sum_r / (float)active_keys;
                float target_amb_g = sum_g / (float)active_keys;
                float target_amb_b = sum_b / (float)active_keys;

                local_ambient_smooth.r += 0.25f * (target_amb_r - local_ambient_smooth.r);
                local_ambient_smooth.g += 0.25f * (target_amb_g - local_ambient_smooth.g);
                local_ambient_smooth.b += 0.25f * (target_amb_b - local_ambient_smooth.b);

                unsigned char final_amb_r = (unsigned char)(std::clamp)((int)std::round(local_ambient_smooth.r), 0, 255);
                unsigned char final_amb_g = (unsigned char)(std::clamp)((int)std::round(local_ambient_smooth.g), 0, 255);
                unsigned char final_amb_b = (unsigned char)(std::clamp)((int)std::round(local_ambient_smooth.b), 0, 255);

                last_color.store(ToRGBColor(final_amb_r, final_amb_g, final_amb_b));
                shared_ambient = local_ambient_smooth;
                canvas_ready = true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

#ifdef _WIN32
    if (pBuf)
    {
        UnmapViewOfFile(pBuf);
    }
    if (hMap)
    {
        CloseHandle(hMap);
    }
#else
    if (pBuf && pBuf != MAP_FAILED)
    {
        munmap(pBuf, CHROMA_SHM_SIZE);
    }
#endif
    shm_connected.store(false);
}

/*---------------------------------------------------------*\
| Sub-pixel Bilinear Interpolation for Smooth Continuous    |
| Color Sampling Across Physical Device Geometry            |
\*---------------------------------------------------------*/
static inline RGBColorFloat SampleCanvasBilinear(const RGBColorFloat canvas[CHROMA_ROWS][CHROMA_COLS], float u, float v)
{
    float cu = (std::clamp)(u, 0.0f, (float)(CHROMA_COLS - 1));
    float cv = (std::clamp)(v, 0.0f, (float)(CHROMA_ROWS - 1));

    int x0 = (int)std::floor(cu);
    int x1 = (std::min)(x0 + 1, CHROMA_COLS - 1);
    int y0 = (int)std::floor(cv);
    int y1 = (std::min)(y0 + 1, CHROMA_ROWS - 1);

    float fx = cu - (float)x0;
    float fy = cv - (float)y0;

    const RGBColorFloat& c00 = canvas[y0][x0];
    const RGBColorFloat& c10 = canvas[y0][x1];
    const RGBColorFloat& c01 = canvas[y1][x0];
    const RGBColorFloat& c11 = canvas[y1][x1];

    RGBColorFloat top = {
        (1.0f - fx) * c00.r + fx * c10.r,
        (1.0f - fx) * c00.g + fx * c10.g,
        (1.0f - fx) * c00.b + fx * c10.b
    };

    RGBColorFloat bot = {
        (1.0f - fx) * c01.r + fx * c11.r,
        (1.0f - fx) * c01.g + fx * c11.g,
        (1.0f - fx) * c01.b + fx * c11.b
    };

    return {
        (1.0f - fy) * top.r + fy * bot.r,
        (1.0f - fy) * top.g + fy * bot.g,
        (1.0f - fy) * top.b + fy * bot.b
    };
}


/*---------------------------------------------------------*\
| Perceptual LED Color Calibration (Gamma & Diffuser Bleed) |
\*---------------------------------------------------------*/
static inline void CalibrateForCaseHardware(RGBColorFloat& color)
{
    float r = (std::clamp)(color.r, 0.0f, 255.0f);
    float g = (std::clamp)(color.g, 0.0f, 255.0f);
    float b = (std::clamp)(color.b, 0.0f, 255.0f);

    float max_c = (std::max)({ r, g, b });
    float min_c = (std::min)({ r, g, b });
    float chroma = max_c - min_c;

    if (max_c > 1.0f && chroma > 1.0f)
    {
        // 1. Diffuser Achromatic White Cancellation
        // Thick white frosted silicone/acrylic diffusers on Corsair fans & Trident Z RAM
        // scatter common-mode white light, washing out saturation.
        // Subtract a portion of the white floor proportionally to color saturation.
        float sat_factor = chroma / max_c;
        float white_sub = min_c * sat_factor * 0.45f;
        r = (std::max)(0.0f, r - white_sub);
        g = (std::max)(0.0f, g - white_sub);
        b = (std::max)(0.0f, b - white_sub);

        max_c = (std::max)({ r, g, b });

        // 2. Dominant-Hue Bleed Alignment
        if (r == max_c && r > 2.0f)
        {
            // Red is dominant: Suppress secondary blue & green bleed
            // White diffusers scatter 450nm blue light aggressively. Damping residual blue
            // when red is dominant prevents crimson/dark red from turning into pink.
            float r_dominance = (std::clamp)((r - (std::max)(g, b)) / r, 0.0f, 1.0f);
            b *= (1.0f - 0.50f * r_dominance);
            g *= (1.0f - 0.40f * r_dominance);
        }
        else if (g == max_c && g > 2.0f)
        {
            // Green is dominant: Balance blue/red ratio to preserve soft turquoise green
            // Without this, the higher eye sensitivity to blue turns dark green into pale icy turquoise.
            float g_dominance = (std::clamp)((g - (std::max)(r, b)) / g, 0.0f, 1.0f);
            b *= (1.0f - 0.25f * g_dominance);
            r *= (1.0f - 0.45f * g_dominance);
        }
    }

    // 3. Perceptual Gamma Exponent (~2.1)
    // Converts sRGB input space into linear LED PWM luminous flux
    const float inv255 = 1.0f / 255.0f;
    const float gamma = 2.1f;
    color.r = 255.0f * std::pow(r * inv255, gamma);
    color.g = 255.0f * std::pow(g * inv255, gamma);
    color.b = 255.0f * std::pow(b * inv255, gamma);
}

/*---------------------------------------------------------*\
| Device Hardware Profiles & Adaptive Pacing                |
\*---------------------------------------------------------*/
static inline DeviceHardwareCapability AnalyzeDeviceHardware(RGBControllerInterface* ctrl, CaseLocationZone zone)
{
    DeviceHardwareCapability cap;
    cap.profile = PROFILE_CASE_FAN_ARGB;
    cap.alpha_mult = 0.65f;
    cap.slew_mult = 0.60f;
    cap.min_interval_ms = 40;
    cap.supports_per_led = false;
    cap.has_hardware_wave = false;

    if (!ctrl) return cap;

    unsigned int dev_type = ctrl->GetDeviceType();
    std::string name = ctrl->GetName();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return (char)std::tolower(c); });

    // 1. Classify device bus / hardware profile with comprehensive hardware vendor detection
    if (dev_type == DEVICE_TYPE_GPU || zone == ZONE_GPU ||
        name.find("geforce") != std::string::npos || name.find("radeon") != std::string::npos ||
        name.find("rtx") != std::string::npos || name.find("gtx") != std::string::npos ||
        name.find("gpu") != std::string::npos || name.find("graphics") != std::string::npos ||
        name.find("vga") != std::string::npos || name.find("aorus") != std::string::npos ||
        (name.find("strix") != std::string::npos && name.find("board") == std::string::npos) ||
        name.find("suprim") != std::string::npos || name.find("gaming x") != std::string::npos ||
        name.find("nitro") != std::string::npos || name.find("red devil") != std::string::npos ||
        name.find("ftw") != std::string::npos || name.find("arc") != std::string::npos)
    {
        cap.profile = PROFILE_GPU_I2C;
    }
    else if (dev_type == DEVICE_TYPE_DRAM || zone == ZONE_RAM ||
             name.find("dram") != std::string::npos || name.find("dimm") != std::string::npos ||
             name.find("trident") != std::string::npos || name.find("memory") != std::string::npos ||
             name.find("vengeance") != std::string::npos || name.find("dominator") != std::string::npos ||
             name.find("fury") != std::string::npos || name.find("ballistix") != std::string::npos ||
             name.find("t-force") != std::string::npos || name.find("xpg") != std::string::npos ||
             name.find("hyperx") != std::string::npos || name.find("super luce") != std::string::npos ||
             name.find("ene") != std::string::npos)
    {
        cap.profile = PROFILE_DRAM_SMBUS;
    }
    else if (dev_type == DEVICE_TYPE_MOTHERBOARD || zone == ZONE_MOTHERBOARD ||
             name.find("motherboard") != std::string::npos || name.find("mainboard") != std::string::npos ||
             name.find("mobo") != std::string::npos || name.find("aura") != std::string::npos ||
             name.find("mystic") != std::string::npos || name.find("fusion") != std::string::npos ||
             name.find("polychrome") != std::string::npos || name.find("chipset") != std::string::npos)
    {
        cap.profile = PROFILE_MOTHERBOARD_SMB;
    }
    else if (dev_type == DEVICE_TYPE_COOLER || zone == ZONE_AIO_COOLER ||
             name.find("cooler") != std::string::npos || name.find("pump") != std::string::npos ||
             name.find("aio") != std::string::npos || name.find("kraken") != std::string::npos ||
             name.find("capellix") != std::string::npos || name.find("nautilus") != std::string::npos ||
             name.find("hydro") != std::string::npos || name.find("ryujin") != std::string::npos ||
             name.find("celsius") != std::string::npos || name.find("liquid") != std::string::npos ||
             name.find("h100") != std::string::npos || name.find("h115") != std::string::npos ||
             name.find("h150") != std::string::npos)
    {
        cap.profile = PROFILE_COOLER_USB;
    }
    else if (dev_type == DEVICE_TYPE_KEYBOARD || dev_type == DEVICE_TYPE_MOUSE ||
             dev_type == DEVICE_TYPE_MOUSEMAT || dev_type == DEVICE_TYPE_HEADSET ||
             dev_type == DEVICE_TYPE_HEADSET_STAND)
    {
        cap.profile = PROFILE_PERIPHERAL_FAST;
    }
    else
    {
        cap.profile = PROFILE_CASE_FAN_ARGB;
    }

    // 2. Inspect controller's reported native modes
    unsigned int mode_count = ctrl->GetModeCount();
    for (unsigned int m = 0; m < mode_count; ++m)
    {
        std::string m_name = ctrl->GetModeName(m);
        std::string lower_m = m_name;
        std::transform(lower_m.begin(), lower_m.end(), lower_m.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        unsigned int flags = ctrl->GetModeFlags(m);

        if (lower_m == "direct" || (flags & MODE_FLAG_HAS_PER_LED_COLOR))
        {
            cap.supports_per_led = true;
        }
        if (lower_m.find("rainbow") != std::string::npos ||
            lower_m.find("wave") != std::string::npos ||
            lower_m.find("cycle") != std::string::npos ||
            lower_m.find("spectrum") != std::string::npos)
        {
            cap.has_hardware_wave = true;
        }
    }

    // 3. Assign tailored pacing, interval, and slew limits per hardware profile
    switch (cap.profile)
    {
        case PROFILE_GPU_I2C:
            // GPU: Aorus / ASUS / MSI / EVGA on GPU I2C bus.
            // 100ms cadence (10 Hz) with gentle alpha/slew eliminates all bus collisions and PWM resets.
            cap.alpha_mult      = 0.40f;
            cap.slew_mult       = 0.35f;
            cap.min_interval_ms = 100;
            break;

        case PROFILE_DRAM_SMBUS:
            // DRAM: Trident Z / Corsair / HyperX on 100 kHz SMBus.
            // 60ms cadence (~16 Hz) provides dignified flow with zero bus collisions.
            cap.alpha_mult      = 0.50f;
            cap.slew_mult       = 0.45f;
            cap.min_interval_ms = 60;
            break;

        case PROFILE_MOTHERBOARD_SMB:
            // Motherboard: Ambient backdrop on SMBus (~14 Hz, 70ms).
            cap.alpha_mult      = 0.45f;
            cap.slew_mult       = 0.40f;
            cap.min_interval_ms = 70;
            break;

        case PROFILE_COOLER_USB:
            // AIO Cooler: USB HID communication (~25 Hz, 40ms).
            cap.alpha_mult      = 0.70f;
            cap.slew_mult       = 0.65f;
            cap.min_interval_ms = 40;
            break;

        case PROFILE_CASE_FAN_ARGB:
            // ARGB Fans, Hubs & Strips (~25 Hz, 40ms).
            cap.alpha_mult      = 0.65f;
            cap.slew_mult       = 0.60f;
            cap.min_interval_ms = 40;
            break;

        case PROFILE_PERIPHERAL_FAST:
        default:
            // Fast USB Peripherals up to 60 Hz (16ms).
            cap.alpha_mult      = 1.0f;
            cap.slew_mult       = 1.0f;
            cap.min_interval_ms = 16;
            break;
    }

    return cap;
}

/*---------------------------------------------------------*\
| Rebuild Cached Controller Metadata (Zero Alloc Hot Loop)  |
\*---------------------------------------------------------*/
void OpenRGBWallpaperEnginePlugin::RebuildControllerCache()
{
    if (!plugin_api) return;

    auto controllers = plugin_api->GetRGBControllers();
    std::vector<CachedController> new_cache;
    new_cache.reserve(controllers.size());

    std::map<void*, std::vector<LEDSmoothingState>> old_states;
    std::map<void*, std::chrono::steady_clock::time_point> old_times;
    for (auto& entry : cached_controllers)
    {
        if (entry.ctrl)
        {
            old_states[(void*)entry.ctrl] = std::move(entry.led_states);
            old_times[(void*)entry.ctrl] = entry.last_update_time;
        }
    }

    for (RGBControllerInterface* ctrl : controllers)
    {
        if (!ctrl) continue;
        std::string dev_name = ctrl->GetName();
        if (dev_name.find("Wallpaper") != std::string::npos) continue;
        if (!IsDeviceEnabled(dev_name)) continue;

        unsigned int led_count = ctrl->GetLEDCount();
        if (led_count == 0) continue;

        try
        {
            int direct_idx = -1;
            int custom_idx = -1;
            int per_led_idx = -1;
            unsigned int mode_count = ctrl->GetModeCount();

            for (unsigned int m = 0; m < mode_count; ++m)
            {
                std::string m_name = ctrl->GetModeName(m);
                unsigned int flags = ctrl->GetModeFlags(m);

                if (m_name == "Direct")
                {
                    direct_idx = (int)m;
                }
                else if (m_name == "Custom")
                {
                    custom_idx = (int)m;
                }
                else if (flags & MODE_FLAG_HAS_PER_LED_COLOR)
                {
                    if (per_led_idx == -1) per_led_idx = (int)m;
                }
            }

            int chosen_mode = -1;
            if (direct_idx >= 0)       chosen_mode = direct_idx;
            else if (custom_idx >= 0)  chosen_mode = custom_idx;
            else if (per_led_idx >= 0) chosen_mode = per_led_idx;

            if (chosen_mode >= 0 && ctrl->GetActiveMode() != chosen_mode)
            {
                ctrl->SetCustomMode();
                ctrl->SetActiveMode(chosen_mode);
                ctrl->UpdateMode();
            }
        }
        catch (...) {}

        CachedController entry;
        entry.ctrl = ctrl;
        entry.name = dev_name;
        entry.led_count = led_count;

        CaseLocationZone user_zone = GetDeviceZone(dev_name);
        entry.effective_zone = user_zone;
        if (entry.effective_zone == ZONE_AUTO)
        {
            switch (ctrl->GetDeviceType())
            {
                case DEVICE_TYPE_DRAM:        entry.effective_zone = ZONE_RAM; break;
                case DEVICE_TYPE_GPU:         entry.effective_zone = ZONE_GPU; break;
                case DEVICE_TYPE_COOLER:      entry.effective_zone = ZONE_AIO_COOLER; break;
                case DEVICE_TYPE_MOTHERBOARD: entry.effective_zone = ZONE_MOTHERBOARD; break;
                default:
                {
                    DeviceSpatialRole role = ClassifyDevice(dev_name);
                    switch (role)
                    {
                        case ROLE_VERTICAL_RAM:     entry.effective_zone = ZONE_RAM; break;
                        case ROLE_HORIZONTAL_GPU:   entry.effective_zone = ZONE_GPU; break;
                        case ROLE_CENTER_COOLER:    entry.effective_zone = ZONE_AIO_COOLER; break;
                        case ROLE_LEFT_MOTHERBOARD: entry.effective_zone = ZONE_MOTHERBOARD; break;
                        default:                    entry.effective_zone = ZONE_PANORAMA; break;
                    }
                    break;
                }
            }
        }

        entry.hw_cap = AnalyzeDeviceHardware(ctrl, entry.effective_zone);

        entry.is_fan_or_accent = (entry.effective_zone == ZONE_TOP_FANS ||
                                  entry.effective_zone == ZONE_BOTTOM_FANS ||
                                  entry.effective_zone == ZONE_FRONT_FANS ||
                                  entry.effective_zone == ZONE_REAR_FAN ||
                                  entry.effective_zone == ZONE_GPU);

        entry.dev_is_af = anti_flash_enabled.load() &&
            (IsDeviceAntiFlash(dev_name) || (entry.hw_cap.profile == PROFILE_GPU_I2C) || entry.is_fan_or_accent);

        entry.led_is_af.resize(led_count, false);
        entry.led_is_gpu_fan.resize(led_count, false);

        for (unsigned int i = 0; i < led_count; ++i)
        {
            std::string led_name = ctrl->GetLEDName(i);
            std::string lower_led = led_name;
            std::transform(lower_led.begin(), lower_led.end(), lower_led.begin(), ::tolower);

            if (lower_led.find("fan") != std::string::npos)
            {
                entry.led_is_gpu_fan[i] = true;
            }

            bool is_af = entry.dev_is_af || entry.is_fan_or_accent;
            if (anti_flash_enabled.load() && !is_af)
            {
                if (led_count <= 3 ||
                    lower_led.find("fan") != std::string::npos ||
                    lower_led.find("logo") != std::string::npos ||
                    lower_led.find("stop") != std::string::npos ||
                    lower_led.find("indicator") != std::string::npos)
                {
                    is_af = true;
                }
            }
            entry.led_is_af[i] = is_af;
        }

        entry.zone_count = ctrl->GetZoneCount();
        entry.zone_led_counts.resize(entry.zone_count);
        for (int z = 0; z < entry.zone_count; ++z)
        {
            entry.zone_led_counts[z] = ctrl->GetZoneLEDsCount(z);
        }

        auto it = old_states.find((void*)ctrl);
        if (it != old_states.end() && it->second.size() == led_count)
        {
            entry.led_states = std::move(it->second);
        }
        else
        {
            entry.led_states.resize(led_count);
        }

        auto it_t = old_times.find((void*)ctrl);
        if (it_t != old_times.end())
        {
            entry.last_update_time = it_t->second;
        }

        new_cache.push_back(std::move(entry));
    }

    cached_controllers = std::move(new_cache);
}

/*---------------------------------------------------------*\
| Hardware Thread: Asynchronous Safe Controller Dispatch    |
\*---------------------------------------------------------*/
void OpenRGBWallpaperEnginePlugin::HardwareThreadFunc()
{
    while (threads_running.load())
    {
        if (!sync_enabled.load() || !plugin_api)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        auto start_time = std::chrono::steady_clock::now();

        if (controller_cache_dirty.exchange(false) || needs_mode_init.exchange(false))
        {
            RebuildControllerCache();
        }

        if (cached_controllers.empty())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        RGBColorFloat local_canvas[CHROMA_ROWS][CHROMA_COLS];
        RGBColorFloat local_ambient;
        bool ready = false;

        {
            std::lock_guard<std::mutex> lock(canvas_mutex);
            if (canvas_ready)
            {
                ready = true;
                std::memcpy(local_canvas, shared_canvas, sizeof(local_canvas));
                local_ambient = shared_ambient;
            }
        }

        if (ready)
        {
            float bright_factor = (float)brightness.load() / 100.0f;

            bool is_paused = stream_idle.load();
            float shift_u = 0.0f;
            if (is_paused && loop_when_paused.load())
            {
                idle_phase += 0.04f;
                if (idle_phase >= 21.0f) idle_phase -= 21.0f;
                shift_u = idle_phase;
            }

            int s_val = (std::clamp)(smoothing_level.load(), 10, 95);
            float s = (float)(s_val - 10) / 85.0f;

            float motion_vel = stream_motion_velocity.load();
            bool is_calm_scene = (motion_vel < 8.0f);

            float base_alpha    = 0.08f + 0.28f * std::pow(1.0f - s, 1.4f);
            float base_max_slew = 2.5f + 8.0f * std::pow(1.0f - s, 1.6f);

            int ram_stick_counter = 0;

            for (auto& entry : cached_controllers)
            {
                RGBControllerInterface* ctrl = entry.ctrl;
                if (!ctrl) continue;

                unsigned int led_count = entry.led_count;
                int deadband_thresh = 3;
                bool ctrl_dirty = false;

                for (unsigned int i = 0; i < led_count; ++i)
                {
                    RGBColorFloat target_color = { 0.0f, 0.0f, 0.0f };
                    float frac = (led_count > 1) ? ((float)i / (float)(led_count - 1)) : 0.5f;

                    switch (entry.effective_zone)
                    {
                        case ZONE_AMBIENT:
                        {
                            target_color = local_ambient;
                            break;
                        }

                        case ZONE_PANORAMA:
                        {
                            float u = (0.5f + frac * 20.5f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                            target_color = SampleCanvasBilinear(local_canvas, u, 2.5f);
                            break;
                        }

                        case ZONE_TOP_FANS:
                        {
                            float u = (0.5f + frac * 20.5f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                            target_color = SampleCanvasBilinear(local_canvas, u, 0.2f);
                            break;
                        }

                        case ZONE_BOTTOM_FANS:
                        {
                            float u = (0.5f + frac * 20.5f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                            target_color = SampleCanvasBilinear(local_canvas, u, 5.0f);
                            break;
                        }

                        case ZONE_FRONT_FANS:
                        {
                            float u = (16.0f + frac * 5.0f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                            float v = 0.5f + frac * 4.5f;
                            target_color = SampleCanvasBilinear(local_canvas, u, v);
                            break;
                        }

                        case ZONE_REAR_FAN:
                        {
                            float u = (1.0f + frac * 5.0f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                            float v = 0.5f + frac * 4.0f;
                            target_color = SampleCanvasBilinear(local_canvas, u, v);
                            break;
                        }

                        case ZONE_RAM:
                        {
                            int stick_idx = ram_stick_counter;
                            float stick_led_frac = frac;

                            if (entry.zone_count > 1)
                            {
                                unsigned int accumulated_leds = 0;
                                for (int z = 0; z < entry.zone_count; ++z)
                                {
                                    unsigned int z_count = entry.zone_led_counts[z];
                                    if (i >= accumulated_leds && i < accumulated_leds + z_count)
                                    {
                                        stick_idx = ram_stick_counter + z;
                                        stick_led_frac = (z_count > 1) ? ((float)(i - accumulated_leds) / (float)(z_count - 1)) : 0.5f;
                                        break;
                                    }
                                    accumulated_leds += z_count;
                                }
                            }
                            else if (led_count >= 16 && (led_count % 8 == 0))
                            {
                                int stick_in_ctrl = i / 8;
                                stick_idx = ram_stick_counter + stick_in_ctrl;
                                stick_led_frac = (float)(i % 8) / 7.0f;
                            }
                            else if (led_count >= 20 && (led_count % 10 == 0))
                            {
                                int stick_in_ctrl = i / 10;
                                stick_idx = ram_stick_counter + stick_in_ctrl;
                                stick_led_frac = (float)(i % 10) / 9.0f;
                            }

                            // Physical RAM Layout:
                            // Consecutive sticks side-by-side: Stick 0 at U ~ 4.5, Stick 1 at U ~ 8.0, Stick 2 at U ~ 13.0, Stick 3 at U ~ 16.5
                            float stick_center_u = 4.5f + (float)(stick_idx % 4) * 3.8f;
                            float u = stick_center_u + (stick_led_frac * 1.5f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;

                            float v = 0.5f + stick_led_frac * 4.0f;
                            target_color = SampleCanvasBilinear(local_canvas, u, v);
                            break;
                        }

                        case ZONE_GPU:
                        {
                            if (entry.led_is_gpu_fan[i])
                            {
                                float u = 11.0f + shift_u;
                                if (u >= 21.0f) u = std::fmod(u, 21.0f);
                                if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                                target_color = SampleCanvasBilinear(local_canvas, u, 4.5f);
                            }
                            else
                            {
                                float u = (4.0f + frac * 14.0f) + shift_u;
                                if (u >= 21.0f) u = std::fmod(u, 21.0f);
                                if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                                target_color = SampleCanvasBilinear(local_canvas, u, 4.5f);
                            }
                            break;
                        }

                        case ZONE_AIO_COOLER:
                        {
                            if (led_count > 1)
                            {
                                float angle = 2.0f * 3.14159265f * (float)i / (float)led_count;
                                float u = (11.0f + 8.5f * std::cos(angle)) + shift_u;
                                if (u >= 21.0f) u = std::fmod(u, 21.0f);
                                if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                                float v = 2.5f + 2.0f * std::sin(angle);
                                target_color = SampleCanvasBilinear(local_canvas, u, v);
                            }
                            else
                            {
                                float u = 11.0f + shift_u;
                                if (u >= 21.0f) u = std::fmod(u, 21.0f);
                                if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                                target_color = SampleCanvasBilinear(local_canvas, u, 2.5f);
                            }
                            break;
                        }

                        case ZONE_MOTHERBOARD:
                        {
                            float u = (1.0f + frac * 3.5f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                            float v = 0.5f + frac * 4.2f;
                            target_color = SampleCanvasBilinear(local_canvas, u, v);
                            break;
                        }

                        default:
                        {
                            float u = (0.5f + frac * 20.5f) + shift_u;
                            if (u >= 21.0f) u = std::fmod(u, 21.0f);
                            if (u < 0.0f) u = std::fmod(u, 21.0f) + 21.0f;
                            target_color = SampleCanvasBilinear(local_canvas, u, 2.5f);
                            break;
                        }
                    }

                    // Apply Hardware LED Color Calibration (Gamma & Diffuser Bleed Alignment)
                    CalibrateForCaseHardware(target_color);

                    bool is_af_led = entry.led_is_af[i];
                    auto& state = entry.led_states[i];
                    if (!state.initialized)
                    {
                        state.current_r = target_color.r;
                        state.current_g = target_color.g;
                        state.current_b = target_color.b;
                        state.initialized = true;
                        state.is_anti_flash = is_af_led;
                    }
                    else
                    {
                        state.is_anti_flash = is_af_led;
                    }

                    float effective_alpha = base_alpha * entry.hw_cap.alpha_mult;
                    float effective_max_slew = base_max_slew * entry.hw_cap.slew_mult;
                    if (is_af_led || entry.hw_cap.profile == PROFILE_GPU_I2C)
                    {
                        effective_alpha *= 0.65f;
                        effective_max_slew *= 0.60f;
                    }

                    float delta_r = target_color.r - state.current_r;
                    float delta_g = target_color.g - state.current_g;
                    float delta_b = target_color.b - state.current_b;

                    float step_r = (std::clamp)(delta_r * effective_alpha, -effective_max_slew, effective_max_slew);
                    float step_g = (std::clamp)(delta_g * effective_alpha, -effective_max_slew, effective_max_slew);
                    float step_b = (std::clamp)(delta_b * effective_alpha, -effective_max_slew, effective_max_slew);

                    state.current_r += step_r;
                    state.current_g += step_g;
                    state.current_b += step_b;

                    float out_r = state.current_r * bright_factor;
                    float out_g = state.current_g * bright_factor;
                    float out_b = state.current_b * bright_factor;

                    if (is_af_led)
                    {
                        float max_ch = (std::max)({ out_r, out_g, out_b });
                        if (max_ch > 1.5f && max_ch < 14.0f)
                        {
                            float boost = 14.0f / max_ch;
                            out_r = (std::min)(255.0f, out_r * boost);
                            out_g = (std::min)(255.0f, out_g * boost);
                            out_b = (std::min)(255.0f, out_b * boost);
                        }
                    }

                    unsigned char final_r = (unsigned char)(std::clamp)((int)std::round(out_r), 0, 255);
                    unsigned char final_g = (unsigned char)(std::clamp)((int)std::round(out_g), 0, 255);
                    unsigned char final_b = (unsigned char)(std::clamp)((int)std::round(out_b), 0, 255);

                    int diff_r = std::abs((int)final_r - (int)state.last_out_r);
                    int diff_g = std::abs((int)final_g - (int)state.last_out_g);
                    int diff_b = std::abs((int)final_b - (int)state.last_out_b);

                    if (diff_r >= deadband_thresh || diff_g >= deadband_thresh || diff_b >= deadband_thresh)
                    {
                        ctrl_dirty = true;
                    }

                    ctrl->SetColor(i, ToRGBColor(final_r, final_g, final_b));
                }

                if (entry.effective_zone == ZONE_RAM)
                {
                    ram_stick_counter += (entry.zone_count > 1) ? entry.zone_count : 1;
                }

                if (ctrl_dirty)
                {
                    entry.has_pending = true;
                }

                auto now = std::chrono::steady_clock::now();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.last_update_time).count();

                int effective_min_interval = entry.hw_cap.min_interval_ms;
                if (is_calm_scene && effective_min_interval < 80)
                {
                    effective_min_interval *= 2;
                }
                if (effective_min_interval > 150) effective_min_interval = 150;

                bool rate_ok = (elapsed_ms >= effective_min_interval);

                if ((entry.has_pending && rate_ok) || elapsed_ms > 2000)
                {
                    try
                    {
                        ctrl->UpdateLEDs();
                        entry.last_update_time = now;
                        entry.has_pending = false;

                        for (unsigned int i = 0; i < led_count; ++i)
                        {
                            RGBColor c = ctrl->GetColor(i);
                            entry.led_states[i].last_out_r = (unsigned char)RGBGetRValue(c);
                            entry.led_states[i].last_out_g = (unsigned char)RGBGetGValue(c);
                            entry.led_states[i].last_out_b = (unsigned char)RGBGetBValue(c);
                        }
                    }
                    catch (...) {}
                }
            }
        }

        // Maintain ~30 FPS hardware update rate (33ms cycle) without bus flooding
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
        int sleep_ms = 33 - (int)elapsed.count();
        if (sleep_ms > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        }
    }
}
