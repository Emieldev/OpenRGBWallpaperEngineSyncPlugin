/*---------------------------------------------------------*\
| OpenRGBWallpaperEnginePlugin.h                            |
|                                                           |
|   OpenRGB companion plugin for Wallpaper Engine           |
|   lighting synchronization via Razer Chroma & iCUE        |
\*---------------------------------------------------------*/

#pragma once

#include <QObject>
#include <QWidget>
#include <QMenu>
#include <QLabel>
#include <QCheckBox>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTimer>
#include <QFrame>

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <set>
#include <map>

#include "OpenRGBPluginInterface.h"

// Forward declaration
class OpenRGBWallpaperEnginePlugin;

/*---------------------------------------------------------*\
| Case Location / Spatial Placement Zones                   |
\*---------------------------------------------------------*/
enum CaseLocationZone
{
    ZONE_AUTO = 0,             // Auto-detect based on device name & controller type
    ZONE_TOP_FANS,             // Top Case Fans / Radiator Exhaust
    ZONE_BOTTOM_FANS,          // Bottom Case Fans / Underglow Strips
    ZONE_FRONT_FANS,           // Front Intake Fans
    ZONE_REAR_FAN,             // Rear Exhaust Fan
    ZONE_MOTHERBOARD,          // Motherboard I/O Shield & Heatsink Accents
    ZONE_RAM,                  // Vertical RAM Modules
    ZONE_GPU,                  // Horizontal GPU & Sag Brackets
    ZONE_AIO_COOLER,           // Center CPU Cooler / AIO Pump Halo
    ZONE_PANORAMA,             // Full Screen Panoramic Sweep
    ZONE_AMBIENT               // Ambient Room Wash (Dominant Hue)
};

/*---------------------------------------------------------*\
| UI Tab Widget for Wallpaper Engine Sync                   |
\*---------------------------------------------------------*/
class WallpaperEngineSyncWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WallpaperEngineSyncWidget(OpenRGBWallpaperEnginePlugin* plugin_parent, QWidget* parent = nullptr);
    virtual ~WallpaperEngineSyncWidget();

    void RefreshDeviceList();
    void UpdateStatusUI();

private slots:
    void OnSyncToggled(bool checked);
    void OnBrightnessChanged(int value);
    void OnSmoothingChanged(int value);
    void OnLoopWhenPausedToggled(bool checked);
    void OnDeviceItemChanged(QTreeWidgetItem* item, int column);
    void OnSelectAllClicked();
    void OnDeselectAllClicked();
    void OnRefreshClicked();
    void OnUiTimerTick();

private:
    OpenRGBWallpaperEnginePlugin* plugin;

    // Status UI
    QLabel*         status_label;
    QLabel*         fps_label;
    QFrame*         spectrum_preview_bar;
    QFrame*         color_preview_box;
    QLabel*         color_rgb_label;

    // Control UI (Clean, Condensed)
    QCheckBox*      enable_checkbox;
    QSlider*        brightness_slider;
    QLabel*         brightness_label;
    QSlider*        smoothing_slider;
    QLabel*         smoothing_label;
    QCheckBox*      loop_when_paused_cb;

    // Device Tree UI
    QTreeWidget*    device_tree;
    QPushButton*    select_all_btn;
    QPushButton*    deselect_all_btn;
    QPushButton*    refresh_btn;

    // Periodic GUI update timer
    QTimer*         ui_timer;
    bool            updating_list_ui = false;
};

/*---------------------------------------------------------*\
| Internal Floating Point Color for Per-LED Smoothing      |
\*---------------------------------------------------------*/
struct RGBColorFloat
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

/*---------------------------------------------------------*\
| Per-LED Smoothing State with Anti-Flashing Hysteresis    |
\*---------------------------------------------------------*/
struct LEDSmoothingState
{
    float current_r = 0.0f;
    float current_g = 0.0f;
    float current_b = 0.0f;

    float committed_r = 0.0f;
    float committed_g = 0.0f;
    float committed_b = 0.0f;

    unsigned char last_out_r = 0;
    unsigned char last_out_g = 0;
    unsigned char last_out_b = 0;

    bool  initialized = false;
    bool  is_anti_flash = false;
};

/*---------------------------------------------------------*\
| Device Hardware Profiles & Adaptive Bus Pacing            |
\*---------------------------------------------------------*/
enum DeviceHardwareProfile
{
    PROFILE_PERIPHERAL_FAST, // USB Keyboards, Mice (30-60 Hz, snappy direct response)
    PROFILE_COOLER_USB,      // AIO Coolers, USB pumps (~28 Hz, fluid transitions)
    PROFILE_DRAM_SMBUS,      // ENE / Trident Z DRAM SMBus (~20 Hz, liquid rainbow wave)
    PROFILE_GPU_I2C,         // GPU Holtek/ITE I2C (~16 Hz, fan-safe, zero PWM interrupt)
    PROFILE_MOTHERBOARD_SMB, // Motherboard I/O & Chipset (~18 Hz, ambient wash)
    PROFILE_CASE_FAN_ARGB    // Case fans & ARGB strips (~28 Hz, calm ambient sweeps)
};

struct DeviceHardwareCapability
{
    DeviceHardwareProfile profile;
    float alpha_mult;       // Smoothing responsiveness multiplier (0.4 - 1.0)
    float slew_mult;        // Slew-rate speed cap multiplier (0.4 - 1.0)
    int   min_interval_ms;  // Minimum elapsed ms between physical hardware writes
    bool  supports_per_led; // Direct per-LED streaming supported
    bool  has_hardware_wave;// Hardware has native wave/rainbow modes
};

/*---------------------------------------------------------*\
| Cached Controller State (Zero-Alloc Dispatch in Loop)     |
\*---------------------------------------------------------*/
struct CachedController
{
    RGBControllerInterface*             ctrl = nullptr;
    std::string                         name;
    unsigned int                        led_count = 0;
    CaseLocationZone                    effective_zone = ZONE_PANORAMA;
    DeviceHardwareCapability            hw_cap;
    bool                                is_fan_or_accent = false;
    bool                                dev_is_af = false;
    std::vector<bool>                   led_is_af;
    std::vector<bool>                   led_is_gpu_fan;
    int                                 zone_count = 0;
    std::vector<unsigned int>           zone_led_counts;
    std::vector<LEDSmoothingState>      led_states;
    std::chrono::steady_clock::time_point last_update_time{};
    bool                                has_pending = false;
};

/*---------------------------------------------------------*\
| OpenRGB Plugin Implementation Class                       |
\*---------------------------------------------------------*/
class OpenRGBWallpaperEnginePlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "OpenRGBWallpaperEnginePlugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    OpenRGBWallpaperEnginePlugin();
    virtual ~OpenRGBWallpaperEnginePlugin();

    /*-----------------------------------------------------*\
    | OpenRGBPluginInterface Methods                        |
    \*-----------------------------------------------------*/
    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;

    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;

    void                OnProfileAboutToLoad() override;
    void                OnProfileLoad(nlohmann::json profile_data) override;
    nlohmann::json      OnProfileSave() override;
    unsigned char*      OnSDKCommand(unsigned int pkt_id, unsigned char * pkt_data, unsigned int *pkt_size) override;

    void                ProfileManagerUpdated(unsigned int update_reason) override;
    void                ResourceManagerUpdated(unsigned int update_reason) override;
    void                SettingsManagerUpdated(unsigned int update_reason) override;

    /*-----------------------------------------------------*\
    | Public helpers for Widget access                      |
    \*-----------------------------------------------------*/
    OpenRGBPluginAPIInterface*      GetAPI() { return plugin_api; }
    std::vector<RGBControllerInterface*> GetControllers();

    bool    IsSyncEnabled() const { return sync_enabled.load(); }
    void    SetSyncEnabled(bool enabled);

    int     GetBrightness() const { return brightness.load(); }
    void    SetBrightness(int b);

    int     GetSmoothingLevel() const { return smoothing_level.load(); }
    void    SetSmoothingLevel(int s);

    bool    IsAntiFlashEnabled() const { return anti_flash_enabled.load(); }
    void    SetAntiFlashEnabled(bool en);

    int     GetFanBrightness() const { return fan_accent_brightness.load(); }
    void    SetFanBrightness(int b);

    int     GetFanHarmony() const { return fan_accent_harmony.load(); }
    void    SetFanHarmony(int h);

    bool    IsDeviceAntiFlash(const std::string& name) const;

    CaseLocationZone GetDeviceZone(const std::string& name) const;
    void             SetDeviceZone(const std::string& name, CaseLocationZone zone);

    bool    IsLoopWhenPausedEnabled() const { return loop_when_paused.load(); }
    void    SetLoopWhenPaused(bool l);

    int     GetSyncMode() const { return sync_mode.load(); }
    void    SetSyncMode(int m);

    bool    IsDeviceEnabled(const std::string& name);
    void    SetDeviceEnabled(const std::string& name, bool enabled);
    void    SetAllDevicesEnabled(bool enabled);

    bool    IsConnected() const { return shm_connected.load(); }
    float   GetFPS() const { return current_fps.load(); }
    float   GetStreamMotionVelocity() const { return stream_motion_velocity.load(); }
    RGBColor GetCurrentColor() const { return last_color.load(); }
    std::vector<RGBColor> GetSpectrumPreview();

    void    SavePluginSettings();
    void    LoadPluginSettings();

private:
    void    StreamThreadFunc();
    void    HardwareThreadFunc();

    OpenRGBPluginAPIInterface*      plugin_api = nullptr;
    WallpaperEngineSyncWidget*      widget = nullptr;

    // Decoupled Threads: Fast Stream Reader & Safe Hardware Dispatcher
    std::thread                     stream_thread;
    std::thread                     hardware_thread;
    std::atomic<bool>               threads_running{false};

    std::atomic<bool>               sync_enabled{false};
    std::atomic<int>                brightness{100};
    std::atomic<int>                smoothing_level{75}; // 10 = Fast, 75 = Smooth Flow (Razer-style), 95 = Ultra Dreamy
    std::atomic<bool>               anti_flash_enabled{true}; // High-stability deadband filter for fans & indicators
    std::atomic<int>                fan_accent_brightness{100}; // Fan & single LED brightness dimmer (15% to 100%)
    std::atomic<int>                fan_accent_harmony{0};     // Fan & accent theme harmony (0% to 100%)
    std::atomic<bool>               loop_when_paused{false}; // Default: freeze on pause (matches Razer Synapse)
    std::atomic<int>                sync_mode{0}; // 0 = Smart Spatial, 1 = Horizontal Panorama, 2 = Ambient Wash
    std::atomic<bool>               shm_connected{false};
    std::atomic<float>              current_fps{0.0f};
    std::atomic<float>              stream_motion_velocity{0.0f}; // Genuine stream motion activity (RGB units/sec)
    std::atomic<RGBColor>           last_color{0};
    std::atomic<bool>               needs_mode_init{true};
    std::atomic<bool>               stream_idle{false};

    std::chrono::steady_clock::time_point last_stream_arrival;
    float                           idle_phase = 0.0f;

    std::mutex                      settings_mutex;
    std::set<std::string>           enabled_devices;
    std::set<std::string>           anti_flash_custom_devices;
    std::map<std::string, CaseLocationZone> device_zones;

    // Shared Double-Buffered Canvas protected by mutex
    std::mutex                      canvas_mutex;
    RGBColorFloat                   shared_canvas[6][22];
    RGBColorFloat                   shared_ambient;
    bool                            canvas_ready = false;

    // Pre-cached hardware controllers for zero-allocation 30 FPS dispatch
    std::atomic<bool>               controller_cache_dirty{true};
    std::vector<CachedController>   cached_controllers;
    void                            RebuildControllerCache();
};
